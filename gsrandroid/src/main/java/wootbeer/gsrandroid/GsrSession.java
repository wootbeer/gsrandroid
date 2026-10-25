package wootbeer.gsrandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

import wootbeer.descore.DescoreGameActivity;

/**
 * What the running game asks of the Android side, polled a few times a second:
 *
 * The Lost Age's "Load a Golden Sun save file?" opens a chooser: Golden Sun's save in this app, or a .sav file
 * picked with Android's file browser (copied into the app first). The game reads the slots from it.
 *
 * Golden Sun's Seamless mode ends the game with code 75 + the slot it saved to. The Lost Age is then started
 * in a fresh process (GsrRestartActivity), told the slot through seamless_slot.txt (read by gsr_main.c).
 */
final class GsrSession {
	static final int REQUEST_PICK_SAVE = 4711;
	/** Seamless mode's exit codes (engine runtime/seamless.h kExitCodeBase, kSlotCount). */
	private static final int SEAMLESS_BASE = 75, SEAMLESS_SLOTS = 3;

	private final Activity activity;
	private final boolean tla;
	private final Handler handler = new Handler(Looper.getMainLooper());
	private boolean stopped, handingOver, choosing;

	private final Runnable tick = new Runnable() {
		@Override public void run() {
			if (stopped) return;
			poll();
			handler.postDelayed(this, 200);
		}
	};

	GsrSession(Activity a) {
		activity = a;
		tla = a.getIntent().getIntExtra(DescoreGameActivity.EXTRA_UNIT_ID, GsrConfig.UNIT_GAME) == GsrConfig.UNIT_TLA;
	}

	void start() { handler.postDelayed(tick, 500); }

	void stop() {
		stopped = true;
		handler.removeCallbacks(tick);
	}

	private File gameFile(String dir, String name) {
		return new File(activity.getFilesDir(), "gsr/" + dir + "/" + name);
	}

	private void poll() {
		try {
			if (tla && !choosing && GsrNative.pickTakeRequest()) showChooser();
			if (!tla && !handingOver) {
				String[] i = GsrNative.buildInfo();
				if (i != null && i.length >= 8 && "engine_stopped".equals(i[0])) {
					int code = Integer.parseInt(i[7]);
					if (code >= SEAMLESS_BASE && code < SEAMLESS_BASE + SEAMLESS_SLOTS) handOver(code - SEAMLESS_BASE + 1);
				}
			}
		} catch (Throwable ignored) {
			// native library not ready yet
		}
	}

	// ---- The Lost Age: the Golden Sun save chooser --------------------------------------------------------

	private void showChooser() {
		choosing = true;
		GsrNative.setMenuOpen(true);
		final File own = gameFile("game", "golden_sun.sav");
		final boolean haveOwn = own.isFile() && own.length() > 0;
		final String[] items = haveOwn
				? new String[] { "Golden Sun save in this app", "Choose a .sav file..." }
				: new String[] { "Choose a .sav file..." };
		new AlertDialog.Builder(activity)
				.setTitle("Load a Golden Sun save")
				.setItems(items, new DialogInterface.OnClickListener() {
					@Override public void onClick(DialogInterface d, int which) {
						if (haveOwn && which == 0) answer(own.getAbsolutePath());
						else browse();
					}
				})
				.setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
					@Override public void onClick(DialogInterface d, int which) { answer(""); }
				})
				.setOnCancelListener(new DialogInterface.OnCancelListener() {
					@Override public void onCancel(DialogInterface d) { answer(""); }
				})
				.show();
	}

	private void browse() {
		Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
		pick.addCategory(Intent.CATEGORY_OPENABLE);
		pick.setType("*/*");
		try {
			activity.startActivityForResult(pick, REQUEST_PICK_SAVE);
		} catch (RuntimeException e) {
			Toast.makeText(activity, "No file browser on this device", Toast.LENGTH_LONG).show();
			answer("");
		}
	}

	/** From the activity's onActivityResult. */
	void onPickResult(int resultCode, Intent data) {
		Uri uri = resultCode == Activity.RESULT_OK && data != null ? data.getData() : null;
		if (uri == null) { answer(""); return; }
		File out = gameFile("tla", "golden_sun_import.sav");
		try (InputStream in = activity.getContentResolver().openInputStream(uri);
		     OutputStream os = new FileOutputStream(out)) {
			if (in == null) throw new java.io.IOException("no data");
			byte[] buf = new byte[65536];
			long total = 0;
			for (int n; (n = in.read(buf)) > 0; ) {
				total += n;
				if (total > 1024 * 1024) throw new java.io.IOException("too large for a save file");
				os.write(buf, 0, n);
			}
		} catch (Exception e) {
			Toast.makeText(activity, "Could not read that file: " + e.getMessage(), Toast.LENGTH_LONG).show();
			out.delete();
			answer("");
			return;
		}
		answer(out.getAbsolutePath());
	}

	private void answer(String path) {
		if (!choosing) return;
		choosing = false;
		GsrNative.pickAnswer(path);
		GsrNative.setMenuOpen(false);
	}

	// ---- Golden Sun: Seamless mode hands over to The Lost Age ---------------------------------------------

	private void handOver(int slot) {
		handingOver = true;
		File marker = gameFile("tla", "seamless_slot.txt");
		try (OutputStream os = new FileOutputStream(marker)) {
			os.write(Integer.toString(slot).getBytes("US-ASCII"));
		} catch (Exception e) {
			Toast.makeText(activity, "Could not start The Lost Age: " + e.getMessage(), Toast.LENGTH_LONG).show();
			return;
		}
		Intent target = new Intent(activity, GsrGameActivity.class);
		target.putExtra(DescoreGameActivity.EXTRA_UNIT_ID, GsrConfig.UNIT_TLA);
		Intent restart = new Intent(activity, GsrRestartActivity.class);
		restart.putExtra(GsrRestartActivity.EXTRA_PID, Process.myPid());
		restart.putExtra(GsrRestartActivity.EXTRA_TARGET, target);
		activity.startActivity(restart);
		// GsrRestartActivity ends this process and starts The Lost Age once it is gone.
	}
}
