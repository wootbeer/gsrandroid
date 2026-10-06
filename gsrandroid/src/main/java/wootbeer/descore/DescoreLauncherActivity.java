package wootbeer.descore;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Shared Storage Access Framework asset picker + "is my game data ready" screen for every
 * descore-shell port. Subclasses only say which {@link DescoreGameConfig} describes their game and
 * which {@link DescoreGameActivity} subclass to start; everything else - the folder prompt, the
 * recursive folder scan, the verified copy into private storage, the ready/missing status screen
 * and the Play buttons - lives here so every port built on descore gets the same behavior and
 * the same bug fixes.
 *
 * Two things set it apart from a game-specific picker: the folder scan is recursive (a player can
 * point at a whole game folder and we find the files wherever they are, first match wins), and what
 * to copy comes from the config instead of being hardcoded per game.
 */
public abstract class DescoreLauncherActivity extends Activity {
	private static final String TAG = "DescoreLauncher";
	private static final int REQUEST_CODE_OPEN_DATA_FOLDER = 4242;
	/** Deepest sub-folder level the recursive scan will descend into below the picked folder. */
	private static final int MAX_SCAN_DEPTH = 6;
	/** Safety valve so picking something enormous (a whole drive) can't scan forever. */
	private static final int MAX_SCAN_ENTRIES = 30000;

	protected abstract DescoreGameConfig getConfig();

	protected abstract Class<? extends DescoreGameActivity> getGameActivityClass();

