// gpu_surface_android.cpp -- GLES 3.0 implementation of gbarecomp's GpuSurface (see gpu_surface.h).
//
// Replaces upstream gpu_surface.cpp (desktop GL 1.30 + SDL entry-point loader) on Android. The class
// interface is unchanged, so the engine's field renderer is compiled as-is. Differences:
//
//   * No window context is shared with us. init() creates a private offscreen EGL context (a 1x1
//     pbuffer) and makes it current on the calling thread -- the engine thread, which is also the
//     thread that draws every GPU frame. The finished frame is read back into the engine's CPU
//     buffer (read_texture_rgb), so nothing here touches the render thread's display context.
//   * Shaders are written for GLSL 1.30; create_program() rewrites the version line to
//     "#version 300 es" and adds default precisions. Everything else is source compatible.
//   * Entry points are linked directly (libGLESv3), so there is no loader.
//   * GLES has no glGetTexLevelParameteriv / glGetTexImage / glClearDepth: texture formats are
//     tracked here, and readback goes through a framebuffer + glReadPixels.
#include "gpu_surface.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#define GLOG(...) __android_log_print(ANDROID_LOG_INFO, "gsr-gpu", __VA_ARGS__)

namespace gbarecomp {
namespace {

// ---- offscreen EGL context ---------------------------------------------------------------------------
EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLContext g_ctx = EGL_NO_CONTEXT;
EGLSurface g_pb = EGL_NO_SURFACE;
std::string g_ctx_error;

bool ensure_context() {
    if (g_ctx != EGL_NO_CONTEXT) {
        if (eglGetCurrentContext() != g_ctx) eglMakeCurrent(g_dpy, g_pb, g_pb, g_ctx);
        return eglGetCurrentContext() == g_ctx;
    }
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { g_ctx_error = "no EGL display"; return false; }
    EGLint maj = 0, min = 0;
    if (!eglInitialize(g_dpy, &maj, &min)) { g_ctx_error = "eglInitialize failed"; return false; }
    const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                               EGL_NONE};
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
        g_ctx_error = "no ES3 pbuffer EGL config";
        return false;
    }
    const EGLint pb_attr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    g_pb = eglCreatePbufferSurface(g_dpy, cfg, pb_attr);
    if (g_pb == EGL_NO_SURFACE) { g_ctx_error = "eglCreatePbufferSurface failed"; return false; }
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) {
        eglDestroySurface(g_dpy, g_pb);
        g_pb = EGL_NO_SURFACE;
        g_ctx_error = "eglCreateContext (ES 3.0) failed";
        return false;
    }
    if (!eglMakeCurrent(g_dpy, g_pb, g_pb, g_ctx)) {
        g_ctx_error = "eglMakeCurrent failed";
        eglDestroyContext(g_dpy, g_ctx);
        eglDestroySurface(g_dpy, g_pb);
        g_ctx = EGL_NO_CONTEXT;
        g_pb = EGL_NO_SURFACE;
        return false;
    }
    GLOG("offscreen GLES context: %s / %s", (const char*)glGetString(GL_RENDERER),
         (const char*)glGetString(GL_VERSION));
    return true;
}

// ---- per-texture bookkeeping (GLES cannot query a level's internal format) ---------------------------
std::unordered_map<GLuint, GpuTextureFormat> g_formats;
GLuint g_read_fbo = 0;

constexpr int kFloatsPerVertex = 9;
constexpr int kVerticesPerQuad = 6;

const char* kBlitVertex =
    "#version 130\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "in float a_depth;\n"
    "in vec4 a_tint;\n"
    "uniform mat4 u_transform;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_tint;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  v_tint = a_tint;\n"
    "  gl_Position = u_transform * vec4(a_pos, a_depth, 1.0);\n"
    "}\n";

const char* kBlitFragment =
    "#version 130\n"
    "in vec2 v_uv;\n"
    "in vec4 v_tint;\n"
    "uniform sampler2D u_texture;\n"
    "out vec4 o_colour;\n"
    "void main() {\n"
    "  o_colour = texture(u_texture, v_uv) * v_tint;\n"
    "}\n";

void ortho(float* m, float width, float height) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 2.0f / width;
    m[5] = -2.0f / height;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

