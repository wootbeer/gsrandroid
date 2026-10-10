// gsr_screen_filter.cpp -- the optional screen filters (Display > Screen filter), drawn by the presenter
// (gsr_host_render) instead of its plain copy: LCD3x, CRT Lottes and xBR-lv2, from GSRecomp 0.4.3's
// screen_filter.cpp. Their shader text and licence notices are in gsr_screen_filter_shaders.inc, copied
// verbatim; here only the "#version 130" line becomes GLSL ES 3.00 and the vertex stage is ours (the same
// rectangle-and-texture-range layout as the presenter's plain shader).
#include "gsr_screen_filter.h"

#include <GLES3/gl3.h>
#include <android/log.h>

#include <cstring>
#include <string>

#define SLOG(...) __android_log_print(ANDROID_LOG_INFO, "gsr-filter", __VA_ARGS__)

namespace {

#include "gsr_screen_filter_shaders.inc"

// Scanlines only (this port's own, not from upstream): each original row is full brightness through its middle
// and darkens toward its top and bottom edges; columns stay sharp. The small boost keeps the average brightness
// close to the unfiltered picture. Uses kFragmentCommon's src_texel like the others.
const char* kScanlineFragment = R"GLSL(
void main()
{
    vec2 px = v_uv * SourceSize;
    vec3 c = src_texel(px).rgb;
    float d = abs(fract(px.y) - 0.5) * 2.0;   // 0 in the middle of a row, 1 at its edge
    float shade = 1.0 - 0.45 * d * d;
    FragColor = vec4(min(c * shade * 1.08, vec3(1.0)), 1.0);
}
)GLSL";

const char* kVertex =
    "#version 300 es\n"
    "uniform vec4 rect; uniform vec4 uvr; out vec2 v_uv;\n"
    "void main(){ vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
    "  gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);\n"
    "  v_uv = vec2(mix(uvr.x, uvr.z, c.x), mix(uvr.w, uvr.y, c.y)); }\n";

struct Program {
    GLuint id = 0;
    bool tried = false;
    GLint texture = -1, source_size = -1, tex_rect = -1, output_size = -1, rect = -1, uvr = -1;
};
Program g_programs[GSR_FILTER_COUNT];

GLuint compile(GLenum type, const std::string& source, const char* what) {
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    const char* text = source.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        SLOG("%s did not compile: %s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void build(Program* p, const char* body, const char* name, bool flat) {
    p->tried = true;
    std::string fs = std::string(kFragmentCommon) + body;
    if (flat) {
        // CRT: flat, not curved -- Lottes' own curvature parameters at 0 (Warp() is then the identity).
        for (const char* d : {"#define warpX 0.031", "#define warpY 0.041"}) {
            const size_t w = fs.find(d);
            if (w != std::string::npos) fs.replace(w, std::strlen(d), std::string(d, std::strlen(d) - 5) + "0.0");
        }
    }
    const std::string from = "#version 130\n";
    const size_t at = fs.find(from);
    if (at != std::string::npos)
        fs.replace(at, from.size(), "#version 300 es\nprecision highp float;\nprecision highp int;\n");
    GLuint v = compile(GL_VERTEX_SHADER, kVertex, name);
    GLuint f = v ? compile(GL_FRAGMENT_SHADER, fs, name) : 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        return;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        SLOG("%s did not link", name);
        glDeleteProgram(program);
        return;
    }
    p->id = program;
    p->texture = glGetUniformLocation(program, "Texture");
    p->source_size = glGetUniformLocation(program, "SourceSize");
    p->tex_rect = glGetUniformLocation(program, "TexRect");
    p->output_size = glGetUniformLocation(program, "OutputSize");
    p->rect = glGetUniformLocation(program, "rect");
    p->uvr = glGetUniformLocation(program, "uvr");
}

}  // namespace

extern "C" int gsr_screen_filter_draw(int filter, const float rect[4], const float uvr[4],
                                      float src_w, float src_h, float out_w, float out_h) {
    if (filter <= GSR_FILTER_OFF || filter >= GSR_FILTER_COUNT || src_w < 1.0f || src_h < 1.0f) return 0;
    Program* p = &g_programs[filter];
    // A rebuilt GL context (the app was in the background) loses every program: build again.
    if (p->id && !glIsProgram(p->id)) *p = Program();
    if (!p->tried) {
        const bool crt = filter == GSR_FILTER_CRT;
        const char* body = filter == GSR_FILTER_LCD ? kLcd3xFragment
                         : crt ? kLottesFragment
                         : filter == GSR_FILTER_SCANLINES ? kScanlineFragment : kXbrFragment;
        build(p, body, filter == GSR_FILTER_LCD ? "LCD3x" : crt ? "CRT Lottes"
                       : filter == GSR_FILTER_SCANLINES ? "Scanlines" : "xBR-lv2", crt);
    }
    if (!p->id) return 0;
    glUseProgram(p->id);
    glUniform1i(p->texture, 0);
    glUniform2f(p->source_size, src_w, src_h);
    glUniform4f(p->tex_rect, 0.0f, 0.0f, 1.0f, 1.0f);
    glUniform2f(p->output_size, out_w, out_h);
    glUniform4f(p->rect, rect[0], rect[1], rect[2], rect[3]);
    glUniform4f(p->uvr, uvr[0], uvr[1], uvr[2], uvr[3]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUseProgram(0);
    return 1;
}
