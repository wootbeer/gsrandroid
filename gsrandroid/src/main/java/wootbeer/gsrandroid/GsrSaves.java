package wootbeer.gsrandroid;

import android.app.Activity;
import android.content.ContentResolver;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * The games' own saves (not save states): the cartridge's 64 KB flash, kept beside each ROM as golden_sun.sav and
 * lost_age.sav, the same raw format GBA emulators and flash carts use. Imports keep the save they replace in
 * files/gsr/backups (the newest few per game), so an import can be undone.
 */
final class GsrSaves {
	static final int FLASH_BYTES = 64 * 1024;
	private static final int KEEP_BACKUPS = 5;

	private final Activity activity;

	GsrSaves(Activity a) { activity = a; }

	static String saveName(boolean tla) { return tla ? "lost_age.sav" : "golden_sun.sav"; }

	File saveFile(boolean tla) {
		return new File(activity.getFilesDir(), (tla ? "gsr/tla/" : "gsr/game/") + saveName(tla));
	}

	private File backupDir() {
		File d = new File(activity.getFilesDir(), "gsr/backups");
		d.mkdirs();
		return d;
	}

	/** The import backups of one game, newest first. */
	File[] backups(boolean tla) {
		final String prefix = saveName(tla).replace(".sav", "-");
		File[] all = backupDir().listFiles();
		List<File> out = new ArrayList<File>();
		if (all != null) for (File f : all) if (f.getName().startsWith(prefix) && f.getName().endsWith(".sav")) out.add(f);
		File[] r = out.toArray(new File[0]);
		Arrays.sort(r, new java.util.Comparator<File>() {
			@Override public int compare(File a, File b) { return b.getName().compareTo(a.getName()); }
		});
		return r;
	}

	private static String stamp() {
		return new SimpleDateFormat("yyyy-MM-dd_HHmmss", Locale.US).format(new Date());
	}

	private static byte[] readAll(InputStream in, int limit) throws IOException {
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		byte[] buf = new byte[65536];
		for (int n; (n = in.read(buf)) > 0; ) {
			out.write(buf, 0, n);
			if (out.size() > limit) throw new IOException("the file is too large to be a save");
		}
		return out.toByteArray();
	}

	private static void write(File f, byte[] data) throws IOException {
		File tmp = new File(f.getPath() + ".tmp");
		try (OutputStream os = new FileOutputStream(tmp)) { os.write(data); }
		if (!tmp.renameTo(f)) { tmp.delete(); throw new IOException("could not replace " + f.getName()); }
	}

	private static byte[] readFile(File f) throws IOException {
		try (InputStream in = new FileInputStream(f)) { return readAll(in, 4 * FLASH_BYTES); }
	}

	/**
	 * Accepts a 64 KB flash save, or a 128 KB one whose second half is unused (some emulators write that size).
	 * Returns the 64 KB the game reads, or throws with a reason the player can read.
	 */
	static byte[] checkSave(byte[] data) throws IOException {
		if (data.length == FLASH_BYTES) return data;
		if (data.length == 2 * FLASH_BYTES) {
			boolean unused = true;
			byte first = data[FLASH_BYTES];
			for (int i = FLASH_BYTES; i < data.length && unused; i++) unused = data[i] == first && (first == 0 || first == (byte) 0xFF);
			if (unused) return Arrays.copyOf(data, FLASH_BYTES);
		}
		throw new IOException("this is not a Golden Sun save (" + data.length + " bytes; a save is 64 KB)");
	}

	/** Writes this game's save to a document the player created. */
	void exportTo(boolean tla, Uri uri) throws IOException {
		byte[] data = readFile(saveFile(tla));
		try (OutputStream os = activity.getContentResolver().openOutputStream(uri, "wt")) {
			if (os == null) throw new IOException("could not write there");
			os.write(data);
		}
	}

	/** Replaces this game's save with the file at uri; the old save goes to the backups first. */
	void importFrom(boolean tla, Uri uri) throws IOException {
		byte[] data;
		try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
			if (in == null) throw new IOException("could not read that file");
			data = checkSave(readAll(in, 4 * FLASH_BYTES));
		}
		backupCurrent(tla);
		write(saveFile(tla), data);
	}

	/** Puts a backup back as the game's save (the current one is backed up too). */
	void restore(boolean tla, File backup) throws IOException {
		byte[] data = checkSave(readFile(backup));
		backupCurrent(tla);
		write(saveFile(tla), data);
	}

	private void backupCurrent(boolean tla) throws IOException {
		File cur = saveFile(tla);
		if (!cur.isFile() || cur.length() == 0) return;
		write(new File(backupDir(), saveName(tla).replace(".sav", "-") + stamp() + ".sav"), readFile(cur));
		File[] all = backups(tla);
		for (int i = KEEP_BACKUPS; i < all.length; i++) all[i].delete();
	}

	/** Copies both games' saves (those that exist) into a new folder inside the folder the player picked. */
	int backupAllTo(Uri tree) throws IOException {
		ContentResolver cr = activity.getContentResolver();
		Uri parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
		Uri folder = DocumentsContract.createDocument(cr, parent, DocumentsContract.Document.MIME_TYPE_DIR,
				"Golden Sun saves " + stamp());
		if (folder == null) throw new IOException("could not make a folder there");
		int n = 0;
		for (boolean tla : new boolean[] { false, true }) {
			File f = saveFile(tla);
			if (!f.isFile() || f.length() == 0) continue;
			Uri doc = DocumentsContract.createDocument(cr, folder, "application/octet-stream", saveName(tla));
			if (doc == null) throw new IOException("could not write " + saveName(tla));
			try (OutputStream os = cr.openOutputStream(doc, "w")) {
				if (os == null) throw new IOException("could not write " + saveName(tla));
				os.write(readFile(f));
			}
			n++;
		}
		return n;
	}
}
