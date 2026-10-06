package wootbeer.gsrandroid;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

/**
 * The startup / first-run build screen: black, with the same plain text styling as the shared Descore
 * asset picker, and the progress bar. It sits on top of the game surface and goes away by itself once the
 * game is drawing. All the state comes from the native side ({@link GsrNative#buildInfo()}).
 */
final class GsrBuildOverlay {
	private final Activity activity;
	private final Handler handler = new Handler(Looper.getMainLooper());
	private final FrameLayout root;
	private final TextView title, detail, percent, note, error;
	private final ProgressBar bar;
	private boolean done;

	private final Runnable tick = new Runnable() {
		@Override public void run() {
			if (done) return;
			update();
			if (!done) handler.postDelayed(this, 250);
		}
	};

	GsrBuildOverlay(Activity a) {
		activity = a;
		root = new FrameLayout(a);
		root.setBackgroundColor(Color.BLACK);
		// Never take focus or touches from the game view underneath (gamepad keys must reach it).
		root.setFocusable(false);
		root.setClickable(false);

		LinearLayout col = new LinearLayout(a);
		col.setOrientation(LinearLayout.VERTICAL);
		col.setGravity(Gravity.CENTER_HORIZONTAL);
		int pad = dp(24);
		col.setPadding(pad, pad, pad, pad);
		FrameLayout.LayoutParams cp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
		root.addView(col, cp);

		title = text(col, Color.WHITE, 22, 0);
		detail = text(col, Color.LTGRAY, 16, 16);
		bar = new ProgressBar(a, null, android.R.attr.progressBarStyleHorizontal);
		bar.setMax(1000);
		bar.setIndeterminate(false);
		if (Build.VERSION.SDK_INT >= 21) {
			bar.setProgressTintList(ColorStateList.valueOf(Color.WHITE));
			bar.setProgressBackgroundTintList(ColorStateList.valueOf(Color.rgb(60, 60, 60)));
		}
		LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(0, dp(10));
		bp.topMargin = dp(20);
		bp.width = Math.min(dp(520), a.getResources().getDisplayMetrics().widthPixels - 2 * pad);
		col.addView(bar, bp);
		percent = text(col, Color.WHITE, 16, 8);
		note = text(col, Color.GRAY, 14, 24);
		error = text(col, Color.rgb(255, 120, 120), 16, 16);
	}

	private TextView text(LinearLayout parent, int color, float size, int topDp) {
		TextView t = new TextView(activity);
		t.setTextColor(color);
		t.setTextSize(size);
		t.setGravity(Gravity.CENTER);
		t.setPadding(0, dp(topDp), 0, 0);
		parent.addView(t);
		return t;
	}

	private int dp(float v) {
		DisplayMetrics m = activity.getResources().getDisplayMetrics();
		return (int) (v * m.density + 0.5f);
	}

	void show() {
		activity.addContentView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT));
		update();
		handler.postDelayed(tick, 250);
	}

	void stop() {
		done = true;
		handler.removeCallbacks(tick);
	}

	private static String clock(int sec) {
		return (sec / 60) + ":" + (sec % 60 < 10 ? "0" : "") + (sec % 60);
	}

	private void set(TextView t, String s) {
		if (s == null || s.length() == 0) {
			t.setVisibility(View.GONE);
		} else {
			t.setText(s);
			t.setVisibility(View.VISIBLE);
		}
	}

	private void update() {
		String[] i;
		try {
			i = GsrNative.buildInfo();
		} catch (Throwable t) {
			return; // native library not ready yet
		}
		if (i == null || i.length < 8) return;
		String phase = i[0];
		if ("running".equals(phase)) {
			root.setVisibility(View.GONE);
			stop();
			return;
		}
		root.setVisibility(View.VISIBLE);
		int progress = 0, elapsed = 0;
		try { progress = Integer.parseInt(i[3]); } catch (NumberFormatException ignored) {}
		try { elapsed = Integer.parseInt(i[4]); } catch (NumberFormatException ignored) {}
		boolean cached = "1".equals(i[5]);

		bar.setVisibility(View.GONE);
		set(percent, null);
		set(note, null);
		set(error, null);
		set(detail, null);

		if ("rom_missing".equals(phase)) {
			title.setTextColor(Color.rgb(255, 120, 120));
			set(title, "No usable ROM found");
			set(detail, "Restart the app and pick your Golden Sun (USA or Europe) .gba file.");
		} else if ("rom_wrong".equals(phase)) {
			title.setTextColor(Color.rgb(255, 120, 120));
			set(title, "This is not the supported ROM");
			set(detail, "Golden Sun (USA or Europe) is required. Clear the app's storage to pick another file.");
		} else if ("failed".equals(phase)) {
			title.setTextColor(Color.rgb(255, 120, 120));
			set(title, "Building the game failed");
			set(error, i[2]);
			set(detail, "Log: " + i[6]);
			set(note, "Close and reopen the app to try again.");
		} else if ("engine_error".equals(phase)) {
			title.setTextColor(Color.rgb(255, 120, 120));
			set(title, "The game engine could not start");
			set(error, i[2]);
		} else if ("engine_stopped".equals(phase)) {
			title.setTextColor(Color.rgb(255, 120, 120));
			set(title, "The game engine stopped (code " + i[7] + ")");
			set(detail, "Close and reopen the app. If this keeps happening, the log in the app folder says why.");
		} else if ("ready".equals(phase)) {
			title.setTextColor(Color.WHITE);
			set(title, "Starting the game");
			set(detail, (cached ? "Loaded from the saved build" : "Built") + " in " + clock(elapsed) + ".");
		} else { // building / waiting
			title.setTextColor(Color.WHITE);
			set(title, cached ? "Loading Golden Sun" : "Preparing Golden Sun for this device");
			set(detail, i[1]);
			bar.setProgress(progress);
			bar.setVisibility(View.VISIBLE);
			set(percent, (progress / 10) + "%    " + clock(elapsed));
			if (!cached) set(note, "This only happens once. It can take 10 minutes or more. Keep the device awake.");
		}
	}
}