	private File rootDir;
	private float buttonSizeBias;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setImmersive();
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
					new View.OnSystemUiVisibilityChangeListener() {
						@Override
						public void onSystemUiVisibilityChange(int visibility) {
							if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
								setImmersive();
							}
						}
					});
		}
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		buttonSizeBias = (float) Math.min(Math.max((metrics.widthPixels / metrics.xdpi
				+ metrics.heightPixels / metrics.ydpi) / 5.5f, 1), 1.4);

		rootDir = new File(getFilesDir(), getConfig().rootDirName);
		if (launchIfComplete()) {
			return;
		}
		if (anyUnitReady()) {
			showUnitSelect(null);
		} else {
			showDataPicker(null);
		}
	}

	/**
	 * Single-unit games that opt in (DescoreGameConfig.autoLaunchWhenComplete): once every required
	 * and optional file is present, skip the picker/status screens and go straight into the game.
	 * The launcher finishes itself, so quitting the game returns to the home screen rather than here.
	 */
	private boolean launchIfComplete() {
		DescoreGameConfig cfg = getConfig();
		if (!cfg.autoLaunchWhenComplete || cfg.units.size() != 1) return false;
		DescoreGameConfig.Unit unit = cfg.units.get(0);
		if (!isUnitReady(unit) || presentOptional(unit).size() != unit.optionalFiles.length) return false;
		Intent intent = new Intent(this, getGameActivityClass());
		intent.putExtra(DescoreGameActivity.EXTRA_UNIT_ID, unit.id);
		startActivity(intent);
		finish();
		return true;
	}

	// --- Readiness ---------------------------------------------------------------------------

	private File unitDir(DescoreGameConfig.Unit unit) {
		return new File(rootDir, unit.dirName);
	}

	private boolean isUnitReady(DescoreGameConfig.Unit unit) {
		return missingFiles(unit).isEmpty();
	}

	private boolean anyUnitReady() {
		for (DescoreGameConfig.Unit u : getConfig().units) {
			if (isUnitReady(u)) return true;
		}
		return false;
	}

	private List<String> missingFiles(DescoreGameConfig.Unit unit) {
		List<String> missing = new ArrayList<>();
		File dir = unitDir(unit);
		for (String name : unit.requiredFiles) {
			File f = new File(dir, name);
			if (!f.exists() || f.length() <= 0) missing.add(name);
		}
		return missing;
	}

	/** Optional files that did make it into private storage, for the status line. */
	private List<String> presentOptional(DescoreGameConfig.Unit unit) {
		List<String> present = new ArrayList<>();
		File dir = unitDir(unit);
		for (String name : unit.optionalFiles) {
			File f = new File(dir, name);
			if (f.exists() && f.length() > 0) present.add(name);
		}
		return present;
	}

	// --- UI -----------------------------------------------------------------------------------

	private LinearLayout newScreen(int gravity) {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(gravity);
		layout.setBackgroundColor(Color.BLACK);
		int pad = (int) dpToPx(24);
		layout.setPadding(pad, (int) dpToPx(8), pad, pad);
		return layout;
	}

	private TextView addText(LinearLayout parent, String text, int color, float size, int topPadDp) {
		TextView tv = new TextView(this);
		tv.setText(text);
		tv.setTextColor(color);
		if (size > 0) tv.setTextSize(size);
		tv.setGravity(Gravity.CENTER);
		tv.setPadding(0, (int) dpToPx(topPadDp), 0, 0);
		parent.addView(tv);
		return tv;
	}

	private void showDataPicker(String errorMessage) {
		LinearLayout layout = newScreen(Gravity.CENTER);
		addText(layout, getConfig().title + " game data needed", Color.WHITE, 22, 0);
		addText(layout, getConfig().pickerHint, Color.LTGRAY, 0, 16);
		if (errorMessage != null) {
			addText(layout, errorMessage, Color.rgb(255, 120, 120), 0, 16);
		}
		Button choose = new Button(this);
		choose.setText(getConfig().pickSingleFile ? "Choose File" : "Choose Folder");
		choose.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		choose.measure(View.MeasureSpec.UNSPECIFIED, View.MeasureSpec.UNSPECIFIED);
		layout.addView(choose, buttonParams(choose.getMeasuredWidth() + (int) dpToPx(16)));
		setContentView(layout);
	}

	private void showCopyingProgress() {
		LinearLayout layout = newScreen(Gravity.CENTER);
		layout.addView(new ProgressBar(this));
		addText(layout, "Copying game data...", Color.WHITE, 0, 16);
		setContentView(layout);
	}

	private void showUnitSelect(String statusMessage) {
		LinearLayout layout = newScreen(Gravity.CENTER_HORIZONTAL);
		addText(layout, getConfig().title, Color.WHITE, 22, 0);
		if (statusMessage != null) {
			addText(layout, statusMessage, Color.rgb(150, 220, 150), 0, 12);
		}

		Button addMore = new Button(this);
		addMore.setText("Add More Game Data");
		addMore.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		addMore.measure(View.MeasureSpec.UNSPECIFIED, View.MeasureSpec.UNSPECIFIED);
		int buttonWidth = addMore.getMeasuredWidth() + (int) dpToPx(16);

		for (final DescoreGameConfig.Unit unit : getConfig().units) {
			if (isUnitReady(unit)) {
				List<String> optional = presentOptional(unit);
				String extra = optional.isEmpty() ? ""
						: " (" + optional.size() + " of " + unit.optionalFiles.length
						+ " optional files found)";
				addText(layout, unit.title + extra, Color.WHITE, 0, 12);
				Button play = new Button(this);
				play.setText("Play " + unit.title);
				play.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View v) {
						Intent intent = new Intent(DescoreLauncherActivity.this,
								getGameActivityClass());
						intent.putExtra(DescoreGameActivity.EXTRA_UNIT_ID, unit.id);
						startActivity(intent);
					}
				});
				layout.addView(play, buttonParams(buttonWidth));
			} else {
				addText(layout, unit.title + " -- missing " + join(missingFiles(unit)),
						Color.GRAY, 0, 12);
			}
		}

		LinearLayout.LayoutParams p = buttonParams(buttonWidth);
		p.topMargin = (int) dpToPx(20);
		layout.addView(addMore, p);
		setContentView(layout);
	}

	private LinearLayout.LayoutParams buttonParams(int width) {
		return new LinearLayout.LayoutParams(width, LinearLayout.LayoutParams.WRAP_CONTENT);
	}

	private static String join(List<String> values) {
		StringBuilder sb = new StringBuilder();
		for (int i = 0; i < values.size(); i++) {
			if (i > 0) sb.append(", ");
			sb.append(values.get(i));
		}
		return sb.toString();
	}

	// --- Folder pick + copy ---------------------------------------------------------------------

	private void openFolderPicker() {
		if (Build.VERSION.SDK_INT < 21) {
			showDataPicker("This Android version can't select external files.");
			return;
		}
		if (getConfig().pickSingleFile) {
			Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
			pick.addCategory(Intent.CATEGORY_OPENABLE);
			pick.setType("*/*"); // ROM dumps have no registered MIME type
			startActivityForResult(pick, REQUEST_CODE_OPEN_DATA_FOLDER);
			return;
		}
		startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE),
				REQUEST_CODE_OPEN_DATA_FOLDER);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == REQUEST_CODE_OPEN_DATA_FOLDER) {
			if (resultCode != RESULT_OK || data == null || data.getData() == null) {
				return; // cancelled - leave whichever screen was already showing
			}
			copyDataFromTree(data.getData(), getConfig().pickSingleFile);
		}
	}

	private void copyDataFromTree(final Uri treeUri, final boolean singleFile) {
		showCopyingProgress();
		new Thread(new Runnable() {
			@Override
			public void run() {
				final String result = singleFile ? doCopySingleFile(treeUri) : doCopyData(treeUri);
				runOnUiThread(new Runnable() {
					@Override
					public void run() {
						if (anyUnitReady()) {
							showUnitSelect(result);
						} else {
							showDataPicker(result != null ? result
									: "Didn't find any of the game's files in that folder.");
						}
					}
				});
			}
		}).start();
	}

	/** Background thread: scan the picked tree once, then copy every file any unit wants. */
	private String doCopyData(Uri treeUri) {
		Map<String, Uri> found = new HashMap<>();
		try {
			int[] budget = {MAX_SCAN_ENTRIES};
			scanTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri), 0, found, budget);
		} catch (IOException e) {
			Log.i(TAG, "doCopyData: failed to list picked folder: " + e.getMessage());
			return "Couldn't read that folder: " + e.getMessage();
		}
		Log.i(TAG, "doCopyData: scan found " + found.size() + " distinct file names");
		return copyFound(found);
	}

	/** Single-file mode: the picked document is the whole "tree". */
	private String doCopySingleFile(Uri docUri) {
		Map<String, Uri> found = new HashMap<>();
		Cursor cursor = getContentResolver().query(docUri,
				new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null);
		String name = null;
		if (cursor != null) {
			try {
				if (cursor.moveToFirst()) name = cursor.getString(0);
			} finally {
				cursor.close();
			}
		}
		if (name == null) name = docUri.getLastPathSegment();
		if (name == null) return "Couldn't read that file.";
		found.put(name.toLowerCase(Locale.US), docUri);
		return copyFound(found);
	}

	/** Copies every found file any unit wants (aliased files go under their stored name). */
	private String copyFound(Map<String, Uri> found) {
		//noinspection ResultOfMethodCallIgnored
		rootDir.mkdirs();
		List<String> summary = new ArrayList<>();
		for (DescoreGameConfig.Unit unit : getConfig().units) {
			File dir = unitDir(unit);
			int copied = 0;
			boolean sawAny = false;
			List<String> aliasesDone = new ArrayList<>();
			for (Map.Entry<String, Uri> entry : found.entrySet()) {
				String lowerName = entry.getKey();
				if (!unit.wants(lowerName)) continue;
				String storeName = unit.aliasFor(lowerName);
				if (storeName == null) {
					storeName = lowerName;
				} else if (aliasesDone.contains(storeName)) {
					continue; // first file with that extension wins
				}
				sawAny = true;
				//noinspection ResultOfMethodCallIgnored
				dir.mkdirs();
				if (copyOneFile(entry.getValue(), lowerName, new File(dir, storeName))) {
					copied++;
					aliasesDone.add(storeName);
				}
			}
			if (sawAny) {
				summary.add(copied + " file" + (copied == 1 ? "" : "s") + " for " + unit.title);
			}
		}
		if (summary.isEmpty()) return null;
		return "Copied " + join(summary) + ".";
	}

	/**
	 * Recursively lists the picked tree into {@code out}, keyed by lower-cased display name. The
	 * first file found with a given name wins, so a clean top-level copy beats a stray duplicate
	 * buried in a backup folder that happens to be visited later.
	 */
	private void scanTree(Uri treeUri, String parentDocId, int depth, Map<String, Uri> out,
			int[] budget) throws IOException {
		Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
		Cursor cursor = getContentResolver().query(childrenUri, new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME,
				DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null);
		if (cursor == null) throw new IOException("folder query returned nothing");
		List<String> subDirs = new ArrayList<>();
		try {
			while (cursor.moveToNext() && budget[0] > 0) {
				budget[0]--;
				String docId = cursor.getString(0);
				String name = cursor.getString(1);
				String mime = cursor.getString(2);
				if (name == null) continue;
				if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
					subDirs.add(docId);
				} else {
					String key = name.toLowerCase(Locale.US);
					if (!out.containsKey(key)) {
						out.put(key, DocumentsContract.buildDocumentUriUsingTree(treeUri, docId));
					}
				}
			}
		} finally {
			cursor.close();
		}
		if (depth < MAX_SCAN_DEPTH) {
			for (String dirId : subDirs) {
				if (budget[0] <= 0) break;
				scanTree(treeUri, dirId, depth + 1, out, budget);
			}
		}
	}

	/**
	 * Copies one file to a temp name, verifies the byte count against the provider's reported size
	 * (an interrupted SAF read can otherwise silently leave a short file that an existence-only
	 * check would accept), then renames into place. Logs and returns false rather than throwing so
	 * one bad file can't abort the rest.
	 */
	private boolean copyOneFile(Uri sourceUri, String displayName, File destination) {
		File temp = new File(destination.getParentFile(), destination.getName() + ".tmp");
		InputStream in = null;
		OutputStream out = null;
		try {
			long expectedSize = queryDocumentSize(sourceUri);
			in = getContentResolver().openInputStream(sourceUri);
			if (in == null) {
				Log.i(TAG, "copyOneFile: could not open " + displayName);
				return false;
			}
			out = new FileOutputStream(temp);
			byte[] buffer = new byte[64 * 1024];
			long total = 0;
			int read;
			while ((read = in.read(buffer)) != -1) {
				out.write(buffer, 0, read);
				total += read;
			}
			out.flush();
			out.close();
			out = null;
			in.close();
			in = null;
			if (expectedSize >= 0 && total != expectedSize) {
				Log.i(TAG, "copyOneFile: " + displayName + " incomplete, got " + total + " of "
						+ expectedSize + " bytes");
				//noinspection ResultOfMethodCallIgnored
				temp.delete();
				return false;
			}
			//noinspection ResultOfMethodCallIgnored
			destination.delete();
			if (!temp.renameTo(destination)) {
				Log.i(TAG, "copyOneFile: renameTo(" + destination + ") failed");
				return false;
			}
			return true;
		} catch (IOException e) {
			Log.i(TAG, "copyOneFile: " + displayName + ": " + e.getMessage());
			//noinspection ResultOfMethodCallIgnored
			temp.delete();
			return false;
		} finally {
			if (in != null) {
				try {
					in.close();
				} catch (IOException ignored) {
				}
			}
			if (out != null) {
				try {
					out.close();
				} catch (IOException ignored) {
				}
			}
		}
	}

	private long queryDocumentSize(Uri documentUri) {
		Cursor cursor = getContentResolver().query(documentUri,
				new String[]{DocumentsContract.Document.COLUMN_SIZE}, null, null, null);
		if (cursor == null) return -1;
		try {
			if (cursor.moveToFirst() && !cursor.isNull(0)) return cursor.getLong(0);
			return -1;
		} finally {
			cursor.close();
		}
	}

	// --- Lifecycle / misc --------------------------------------------------------------------

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
	}

	private float dpToPx(float dp) {
		DisplayMetrics metrics = getResources().getDisplayMetrics();
		return dp * (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	private void setImmersive() {
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setSystemUiVisibility(
					View.SYSTEM_UI_FLAG_LAYOUT_STABLE
							| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
							| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_FULLSCREEN
							| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}
}