// "#version 130" -> GLSL ES 3.00 with the precisions desktop GLSL does not need.
//
// texelFetch outside the texture: desktop drivers return zero, which the field shaders rely on to mean "no data
// here" (past the room, past the map). GLSL ES leaves it undefined, and phone GPUs commonly clamp to the edge
// texel instead -- which paints edge art into regions that must stay empty. Fragment shaders therefore get a
// bounds-checked texelFetch that returns zero outside, matching the desktop behaviour.
std::string to_es(const char* src, bool fragment) {
    std::string s(src ? src : "");
    const char* tag = "#version 130";
    size_t p = s.find(tag);
    if (p == std::string::npos) return s;
    size_t eol = s.find('\n', p);
    std::string head =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp int;\n"
        "precision highp sampler2D;\n"
        "precision highp usampler2D;\n";
    if (fragment) {
        head +=
            "uvec4 gsr_fetch(usampler2D s, ivec2 p, int l) {\n"
            "  ivec2 z = textureSize(s, l);\n"
            "  if (p.x < 0 || p.y < 0 || p.x >= z.x || p.y >= z.y) return uvec4(0u);\n"
            "  return texelFetch(s, p, l);\n"
            "}\n"
            "vec4 gsr_fetch(sampler2D s, ivec2 p, int l) {\n"
            "  ivec2 z = textureSize(s, l);\n"
            "  if (p.x < 0 || p.y < 0 || p.x >= z.x || p.y >= z.y) return vec4(0.0);\n"
            "  return texelFetch(s, p, l);\n"
            "}\n"
            "#define texelFetch gsr_fetch\n";
    }
    return s.substr(0, p) + head + (eol == std::string::npos ? "" : s.substr(eol + 1));
}

}  // namespace

struct GpuSurface::SavedState {
    GLint framebuffer = 0;
    GLint program = 0;
    GLint array_buffer = 0;
    GLint vertex_array = 0;
    GLint active_texture = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean blend = GL_FALSE;
    GLboolean depth_test = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean depth_mask = GL_TRUE;
    GLfloat clear_color[4] = {0, 0, 0, 1};
    GLint blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO;
    GLint blend_src_alpha = GL_ONE, blend_dst_alpha = GL_ZERO;
};

GpuSurface::~GpuSurface() { release(); }

bool GpuSurface::load_entry_points() { return true; }  // linked directly

bool GpuSurface::init() {
    if (ready_) return true;
    if (!ensure_context()) {
        static std::string msg;
        msg = "could not create an offscreen OpenGL ES context: " + g_ctx_error;
        failure_ = msg.c_str();
        return false;
    }
    saved_ = new SavedState();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenFramebuffers(1, &fbo_);
    if (!vao_ || !vbo_ || !fbo_) {
        failure_ = "could not create the vertex array, buffer or framebuffer";
        release();
        return false;
    }
    char log[1024] = {};
    blit_program_ = create_program(kBlitVertex, kBlitFragment, log, sizeof log);
    if (!blit_program_) {
        GLOG("blit shader failed: %s", log);
        failure_ = "the built-in shader did not compile on this driver";
        release();
        return false;
    }
    ready_ = true;
    failure_ = nullptr;
    return true;
}

void GpuSurface::release() {
    if (g_ctx != EGL_NO_CONTEXT && eglGetCurrentContext() == g_ctx) {
        if (blit_program_) { glDeleteProgram(blit_program_); blit_program_ = 0; }
        if (target_tex_) { glDeleteTextures(1, &target_tex_); g_formats.erase(target_tex_); target_tex_ = 0; }
        if (depth_rb_) { glDeleteRenderbuffers(1, &depth_rb_); depth_rb_ = 0; }
        if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
        if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        if (g_read_fbo) { glDeleteFramebuffers(1, &g_read_fbo); g_read_fbo = 0; }
    }
    delete saved_;
    saved_ = nullptr;
    ready_ = false;
}

// ---- resources ---------------------------------------------------------------------------------------

GpuTexture GpuSurface::create_texture(int width, int height, GpuTextureFormat format, bool filtered) {
    if (width <= 0 || height <= 0) return 0;
    GLint max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (max_size <= 0 || width > max_size || height > max_size) return 0;

    while (glGetError() != GL_NO_ERROR) {}
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, tex);
    // Integer textures cannot be filtered in GLES either; force NEAREST for them.
    const bool integer = format != GpuTextureFormat::RGBA8;
    const GLint filter = (filtered && !integer) ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    switch (format) {
    case GpuTextureFormat::R8UI:
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
        break;
    case GpuTextureFormat::R16UI:
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
        break;
    case GpuTextureFormat::RGBA8:
    default:
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        break;
    }
    const GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (err != GL_NO_ERROR) {
        glDeleteTextures(1, &tex);
        return 0;
    }
    g_formats[tex] = format;
    return tex;
}

