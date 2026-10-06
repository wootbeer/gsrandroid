/* descore_audio.c -- the small real-time mixer every descore-shell game shares.
 *
 * Java runs one always-on AudioTrack and pulls PCM from native in short chunks (see
 * DescoreView.startAudio); this file mixes whatever sources the game registered into that chunk:
 * the MOD player, an OPL/CMF music player, digitized effects, anything that can fill a buffer.
 * Idle sources cost nothing but a function call, and with no sources the output is silence, so the
 * track never has to be stopped and restarted (which is where clicks and latency come from).
 *
 * Locking: one mutex guards the source table AND is held while sources render. That is what makes
 * descore_audio_remove_source() a real barrier: when it returns, the audio thread is guaranteed
 * not to be inside that source's callback, so the caller may free the source's state. The price is
 * that a source callback must not call add/remove/set_gain itself.
 */

#include "descore.h"
#include "descore_internal.h"

#include <pthread.h>
#include <string.h>

#define MAX_SOURCES 8
#define MAX_CHUNK_FRAMES 2048

typedef struct Source {
	DescoreAudioSourceFn fn;
	void *user;
	float gain;
	int used;
} Source;

static Source g_sources[MAX_SOURCES];
static float g_master_gain = 1.0f;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

int descore_audio_add_source(DescoreAudioSourceFn fn, void *user)
{
	int i, handle = -1;
	pthread_mutex_lock(&g_lock);
	for (i = 0; i < MAX_SOURCES; ++i) {
		if (!g_sources[i].used) {
			g_sources[i].fn = fn;
			g_sources[i].user = user;
			g_sources[i].gain = 1.0f;
			g_sources[i].used = 1;
			handle = i;
			break;
		}
	}
	pthread_mutex_unlock(&g_lock);
	return handle;
}

void descore_audio_remove_source(int handle)
{
	if (handle < 0 || handle >= MAX_SOURCES) return;
	pthread_mutex_lock(&g_lock);
	g_sources[handle].used = 0;
	g_sources[handle].fn = NULL;
	g_sources[handle].user = NULL;
	pthread_mutex_unlock(&g_lock);
}

void descore_audio_set_source_gain(int handle, float gain)
{
	if (handle < 0 || handle >= MAX_SOURCES) return;
	if (gain < 0.0f) gain = 0.0f;
	pthread_mutex_lock(&g_lock);
	g_sources[handle].gain = gain;
	pthread_mutex_unlock(&g_lock);
}

void descore_audio_set_master_gain(float gain)
{
	if (gain < 0.0f) gain = 0.0f;
	pthread_mutex_lock(&g_lock);
	g_master_gain = gain;
	pthread_mutex_unlock(&g_lock);
}

static int16_t clamp16(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

static void render_chunk(int16_t *out, size_t frames)
{
	static int32_t acc[MAX_CHUNK_FRAMES * 2];
	static int16_t tmp[MAX_CHUNK_FRAMES * 2];
	size_t n = frames * 2;
	size_t i;
	int s;

	memset(acc, 0, n * sizeof(acc[0]));
	for (s = 0; s < MAX_SOURCES; ++s) {
		size_t got;
		float gain;
		if (!g_sources[s].used || g_sources[s].fn == NULL) continue;
		got = g_sources[s].fn(g_sources[s].user, tmp, frames);
		if (got > frames) got = frames;
		gain = g_sources[s].gain;
		for (i = 0; i < got * 2; ++i) {
			acc[i] += (int32_t)((float)tmp[i] * gain);
		}
	}
	for (i = 0; i < n; ++i) {
		out[i] = clamp16((int32_t)((float)acc[i] * g_master_gain));
	}
}

size_t descore_audio_render(int16_t *out, size_t frames)
{
	size_t done = 0;
	pthread_mutex_lock(&g_lock);
	while (done < frames) {
		size_t chunk = frames - done;
		if (chunk > MAX_CHUNK_FRAMES) chunk = MAX_CHUNK_FRAMES;
		render_chunk(out + done * 2, chunk);
		done += chunk;
	}
	pthread_mutex_unlock(&g_lock);
	return frames;
}
