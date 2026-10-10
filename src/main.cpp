#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <deque>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;

class $modify(AssistPlayer, PlayerObject) {
public:
	struct Fields {
		std::deque<CCPoint> hist;
		int streak = 0;
		int calm = 0;
	};

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();
		if (!m_isDart) {
			f->hist.clear();
			return;
		}

		f->hist.push_back(this->getPosition());
		if (f->hist.size() > 64) f->hist.pop_front();

		f->calm++;
		if (f->calm > 20) f->streak = 0;
	}

	bool tryRescue() {
		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("assist-enabled")) return false;
		if (!m_isDart) return false;

		if (mod->getSettingValue<bool>("practice-only")) {
			auto pl = PlayLayer::get();
			if (!pl || !pl->m_isPracticeMode) return false;
		}

		float miss = static_cast<float>(mod->getSettingValue<double>("miss-chance"));
		if (miss > 0.f && (std::rand() % 10000) / 100.f < miss) return false;

		auto f = m_fields.self();

		int maxStreak = static_cast<int>(mod->getSettingValue<int64_t>("max-rescues"));
		if (maxStreak > 0 && f->streak >= maxStreak) return false;

		int steps = 1;
		if (mod->getSettingValue<std::string>("assist-method") == "Rewind") {
			steps = static_cast<int>(mod->getSettingValue<int64_t>("rewind-steps"));
		}

		int n = static_cast<int>(f->hist.size());
		if (n < 2) return false;

		// direction of the last move: up or down
		float dy = f->hist[n - 1].y - f->hist[n - 2].y;
		if (std::fabs(dy) < 0.001f) return false;

		bool wasHolding = (dy > 0.f) != m_isUpsideDown;

		int back = std::min(steps, n - 1);
		CCPoint target = f->hist[n - 1 - back];
		f->hist.resize(n - back);

		this->setPosition(target);

		// flip the click: reverse the wave's direction
		if (wasHolding) {
			this->releaseButton(static_cast<PlayerButton>(1));
		} else {
			this->pushButton(static_cast<PlayerButton>(1));
		}

		f->streak++;
		f->calm = 0;
		return true;
	}
};

class $modify(AssistLayer, PlayLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* object) {
		if (player && object != m_anticheatSpike) {
			if (static_cast<AssistPlayer*>(player)->tryRescue()) {
				return; // cancel the death
			}
		}
		PlayLayer::destroyPlayer(player, object);
	}
};

class $modify(AssistPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto spr = ButtonSprite::create("Assist");
		spr->setScale(0.7f);

		auto btn = CCMenuItemSpriteExtra::create(
			spr, this, menu_selector(AssistPause::onAssist)
		);

		auto menu = CCMenu::create();
		menu->addChild(btn);

		auto win = CCDirector::get()->getWinSize();
		menu->setPosition({ win.width - 60.f, win.height - 30.f });
		this->addChild(menu);
	}

	void onAssist(CCObject*) {
		openSettingsPopup(Mod::get());
	}
};
