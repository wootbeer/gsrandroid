package wootbeer.gsrandroid;

import java.util.ArrayList;
import java.util.List;

import wootbeer.descore.DescoreGameConfig;

/**
 * Golden Sun Recompiled's description for the shared descore launcher.
 *
 * The only thing the player supplies is their own Golden Sun (USA/Europe) GBA ROM. It can have any
 * file name: the launcher takes the first .gba it is given and stores it as golden_sun.gba in the
 * game's private folder, where the native side checks its SHA-1 before starting.
 */
final class GsrConfig {
	private GsrConfig() {
	}

	static final int UNIT_GAME = 1;
	/** Name the picked ROM is stored under (kept in sync with GSR_ROM_NAME in gsr_main.c). */
	static final String ROM_NAME = "golden_sun.gba";

	static DescoreGameConfig create() {
		List<DescoreGameConfig.Unit> units = new ArrayList<>();
		units.add(new DescoreGameConfig.Unit(
				UNIT_GAME,
				"Golden Sun",
				"game",
				new String[]{ROM_NAME},
				new String[]{},
				new String[]{},
				new String[][]{{".gba", ROM_NAME}}));
		return new DescoreGameConfig(
				"Golden Sun Recompiled",
				"gsrhost",
				"gsr",
				"Select your own Golden Sun (USA or Europe) GBA ROM file.\n\n"
						+ "The file can have any name, but it must end in .gba. Zipped ROMs "
						+ "must be extracted first.",
				units,
				true)   // once the ROM is in place, skip the picker on every later launch
				.withFilePicker()
				.withGles3(0, 0);
	}
}
