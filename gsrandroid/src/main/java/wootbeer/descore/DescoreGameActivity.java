package wootbeer.descore;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;

import java.io.File;

/**
 * Shared game screen: hosts the {@link DescoreView} and forwards the Android lifecycle to it. All
 * of the screen-lock/backgrounding behavior every descore port has had to get right lives here:
 * the render thread is parked (not the play session killed) on pause and woken on resume, music
 * pauses with the app, and the screen stays on while playing.
 *
 * Subclasses only supply the game's {@link DescoreGameConfig}; register the subclass in the
 * manifest with android:configChanges="orientation|screenSize|screenLayout|keyboardHidden" and a
 * sensor-landscape orientation, otherwise a lock/unlock orientation flip recreates the Activity
 * and starts a second native game thread on top of the parked original.
 */
public abstract class DescoreGameActivity extends Activity {

	/** Intent extra: the {@link DescoreGameConfig.Unit#id} the launcher wants played. */
	public static final String EXTRA_UNIT_ID = "descore_unit_id";

	protected abstract DescoreGameConfig getConfig();

	private DescoreView view;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
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
		DescoreGameConfig config = getConfig();
		int unitId = getIntent().getIntExtra(EXTRA_UNIT_ID, config.units.get(0).id);
		DescoreGameConfig.Unit unit = config.unitById(unitId);
		File dataDir = new File(new File(getFilesDir(), config.rootDirName), unit.dirName);
		view = new DescoreView(this, config, dataDir.getAbsolutePath(), unit.id);
		view.setMenuRequestListener(new DescoreView.MenuRequestListener() {
			@Override
			public void onMenuRequested() {
				DescoreGameActivity.this.onMenuRequested();
			}
		});
		setContentView(view);
		view.requestFocus();
	}

	@Override
	protected void onPause() {
		super.onPause();
		if (view != null) {
			view.onAppPause();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
		if (view != null) {
			view.onAppResume();
		}
	}

	/**
	 * Back is handed to the game as an ordinary key (Android KEYCODE_BACK) so it can open its own
	 * pause/quit menu; gesture navigation reaches here without any key event, so it is forwarded
	 * the same way.
	 */
	@Override
	public void onBackPressed() {
		if (view != null) {
			view.sendKey(KeyEvent.KEYCODE_BACK);
		}
	}

	/** The native game asked for its settings menu (Back, a gamepad Menu button, an on-screen MENU button). */
	protected void onMenuRequested() {
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
