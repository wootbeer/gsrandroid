package wootbeer.gsrandroid;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Unpacks assets/gsr_builder_data (GSRecomp's translator configuration: function addresses, section
 * ranges and hashes of the pinned ROM) to files/gsr/builder_data. Re-done after every app update.
 * Contains no game code and no ROM bytes; the ROM is the player's own file.
 */
final class GsrBuilderData {
	private static final String ASSET_DIR = "gsr_builder_data";
	private static final String MARKER = ".extracted";

	private GsrBuilderData() {
	}

	static void ensureExtracted(Context context) {
		try {
			File root = new File(new File(context.getFilesDir(), "gsr"), "builder_data");
			String version = String.valueOf(context.getPackageManager()
					.getPackageInfo(context.getPackageName(), 0).lastUpdateTime);
			File marker = new File(root, MARKER);
			if (marker.exists() && version.equals(readText(marker))) {
				return;
			}
			deleteRecursive(root);
			if (!root.mkdirs() && !root.isDirectory()) {
				return;
			}
			copyTree(context.getAssets(), ASSET_DIR, root);
			try (OutputStream out = new FileOutputStream(marker)) {
				out.write(version.getBytes("UTF-8"));
			}
		} catch (Exception e) {
			// The native side reports "builder data missing" on screen if this did not work.
		}
	}

	private static void copyTree(AssetManager assets, String assetPath, File target) throws IOException {
		String[] children = assets.list(assetPath);
		if (children != null && children.length > 0) {
			if (!target.isDirectory() && !target.mkdirs()) {
				throw new IOException("cannot create " + target);
			}
			for (String child : children) {
				copyTree(assets, assetPath + "/" + child, new File(target, child));
			}
			return;
		}
		try (InputStream in = assets.open(assetPath); OutputStream out = new FileOutputStream(target)) {
			byte[] buf = new byte[64 * 1024];
			int n;
			while ((n = in.read(buf)) > 0) {
				out.write(buf, 0, n);
			}
		}
	}

	private static String readText(File f) throws IOException {
		try (InputStream in = new java.io.FileInputStream(f)) {
			byte[] buf = new byte[256];
			int n = in.read(buf);
			return n > 0 ? new String(buf, 0, n, "UTF-8") : "";
		}
	}

	private static void deleteRecursive(File f) {
		File[] kids = f.listFiles();
		if (kids != null) {
			for (File k : kids) {
				deleteRecursive(k);
			}
		}
		f.delete();
	}
}