void GpuSurface::update_texture(GpuTexture texture, int x, int y, int width, int height, const void* pixels) {
    if (!texture || width <= 0 || height <= 0 || !pixels) return;
    auto it = g_formats.find(texture);
    if (it == g_formats.end()) return;
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    // Pin every unpack setting this transfer depends on (see the upstream file for why), restore after.
    GLint prev_alignment = 4, prev_row_length = 0, prev_skip_rows = 0, prev_skip_pixels = 0;
    GLint prev_image_height = 0, prev_skip_images = 0, prev_unpack_buffer = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prev_row_length);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &prev_skip_rows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &prev_skip_pixels);
    glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &prev_image_height);
    glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &prev_skip_images);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &prev_unpack_buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    if (prev_unpack_buffer) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    switch (it->second) {
    case GpuTextureFormat::R8UI:
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
        break;
    case GpuTextureFormat::R16UI:
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
        break;
    default:
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        break;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prev_row_length);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, prev_skip_rows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, prev_skip_pixels);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, prev_image_height);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, prev_skip_images);
    if (prev_unpack_buffer) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(prev_unpack_buffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

void GpuSurface::destroy_texture(GpuTexture texture) {
    if (!texture) return;
    glDeleteTextures(1, &texture);
    g_formats.erase(texture);
}

GpuProgram GpuSurface::create_program(const char* vertex_source, const char* fragment_source, char* log,
                                      std::size_t log_bytes) {
    if (log && log_bytes) log[0] = 0;
    auto compile = [&](GLenum type, const char* source) -> GLuint {
        GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        std::string es = to_es(source, type == GL_FRAGMENT_SHADER);
        const char* p = es.c_str();
        glShaderSource(shader, 1, &p, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            if (log && log_bytes) glGetShaderInfoLog(shader, static_cast<GLsizei>(log_bytes), nullptr, log);
            GLOG("%s shader failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log ? log : "");
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_source);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint program = glCreateProgram();
    if (!program) { glDeleteShader(vs); glDeleteShader(fs); return 0; }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glBindAttribLocation(program, 2, "a_depth");
    glBindAttribLocation(program, 3, "a_tint");
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        if (log && log_bytes) glGetProgramInfoLog(program, static_cast<GLsizei>(log_bytes), nullptr, log);
        GLOG("program link failed: %s", log ? log : "");
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void GpuSurface::destroy_program(GpuProgram program) {
    if (program) glDeleteProgram(program);
}

#define GPU_WITH_PROGRAM(p, body)                      \
    do {                                               \
        if (!(p)) break;                               \
        GLint previous = 0;                            \
        glGetIntegerv(GL_CURRENT_PROGRAM, &previous);  \
        glUseProgram(p);                               \
        body;                                          \
        glUseProgram(static_cast<GLuint>(previous));   \
    } while (false)

void GpuSurface::set_uniform_int(GpuProgram p, const char* name, int v) {
    GPU_WITH_PROGRAM(p, { GLint loc = glGetUniformLocation(p, name); if (loc >= 0) glUniform1i(loc, v); });
}
void GpuSurface::set_uniform_float(GpuProgram p, const char* name, float v) {
    GPU_WITH_PROGRAM(p, { GLint loc = glGetUniformLocation(p, name); if (loc >= 0) glUniform1f(loc, v); });
}
void GpuSurface::set_uniform_vec2(GpuProgram p, const char* name, float x, float y) {
    GPU_WITH_PROGRAM(p, { GLint loc = glGetUniformLocation(p, name); if (loc >= 0) glUniform2f(loc, x, y); });
}
void GpuSurface::set_uniform_vec4(GpuProgram p, const char* name, float x, float y, float z, float w) {
    GPU_WITH_PROGRAM(p, { GLint loc = glGetUniformLocation(p, name); if (loc >= 0) glUniform4f(loc, x, y, z, w); });
}
void GpuSurface::set_uniform_mat4(GpuProgram p, const char* name, const float* m) {
    GPU_WITH_PROGRAM(p, {
        GLint loc = glGetUniformLocation(p, name);
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
    });
}
#undef GPU_WITH_PROGRAM

bool GpuSurface::get_uniform_int(GpuProgram p, const char* name, int* value) {
    if (!ready_ || !p || !value) return false;
    const GLint loc = glGetUniformLocation(p, name);
    if (loc < 0) return false;
    glGetUniformiv(p, loc, value);
    return true;
}

bool GpuSurface::get_uniform_vec2(GpuProgram p, const char* name, float* values) {
    if (!ready_ || !p || !values) return false;
    const GLint loc = glGetUniformLocation(p, name);
    if (loc < 0) return false;
    glGetUniformfv(p, loc, values);
    return true;
}

// Diagnostic only; GLES cannot read an integer texture back through a query, so just report what we track.
void GpuSurface::log_texture_r8ui(GpuTexture texture, unsigned unit, int width, int height,
                                  const std::uint8_t*) {
    if (!ready_) return;
    GLOG("texture audit: texture=%u unit=%u expected=%dx%d tracked=%d", texture, unit, width, height,
         g_formats.count(texture) ? 1 : 0);
}

// ---- drawing -----------------------------------------------------------------------------------------

bool GpuSurface::set_target_size(int width, int height) {
    if (!ready_ || width <= 0 || height <= 0) return false;
    if (width == target_w_ && height == target_h_ && target_tex_) return true;

    if (target_tex_) { destroy_texture(target_tex_); target_tex_ = 0; }
    target_tex_ = create_texture(width, height, GpuTextureFormat::RGBA8, false);
    if (!target_tex_) return false;

    GLint previous_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_tex_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroy_texture(target_tex_);
        target_tex_ = 0;
        return false;
    }
    target_w_ = width;
    target_h_ = height;
    return true;
}

void GpuSurface::begin_frame(float r, float g, float b, float a) {
    if (!ready_ || !target_tex_ || in_frame_) return;
    if (!ensure_context()) return;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_->framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_->program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_->array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_->vertex_array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_->active_texture);
    glGetIntegerv(GL_VIEWPORT, saved_->viewport);
    saved_->blend = glIsEnabled(GL_BLEND);
    saved_->depth_test = glIsEnabled(GL_DEPTH_TEST);
    saved_->scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved_->depth_mask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_->clear_color);
    glGetIntegerv(GL_BLEND_SRC_RGB, &saved_->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &saved_->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved_->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &saved_->blend_dst_alpha);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, target_w_, target_h_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    in_frame_ = true;
}

void GpuSurface::end_frame() {
    if (!ready_ || !in_frame_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(saved_->framebuffer));
    glUseProgram(static_cast<GLuint>(saved_->program));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(saved_->array_buffer));
    glBindVertexArray(static_cast<GLuint>(saved_->vertex_array));
    glActiveTexture(static_cast<GLenum>(saved_->active_texture));
    glViewport(saved_->viewport[0], saved_->viewport[1], saved_->viewport[2], saved_->viewport[3]);
    if (saved_->blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (saved_->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (saved_->scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glDepthMask(saved_->depth_mask);
    glClearColor(saved_->clear_color[0], saved_->clear_color[1], saved_->clear_color[2], saved_->clear_color[3]);
    glBlendFuncSeparate(static_cast<GLenum>(saved_->blend_src_rgb), static_cast<GLenum>(saved_->blend_dst_rgb),
                        static_cast<GLenum>(saved_->blend_src_alpha), static_cast<GLenum>(saved_->blend_dst_alpha));
    in_frame_ = false;
}

void GpuSurface::set_blend_mode(GpuBlendMode mode) {
    if (!ready_ || !in_frame_) return;
    if (mode == GpuBlendMode::Off) { glDisable(GL_BLEND); return; }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, mode == GpuBlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
}

void GpuSurface::set_blend_alpha(bool enabled) {
    set_blend_mode(enabled ? GpuBlendMode::Alpha : GpuBlendMode::Off);
}

void GpuSurface::bind_color_target(GpuTexture texture) {
    if (!ready_) return;
    const GLuint tex = texture ? texture : target_tex_;
    GLint previous_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
}

void GpuSurface::clear_color_target(float r, float g, float b, float a) {
    if (!ready_ || !in_frame_) return;
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GpuSurface::resize_depth_to_target() {
    if (!ready_ || !depth_rb_) return;
    if (depth_w_ == target_w_ && depth_h_ == target_h_) return;
    GLint previous_rb = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, target_w_, target_h_);
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_rb));
    depth_w_ = target_w_;
    depth_h_ = target_h_;
}

void GpuSurface::set_depth_enabled(bool enabled) {
    if (!ready_) return;
    resize_depth_to_target();
    if (enabled) {
        if (!depth_rb_ || depth_w_ != target_w_ || depth_h_ != target_h_) {
            if (!depth_rb_) glGenRenderbuffers(1, &depth_rb_);
            GLint previous_rb = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_rb);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, target_w_, target_h_);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_rb));
            GLint previous_fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
            depth_w_ = target_w_;
            depth_h_ = target_h_;
        }
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void GpuSurface::clear_depth(float value) {
    if (!ready_ || !depth_rb_ || !in_frame_) return;
    glClearDepthf(value);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void GpuSurface::draw_quads(GpuProgram program, const GpuQuad* quads, std::size_t count,
                            const GpuTexture* textures, std::size_t texture_count) {
    if (!ready_ || !program || !quads || count == 0) return;

    std::vector<float> vertices;
    vertices.reserve(count * kVerticesPerQuad * kFloatsPerVertex);
    for (std::size_t i = 0; i < count; ++i) {
        const GpuQuad& q = quads[i];
        const float corners[6][4] = {
            {q.x, q.y, q.u0, q.v0},           {q.x + q.w, q.y, q.u1, q.v0},
            {q.x + q.w, q.y + q.h, q.u1, q.v1}, {q.x, q.y, q.u0, q.v0},
            {q.x + q.w, q.y + q.h, q.u1, q.v1}, {q.x, q.y + q.h, q.u0, q.v1},
        };
        for (const auto& c : corners) {
            vertices.push_back(c[0]);
            vertices.push_back(c[1]);
            vertices.push_back(c[2]);
            vertices.push_back(c[3]);
            vertices.push_back(q.depth);
            vertices.push_back(q.tint[0]);
            vertices.push_back(q.tint[1]);
            vertices.push_back(q.tint[2]);
            vertices.push_back(q.tint[3]);
        }
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const std::size_t bytes = vertices.size() * sizeof(float);
    if (bytes > vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), vertices.data(), GL_STREAM_DRAW);
        vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices.data());
    }

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(5 * sizeof(float)));

    for (std::size_t i = 0; i < texture_count; ++i) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    glActiveTexture(GL_TEXTURE0);

    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count * kVerticesPerQuad));
}

