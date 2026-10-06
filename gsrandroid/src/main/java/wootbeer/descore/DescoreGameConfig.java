package wootbeer.descore;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Everything a descore-shell game has to tell the shared launcher/game classes about itself.
 * A game supplies one of these (see wootbeer.gsrandroid.GsrConfig) and gets the asset
 * picker, the copy-into-private-storage step, and the game/render-thread shell for free.
 *
 * Game data is proprietary and is never bundled: the player points the launcher at a folder
 * holding their own copy, and the launcher copies only the files named here into private app
 * storage under {@code <filesDir>/<rootDirName>/<unit.dirName>/}. Native code then finds
 * everything with plain relative or absolute file paths, no Android storage APIs involved.
 *
 * A "unit" is one independently playable/verifiable chunk of game data: a game with several
 * separately installed episodes has one unit per episode; a single-ROM game has one unit.
 */
public final class DescoreGameConfig {

	public static final class Unit {
		public final int id;
		public final String title;
		/** Sub-directory of the game's private root this unit's files are copied into. */
		public final String dirName;
		/** Lower-case file names that must be present and non-empty for the unit to be ready. */
		public final String[] requiredFiles;
		/** Lower-case file names copied if found but not needed for "ready". */
		public final String[] optionalFiles;
		/** Lower-case extensions (with the dot) - every file with one is copied if found. */
		public final String[] optionalExtensions;
		/**
		 * For data that players own under arbitrary file names (a ROM dump, say): pairs of
		 * {lower-case extension with the dot, name to store it under}. The first file found with the
		 * extension is copied to the stored name, which can then be listed in requiredFiles.
		 */
		public final String[][] extensionAliases;

		public Unit(int id, String title, String dirName, String[] requiredFiles,
				String[] optionalFiles, String[] optionalExtensions) {
			this(id, title, dirName, requiredFiles, optionalFiles, optionalExtensions,
					new String[0][]);
		}

		public Unit(int id, String title, String dirName, String[] requiredFiles,
				String[] optionalFiles, String[] optionalExtensions, String[][] extensionAliases) {
			this.id = id;
			this.title = title;
			this.dirName = dirName;
			this.requiredFiles = requiredFiles;
			this.optionalFiles = optionalFiles;
			this.optionalExtensions = optionalExtensions;
			this.extensionAliases = extensionAliases;
		}

		/** The name to store a found file under, or null when this unit does not alias it. */
		String aliasFor(String lowerName) {
			for (String[] a : extensionAliases) {
				if (lowerName.endsWith(a[0])) return a[1];
			}
			return null;
		}

		boolean wants(String lowerName) {
			if (aliasFor(lowerName) != null) return true;
			for (String f : requiredFiles) {
				if (f.equals(lowerName)) return true;
			}
			for (String f : optionalFiles) {
				if (f.equals(lowerName)) return true;
			}
			for (String ext : optionalExtensions) {
				if (lowerName.endsWith(ext)) return true;
			}
			return false;
		}
	}

	/** Shown in the launcher's header. */
	public final String title;
	/** Name passed to System.loadLibrary(): the game's own native shared library. */
	public final String nativeLibrary;
	/** Directory under getFilesDir() holding every unit's copied data. */
	public final String rootDirName;
	/** One-line hint on the picker screen naming the kinds of files to look for. */
	public final String pickerHint;
	public final List<Unit> units;
	/**
	 * When true and the game has a single unit whose required AND optional files are all in private
	 * storage, the launcher skips its own screens and starts the game straight away (the picker only
	 * comes back if a file goes missing, e.g. after clearing the app's data or a reinstall).
	 */
	public final boolean autoLaunchWhenComplete;
	/** True: the picker asks for a single file (a ROM) instead of a folder. */
	public boolean pickSingleFile = false;
	/** EGL context version: 1 (default, the 2D ports) or 3 (GLES 3.x, the 3D ports). */
	public int glesMajor = 1;
	/** Depth/stencil bits requested for the EGL surface (0 = none, the 2D default). */
	public int depthBits = 0;
	public int stencilBits = 0;

	public DescoreGameConfig(String title, String nativeLibrary, String rootDirName,
			String pickerHint, List<Unit> units) {
		this(title, nativeLibrary, rootDirName, pickerHint, units, false);
	}

	public DescoreGameConfig(String title, String nativeLibrary, String rootDirName,
			String pickerHint, List<Unit> units, boolean autoLaunchWhenComplete) {
		this.autoLaunchWhenComplete = autoLaunchWhenComplete;
		this.title = title;
		this.nativeLibrary = nativeLibrary;
		this.rootDirName = rootDirName;
		this.pickerHint = pickerHint;
		this.units = Collections.unmodifiableList(new ArrayList<>(units));
	}

	/** Makes the picker ask for one file (for example the game's ROM) rather than a folder. */
	public DescoreGameConfig withFilePicker() {
		this.pickSingleFile = true;
		return this;
	}

	/**
	 * Requests a GLES 3 context with a depth/stencil buffer. With GLES 3 the EGL context survives a
	 * lost Surface (only the window surface is rebuilt), so GL objects stay valid across screen
	 * lock / app switching. The 1.x default keeps the old rebuild-everything behavior.
	 */
	public DescoreGameConfig withGles3(int depthBits, int stencilBits) {
		this.glesMajor = 3;
		this.depthBits = depthBits;
		this.stencilBits = stencilBits;
		return this;
	}

	public Unit unitById(int id) {
		for (Unit u : units) {
			if (u.id == id) return u;
		}
		return units.get(0);
	}
}
