package wootbeer.gsrandroid;

import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;

import wootbeer.descore.DescoreGameActivity;
import wootbeer.descore.DescoreGameConfig;

/**
 * Golden Sun Recompiled's game screen: the shared descore game shell, configured by {@link GsrConfig}.
 *
 * Before the native game starts, this makes sure the translator's data files (addresses and hashes
 * only, no game code or ROM bytes) are unpacked from the APK, and starts {@link GsrBuildService} so the
 * first-run build (translate + compile on the device) keeps running if the player leaves the app.
 */
public class GsrGameActivity extends DescoreGameActivity {
	private static final DescoreGameConfig CONFIG = GsrConfig.create();

	private GsrMenu menu;
	private GsrBuildOverlay overlay;
	private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
		@Override public void onDisplayAdded(int id) {}
		@Override public void onDisplayRemoved(int id) {}
		@Override public void onDisplayChanged(int id) { GsrDisplay.publish(GsrGameActivity.this); }
	};

	@Override
	protected void onResume() {
		super.onResume();
		DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
		if (dm != null) dm.registerDisplayListener(displayListener, new Handler(Looper.getMainLooper()));
		GsrDisplay.publish(this);
		GsrDisplay.apply(this);
	}

	@Override
	protected void onPause() {
		DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
		if (dm != null) dm.unregisterDisplayListener(displayListener);
		super.onPause();
	}

	@Override
	protected void onMenuRequested() {
		if (menu == null) menu = new GsrMenu(this);
		menu.show();
	}

	@Override
	protected DescoreGameConfig getConfig() {
		return CONFIG;
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		GsrBuilderData.ensureExtracted(this);
		// The build can take ten minutes; do not let the screen sleep while it is on screen.
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		super.onCreate(savedInstanceState);
		try {
			// Diagnostics (logcat statistics, engine log forwarding) only in debuggable builds; never for players.
			GsrNative.setDebug((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0);
		} catch (Throwable ignored) {
			// native library not loaded yet
		}
		overlay = new GsrBuildOverlay(this);
		overlay.show();
		Intent service = new Intent(this, GsrBuildService.class);
		try {
			if (Build.VERSION.SDK_INT >= 26) {
				startForegroundService(service);
			} else {
				startService(service);
			}
		} catch (RuntimeException e) {
			// Not being allowed to start a service must not stop the game; the build still runs.
		}
	}

	/** True once the game engine has started (it lives in this process, not in the Activity). */
	private static boolean engineStarted() {
		try {
			String phase = GsrNative.buildInfo()[0];
			return "running".equals(phase) || "engine_stopped".equals(phase) || "engine_error".equals(phase);
		} catch (Throwable t) {
			return false;
		}
	}

	@Override
	protected void onDestroy() {
		if (overlay != null) overlay.stop();
		super.onDestroy();
		// The engine thread belongs to the process. If the game screen goes away (swiped out of the recent apps, or
		// dropped by the system) the process can linger with the old engine still in it, and pressing Play again would
		// start a SECOND engine beside it: slow, with broken audio. Once the game has started there is nothing worth
		// keeping, so end the process, as "Exit game" does. Rotation and similar changes are handled by configChanges.
		if (!isChangingConfigurations() && engineStarted()) android.os.Process.killProcess(android.os.Process.myPid());
	}
}