void GpuSurface::present_target(int x, int y, int width, int height, int viewport_width, int viewport_height,
                                bool filtered) {
    if (!ready_ || !target_tex_ || width <= 0 || height <= 0) return;
    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glBindTexture(GL_TEXTURE_2D, target_tex_);
    const GLint filter = filtered ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));

    float transform[16];
    ortho(transform, static_cast<float>(viewport_width), static_cast<float>(viewport_height));
    set_uniform_mat4(blit_program_, "u_transform", transform);
    set_uniform_int(blit_program_, "u_texture", 0);

    GpuQuad quad;
    quad.x = static_cast<float>(x);
    quad.y = static_cast<float>(y);
    quad.w = static_cast<float>(width);
    quad.h = static_cast<float>(height);
    quad.v0 = 1.0f;
    quad.v1 = 0.0f;
    const GpuTexture textures[1] = {target_tex_};
    draw_quads(blit_program_, &quad, 1, textures, 1);
}

void GpuSurface::read_texture_rgb(GpuTexture texture, int width, int height, std::uint8_t* out_rgb) {
    if (!ready_ || !texture || width <= 0 || height <= 0 || !out_rgb) return;

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u);
    GLint prev_fbo = 0, prev_alignment = 4, prev_row_length = 0, prev_skip_rows = 0, prev_skip_pixels = 0;
    GLint prev_pack_buffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &prev_row_length);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &prev_skip_rows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &prev_skip_pixels);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prev_pack_buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    if (prev_pack_buffer) glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (!g_read_fbo) glGenFramebuffers(1, &g_read_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));

    glPixelStorei(GL_PACK_ALIGNMENT, prev_alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, prev_row_length);
    glPixelStorei(GL_PACK_SKIP_ROWS, prev_skip_rows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, prev_skip_pixels);
    if (prev_pack_buffer) glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prev_pack_buffer));
    if (!complete) return;

    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(height - 1 - y) * width * 4u;
        std::uint8_t* dst = out_rgb + static_cast<std::size_t>(y) * width * 3u;
        for (int x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
}

}  // namespace gbarecomp
