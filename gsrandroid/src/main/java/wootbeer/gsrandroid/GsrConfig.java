package wootbeer.gsrandroid;

import android.app.Activity;

import java.util.ArrayList;
import java.util.List;

import wootbeer.descore.DescoreGameActivity;
import wootbeer.descore.DescoreGameConfig;

/**
 * Golden Sun Recompiled's description for the shared descore launcher.
 *
 * The player supplies their own Golden Sun and/or Golden Sun: The Lost Age GBA ROMs, in any of the languages
 * upstream supports (USA/Europe, Spanish, Italian, French, German), under any file names. Each game and
 * language is a launcher unit of its own: the launcher tells the ROMs apart by the game code in the ROM header
 * and stores each as golden_sun.gba or lost_age.gba in that unit's private folder (game, tla, game_es,
 * tla_es, ...), where the native side checks the SHA-1 before building that language's game code. So every
 * language keeps its own game build, save and save states, as upstream does.
 */
final class GsrConfig {
	private GsrConfig() {
	}

	/** Upstream's revision order (rom_revisions.inc): USA/Europe, Spain, Italy, France, Germany. */
	static final String[] LANG_CODES = { "", "es", "it", "fr", "de" };
	static final String[] LANG_NAMES = { "", "Spanish", "Italian", "French", "German" };
	/** The GBA header's game code (offset 0xAC) of each language's ROM. */
	private static final String[] TBS_CODES = { "AGSE", "AGSS", "AGSI", "AGSF", "AGSD" };
	private static final String[] TLA_CODES = { "AGFE", "AGFS", "AGFI", "AGFF", "AGFD" };

	/** Unit ids: Golden Sun 1, 3, 5, 7, 9 and The Lost Age 2, 4, 6, 8, 10, by language. */
	static final int UNIT_GAME = 1;
	/** Names the picked ROMs are stored under (kept in sync with gsr_game.c). */
	static final String ROM_NAME = "golden_sun.gba";
	static final String TLA_ROM_NAME = "lost_age.gba";

	static boolean isTla(int unitId) { return unitId % 2 == 0; }
	static int lang(int unitId) { return Math.max(0, Math.min(4, (unitId - 1) / 2)); }
	static int unitId(boolean tla, int lang) { return 1 + 2 * lang + (tla ? 1 : 0); }
	/** The unit's folder under files/gsr. */
	static String dirName(boolean tla, int lang) {
		return (tla ? "tla" : "game") + (lang == 0 ? "" : "_" + LANG_CODES[lang]);
	}
	static String title(boolean tla, int lang) {
		return (tla ? "Golden Sun: The Lost Age" : "Golden Sun") + (lang == 0 ? "" : " (" + LANG_NAMES[lang] + ")");
	}

	/** The unit this game screen plays. */
	static int unitOf(Activity a) {
		return a.getIntent().getIntExtra(DescoreGameActivity.EXTRA_UNIT_ID, UNIT_GAME);
	}

	static DescoreGameConfig create() {
		List<DescoreGameConfig.Unit> units = new ArrayList<>();
		for (int lang = 0; lang < LANG_CODES.length; lang++) {
			for (int g = 0; g < 2; g++) {
				boolean tla = g == 1;
				String rom = tla ? TLA_ROM_NAME : ROM_NAME;
				DescoreGameConfig.Unit u = new DescoreGameConfig.Unit(
						unitId(tla, lang),
						title(tla, lang),
						dirName(tla, lang),
						new String[]{rom},
						new String[]{},
						new String[]{},
						new String[][]{{".gba", rom}}).withAliasCheck(0xAC, (tla ? TLA_CODES : TBS_CODES)[lang]);
				// The other languages only show up in the launcher once their ROM has been added.
				if (lang != 0) u.withHideWhenMissing();
				units.add(u);
			}
		}
		return new DescoreGameConfig(
				"Golden Sun Recompiled",
				"gsrhost",
				"gsr",
				"Select your own Golden Sun or Golden Sun: The Lost Age GBA ROM file (USA/Europe, Spanish, "
						+ "Italian, French or German). You can add more later.\n\n"
						+ "The file can have any name, but it must end in .gba. Zipped ROMs "
						+ "must be extracted first.",
				units,
				true)   // only for a single unit; with several the launcher asks which to play (or starts the last one)
				.withFilePicker()
				.withGles3(0, 0);
	}
}
