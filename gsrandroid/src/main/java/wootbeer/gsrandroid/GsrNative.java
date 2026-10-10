package wootbeer.gsrandroid;

/** The native option and button tables (gsr_settings.c, gsr_controls.c). Setting ids match GsrSetting in gsr_settings.h. */
final class GsrNative {
	static final int VIEW_MODE = 0, SCALE_MODE = 1, FILTER = 2, SCREEN = 3, BLEND = 4, INTERP = 5,
			ENH_TIMING = 6, NATIVE_RENDER = 7, NATIVE_SCALE = 8, VOLUME = 9, MUTE = 10,
			FF_MULT10 = 11, FF_MODE = 12, FF_UNCAPPED = 13, FF_MUTE = 14, SHOW_FPS = 15,
			TOUCH_SCALE = 16, TOUCH_OPACITY = 17, RENDERER = 18, AUTO_CPU = 19,
			TOUCH_DPAD = 20, SCREEN_FILTER = 21,
			CHEAT_HP = 22, CHEAT_PP = 23, CHEAT_EXP = 24, CHEAT_COIN = 25, CHEAT_DROP = 26;

	/** GsrAction in gsr_controls.h: the GBA buttons first, then these. */
	static final String[] ACTIONS = { "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L",
			"Fast forward", "Menu" };

	private GsrNative() {}

	static native int getSetting(int id);
	static native void setSetting(int id, int value);
	static native void setDebug(boolean on);
	static native int getBind(int action, int slot);
	static native void setBind(int action, int slot, int keyCode);
	static native void resetBinds();
	static native void setMenuOpen(boolean open);
	/** 0 GPU renderer not tried yet, 1 available, 2 unavailable on this device. */
	static native void setDisplayRefresh(float hz);
	static native int gpuStatus();
	/** Snapshot of the first-run build / startup state; see gsr_jni.c for the fields. */
	static native String[] buildInfo();
	/** 1 if the last frame was drawn by the GPU renderer. */
	static native int gpuActive();
	/** Save (save=true) or load save-state slot 1..9; done by the engine at its next input pump. */
	static native void requestState(int slot, boolean save);
}
