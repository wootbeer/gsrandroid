package wootbeer.gsrandroid;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * Keeps the process alive while the game code is being built from the ROM. The native build writes
 * files/gsr/build_status.txt ("running" / "done" / "failed"); this service shows a notification only
 * while it says "running" is expected, and stops itself when the build is finished, or when nothing
 * started building within a few seconds of launch (the normal case once the build is saved).
 */
public class GsrBuildService extends Service {
	private static final String CHANNEL = "gsr_build";
	private static final int NOTE_ID = 1;
	private static final int IDLE_GRACE_MS = 8000;

	private volatile boolean stopping;
	private PowerManager.WakeLock wakeLock;

	@Override
	public IBinder onBind(Intent intent) {
		return null;
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {
		startForeground(NOTE_ID, buildNotification());
		if (wakeLock == null) {
			PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
			if (pm != null) {
				wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "gsr:build");
				wakeLock.acquire(60 * 60 * 1000L); // a build never needs more than an hour
			}
		}
		final File status = new File(new File(getFilesDir(), "gsr"), "build_status.txt");
		Thread watcher = new Thread(new Runnable() {
			@Override
			public void run() {
				long start = System.currentTimeMillis();
				while (!stopping) {
					String s = readStatus(status);
					boolean running = "running".equals(s);
					if (!running && System.currentTimeMillis() - start > IDLE_GRACE_MS && !"".equals(s)) {
						break; // done or failed
					}
					if (!running && System.currentTimeMillis() - start > 3 * IDLE_GRACE_MS) {
						break; // nothing ever started
					}
					try {
						Thread.sleep(1000);
					} catch (InterruptedException e) {
						return;
					}
				}
				stopSelf();
			}
		}, "gsr-build-watch");
		watcher.setDaemon(true);
		watcher.start();
		return START_NOT_STICKY;
	}

	@Override
	public void onDestroy() {
		stopping = true;
		if (wakeLock != null && wakeLock.isHeld()) {
			wakeLock.release();
		}
		super.onDestroy();
	}

	private static String readStatus(File f) {
		try (BufferedReader r = new BufferedReader(new FileReader(f))) {
			String line = r.readLine();
			return line == null ? "" : line.trim();
		} catch (Exception e) {
			return "";
		}
	}

	private Notification buildNotification() {
		NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
		Notification.Builder b;
		if (Build.VERSION.SDK_INT >= 26) {
			if (nm != null) {
				nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Game setup",
						NotificationManager.IMPORTANCE_LOW));
			}
			b = new Notification.Builder(this, CHANNEL);
		} else {
			b = new Notification.Builder(this);
		}
		b.setContentTitle("Golden Sun Recompiled")
				.setContentText("Preparing the game from your ROM (first run only)")
				.setSmallIcon(android.R.drawable.stat_sys_download)
				.setOngoing(true);
		return b.build();
	}
}
