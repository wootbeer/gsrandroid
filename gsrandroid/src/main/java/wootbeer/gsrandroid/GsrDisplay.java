package wootbeer.gsrandroid;

import android.app.Activity;
import android.os.Build;
import android.view.Display;
import android.view.WindowManager;

/**
 * Display refresh rate handling for 2x frame interpolation. The option only does something when the screen
 * is really running at about 120 Hz, so: find the fastest mode at the current resolution, ask for it while
 * the option is on (and let the system choose again when it is off), and tell the native side what rate the
 * display is running at now.
 */
final class GsrDisplay {
	private GsrDisplay() {}

	/** Highest refresh rate this screen offers at its current resolution (the current rate if it cannot tell). */
	static float maxRefresh(Activity a) {
		Display d = a.getWindowManager().getDefaultDisplay();
		float best = d.getRefreshRate();
		if (Build.VERSION.SDK_INT >= 23) {
			Display.Mode cur = d.getMode();
			for (Display.Mode m : d.getSupportedModes()) {
				if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight()
						&& m.getRefreshRate() > best) {
					best = m.getRefreshRate();
				}
			}
		}
		return best;
	}

	/** Ask for the fastest mode while interpolation is on; otherwise drop the request. */
	static void apply(final Activity a) {
		if (Build.VERSION.SDK_INT < 23) return;
		final boolean want = GsrNative.getSetting(GsrNative.INTERP) != 0;
		a.runOnUiThread(new Runnable() {
			@Override public void run() {
				Display d = a.getWindowManager().getDefaultDisplay();
				int id = 0;
				if (want) {
					Display.Mode cur = d.getMode();
					float best = 0;
					for (Display.Mode m : d.getSupportedModes()) {
						if (m.getPhysicalWidth() == cur.getPhysicalWidth()
								&& m.getPhysicalHeight() == cur.getPhysicalHeight() && m.getRefreshRate() > best) {
							best = m.getRefreshRate();
							id = m.getModeId();
						}
					}
				}
				WindowManager.LayoutParams lp = a.getWindow().getAttributes();
				if (lp.preferredDisplayModeId != id) {
					lp.preferredDisplayModeId = id;
					a.getWindow().setAttributes(lp);
				}
				publish(a);
			}
		});
	}

	// ---- Orientation: Landscape (the default) or Portrait, a fixed choice (no following the sensor) -------

	private static final String PREFS = "gsr_display", KEY_PORTRAIT = "portrait";

	static boolean portrait(Activity a) {
		return a.getSharedPreferences(PREFS, Activity.MODE_PRIVATE).getBoolean(KEY_PORTRAIT, false);
	}

	static void setPortrait(Activity a, boolean on) {
		a.getSharedPreferences(PREFS, Activity.MODE_PRIVATE).edit().putBoolean(KEY_PORTRAIT, on).apply();
		applyOrientation(a);
	}

	/** Portrait: tell the native side how far down a camera cutout reaches, so the picture starts below it. */
	static void publishTopInset(final Activity a) {
		if (Build.VERSION.SDK_INT < 28) return;
		final android.view.View d = a.getWindow().getDecorView();
		d.postDelayed(new Runnable() {
			@Override public void run() {
				int top = 0;
				android.view.WindowInsets wi = d.getRootWindowInsets();
				if (wi != null && wi.getDisplayCutout() != null) top = wi.getDisplayCutout().getSafeInsetTop();
				try {
					GsrNative.setTopInset(top);
				} catch (Throwable ignored) {
					// native library not loaded yet
				}
			}
		}, 300);
	}

	/** Landscape either way up (as before), or upright portrait with the controls below the picture. */
	static void applyOrientation(Activity a) {
		a.setRequestedOrientation(portrait(a)
				? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
				: android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
	}

	/** Tell the native side the refresh rate the display is running at right now. */
	static void publish(Activity a) {
		try {
			GsrNative.setDisplayRefresh(a.getWindowManager().getDefaultDisplay().getRefreshRate());
		} catch (Throwable ignored) {
			// native library not loaded yet
		}
	}
}
