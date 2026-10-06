/* descore_jni.c -- JNI entry points and lifecycle for wootbeer.descore.DescoreView.
 *
 * The Java side (DescoreView) never knows what game it hosts, and the game never touches JNI: this
 * file is the whole bridge. It owns the render thread's pause/resume protocol (see descore_pump),
 * the EGL teardown/rebuild after a destroyed Surface, and the tiny set of upcalls into Java
 * (park the thread, rebuild EGL, show/hide the soft keyboard, quit the app).
 */

#include "descore.h"
#include "descore_internal.h"
#include "touch/descore_touch.h"

#include <EGL/egl.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "descore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Optional game hook: the surface's real pixel size changed (first known size, rotation, ...). */
__attribute__((weak)) void descore_game_surface_resized(int width, int height);

static JavaVM *g_vm;
static jobject g_view; /* global ref to the DescoreView instance */
static jmethodID g_mid_pause_render_thread;
static jmethodID g_mid_init_egl;
static jmethodID g_mid_keep_context;
static jmethodID g_mid_rebuild_surface;
static jmethodID g_mid_show_keyboard;
static jmethodID g_mid_hide_keyboard;
static jmethodID g_mid_request_exit;
static jmethodID g_mid_request_menu;

/* Written from the UI thread, read on the render thread. volatile is sufficient for these single
 * word flags: each is only ever set by one side and cleared by the other, and the protocol
 * tolerates seeing a flag one tick late. */
static volatile int g_want_pause;
static volatile int g_surface_was_destroyed;
static volatile int g_stop_requested;
static volatile int g_surface_w, g_surface_h;
static volatile int g_gamepad_connected;
static volatile int g_paused_now;

int descore_surface_width(void) { return g_surface_w; }
int descore_surface_height(void) { return g_surface_h; }
int descore_gamepad_connected(void) { return g_gamepad_connected; }
int descore_is_paused(void) { return g_paused_now; }

/* ---- JNIEnv helpers ---------------------------------------------------------------------- */

static JNIEnv *get_env(int *did_attach)
{
	JNIEnv *env = NULL;
	*did_attach = 0;
	if (g_vm == NULL) return NULL;
	if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) return env;
	if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == JNI_OK) {
		*did_attach = 1;
		return env;
	}
	return NULL;
}

static void release_env(int did_attach)
{
	if (did_attach && g_vm != NULL) (*g_vm)->DetachCurrentThread(g_vm);
}

static void call_void(jmethodID mid)
{
	int did_attach;
	JNIEnv *env;
	if (g_view == NULL || mid == NULL) return;
	env = get_env(&did_attach);
	if (env == NULL) return;
	(*env)->CallVoidMethod(env, g_view, mid);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
	}
	release_env(did_attach);
}

static int call_bool(jmethodID mid)
{
	int did_attach, r = 0;
	JNIEnv *env;
	if (g_view == NULL || mid == NULL) return 0;
	env = get_env(&did_attach);
	if (env == NULL) return 0;
	r = (*env)->CallBooleanMethod(env, g_view, mid) ? 1 : 0;
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		r = 0;
	}
	release_env(did_attach);
	return r;
}

/* Optional: asks the activity to open its settings menu (DescoreView.requestMenu). */
void descore_request_menu(void) { call_void(g_mid_request_menu); }
void descore_show_keyboard(void) { call_void(g_mid_show_keyboard); }
void descore_hide_keyboard(void) { call_void(g_mid_hide_keyboard); }
void descore_request_exit(void)
{
	g_stop_requested = 1;
	call_void(g_mid_request_exit);
}

/* ---- Pause / resume ------------------------------------------------------------------------ */

/* Runs on the render thread from descore_pump() when the UI thread asked for a pause. Parks the
 * thread inside DescoreView.pauseRenderThread() until the Activity is resumed and a Surface
 * exists. If the Surface itself was destroyed meanwhile, the EGL objects built against it are
 * dead: tear them down for real and have Java build a fresh set against the new Surface. Any GL
 * object names (textures) died with the old context too, hence descore_video_reset_gl(). */
static void handle_pause(void)
{
	EGLContext ctx;
	EGLDisplay dpy;
	EGLSurface surf;

	g_want_pause = 0;
	if (g_view == NULL || g_mid_pause_render_thread == NULL) {
		LOGW("handle_pause: view or method not bound, not pausing");
		return;
	}

	/* Cache these BEFORE blocking: if the Surface dies while parked, these (not whatever the
	 * current-context queries would report afterwards) are what must be torn down. */
	ctx = eglGetCurrentContext();
	dpy = eglGetCurrentDisplay();
	surf = eglGetCurrentSurface(EGL_DRAW);

	g_paused_now = 1;
	call_void(g_mid_pause_render_thread);
	g_paused_now = 0;

	if (g_surface_was_destroyed) {
		g_surface_was_destroyed = 0;
		if (g_mid_keep_context != NULL && g_mid_rebuild_surface != NULL
				&& call_bool(g_mid_keep_context)) {
			/* GLES 3 mode: keep the EGL context (and every texture/buffer/program in it), replace
			 * only the window surface. */
			eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroySurface(dpy, surf);
			call_void(g_mid_rebuild_surface);
			return;
		}
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(dpy, surf);
		eglDestroyContext(dpy, ctx);
		eglTerminate(dpy);
		descore_video_reset_gl();
		if (g_mid_init_egl != NULL) {
			call_void(g_mid_init_egl);
		} else {
			LOGE("handle_pause: cannot rebuild EGL (initEgl method missing)");
		}
	}
}

