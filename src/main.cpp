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
		int stuck = 0;
		float stuckX = 0.f;
		int releaseIn = 0;
	};

	bool modeEnabled() {
		auto mod = Mod::get();
		if (m_isShip)   return mod->getSettingValue<bool>("assist-ship");
		if (m_isBird)   return mod->getSettingValue<bool>("assist-ufo");
		if (m_isBall)   return mod->getSettingValue<bool>("assist-ball");
		if (m_isDart)   return mod->getSettingValue<bool>("assist-wave");
		if (m_isRobot)  return mod->getSettingValue<bool>("assist-robot");
		if (m_isSpider) return mod->getSettingValue<bool>("assist-spider");
		if (m_isSwing)  return mod->getSettingValue<bool>("assist-swing");
		return mod->getSettingValue<bool>("assist-cube");
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();
		CCPoint pos = this->getPosition();

		// respawn / big jump back: forget old history
		if (!f->hist.empty() && pos.x < f->hist.back().x - 300.f) {
			f->hist.clear();
			f->stuck = 0;
			f->releaseIn = 0;
		}

		f->hist.push_back(pos);
		if (f->hist.size() > 64) f->hist.pop_front();

		// moved far enough past the trap point: reset the stuck counter
		if (f->stuck > 0 && pos.x > f->stuckX + 60.f) f->stuck = 0;

		// finish a tap click
		if (f->releaseIn > 0) {
			f->releaseIn--;
			if (f->releaseIn == 0) {
				this->releaseButton(static_cast<PlayerButton>(1));
			}
		}
	}

	bool tryRescue() {
		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("assist-enabled")) return false;
		if (!this->modeEnabled()) return false;

		if (mod->getSettingValue<bool>("practice-only")) {
			auto pl = PlayLayer::get();
			if (!pl || !pl->m_isPracticeMode) return false;
		}

		float miss = static_cast<float>(mod->getSettingValue<double>("miss-chance"));
		if (miss > 0.f && (std::rand() % 10000) / 100.f < miss) return false;

		auto f = m_fields.self();

		// too many rescues without moving forward = trapped, let it die
		int maxStuck = static_cast<int>(mod->getSettingValue<int64_t>("max-rescues"));
		if (maxStuck > 0 && f->stuck >= maxStuck) return false;

		int steps = 1;
		if (mod->getSettingValue<std::string>("assist-method") == "Rewind") {
			steps = static_cast<int>(mod->getSettingValue<int64_t>("rewind-steps"));
		}

		int n = static_cast<int>(f->hist.size());
		if (n < 2) return false;

		// wave and ship: hold/release modes, we flip the current state
		bool flipMode = m_isDart || m_isShip;
		bool wasHolding = false;

		if (m_isDart) {
			float dy = f->hist[n - 1].y - f->hist[n - 2].y;
			if (std::fabs(dy) < 0.001f) return false;
			wasHolding = (dy > 0.f) != m_isUpsideDown;
		} else if (m_isShip) {
			if (n < 3) return false;
			float dy1 = f->hist[n - 1].y - f->hist[n - 2].y;
			float dy2 = f->hist[n - 2].y - f->hist[n - 3].y;
			float ddy = dy1 - dy2;
			if (std::fabs(ddy) < 0.0001f) return false;
			wasHolding = (ddy > 0.f) != m_isUpsideDown;
		}

		if (f->stuck == 0) f->stuckX = f->hist[n - 1].x;
		f->stuck++;

		int back = std::min(steps, n - 1);
		CCPoint target = f->hist[n - 1 - back];
		f->hist.resize(n - back);

		this->setPosition(target);

		if (flipMode) {
			if (wasHolding) {
				this->releaseButton(static_cast<PlayerButton>(1));
			} else {
				this->pushButton(static_cast<PlayerButton>(1));
			}
		} else {
			// cube, ball, ufo, spider, swing, robot: one click
			this->pushButton(static_cast<PlayerButton>(1));
			f->releaseIn = m_isRobot ? 8 : 2;
		}

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
