package wootbeer.gsrandroid;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The language of this app's own screens (the side menu, the Saves page, the launcher, the build screen and their
 * dialogs). The games' own text is untouched: that comes from the ROM.
 *
 * Setting: Default (the phone's language: Spanish phones get Spanish, everything else English), English, Español.
 * Every English text is its own key; a text with no Spanish entry stays English. Texts with a number or a name in
 * them are format strings (f): the Spanish entry keeps the same %s / %d placeholders.
 */
final class GsrLang {
	static final String[] CHOICES = { "Default", "English", "Español" };
	private static final String PREFS = "gsr_lang", KEY = "lang";
	private static Context app;

	private GsrLang() {}

	static void init(Context c) {
		if (c != null) app = c.getApplicationContext();
	}

	/** 0 Default (the phone's language), 1 English, 2 Español. */
	static int setting() {
		if (app == null) return 0;
		return app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY, 0);
	}

	static void set(int v) {
		if (app == null) return;
		SharedPreferences.Editor e = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
		e.putInt(KEY, v).apply();
	}

	static boolean spanish() {
		int s = setting();
		if (s == 2) return true;
		if (s == 1) return false;
		return "es".equals(Locale.getDefault().getLanguage());
	}

	static String t(String en) {
		if (en == null || !spanish()) return en;
		String es = ES.get(en);
		return es != null ? es : en;
	}

	static String f(String enFormat, Object... args) {
		return String.format(Locale.ROOT, t(enFormat), args);
	}

	/** A line from the game builder or the start-up (gsr_prepare.c, builder_main.cpp), some with a count in them. */
	static String stage(String en) {
		if (en == null || !spanish()) return en;
		if (en.startsWith("Compiling the game (")) return "Compilando el juego" + en.substring("Compiling the game".length())
				.replace(" files)", " archivos)");
		if (en.startsWith("Linking ")) return "Enlazando " + en.substring(8);
		return t(en);
	}

	private static final Map<String, String> ES = new HashMap<String, String>();

	private static void es(String en, String es) { ES.put(en, es); }

	static {
		// ---- general ------------------------------------------------------------------------------------------
		es("Default", "Predeterminado");
		es("Language", "Idioma");
		es("Off", "No");
		es("On", "Sí");
		es("Cancel", "Cancelar");
		es("OK", "Aceptar");
		es("Save", "Guardar");
		es("Load", "Cargar");
		es("Copy", "Copiar");
		es("Restore", "Restaurar");
		es("Clear", "Borrar");
		es("Reset", "Restablecer");
		es("Done", "Listo");
		es("open", "abrir");
		es("< Back", "< Volver");
		es("empty", "vacía");
		es("not set", "sin asignar");

		// ---- side menu: main page --------------------------------------------------------------------------------
		es("Settings", "Ajustes");
		es("D-pad: move    Left/Right: change    A: select    B or Back: back    R3: close",
				"Cruceta: mover    Izq./Der.: cambiar    A: elegir    B o Atrás: volver    R3: cerrar");
		es("Display", "Pantalla");
		es("Screen view", "Vista");
		es("Native 240x160", "Nativa 240x160");
		es("Expanded 360x240", "Ampliada 360x240");
		es("Orientation", "Orientación");
		es("Landscape", "Horizontal");
		es("Portrait", "Vertical");
		es("Renderer", "Renderizador");
		es("Auto", "Auto");
		es("GPU (experimental)", "GPU (experimental)");
		es("CPU (GPU not available)", "CPU (GPU no disponible)");
		es("Auto (%s)", "Auto (%s)");
		es("Scaling", "Escalado");
		es("Fit", "Ajustar");
		es("Whole pixels", "Píxeles enteros");
		es("Stretch", "Estirar");
		es("Zoom to fill", "Ampliar para llenar");
		es("Pixels", "Píxeles");
		es("Sharp", "Nítidos");
		es("Smooth", "Suaves");
		es("Screen color", "Color de pantalla");
		es("Raw", "Sin cambios");
		es("Unlit", "Sin luz");
		es("Frontlit", "Luz frontal");
		es("Backlit", "Retroiluminada");
		es("Classic", "Clásico");
		es("Handheld", "Portátil");
		es("Handheld (lighter)", "Portátil (más claro)");
		es("Soft", "Suave");
		es("Natural", "Natural");
		es("Warm", "Cálido");
		es("Deep", "Intenso");
		es("David's OG GBA", "GBA original de David");
		es("Screen filter", "Filtro de pantalla");
		es("LCD", "LCD");
		es("CRT", "CRT");
		es("Scanlines", "Líneas de barrido");
		es("Smooth (xBR)", "Suave (xBR)");
		es("Flicker reduction", "Reducir parpadeo");
		es("Light", "Bajo");
		es("Medium", "Medio");
		es("Strong", "Alto");
		es("Frame interpolation", "Interpolación de fotogramas");
		es("Not available (needs a 120 Hz display)", "No disponible (necesita una pantalla de 120 Hz)");
		es("On (120 Hz)", "Sí (120 Hz)");
		es("On (Native view only)", "Sí (solo en la vista nativa)");
		es("High-res rendering", "Renderizado en alta resolución");
		es("On (off while frame interpolation is on)", "Sí (no con la interpolación activada)");
		es("High-res quality", "Calidad en alta resolución");
		es("Show FPS", "Mostrar FPS");
		es("Speed", "Velocidad");
		es("Fast-forward speed", "Velocidad del avance rápido");
		es("Fast-forward button", "Botón de avance rápido");
		es("Hold", "Mantener");
		es("Toggle", "Alternar");
		es("As fast as possible", "Lo más rápido posible");
		es("Mute while fast-forwarding", "Silenciar en avance rápido");
		es("Audio", "Sonido");
		es("Volume", "Volumen");
		es("Mute", "Silenciar");
		es("Controls", "Controles");
		es("Gamepad buttons", "Botones del mando");
		es("Reset gamepad buttons", "Restablecer botones del mando");
		es("Gamepad buttons reset", "Botones del mando restablecidos");
		es("Touch pad", "Control táctil");
		es("Joystick", "Joystick");
		es("D-pad", "Cruceta");
		es("Touch controls size", "Tamaño de los controles táctiles");
		es("Edit touch layout", "Editar controles táctiles");
		es("Portrait controls", "Controles en vertical");
		es("Spread", "Repartidos");
		es("Compact, under the picture", "Juntos, bajo la imagen");
		es("Compact, at the bottom", "Juntos, abajo");
		es("Touch controls opacity", "Opacidad de los controles táctiles");
		es("Game", "Juego");
		es("Save states", "Estados guardados");
		es("Saves", "Partidas");
		es("Cheats", "Trucos");
		es("Enemy HP bar", "Barra de PV enemiga");
		es("Custom Battle Mode", "Modo de batalla personalizada");
		es("Resume", "Continuar");
		es("Switch game", "Cambiar de juego");
		es("Exit game", "Salir del juego");
		es("Change ROM", "Cambiar ROM");
		es("About", "Acerca de");
		es("Credits", "Créditos");

		// ---- cheats ------------------------------------------------------------------------------------------------
		es("Party", "Grupo");
		es("Infinite HP", "PV infinitos");
		es("Infinite PP", "PP infinitos");
		es("Battle rewards", "Recompensas de batalla");
		es("Experience", "Experiencia");
		es("Coins", "Monedas");
		es("Drop chance", "Probabilidad de objetos");

		// ---- save states -------------------------------------------------------------------------------------------
		es("Slot %d  (%s)", "Espacio %d  (%s)");
		es("Save to slot %d", "Guardar en el espacio %d");
		es("Load slot %d", "Cargar el espacio %d");
		es("Overwrite slot %d?", "¿Sobrescribir el espacio %d?");
		es("The state saved there is replaced.", "Se sustituye el estado guardado ahí.");
		es("Load slot %d?", "¿Cargar el espacio %d?");
		es("Progress since your last save is lost.", "Se pierde lo jugado desde que guardaste.");
		es("Save states are separate from the game's own saves and may not load after an app update.",
				"Los estados guardados son aparte de las partidas del juego y puede que no carguen tras actualizar la app.");
		es("Saved to slot %d", "Guardado en el espacio %d");
		es("Saving to slot %d...", "Guardando en el espacio %d...");

		// ---- saves -------------------------------------------------------------------------------------------------
		es("%s save  (%s)", "Partida de %s  (%s)");
		es("saved %s", "guardada %s");
		es("none yet", "aún no hay");
		es("Export save...", "Exportar partida...");
		es("Import save...", "Importar partida...");
		es("Import a %s save?", "¿Importar una partida de %s?");
		es("Pick a .sav file from a GBA emulator, a flash cart or an export. It replaces the save in this app; the current one is kept as a backup first.\n\nThe game closes afterwards. Open it again to play with the imported save.",
				"Elige un archivo .sav de un emulador de GBA, un flash cart o una exportación. Sustituye la partida de esta app; antes se guarda una copia de la actual.\n\nDespués el juego se cierra. Ábrelo otra vez para jugar con la partida importada.");
		es("Choose file", "Elegir archivo");
		es("Copy the USA/Europe save", "Copiar la partida de EE. UU./Europa");
		es("Copy the USA/Europe save?", "¿Copiar la partida de EE. UU./Europa?");
		es("Your %s (USA/Europe) save, from %s, replaces the save here. The current one is kept as a backup.\n\nThe game closes afterwards. Open it again to play.",
				"Tu partida de %s (EE. UU./Europa), del %s, sustituye a la de aquí. Se guarda una copia de la actual.\n\nDespués el juego se cierra. Ábrelo otra vez para jugar.");
		es("Save copied.", "Partida copiada.");
		es("Could not copy: %s", "No se pudo copiar: %s");
		es("Restore a backup...", "Restaurar una copia...");
		es("Backup", "Copia de seguridad");
		es("Back up all saves to a folder...", "Copiar todas las partidas a una carpeta...");
		es("Copies all your saves (both games, every language) into a new folder in the place you pick (for example Downloads, or a cloud drive), so they survive uninstalling the app. Save states are not included.",
				"Copia todas tus partidas (los dos juegos, todos los idiomas) en una carpeta nueva donde elijas (por ejemplo Descargas o una nube), para no perderlas si desinstalas la app. Los estados guardados no se incluyen.");
		es("Move to GSRecomp", "Pasar a GSRecomp");
		es("Copy saves for GSRecomp...", "Copiar partidas para GSRecomp...");
		es("Makes a \"Saves for GSRecomp\" folder with all your saves, already named the way GSRecomp wants. Copy the files in it into GSRecomp's Saves folder. Save states don't carry over.",
				"Crea una carpeta \"Saves for GSRecomp\" con todas tus partidas, ya con los nombres que usa GSRecomp. Copia sus archivos a la carpeta Saves de GSRecomp. Los estados guardados no se pasan.");
		es("No file browser on this device", "No hay explorador de archivos en este dispositivo");
		es("Replaced %s", "Sustituida %s");
		es("Restore which save?", "¿Qué partida restaurar?");
		es("Restore this save?", "¿Restaurar esta partida?");
		es("The current save is kept as a backup. The game closes afterwards; open it again to play.",
				"Se guarda una copia de la partida actual. Después el juego se cierra; ábrelo otra vez para jugar.");
		es("Save restored.", "Partida restaurada.");
		es("Could not restore: %s", "No se pudo restaurar: %s");
		es("The game closes now. Open it again to play.", "El juego se cierra ahora. Ábrelo otra vez para jugar.");
		es("Save exported", "Partida exportada");
		es("Save imported.", "Partida importada.");
		es("There are no saves yet", "Aún no hay partidas");
		es("Copied %d saves. Put the files in GSRecomp's Saves folder.",
				"%d partidas copiadas. Pon los archivos en la carpeta Saves de GSRecomp.");
		es("Copied 1 save. Put the file in GSRecomp's Saves folder.",
				"1 partida copiada. Pon el archivo en la carpeta Saves de GSRecomp.");
		es("Backed up %d saves", "%d partidas copiadas");
		es("Backed up 1 save", "1 partida copiada");
		es("Could not do that: %s", "No se pudo hacer: %s");
		// GsrSaves' messages, shown after "Could not do that: "
		es("the file is too large to be a save", "el archivo es demasiado grande para ser una partida");
		es("this is not a Golden Sun save (%d bytes; a save is 64 KB)", "no es una partida de Golden Sun (%d bytes; una partida ocupa 64 KB)");
		es("there is no saved game in this file", "no hay ninguna partida en este archivo");
		es("this is a Lost Age save. Open The Lost Age to import it there", "es una partida de La Edad Perdida. Abre La Edad Perdida para importarla allí");
		es("this is a Golden Sun save. Open Golden Sun to import it there", "es una partida de Golden Sun. Abre Golden Sun para importarla allí");
		es("could not write there", "no se pudo escribir ahí");
		es("could not read that file", "no se pudo leer ese archivo");
		es("could not make a folder there", "no se pudo crear una carpeta ahí");
		es("could not write %s", "no se pudo escribir %s");

		// ---- change ROM, credits, gamepad buttons ------------------------------------------------------------------
		es("Change ROM?", "¿Cambiar la ROM?");
		es("This removes the stored ROM and closes the game. Your save is kept.\n\nOpen the app again to pick a new ROM file.",
				"Esto quita la ROM guardada y cierra el juego. Tu partida se conserva.\n\nAbre la app otra vez para elegir otra ROM.");
		es("Could not remove the ROM", "No se pudo quitar la ROM");
		es("ROM removed. Open the app again to pick one.", "ROM quitada. Abre la app otra vez para elegir una.");
		es("No app can open %s", "Ninguna app puede abrir %s");
		es("Android port", "Versión para Android");
		es("Android port by wootbeer", "Versión para Android de wootbeer");
		es("Golden Sun Recompiled by Shmargus", "Golden Sun Recompiled de Shmargus");
		es("Engine: gbarecomp by Matthew Stan", "Motor: gbarecomp de Matthew Stan");
		es("Notices", "Avisos");
		es("Free and noncommercial. Contains no Golden Sun game data: your own ROM is translated and compiled on this device and never leaves it. Golden Sun and Golden Sun: The Lost Age belong to Nintendo and Camelot Software Planning.",
				"Gratis y sin fines comerciales. No contiene datos de Golden Sun: tu propia ROM se traduce y compila en este dispositivo y nunca sale de él. Golden Sun y Golden Sun: La Edad Perdida pertenecen a Nintendo y Camelot Software Planning.");
		es("Built with mGBA and JRickey/gba-recomp (via gbarecomp) and TinyCC (LGPL). Full credits and licences are in the COPYING file of the source.",
				"Hecho con mGBA y JRickey/gba-recomp (a través de gbarecomp) y TinyCC (LGPL). Los créditos y licencias completos están en el archivo COPYING del código fuente.");
		es("Screen filters", "Filtros de pantalla");
		es("LCD3x by Gigaherz and CRT Lottes by Timothy Lottes (public domain), and xBR-lv2 by Hyllian, ported by Golden Sun Recompiled from libretro's glsl-shaders.",
				"LCD3x de Gigaherz y CRT Lottes de Timothy Lottes (dominio público), y xBR-lv2 de Hyllian, adaptados por Golden Sun Recompiled desde glsl-shaders de libretro.");
		es("Pick a row, then press the button you want. These only change the game; in menus A always selects and B always goes back.",
				"Elige una fila y pulsa el botón que quieras. Solo cambian el juego; en los menús A siempre elige y B siempre vuelve.");
		es("%s (second button)", "%s (segundo botón)");
		es("Press a button for %s", "Pulsa un botón para %s");
		es("Press the gamepad button to use. Back cancels.", "Pulsa el botón del mando que quieras usar. Atrás cancela.");
		es("Select", "Select");
		es("Start", "Start");
		es("Right", "Derecha");
		es("Left", "Izquierda");
		es("Up", "Arriba");
		es("Down", "Abajo");
		es("Fast forward", "Avance rápido");
		es("Menu", "Menú");

		// ---- game titles and languages (GsrConfig) -----------------------------------------------------------------
		es("Golden Sun: The Lost Age", "Golden Sun: La Edad Perdida");
		es("Spanish", "Español");
		es("Italian", "Italiano");
		es("French", "Francés");
		es("German", "Alemán");

		// ---- launcher (descore) and GsrConfig ----------------------------------------------------------------------
		es("%s game data needed", "Se necesitan los datos de %s");
		es("Choose File", "Elegir archivo");
		es("Choose Folder", "Elegir carpeta");
		es("Copying game data...", "Copiando los datos del juego...");
		es("Add More Game Data", "Añadir más datos de juego");
		es("Play %s", "Jugar a %s");
		es("%s -- missing %s", "%s -- falta %s");
		es(" (%d of %d optional files found)", " (%d de %d archivos opcionales encontrados)");
		es("Start my last game straight away", "Empezar directamente mi último juego");
		es("This Android version can't select external files.", "Esta versión de Android no puede elegir archivos externos.");
		es("Didn't find any of the game's files in that folder.", "No se encontró ningún archivo del juego en esa carpeta.");
		es("Couldn't read that folder: %s", "No se pudo leer esa carpeta: %s");
		es("Couldn't read that file.", "No se pudo leer ese archivo.");
		es("%d files for %s", "%d archivos para %s");
		es("1 file for %s", "1 archivo para %s");
		es("Copied %s.", "Copiado: %s.");
		es("Select your own Golden Sun or Golden Sun: The Lost Age GBA ROM file (USA/Europe, Spanish, Italian, French or German). You can add more later.\n\nThe file can have any name, but it must end in .gba. Zipped ROMs must be extracted first.",
				"Elige tu propio archivo ROM de GBA de Golden Sun o Golden Sun: La Edad Perdida (EE. UU./Europa, español, italiano, francés o alemán). Puedes añadir más después.\n\nEl archivo puede tener cualquier nombre, pero debe terminar en .gba. Las ROM comprimidas hay que descomprimirlas antes.");

		// ---- build screen and start-up (GsrBuildOverlay, gsr_prepare.c, the builder) -------------------------------
		es("No usable ROM found", "No se encontró una ROM válida");
		es("Restart the app and pick your %s (USA or Europe) .gba file.", "Reinicia la app y elige tu archivo .gba de %s (EE. UU. o Europa).");
		es("This is not the supported ROM", "Esta no es la ROM compatible");
		es("%s (USA or Europe) is required. Press Back to open the menu and choose Change ROM.",
				"Se necesita %s (EE. UU. o Europa). Pulsa Atrás para abrir el menú y elige Cambiar ROM.");
		es("Building the game failed", "No se pudo preparar el juego");
		es("Log: %s", "Registro: %s");
		es("Close and reopen the app to try again.", "Cierra y vuelve a abrir la app para intentarlo otra vez.");
		es("The game engine could not start", "No se pudo iniciar el motor del juego");
		es("The game engine stopped (code %s)", "El motor del juego se detuvo (código %s)");
		es("Close and reopen the app. If this keeps happening, the log in the app folder says why.",
				"Cierra y vuelve a abrir la app. Si sigue pasando, el registro de la carpeta de la app dice por qué.");
		es("Starting the game", "Iniciando el juego");
		es("Loaded from the saved build in %s.", "Cargado de lo ya preparado en %s.");
		es("Built in %s.", "Preparado en %s.");
		es("Loading %s", "Cargando %s");
		es("Preparing %s for this device", "Preparando %s para este dispositivo");
		es("This only happens once. It can take 10 minutes or more. Keep the device awake.",
				"Solo pasa una vez. Puede tardar 10 minutos o más. Mantén el dispositivo encendido.");
		es("Checking your ROM", "Comprobando tu ROM");
		es("Translating the game from your ROM", "Traduciendo el juego desde tu ROM");
		es("Translating the main game code. This is the longest step and can take a while on slower devices.",
				"Traduciendo el código principal del juego. Es el paso más largo y puede tardar en dispositivos lentos.");
		es("Preparing The Lost Age battle effects", "Preparando los efectos de batalla de La Edad Perdida");
		es("Preparing the game code", "Preparando el código del juego");
		es("The game code has not changed", "El código del juego no ha cambiado");
		es("Compiling the game on this device", "Compilando el juego en este dispositivo");
		es("Loading the game", "Cargando el juego");
		es("Ready", "Listo");
		es("Game setup", "Preparación del juego");
		es("Preparing the game from your ROM (first run only)", "Preparando el juego desde tu ROM (solo la primera vez)");

		// ---- The Lost Age: loading a Golden Sun save (GsrSession) --------------------------------------------------
		es("Golden Sun save in this app", "Partida de Golden Sun de esta app");
		es("Choose a .sav file...", "Elegir un archivo .sav...");
		es("Load a Golden Sun save", "Cargar una partida de Golden Sun");
		es("Could not read that file: %s", "No se pudo leer ese archivo: %s");
		es("Could not start The Lost Age: %s", "No se pudo iniciar La Edad Perdida: %s");
	}
}
