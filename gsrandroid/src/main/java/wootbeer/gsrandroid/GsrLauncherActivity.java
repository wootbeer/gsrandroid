package wootbeer.gsrandroid;

import wootbeer.descore.DescoreGameActivity;
import wootbeer.descore.DescoreGameConfig;
import wootbeer.descore.DescoreLauncherActivity;

/** Golden Sun Recompiled's launcher: the shared descore ROM picker, configured by {@link GsrConfig}. */
public class GsrLauncherActivity extends DescoreLauncherActivity {
	private static final DescoreGameConfig CONFIG = GsrConfig.create();

	@Override
	protected DescoreGameConfig getConfig() {
		return CONFIG;
	}

	@Override
	protected Class<? extends DescoreGameActivity> getGameActivityClass() {
		return GsrGameActivity.class;
	}
}
