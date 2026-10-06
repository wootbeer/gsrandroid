package wootbeer.gsrandroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Process;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/**
 * The in-game settings menu: a translucent panel over the running game (the game is paused while it is open, but
 * keeps drawing so display changes show at once). Opened with Back, the Menu button on a gamepad, or the on-screen
 * MENU button.
 *
 * It is made of ordinary Android views, so it works with touch and with a gamepad (d-pad moves, A selects, left and
 * right change a value, B or Back goes back) and a controller's A and B keep that meaning here no matter how the
 * gamepad buttons are mapped inside the game. Every change takes effect immediately and is saved by the native side.
 */
final class GsrMenu {
	private static final int TEXT = 0xFFE8EEF4, DIM = 0xFF9AA8B8, ACCENT = 0xFF3D7CB8, PANEL = 0xE8101820;

	private static final String[] VIEW = { "Native 240x160", "Expanded 360x240" };
	private static final String[] SCALE = { "Fit", "Whole pixels", "Stretch", "Zoom to fill" };
	private static final String[] FLICKER = { "Off", "Light", "Medium", "Strong" };
	private static final String[] FILTER = { "Sharp", "Smooth" };
	private static final String[] SCREEN = { "Raw", "Unlit", "Frontlit", "Backlit", "Classic" };
	private static final String[] OFF_ON = { "Off", "On" };
	private static final String[] HOLD_TOGGLE = { "Hold", "Toggle" };
	private static final int[] FF_VALUES = { 15, 20, 30, 40, 60, 80, 100, 160 };
	private static final String[] FF_LABELS = { "1.5x", "2x", "3x", "4x", "6x", "8x", "10x", "16x" };
	private static final String[] CHEAT_STEPS = { "0x", "0.5x", "1x", "2x", "5x", "10x", "25x", "50x", "100x" };
	private static final String[] CHEAT_DROP_STEPS = { "0x", "0.5x", "1x", "2x", "5x", "10x" };
	private static final String[] TOUCH_SIZE = { "0.90x", "0.95x", "1.00x", "1.10x", "1.20x", "1.35x", "1.50x", "1.65x", "1.80x" };

	private final Activity activity;
	private final float density;
	private Dialog dialog;
	private LinearLayout list;
	private TextView title;
	private ScrollView scroll;
	private boolean open;
	private int page; // 0 main, 1 gamepad buttons, 2 credits, 3 cheats

	GsrMenu(Activity activity) {
		this.activity = activity;
		this.density = activity.getResources().getDisplayMetrics().density;
	}

	// ---------------------------------------------------------------------------------------------------------------