int descore_pump(void)
{
	if (g_want_pause) handle_pause();
	return !g_stop_requested;
}

/* ---- JNI entry points ------------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeMain(JNIEnv *env, jobject thiz, jint width, jint height,
		jstring data_dir, jint unit_id)
{
	DescoreGameParams params;
	const char *dir = (*env)->GetStringUTFChars(env, data_dir, NULL);
	jclass cls;

	(*env)->GetJavaVM(env, &g_vm);
	if (g_view != NULL) (*env)->DeleteGlobalRef(env, g_view);
	g_view = (*env)->NewGlobalRef(env, thiz);
	cls = (*env)->GetObjectClass(env, thiz);
	g_mid_pause_render_thread = (*env)->GetMethodID(env, cls, "pauseRenderThread", "()V");
	g_mid_init_egl = (*env)->GetMethodID(env, cls, "initEgl", "()V");
	g_mid_keep_context = (*env)->GetMethodID(env, cls, "keepContextOnSurfaceLoss", "()Z");
	g_mid_rebuild_surface = (*env)->GetMethodID(env, cls, "rebuildSurface", "()V");
	g_mid_show_keyboard = (*env)->GetMethodID(env, cls, "showKeyboard", "()V");
	g_mid_hide_keyboard = (*env)->GetMethodID(env, cls, "hideKeyboard", "()V");
	g_mid_request_exit = (*env)->GetMethodID(env, cls, "requestExit", "()V");
	g_mid_request_menu = (*env)->GetMethodID(env, cls, "requestMenu", "()V");
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		LOGE("nativeMain: a DescoreView method is missing, lifecycle features will be inert");
	}

	g_stop_requested = 0;
	if (g_surface_w == 0) g_surface_w = width;
	if (g_surface_h == 0) g_surface_h = height;

	params.data_dir = strdup(dir);
	params.unit_id = unit_id;
	params.surface_w = width;
	params.surface_h = height;
	(*env)->ReleaseStringUTFChars(env, data_dir, dir);

	LOGI("nativeMain: unit %d, data dir %s, surface %dx%d", unit_id, params.data_dir, width, height);
	descore_game_main(&params);
	LOGI("nativeMain: game returned, requesting exit");
	descore_request_exit();
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeSurfaceResized(JNIEnv *env, jclass clazz, jint width,
		jint height)
{
	(void)env;
	(void)clazz;
	g_surface_w = width;
	g_surface_h = height;
	descore_touch_set_screen_size(width, height);
	if (descore_game_surface_resized) descore_game_surface_resized(width, height);
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeKey(JNIEnv *env, jclass clazz, jint key_code,
		jboolean down, jint unicode_char)
{
	(void)env;
	(void)clazz;
	descore_game_key(key_code, down ? 1 : 0, unicode_char);
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeAxis(JNIEnv *env, jclass clazz, jint axis_id, jfloat value)
{
	(void)env;
	(void)clazz;
	descore_game_axis(axis_id, value);
}

/* Optional game hook (weak): sees every raw touch event before the touch overlay does. Return non-zero to consume it
 * (a game showing menus wants the raw pointer, not the on-screen sticks and buttons). */
__attribute__((weak)) int descore_game_touch(int action, int pointer_id, float x, float y);

JNIEXPORT jboolean JNICALL
Java_wootbeer_descore_DescoreView_nativeTouch(JNIEnv *env, jclass clazz, jint action,
		jint pointer_id, jfloat x, jfloat y)
{
	(void)env;
	(void)clazz;
	if (descore_game_touch && descore_game_touch(action, pointer_id, x, y)) return JNI_TRUE;
	return descore_touch_handle(action, pointer_id, x, y) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeSetGamepadConnected(JNIEnv *env, jclass clazz,
		jboolean connected)
{
	(void)env;
	(void)clazz;
	g_gamepad_connected = connected ? 1 : 0;
	descore_touch_set_gamepad_connected(connected ? true : false);
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeSetDisplayDensity(JNIEnv *env, jclass clazz,
		jfloat density)
{
	(void)env;
	(void)clazz;
	descore_touch_set_display_density(density);
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeRequestPause(JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	g_want_pause = 1;
}

JNIEXPORT void JNICALL
Java_wootbeer_descore_DescoreView_nativeNotifySurfaceDestroyed(JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	/* The Surface is gone: mark it so the next park rebuilds EGL, and make the render thread park
	 * at its next tick so it stops swapping into a dead surface. */
	g_surface_was_destroyed = 1;
	g_want_pause = 1;
}

JNIEXPORT jint JNICALL
Java_wootbeer_descore_DescoreView_nativeRenderAudio(JNIEnv *env, jclass clazz, jshortArray buf)
{
	jsize len = (*env)->GetArrayLength(env, buf);
	jshort *ptr = (*env)->GetShortArrayElements(env, buf, NULL);
	size_t frames;
	if (ptr == NULL) return 0;
	frames = descore_audio_render((int16_t *)ptr, (size_t)len / 2);
	(*env)->ReleaseShortArrayElements(env, buf, ptr, 0);
	(void)clazz;
	return (jint)frames;
}
