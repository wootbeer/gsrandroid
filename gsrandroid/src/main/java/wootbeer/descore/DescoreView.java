package wootbeer.descore;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.hardware.input.InputManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.os.Handler;
import android.os.Process;
import android.text.InputType;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import java.util.HashSet;
import java.util.Set;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

/**
 * Shared game surface for every descore-shell port. Owns the parts that are identical no matter
 * what game is running underneath:
 *
 *  - the EGL/GLES 1.x context on the game's render thread (native code draws into it and swaps),
 *  - the render-thread park/resume handshake that survives screen lock, backgrounding and a
 *    destroyed Surface (the game is paused in place, never restarted),
 *  - raw key, gamepad-axis and multi-touch forwarding to native,
 *  - the pull-model audio path: one always-running AudioTrack whose PCM comes from native
 *    (descore_audio.c mixes music, effects, whatever the game registered),
 *  - the soft keyboard show/hide bridge and "quit the whole app" bridge.
 *
 * Nothing in here knows what game it is hosting. The game supplies descore_game_* entry points in
 * native code (see cpp/descore/descore.h); the JNI symbols for the natives below are implemented
 * once, in cpp/descore/descore_jni.c.
 *
 * The lifecycle bugs hit on real devices are folded in here by design, and the
 * pause handshake is now level-triggered (see pauseRenderThread) so resume can never arrive "before"
 * the park and leave the render thread asleep forever.
 */
