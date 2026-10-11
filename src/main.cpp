#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <cmath>
#include <string>

using namespace geode::prelude;

// speed factor for the current level percent (1.0 = no effect)
static float currentFactor() {
	auto mod = Mod::get();
	if (!mod->getSettingValue<bool>("ssb-enabled")) return 1.f;

	auto pl = PlayLayer::get();
	if (!pl) return 1.f;

	float pct = pl->getCurrentPercent();

	for (int i = 1; i <= 5; i++) {
		std::string p = "t" + std::to_string(i) + "-";
		if (!mod->getSettingValue<bool>(p + "on")) continue;

		float a = static_cast<float>(mod->getSettingValue<double>(p + "start"));
		float b = static_cast<float>(mod->getSettingValue<double>(p + "end"));

		if (pct >= a && pct <= b) {
			return static_cast<float>(mod->getSettingValue<double>(p + "speed")) / 100.f;
		}
	}
	return 1.f;
}

class $modify(SsbPlayer, PlayerObject) {
public:
	void update(float dt) {
		// do not read the settings on every single step
		static int tick = 0;
		static float cached = 1.f;
		if ((tick++ % 4) == 0) cached = currentFactor();
		float f = cached;

		if (f == 1.f) {
			PlayerObject::update(dt);
			return;
		}

		// Speed: the whole player just runs faster or slower
		if (Mod::get()->getSettingValue<std::string>("ssb-method") == "Speed") {
			PlayerObject::update(dt * f);
			return;
		}

		// Shape: only the forward movement is stretched or squeezed
		float oldX = this->getPositionX();
		PlayerObject::update(dt);

		float dx = this->getPositionX() - oldX;
		if (dx <= 0.f || dx > 50.f) return; // ignore teleports and respawns

		this->setPosition(CCPoint(oldX + dx * f, this->getPositionY()));
	}
};

class $modify(SsbPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto spr = ButtonSprite::create("SSB");
		spr->setScale(0.7f);

		auto btn = CCMenuItemSpriteExtra::create(
			spr, this, menu_selector(SsbPause::onSsb)
		);

		auto menu = CCMenu::create();
		menu->addChild(btn);

		auto win = CCDirector::get()->getWinSize();
		menu->setPosition({ win.width - 60.f, win.height - 30.f });
		this->addChild(menu);
	}

	void onSsb(CCObject*) {
		openSettingsPopup(Mod::get());
	}
};
