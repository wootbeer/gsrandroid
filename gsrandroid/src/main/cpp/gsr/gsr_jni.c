/* gsr_jni.c -- the Java settings menu's view of the native option and button tables. */
#include "gsr_controls.h"
#include "gsr_host.h"
#include "gsr_settings.h"

#include "gsr_engine.h"
#include "gsr_prepare.h"

#include <jni.h>
#include <stdio.h>

int gsr_main_rom_status(void);
int gsr_main_engine_started(void);
const char *gsr_main_engine_error(void);

/* One snapshot of everything the build overlay needs:
 * [0] phase: rom_missing, rom_wrong, waiting, building, ready, failed, engine_error, engine_stopped, running
 * [1] current step   [2] error text   [3] progress 0..1000   [4] elapsed seconds   [5] "1" if only loading
 * [6] log path       [7] engine stop code */
JNIEXPORT jobjectArray JNICALL Java_wootbeer_gsrandroid_GsrNative_buildInfo(JNIEnv *env, jclass c)
{
	char msg[200], err[420], logp[700], num[16][24];
	const char *phase;
	int rom = gsr_main_rom_status(), st = gsr_prepare_state(), rc = 0, k;
	jobjectArray arr;
	jclass sc = (*env)->FindClass(env, "java/lang/String");
	(void) c;
	gsr_prepare_message(msg, sizeof msg);
	gsr_prepare_error(err, sizeof err);
	gsr_prepare_log_path(logp, sizeof logp);
	if (rom < 0) phase = "waiting";
	else if (rom == 0) phase = "rom_missing";
	else if (rom == 1) phase = "rom_wrong";
	else if (st == GSR_PREP_FAILED) phase = "failed";
	else if (gsr_main_engine_error()[0]) { phase = "engine_error"; snprintf(err, sizeof err, "%s", gsr_main_engine_error()); }
	else if (gsr_main_engine_started() && gsr_engine_finished(&rc)) phase = "engine_stopped";
	else if (gsr_main_engine_started() && gsr_host_frame_count() > 0) phase = "running";
	else if (st == GSR_PREP_READY) phase = "ready";
	else if (st == GSR_PREP_RUNNING) phase = "building";
	else phase = "waiting";
	snprintf(num[0], sizeof num[0], "%d", gsr_prepare_progress());
	snprintf(num[1], sizeof num[1], "%d", gsr_prepare_elapsed_s());
	snprintf(num[2], sizeof num[2], "%d", gsr_prepare_cached());
	snprintf(num[3], sizeof num[3], "%d", rc);
	arr = (*env)->NewObjectArray(env, 8, sc, NULL);
	{
		const char *v[8];
		v[0] = phase; v[1] = msg; v[2] = err; v[3] = num[0]; v[4] = num[1]; v[5] = num[2]; v[6] = logp; v[7] = num[3];
		for (k = 0; k < 8; k++) {
			jstring s = (*env)->NewStringUTF(env, v[k]);
			(*env)->SetObjectArrayElement(env, arr, k, s);
			(*env)->DeleteLocalRef(env, s);
		}
	}
	return arr;
}

JNIEXPORT jint JNICALL Java_wootbeer_gsrandroid_GsrNative_getSetting(JNIEnv *env, jclass c, jint id)
{
	(void) env; (void) c;
	return gsr_settings_get((GsrSetting) id);
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_setSetting(JNIEnv *env, jclass c, jint id, jint value)
{
	(void) env; (void) c;
	gsr_settings_set((GsrSetting) id, value);
}

JNIEXPORT jint JNICALL Java_wootbeer_gsrandroid_GsrNative_getBind(JNIEnv *env, jclass c, jint action, jint slot)
{
	(void) env; (void) c;
	return gsr_controls_bind_get((GsrAction) action, slot);
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_setBind(JNIEnv *env, jclass c, jint action, jint slot, jint key)
{
	(void) env; (void) c;
	gsr_controls_bind_set((GsrAction) action, slot, key);
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_resetBinds(JNIEnv *env, jclass c)
{
	(void) env; (void) c;
	gsr_controls_bind_reset();
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_setDisplayRefresh(JNIEnv *env, jclass c, jfloat hz)
{
	(void) env; (void) c;
	gsr_host_set_display_hz(hz);
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_setDebug(JNIEnv *env, jclass c, jboolean on)
{
	(void) env; (void) c;
	gsr_host_set_debug(on ? 1 : 0);
}

JNIEXPORT jint JNICALL Java_wootbeer_gsrandroid_GsrNative_gpuStatus(JNIEnv *env, jclass c)
{
	(void) env; (void) c;
	return gsr_host_gpu_status();
}

JNIEXPORT jint JNICALL Java_wootbeer_gsrandroid_GsrNative_gpuActive(JNIEnv *env, jclass c)
{
	(void) env; (void) c;
	return gsr_host_gpu_active();
}

JNIEXPORT void JNICALL Java_wootbeer_gsrandroid_GsrNative_setMenuOpen(JNIEnv *env, jclass c, jboolean open)
{
	(void) env; (void) c;
	if (open) gsr_controls_release_all();
	gsr_host_set_menu_open(open ? 1 : 0);
}