public class DescoreView extends SurfaceView implements SurfaceHolder.Callback,
		InputManager.InputDeviceListener {

	private static final int SAMPLE_RATE = 44100; // must match DESCORE_AUDIO_RATE in descore.h
	private static final int AUDIO_CHUNK_FRAMES = 512;
	private static final float AXIS_EPSILON = 0.02f;

	private final Context context;
	private final Handler mainHandler;
	private final DescoreGameConfig config;
	private final String dataDir;
	private final int unitId;

	private SurfaceHolder holder;
	private Point size;

	// --- render-thread lifecycle ---------------------------------------------------------------
	private boolean gameRunning;
	private final Object renderThreadObj = new Object();
	/** True while the Activity is between onPause() and onResume(). Guarded by renderThreadObj. */
	private boolean appPaused;
	/** True while a Surface exists. Guarded by renderThreadObj. */
	private boolean surfaceValid;

	// --- input ----------------------------------------------------------------------------------
	private InputManager inputManager;
	private final Set<Integer> connectedGamepadIds = new HashSet<>();
	private final float[] lastAxis = new float[AXIS_IDS.length];
	private static final int[] AXIS_IDS = {
			MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
			MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y,
			MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER,
			MotionEvent.AXIS_BRAKE, MotionEvent.AXIS_GAS};

	// --- audio ----------------------------------------------------------------------------------
	private AudioTrack audioTrack;
	private Thread audioThread;
	private volatile boolean audioStopRequested;

	public DescoreView(Activity activity, DescoreGameConfig config, String dataDir, int unitId) {
		super(activity);
		this.context = activity;
		this.config = config;
		this.dataDir = dataDir;
		this.unitId = unitId;
		this.mainHandler = new Handler(activity.getMainLooper());
		this.holder = getHolder();
		System.loadLibrary(config.nativeLibrary);

		// Pin the surface opaque so the EGL config's zero-alpha request can't be second-guessed
		// into a translucent surface by the platform.
		holder.setFormat(PixelFormat.OPAQUE);
		setFocusableInTouchMode(true);
		if (Build.VERSION.SDK_INT >= 26) {
			setDefaultFocusHighlightEnabled(false);
		}
		holder.addCallback(this);

		inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
		if (inputManager != null) {
			inputManager.registerInputDeviceListener(this, mainHandler);
		}
		for (int deviceId : InputDevice.getDeviceIds()) {
			if (isGamepad(InputDevice.getDevice(deviceId))) {
				connectedGamepadIds.add(deviceId);
			}
		}
		nativeSetGamepadConnected(!connectedGamepadIds.isEmpty());
		// Density must reach native before the render thread (and so the game's touch-control init)
		// starts; this constructor always finishes first, on the UI thread.
		nativeSetDisplayDensity(getResources().getDisplayMetrics().density);
	}

	// --- Surface lifecycle ------------------------------------------------------------------------

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		this.holder = holder;
		synchronized (renderThreadObj) {
			surfaceValid = true;
			renderThreadObj.notifyAll();
		}
		if (!gameRunning) {
			gameRunning = true;
			startAudio();
			new Thread(new Runnable() {
				@Override
				public void run() {
					size = new Point(getWidth(), getHeight());
					initEgl();
					nativeMain(size.x, size.y, dataDir, unitId);
					// The game's own main returned: it has already asked the app to exit via
					// requestExit(); nothing more to do on this thread.
				}
			}).start();
		}
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		// Re-pin the buffer size: a transient bounds change (system bars) shouldn't leave a
		// stale-sized buffer compositing against new bounds.
		if (size != null && holder != null) {
			holder.setFixedSize(size.x, size.y);
		}
		nativeSurfaceResized(width, height);
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		// A destroyed Surface is NOT a quit: it means lock screen, backgrounding or a resize. Tell
		// native its EGL surface is dead (it will park at its next tick and rebuild on resume) and
		// keep the play session alive.
		synchronized (renderThreadObj) {
			surfaceValid = false;
		}
		nativeNotifySurfaceDestroyed();
	}

	/** Activity.onPause(). Parks the render thread at its next tick and pauses audio. */
	public void onAppPause() {
		synchronized (renderThreadObj) {
			appPaused = true;
		}
		nativeRequestPause();
		if (audioTrack != null && audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
			audioTrack.pause();
		}
	}

	/** Activity.onResume(). */
	public void onAppResume() {
		synchronized (renderThreadObj) {
			appPaused = false;
			renderThreadObj.notifyAll();
		}
		if (audioTrack != null && audioTrack.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) {
			audioTrack.play();
		}
	}

	/**
	 * Called from native on the render thread (descore_jni.c, from the game's per-tick pump) after
	 * onAppPause()/surfaceDestroyed() asked for a pause. Blocks until the Activity is resumed AND a
	 * Surface exists. Level-triggered on purpose: if resume already happened before the render
	 * thread got here, the condition is already false and this returns immediately, instead of
	 * sleeping forever waiting for a wake-up that has already come and gone.
	 */
	@SuppressWarnings("unused")
	private void pauseRenderThread() {
		synchronized (renderThreadObj) {
			while (appPaused || !surfaceValid) {
				try {
					renderThreadObj.wait();
				} catch (InterruptedException e) {
					Thread.currentThread().interrupt();
					return;
				}
			}
		}
	}

	/**
	 * Builds the EGL display/context/surface against the CURRENT {@code holder} and makes them
	 * current on the calling (render) thread. Called once at cold start, and again from native
	 * after a destroyed Surface has been torn down and replaced. GLES 1.x: descore's video path is
	 * one textured quad plus flat-colored touch buttons.
	 */
	@SuppressWarnings("unused")
	private void initEgl() {
		final int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
		final int EGL_RENDERABLE_TYPE = 0x3040;
		final int EGL_OPENGL_ES3_BIT = 0x40; // EGL_OPENGL_ES3_BIT_KHR
		int[] numConfig = new int[1];
		final EGLConfig[] configs = new EGLConfig[1];
		int[] attribList = {EGL_CONTEXT_CLIENT_VERSION, config.glesMajor, EGL10.EGL_NONE};

		if (size != null) {
			final Point fixedSize = size;
			final Object done = new Object();
			final boolean[] applied = {false};
			synchronized (done) {
				mainHandler.post(new Runnable() {
					@Override
					public void run() {
						holder.setFixedSize(fixedSize.x, fixedSize.y);
						synchronized (done) {
							applied[0] = true;
							done.notifyAll();
						}
					}
				});
				while (!applied[0]) {
					try {
						done.wait();
					} catch (InterruptedException e) {
						Thread.currentThread().interrupt();
						break;
					}
				}
			}
		}

		EGL10 egl = (EGL10) EGLContext.getEGL();
		EGLDisplay display = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
		egl.eglInitialize(display, new int[]{1, 0});
		final boolean es3 = config.glesMajor >= 3;
		egl.eglChooseConfig(display, new int[]{
				EGL10.EGL_RED_SIZE, 8,
				EGL10.EGL_GREEN_SIZE, 8,
				EGL10.EGL_BLUE_SIZE, 8,
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_DEPTH_SIZE, config.depthBits,
				EGL10.EGL_STENCIL_SIZE, config.stencilBits,
				EGL_RENDERABLE_TYPE, es3 ? EGL_OPENGL_ES3_BIT : 1 /* EGL_OPENGL_ES_BIT */,
				EGL10.EGL_NONE}, configs, 1, numConfig);
		EGLConfig eglConfig = configs[0];
		EGLContext eglContext = egl.eglCreateContext(display, eglConfig, EGL10.EGL_NO_CONTEXT,
				attribList);
		EGLSurface eglSurface = egl.eglCreateWindowSurface(display, eglConfig, holder, null);
		egl.eglMakeCurrent(display, eglSurface, eglSurface, eglContext);
		keptDisplay = display;
		keptConfig = eglConfig;
		keptContext = eglContext;
		if (es3) {
			android.opengl.GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			android.opengl.GLES20.glClear(android.opengl.GLES20.GL_COLOR_BUFFER_BIT);
			if (size != null) android.opengl.GLES20.glViewport(0, 0, size.x, size.y);
		} else {
			GL10 gl = (GL10) eglContext.getGL();
			gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
			if (size != null) {
				gl.glViewport(0, 0, size.x, size.y);
			}
		}
	}

	private EGLDisplay keptDisplay;
	private EGLConfig keptConfig;
	private EGLContext keptContext;

	/** Native asks: after a lost Surface, may the EGL context (and every GL object) be kept? */
	@SuppressWarnings("unused")
	private boolean keepContextOnSurfaceLoss() {
		return config.glesMajor >= 3;
	}

	/**
	 * Builds a new window surface against the CURRENT holder and makes it current with the
	 * context the native side kept alive. Render thread only.
	 */
	@SuppressWarnings("unused")
	private void rebuildSurface() {
		EGL10 egl = (EGL10) EGLContext.getEGL();
		EGLSurface s = egl.eglCreateWindowSurface(keptDisplay, keptConfig, holder, null);
		egl.eglMakeCurrent(keptDisplay, s, s, keptContext);
	}

	// --- Keyboard / gamepad buttons ------------------------------------------------------------

	/** Presses and releases one Android key code, for callers with no KeyEvent (Back gesture). */
	public void sendKey(int androidKeyCode) {
		nativeKey(androidKeyCode, true, 0);
		nativeKey(androidKeyCode, false, 0);
	}

	private static boolean isSystemKey(int keyCode) {
		return keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
				|| keyCode == KeyEvent.KEYCODE_VOLUME_MUTE || keyCode == KeyEvent.KEYCODE_POWER;
	}

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (isSystemKey(keyCode)) return super.onKeyDown(keyCode, event);
		if (event.getRepeatCount() == 0) {
			nativeKey(keyCode, true, event.getUnicodeChar());
		}
		return true;
	}

	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		if (isSystemKey(keyCode)) return super.onKeyUp(keyCode, event);
		nativeKey(keyCode, false, event.getUnicodeChar());
		return true;
	}

	/**
	 * TYPE_NULL makes the soft keyboard deliver ordinary key events (with unicode chars) instead of
	 * composing text, which is what a DOS-era game's name-entry screen wants.
	 */
	@Override
	public boolean onCheckIsTextEditor() {
		return true;
	}

	@Override
	public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
		outAttrs.inputType = InputType.TYPE_NULL;
		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
		return new BaseInputConnection(this, false);
	}

	// --- Analog axes -----------------------------------------------------------------------------

	@Override
	public boolean onGenericMotionEvent(MotionEvent event) {
		if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
				&& event.getAction() == MotionEvent.ACTION_MOVE) {
			for (int i = 0; i < AXIS_IDS.length; i++) {
				float v = event.getAxisValue(AXIS_IDS[i]);
				if (Math.abs(v - lastAxis[i]) > AXIS_EPSILON || (v == 0f && lastAxis[i] != 0f)) {
					lastAxis[i] = v;
					nativeAxis(AXIS_IDS[i], v);
				}
			}
			return true;
		}
		return super.onGenericMotionEvent(event);
	}

	// --- Gamepad auto-detect ---------------------------------------------------------------------

	private static boolean isGamepad(InputDevice device) {
		if (device == null) return false;
		int sources = device.getSources();
		return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
	}

	@Override
	public void onInputDeviceAdded(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
			nativeSetGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceRemoved(int deviceId) {
		if (connectedGamepadIds.remove(deviceId)) {
			nativeSetGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceChanged(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
		} else {
			connectedGamepadIds.remove(deviceId);
		}
		nativeSetGamepadConnected(!connectedGamepadIds.isEmpty());
	}

	// --- Touch ---------------------------------------------------------------------------------------

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(MotionEvent event) {
		int action = event.getActionMasked();
		int first = (action == MotionEvent.ACTION_POINTER_DOWN
				|| action == MotionEvent.ACTION_POINTER_UP) ? event.getActionIndex() : 0;
		int count = (action == MotionEvent.ACTION_MOVE) ? event.getPointerCount() : 1;
		boolean handled = false;
		for (int i = first; i < count + first; ++i) {
			handled |= nativeTouch(action, event.getPointerId(i), event.getX(i), event.getY(i));
		}
		return handled;
	}

	// --- Audio (pull model) -------------------------------------------------------------------------

	private void startAudio() {
		int minBuffer = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO,
				AudioFormat.ENCODING_PCM_16BIT);
		if (minBuffer <= 0) return;
		// Twice the device minimum: small enough that sound effects feel
		// immediate, big enough that the mixer thread doesn't fight the smallest buffer allowed.
		audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, SAMPLE_RATE,
				AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT, minBuffer * 2,
				AudioTrack.MODE_STREAM);
		audioStopRequested = false;
		final AudioTrack track = audioTrack;
		audioThread = new Thread(new Runnable() {
			@Override
			public void run() {
				// Real-time producer: same scheduler class Android's own audio threads run at, so
				// CPU pressure from the render thread can't starve it into an underrun.
				Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
				short[] buf = new short[AUDIO_CHUNK_FRAMES * 2];
				while (!audioStopRequested) {
					int frames = nativeRenderAudio(buf);
					if (frames <= 0) {
						java.util.Arrays.fill(buf, (short) 0);
						frames = AUDIO_CHUNK_FRAMES;
					}
					track.write(buf, 0, frames * 2, AudioTrack.WRITE_BLOCKING);
				}
			}
		}, "descore-audio");
		track.play();
		audioThread.start();
	}

	private void stopAudio() {
		audioStopRequested = true;
		if (audioThread != null) {
			try {
				audioThread.join();
			} catch (InterruptedException e) {
				Thread.currentThread().interrupt();
			}
			audioThread = null;
		}
		if (audioTrack != null) {
			audioTrack.stop();
			audioTrack.release();
			audioTrack = null;
		}
	}

	// --- Callbacks invoked from native (descore_jni.c) ------------------------------------------------

	@SuppressWarnings("unused")
	private void showKeyboard() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				requestFocus();
				InputMethodManager imm =
						(InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
				if (imm != null) imm.showSoftInput(DescoreView.this, InputMethodManager.SHOW_FORCED);
			}
		});
	}

	@SuppressWarnings("unused")
	private void hideKeyboard() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				InputMethodManager imm =
						(InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
				if (imm != null) imm.hideSoftInputFromWindow(getWindowToken(), 0);
			}
		});
	}

	/**
	 * The game asked to quit. Finishes the whole task and ends the process: a fresh launch then
	 * always starts with zero-initialized native globals, which is the only state a DOS-derived
	 * engine is written to expect (re-entering main() in a live process is a classic source of
	 * stale-state bugs).
	 */
	/** Lets an activity open its own settings menu when the native game asks for it. */
	public interface MenuRequestListener {
		void onMenuRequested();
	}

	private volatile MenuRequestListener menuRequestListener;

	public void setMenuRequestListener(MenuRequestListener listener) {
		menuRequestListener = listener;
	}

	@SuppressWarnings("unused")
	private void requestMenu() {
		final MenuRequestListener l = menuRequestListener;
		if (l == null) return;
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				l.onMenuRequested();
			}
		});
	}

	@SuppressWarnings("unused")
	private void requestExit() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				stopAudio();
				if (context instanceof Activity) {
					((Activity) context).finishAffinity();
				}
				Process.killProcess(Process.myPid());
			}
		});
	}

	// --- Native entry points (implemented in cpp/descore/descore_jni.c) -------------------------------

	// Not static: native needs a real jobject for THIS view to call back into.
	private native void nativeMain(int width, int height, String dataDir, int unitId);

	private static native void nativeSurfaceResized(int width, int height);

	private static native void nativeKey(int androidKeyCode, boolean down, int unicodeChar);

	private static native void nativeAxis(int axisId, float value);

	private static native boolean nativeTouch(int action, int pointerId, float x, float y);

	private static native void nativeSetGamepadConnected(boolean connected);

	private static native void nativeSetDisplayDensity(float density);

	private static native void nativeRequestPause();

	private static native void nativeNotifySurfaceDestroyed();

	/** Fills buf with interleaved stereo S16 at SAMPLE_RATE; returns frames written. */
	private static native int nativeRenderAudio(short[] buf);
}
