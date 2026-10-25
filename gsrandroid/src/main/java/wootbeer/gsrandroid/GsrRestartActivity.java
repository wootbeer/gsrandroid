package wootbeer.gsrandroid;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;

import java.io.File;

/**
 * Starts a game in a fresh process. It runs in a process of its own (":restart" in the manifest): it ends the
 * game's process (the engine and the first game's code live there and cannot be unloaded), waits until that is
 * gone, starts the next game, and closes. Used when Golden Sun's Seamless mode continues in The Lost Age.
 */
public class GsrRestartActivity extends Activity {
	static final String EXTRA_PID = "gsr_restart_pid", EXTRA_TARGET = "gsr_restart_target";

	private final Handler handler = new Handler(Looper.getMainLooper());
	private int pid, waited;
	private Intent target;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		pid = getIntent().getIntExtra(EXTRA_PID, 0);
		target = getIntent().getParcelableExtra(EXTRA_TARGET);
		if (pid > 0 && pid != Process.myPid()) Process.killProcess(pid);
		handler.post(waitForExit);
	}

	private final Runnable waitForExit = new Runnable() {
		@Override public void run() {
			if (pid > 0 && new File("/proc/" + pid).exists() && waited++ < 50) {
				handler.postDelayed(this, 100);
				return;
			}
			if (target != null) {
				target.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
				startActivity(target);
			}
			finish();
			overridePendingTransition(0, 0);
			handler.postDelayed(new Runnable() {
				@Override public void run() { Process.killProcess(Process.myPid()); }
			}, 1000);
		}
	};
}