	void show() {
		if (open) return;
		open = true;
		GsrNative.setMenuOpen(true);

		dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_Dialog_NoActionBar);
		Window w = dialog.getWindow();
		w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
		w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
		w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);

		LinearLayout root = new LinearLayout(activity);
		root.setOrientation(LinearLayout.HORIZONTAL);

		View outside = new View(activity); // tapping the game area closes the menu
		outside.setOnClickListener(new View.OnClickListener() {
			@Override public void onClick(View v) { close(); }
		});
		// A clickable view is focusable by default, and Android (API 26+) outlines a focused view with a thin
		// "default focus highlight" (the 1 px coloured frame around the visible game area). Same fix as DescoreView.
		noFocusHighlight(outside);
		outside.setFocusable(false);
		root.addView(outside, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.55f));

		LinearLayout panel = new LinearLayout(activity);
		panel.setOrientation(LinearLayout.VERTICAL);
		panel.setBackgroundColor(PANEL);
		noFocusHighlight(panel);
		noFocusHighlight(root);
		panel.setPadding(dp(14), dp(10), dp(14), dp(8));
		root.addView(panel, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.45f));

		title = new TextView(activity);
		title.setTextColor(TEXT);
		title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
		title.setTypeface(Typeface.DEFAULT_BOLD);
		title.setPadding(dp(4), 0, 0, dp(6));
		panel.addView(title);

		scroll = new ScrollView(activity);
		scroll.setFocusable(false);
		noFocusHighlight(scroll);
		list = new LinearLayout(activity);
		list.setOrientation(LinearLayout.VERTICAL);
		scroll.addView(list);
		panel.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

		TextView hint = new TextView(activity);
		hint.setTextColor(DIM);
		hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
		hint.setText("D-pad: move    Left/Right: change    A: select    B or Back: back    R3: close");
		hint.setPadding(dp(4), dp(6), 0, 0);
		panel.addView(hint);

		dialog.setContentView(root);
		dialog.setOnKeyListener(new DialogInterface.OnKeyListener() {
			@Override
			public boolean onKey(DialogInterface d, int keyCode, KeyEvent event) {
				if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_BUTTON_B) {
					// On the press, not the release: the release of the Back press that OPENED the menu arrives
					// here as well, and acting on it would close the menu again straight away.
					if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) goBack();
					return true;
				}
				// The buttons that open the menu (R3, Mode) also close it.
				if (keyCode == KeyEvent.KEYCODE_BUTTON_THUMBR || keyCode == KeyEvent.KEYCODE_BUTTON_MODE) {
					// Close on the press, not the release: the release of the press that OPENED the menu lands here
					// too (the dialog has focus by then), and acting on it would close the menu straight away.
					if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) close();
					return true;
				}
				return false;
			}
		});
		dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
			@Override public void onDismiss(DialogInterface d) {
				open = false;
				GsrNative.setMenuOpen(false);
			}
		});
		dialog.show();
		dialog.getWindow().getDecorView().setSystemUiVisibility(
				View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
						| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
						| View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		showMain();
	}

	private void close() {
		if (dialog != null) dialog.dismiss();
	}

	private void goBack() {
		if (page != 0) showMain(); else close();
	}

	// ---------------------------------------------------------------------------------------------------------------

	private static void noFocusHighlight(View v) {
		if (android.os.Build.VERSION.SDK_INT >= 26) v.setDefaultFocusHighlightEnabled(false);
	}

	private int dp(float v) { return (int) (v * density + 0.5f); }

	private void clear(String heading, int pageNo) {
		page = pageNo;
		title.setText(heading);
		list.removeAllViews();
		scroll.scrollTo(0, 0);
	}

	private void section(String text) {
		TextView t = new TextView(activity);
		t.setText(text.toUpperCase());
		t.setTextColor(ACCENT);
		t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
		t.setTypeface(Typeface.DEFAULT_BOLD);
		t.setPadding(dp(6), dp(12), 0, dp(2));
		list.addView(t);
	}

	/** A row: "label" on the left, a value on the right. Click cycles; left/right on a d-pad steps. */
	private abstract class Row extends TextView {
		private final String label;

		Row(String label) {
			super(activity);
			this.label = label;
			setFocusable(true);
			setClickable(true);
			noFocusHighlight(this);
			setTextColor(TEXT);
			setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
			setMinHeight(dp(46));
			setGravity(Gravity.CENTER_VERTICAL);
			setPadding(dp(10), dp(4), dp(10), dp(4));
			StateListDrawable bg = new StateListDrawable();
			GradientDrawable on = new GradientDrawable();
			on.setColor(ACCENT);
			on.setCornerRadius(dp(6));
			bg.addState(new int[] { android.R.attr.state_focused }, on);
			bg.addState(new int[] { android.R.attr.state_pressed }, on);
			setBackgroundDrawable(bg);
			list.addView(this, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
					ViewGroup.LayoutParams.WRAP_CONTENT));
			setOnClickListener(new View.OnClickListener() {
				@Override public void onClick(View v) { step(1); }
			});
		}

		abstract String value();
		abstract void step(int dir);

		void refresh() {
			String v = value();
			setText(v == null ? label : label + "     " + v);
		}

		@Override
		public boolean onKeyDown(int keyCode, KeyEvent event) {
			if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) { step(-1); return true; }
			if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) { step(1); return true; }
			return super.onKeyDown(keyCode, event);
		}
	}

	private Row choice(String label, final int setting, final int[] values, final String[] labels) {
		Row r = new Row(label) {
			private int index() {
				int cur = GsrNative.getSetting(setting), best = 0;
				for (int i = 0; i < values.length; i++) {
					if (Math.abs(values[i] - cur) < Math.abs(values[best] - cur)) best = i;
				}
				return best;
			}
			@Override String value() { return labels[index()]; }
			@Override void step(int dir) {
				int i = (index() + dir + values.length) % values.length;
				GsrNative.setSetting(setting, values[i]);
				refresh();
			}
		};
		r.refresh();
		return r;
	}

	private Row choice(String label, int setting, String[] labels) {
		int[] v = new int[labels.length];
		for (int i = 0; i < v.length; i++) v[i] = i;
		return choice(label, setting, v, labels);
	}

	/** 2x frame interpolation: only offered on a screen that has a 120 Hz mode, and only used in the Native view. */
	private Row interpolation() {
		Row r = new Row("Frame interpolation") {
			private boolean capable() { return GsrDisplay.maxRefresh(activity) >= 100f; }
			@Override String value() {
				if (!capable()) return "Not available (needs a 120 Hz display)";
				if (GsrNative.getSetting(GsrNative.INTERP) == 0) return "Off";
				return GsrNative.getSetting(GsrNative.VIEW_MODE) == 0 ? "On (120 Hz)" : "On (Native view only)";
			}
			@Override void step(int dir) {
				if (!capable()) return;
				GsrNative.setSetting(GsrNative.INTERP, GsrNative.getSetting(GsrNative.INTERP) == 0 ? 1 : 0);
				GsrDisplay.apply(activity);
				refresh();
			}
			@Override void refresh() {
				super.refresh();
				setAlpha(capable() ? 1f : 0.45f);
			}
		};
		r.refresh();
		return r;
	}

	private static final int[] HIRES_VALUES = { 2, 3, 4, 6 };
	private static final String[] HIRES_LABELS = { "2x", "3x", "4x", "6x" };

	/** High-resolution rendering (the engine's own "native renderer"): the Native view drawn supersampled. */
	private Row highRes() {
		Row r = new Row("High-res rendering") {
			private boolean usable() {
				return GsrNative.getSetting(GsrNative.VIEW_MODE) == 0
						&& GsrNative.getSetting(GsrNative.INTERP) == 0;
			}
			@Override String value() {
				if (GsrNative.getSetting(GsrNative.NATIVE_RENDER) == 0) return "Off";
				if (GsrNative.getSetting(GsrNative.VIEW_MODE) != 0) return "On (Native view only)";
				if (GsrNative.getSetting(GsrNative.INTERP) != 0) return "On (off while frame interpolation is on)";
				return "On";
			}
			@Override void step(int dir) {
				GsrNative.setSetting(GsrNative.NATIVE_RENDER, GsrNative.getSetting(GsrNative.NATIVE_RENDER) == 0 ? 1 : 0);
				refresh();
			}
			@Override void refresh() {
				super.refresh();
				setAlpha(usable() || GsrNative.getSetting(GsrNative.NATIVE_RENDER) == 0 ? 1f : 0.55f);
			}
		};
		r.refresh();
		return r;
	}

	private static final String[] RENDERER = { "Auto", "CPU", "GPU" };

	/** Auto / CPU / GPU. When the GPU renderer could not start on this device the row is greyed and stays on CPU. */
	private Row renderer() {
		Row r = new Row("Renderer") {
			private boolean gpuOff() { return GsrNative.gpuStatus() == 2; }
			@Override String value() {
				if (gpuOff()) return "CPU (GPU not available)";
				int cur = GsrNative.getSetting(GsrNative.RENDERER);
				String now = GsrNative.gpuActive() == 1 ? "GPU" : "CPU";
				return cur == 0 ? "Auto (" + now + ")" : RENDERER[cur];
			}
			@Override void step(int dir) {
				if (gpuOff()) return;
				int i = (GsrNative.getSetting(GsrNative.RENDERER) + dir + 3) % 3;
				GsrNative.setSetting(GsrNative.RENDERER, i);
				if (i == 0) GsrNative.setSetting(GsrNative.AUTO_CPU, 0);  // picking Auto again re-tests the GPU path
				refresh();
			}
			@Override void refresh() {
				super.refresh();
				setAlpha(gpuOff() ? 0.45f : 1f);
			}
		};
		r.refresh();
		return r;
	}

	private Row action(String label, final String value, final Runnable run) {
		Row r = new Row(label) {
			@Override String value() { return value; }
			@Override void step(int dir) { if (dir > 0) run.run(); }
		};
		r.refresh();
		return r;
	}

	// ---------------------------------------------------------------------------------------------------------------

	private void showMain() {
		clear("Settings", 0);

		section("Display");
		choice("Screen view", GsrNative.VIEW_MODE, VIEW);
		renderer();
		choice("Scaling", GsrNative.SCALE_MODE, SCALE);
		choice("Pixels", GsrNative.FILTER, FILTER);
		choice("Screen color", GsrNative.SCREEN, SCREEN);
		choice("Flicker reduction", GsrNative.BLEND, FLICKER);
		interpolation();
		highRes();
		choice("High-res quality", GsrNative.NATIVE_SCALE, HIRES_VALUES, HIRES_LABELS);
		choice("Show FPS", GsrNative.SHOW_FPS, OFF_ON);

		section("Speed");
		choice("Fast-forward speed", GsrNative.FF_MULT10, FF_VALUES, FF_LABELS);
		choice("Fast-forward button", GsrNative.FF_MODE, HOLD_TOGGLE);
		choice("As fast as possible", GsrNative.FF_UNCAPPED, OFF_ON);
		choice("Mute while fast-forwarding", GsrNative.FF_MUTE, OFF_ON);

		section("Audio");
		int[] vols = new int[11];
		String[] volLabels = new String[11];
		for (int i = 0; i <= 10; i++) { vols[i] = i * 10; volLabels[i] = (i * 10) + "%"; }
		choice("Volume", GsrNative.VOLUME, vols, volLabels);
		choice("Mute", GsrNative.MUTE, OFF_ON);

		section("Controls");
		action("Gamepad buttons", ">", new Runnable() {
			@Override public void run() { showButtons(); }
		});
		action("Reset gamepad buttons", "", new Runnable() {
			@Override public void run() {
				GsrNative.resetBinds();
				Toast.makeText(activity, "Gamepad buttons reset", Toast.LENGTH_SHORT).show();
			}
		});
		int[] sizes = new int[9];
		for (int i = 0; i < 9; i++) sizes[i] = i;
		choice("Touch controls size", GsrNative.TOUCH_SCALE, sizes, TOUCH_SIZE);
		int[] ops = new int[8];
		String[] opLabels = new String[8];
		for (int i = 0; i < 8; i++) { ops[i] = 30 + i * 10; opLabels[i] = ops[i] + "%"; }
		choice("Touch controls opacity", GsrNative.TOUCH_OPACITY, ops, opLabels);

		section("Game");
		action("Cheats", ">", new Runnable() {
			@Override public void run() { showCheats(); }
		});
		action("Resume", "", new Runnable() {
			@Override public void run() { close(); }
		});
		action("Exit game", "", new Runnable() {
			@Override public void run() {
				activity.finishAffinity();
				Process.killProcess(Process.myPid());
			}
		});

		section("About");
		action("Credits", ">", new Runnable() {
			@Override public void run() { showCredits(); }
		});
		focusFirst();
	}

	// ---------------------------------------------------------------------------------------------------------------

	private TextView info(String text, int color, float sp) {
		TextView t = new TextView(activity);
		t.setText(text);
		t.setTextColor(color);
		t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
		t.setPadding(dp(6), dp(8), dp(6), dp(2));
		list.addView(t);
		return t;
	}

	private void link(final String url) {
		action(url.replace("https://", ""), "open", new Runnable() {
			@Override public void run() {
				try {
					activity.startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW,
							android.net.Uri.parse(url)));
				} catch (RuntimeException e) {
					Toast.makeText(activity, "No app can open " + url, Toast.LENGTH_SHORT).show();
				}
			}
		});
	}

	/** A row that returns to the main settings list (B or Back does the same). */
	private void backRow() {
		action("< Back", "", new Runnable() {
			@Override public void run() { showMain(); }
		}).setTag("back");
	}

	/** Cheats: infinite HP/PP and reward multipliers. Applied at once, and kept for this play session only. */
	private void showCheats() {
		clear("Cheats", 3);
		section("Party");
		choice("Infinite HP", GsrNative.CHEAT_HP, OFF_ON);
		choice("Infinite PP", GsrNative.CHEAT_PP, OFF_ON);

		section("Battle rewards");
		choice("Experience", GsrNative.CHEAT_EXP, CHEAT_STEPS);
		choice("Coins", GsrNative.CHEAT_COIN, CHEAT_STEPS);
		choice("Drop chance", GsrNative.CHEAT_DROP, CHEAT_DROP_STEPS);

		section("");
		backRow();
		focusFirst();
	}

	private void showCredits() {
		clear("Credits", 2);

		section("Android port");
		info("Android port by wootbeer", Color.WHITE, 16);
		link("https://github.com/wootbeer");

		section("Golden Sun Recompiled");
		info("Golden Sun Recompiled by Shmargus", Color.WHITE, 16);
		link("https://github.com/Shmargus/GSRecomp");
		info("Engine: gbarecomp by Matthew Stan", Color.WHITE, 16);
		link("https://github.com/mstan/gbarecomp");

		section("Notices");
		info("Free and noncommercial. Contains no Golden Sun game data: your own ROM is translated and compiled "
				+ "on this device and never leaves it. Golden Sun belongs to Nintendo and Camelot Software Planning.",
				Color.LTGRAY, 13);
		info("Built with mGBA and JRickey/gba-recomp (via gbarecomp) and TinyCC (LGPL). Full credits and licences "
				+ "are in the COPYING file of the source.", Color.LTGRAY, 13);
		section("");
		backRow();
		focusFirst();
	}

	private static String keyName(int code) {
		if (code == 0) return "not set";
		String n = KeyEvent.keyCodeToString(code);
		if (n.startsWith("KEYCODE_")) n = n.substring(8);
		if (n.startsWith("BUTTON_")) n = n.substring(7);
		return n;
	}

	private void showButtons() {
		clear("Gamepad buttons", 1);
		TextView note = new TextView(activity);
		note.setText("Pick a row, then press the button you want. These only change the game; in menus A always selects and B always goes back.");
		note.setTextColor(DIM);
		note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
		note.setPadding(dp(6), 0, dp(6), dp(6));
		list.addView(note);
		for (int a = 0; a < GsrNative.ACTIONS.length; a++) {
			for (int s = 0; s < 2; s++) {
				final int action = a, slot = s;
				final String name = GsrNative.ACTIONS[a] + (s == 1 ? " (second button)" : "");
				Row r = new Row(name) {
					@Override String value() { return keyName(GsrNative.getBind(action, slot)); }
					@Override void step(int dir) { if (dir > 0) capture(action, slot, name, this); }
				};
				r.refresh();
			}
		}
		action("Reset gamepad buttons", "", new Runnable() {
			@Override public void run() {
				GsrNative.resetBinds();
				showButtons();
			}
		});
		section("");
		backRow();
		focusFirst();
	}

	private void capture(final int action, final int slot, String name, final Row row) {
		final AlertDialog d = new AlertDialog.Builder(activity)
				.setTitle("Press a button for " + name)
				.setMessage("Press the gamepad button to use. Back cancels.")
				.setNegativeButton("Clear", new DialogInterface.OnClickListener() {
					@Override public void onClick(DialogInterface dd, int which) {
						GsrNative.setBind(action, slot, 0);
						showButtons();
					}
				})
				.setPositiveButton("Cancel", null)
				.create();
		d.setOnKeyListener(new DialogInterface.OnKeyListener() {
			@Override
			public boolean onKey(DialogInterface dd, int keyCode, KeyEvent event) {
				if (keyCode == KeyEvent.KEYCODE_BACK) return false; // cancels
				if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
						|| keyCode == KeyEvent.KEYCODE_POWER || keyCode == KeyEvent.KEYCODE_UNKNOWN) return false;
				if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
					GsrNative.setBind(action, slot, keyCode);
					dd.dismiss();
					showButtons();
				}
				return true;
			}
		});
		d.show();
	}

	private void focusFirst() {
		for (int i = 0; i < list.getChildCount(); i++) {
			View v = list.getChildAt(i);
			if (v.isFocusable() && v.getTag() == null) { v.requestFocus(); break; }
		}
	}
}
