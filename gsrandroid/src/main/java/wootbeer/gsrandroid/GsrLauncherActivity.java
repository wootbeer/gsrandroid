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
	protected void onCreate(android.os.Bundle savedInstanceState) {
		GsrLang.init(this);
		super.onCreate(savedInstanceState);
	}

	@Override
	protected String text(String english) {
		return GsrLang.t(english);
	}

	@Override
	protected String unitTitle(DescoreGameConfig.Unit unit) {
		return GsrConfig.title(GsrConfig.isTla(unit.id), GsrConfig.lang(unit.id));
	}

	@Override
	protected Class<? extends DescoreGameActivity> getGameActivityClass() {
		return GsrGameActivity.class;
	}
}
