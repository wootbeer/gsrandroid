package wootbeer.gsrandroid;

import java.util.ArrayList;
import java.util.List;

import wootbeer.descore.DescoreGameConfig;

/**
 * Golden Sun Recompiled's description for the shared descore launcher.
 *
 * The player supplies their own Golden Sun and (optionally) Golden Sun: The Lost Age (USA/Europe) GBA ROMs,
 * under any file names: the launcher tells them apart by the game code in the ROM header and stores them as
 * golden_sun.gba and lost_age.gba in each game's private folder, where the native side checks the SHA-1
 * before starting.
 */
final class GsrConfig {
	private GsrConfig() {
	}

	static final int UNIT_GAME = 1;
	static final int UNIT_TLA = 2;
	/** Names the picked ROMs are stored under (kept in sync with gsr_game.c). */
	static final String ROM_NAME = "golden_sun.gba";
	static final String TLA_ROM_NAME = "lost_age.gba";

	static DescoreGameConfig create() {
		List<DescoreGameConfig.Unit> units = new ArrayList<>();
		// The GBA header's game code (offset 0xAC) tells the two ROMs apart, so either can be picked any time.
		units.add(new DescoreGameConfig.Unit(
				UNIT_GAME,
				"Golden Sun",
				"game",
				new String[]{ROM_NAME},
				new String[]{},
				new String[]{},
				new String[][]{{".gba", ROM_NAME}}).withAliasCheck(0xAC, "AGSE"));
		units.add(new DescoreGameConfig.Unit(
				UNIT_TLA,
				"Golden Sun: The Lost Age",
				"tla",
				new String[]{TLA_ROM_NAME},
				new String[]{},
				new String[]{},
				new String[][]{{".gba", TLA_ROM_NAME}}).withAliasCheck(0xAC, "AGFE"));
		return new DescoreGameConfig(
				"Golden Sun Recompiled",
				"gsrhost",
				"gsr",
				"Select your own Golden Sun or Golden Sun: The Lost Age (USA or Europe) GBA ROM file. "
						+ "You can add the other game later.\n\n"
						+ "The file can have any name, but it must end in .gba. Zipped ROMs "
						+ "must be extracted first.",
				units,
				true)   // only for a single unit: with two games the launcher always asks which to play
				.withFilePicker()
				.withGles3(0, 0);
	}
}
