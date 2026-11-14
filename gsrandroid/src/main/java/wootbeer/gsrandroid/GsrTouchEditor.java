package wootbeer.gsrandroid;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

/**
 * Moving the on-screen controls: the game pauses, the native side lets touches drag the controls
 * (gsr_controls.c), and two buttons near the top finish (Done, or Back) or put this orientation's controls
 * back where they start (Reset). The moves are saved as they are made.
 */
final class GsrTouchEditor {
	private final Activity activity;
	private FrameLayout root;

	GsrTouchEditor(Activity a) { activity = a; }

	boolean active() { return root != null; }

	void start() {
		if (root != null) return;
		GsrNative.setMenuOpen(true);  // pauses the game and lets go of anything held
		GsrNative.setTouchEditing(true);
		root = new FrameLayout(activity);
		LinearLayout row = new LinearLayout(activity);
		row.setOrientation(LinearLayout.HORIZONTAL);
		row.addView(button("Reset", new View.OnClickListener() {
			@Override public void onClick(View v) { GsrNative.resetTouchLayout(); }
		}));
		row.addView(button("Done", new View.OnClickListener() {
			@Override public void onClick(View v) { finish(); }
		}));
		FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
				ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
		lp.topMargin = dp(48);
		root.addView(row, lp);
		// Only the buttons take touches; everywhere else they reach the game view, where they move controls.
		activity.addContentView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT));
	}

	void finish() {
		if (root == null) return;
		GsrNative.setTouchEditing(false);
		GsrNative.setMenuOpen(false);
		ViewGroup parent = (ViewGroup) root.getParent();
		if (parent != null) parent.removeView(root);
		root = null;
	}

	private Button button(String text, View.OnClickListener l) {
		Button b = new Button(activity);
		b.setText(text);
		b.setTextColor(Color.WHITE);
		b.setAllCaps(false);
		GradientDrawable bg = new GradientDrawable();
		bg.setColor(0xE0305070);
		bg.setCornerRadius(dp(8));
		b.setBackgroundDrawable(bg);
		b.setPadding(dp(20), dp(6), dp(20), dp(6));
		b.setOnClickListener(l);
		LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
				ViewGroup.LayoutParams.WRAP_CONTENT);
		p.leftMargin = p.rightMargin = dp(8);
		b.setLayoutParams(p);
		return b;
	}

	private int dp(float v) { return (int) (v * activity.getResources().getDisplayMetrics().density + 0.5f); }
}
