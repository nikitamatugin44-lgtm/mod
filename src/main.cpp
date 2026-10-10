э#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <deque>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

using namespace geode::prelude;

// one recorded click
struct Ev {
	int who;     // 0 = player 1, 1 = player 2
	int mode;    // gamemode id
	float x;
	float y;
	int kind;    // 0 = release, 1 = push
	bool fired;
};

// the click the bot is currently trying to place before a death spot
struct Fix {
	bool active = false;
	bool flipped = false;
	bool fired = false;
	int who = 0;
	int mode = 0;
	int kind = 1;        // 0 = release, 1 = push, 2 = tap
	int k = 1;           // how many steps before the death step
	float deathX = 0.f;
	float baseTrig = 0.f;
	float vx = 1.f;
};

static std::vector<Ev> g_best;   // clicks of the best run so far
static std::vector<Ev> g_cur;    // clicks of the current attempt
static Fix g_fix;
static float g_bestX = 0.f;      // how far the best run got
static float g_startX = 0.f;     // where the current attempt started
static bool g_newAttempt = true;
static int g_deathGuard = 0;
static PlayLayer* g_layer = nullptr;

static void resetLearning() {
	g_best.clear();
	g_cur.clear();
	g_fix = Fix();
	g_bestX = 0.f;
	g_newAttempt = true;
}

class $modify(AssistPlayer, PlayerObject) {
public:
	struct Fields {
		std::deque<CCPoint> hist;
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

	int modeId() {
		if (m_isShip)   return 1;
		if (m_isBird)   return 2;
		if (m_isBall)   return 3;
		if (m_isDart)   return 4;
		if (m_isRobot)  return 5;
		if (m_isSpider) return 6;
		if (m_isSwing)  return 7;
		return 0;
	}

	int whoAmI() {
		auto pl = PlayLayer::get();
		return (pl && pl->m_player2 == this) ? 1 : 0;
	}

	bool assistAllowed() {
		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("assist-enabled")) return false;
		if (!this->modeEnabled()) return false;
		if (mod->getSettingValue<bool>("practice-only")) {
			auto pl = PlayLayer::get();
			if (!pl || !pl->m_isPracticeMode) return false;
		}
		return true;
	}

	// press / release / tap
	void fireClick(int kind) {
		auto f = m_fields.self();
		if (kind == 0) {
			this->releaseButton(static_cast<PlayerButton>(1));
		} else if (kind == 1) {
			this->pushButton(static_cast<PlayerButton>(1));
		} else {
			this->pushButton(static_cast<PlayerButton>(1));
			f->releaseIn = m_isRobot ? 8 : 2;
		}
	}

	// every click (yours and the bot's) is recorded for the current attempt
	void recordClick(int kind) {
		if (!this->assistAllowed()) return;
		if (g_cur.size() > 4000) return;
		CCPoint p = this->getPosition();
		g_cur.push_back(Ev{ this->whoAmI(), this->modeId(), p.x, p.y, kind, false });
	}

	bool pushButton(PlayerButton b) {
		bool r = PlayerObject::pushButton(b);
		if (static_cast<int>(b) == 1) this->recordClick(1);
		return r;
	}

	bool releaseButton(PlayerButton b) {
		bool r = PlayerObject::releaseButton(b);
		if (static_cast<int>(b) == 1) this->recordClick(0);
		return r;
	}

	// which click would change the current movement: wave/ship flip, others tap
	int pickKind() {
		auto f = m_fields.self();
		int n = static_cast<int>(f->hist.size());

		if (m_isDart && n >= 2) {
			float dy = f->hist[n - 1].y - f->hist[n - 2].y;
			if (std::fabs(dy) > 0.001f) {
				bool holding = (dy > 0.f) != m_isUpsideDown;
				return holding ? 0 : 1;
			}
			return 1;
		}
		if (m_isShip) {
			if (n >= 3) {
				float dy1 = f->hist[n - 1].y - f->hist[n - 2].y;
				float dy2 = f->hist[n - 2].y - f->hist[n - 3].y;
				float ddy = dy1 - dy2;
				if (std::fabs(ddy) > 0.0001f) {
					bool holding = (ddy > 0.f) != m_isUpsideDown;
					return holding ? 0 : 1;
				}
			}
			return 1;
		}
		return 2;
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();
		CCPoint pos = this->getPosition();

		// new level: forget everything
		auto pl = PlayLayer::get();
		if (pl != g_layer) {
			g_layer = pl;
			resetLearning();
			f->hist.clear();
		}

		if (g_deathGuard > 0) g_deathGuard--;

		// respawn / restart: a new attempt begins
		if (!f->hist.empty() && pos.x < f->hist.back().x - 100.f) {
			f->hist.clear();
			f->releaseIn = 0;
			g_cur.clear();
			g_newAttempt = true;
		}

		if (g_newAttempt) {
			g_startX = pos.x;
			g_newAttempt = false;
		}

		f->hist.push_back(pos);
		if (f->hist.size() > 64) f->hist.pop_front();

		// finish a tap click
		if (f->releaseIn > 0) {
			f->releaseIn--;
			if (f->releaseIn == 0) {
				this->releaseButton(static_cast<PlayerButton>(1));
			}
		}

		if (!this->assistAllowed()) return;

		auto mod = Mod::get();
		int who = this->whoAmI();
		int mode = this->modeId();

		// replay the clicks of the best run
		if (mod->getSettingValue<bool>("assist-replay")) {
			for (auto& e : g_best) {
				if (e.who != who || e.mode != mode) continue;

				if (e.fired && pos.x < e.x - 5.f) e.fired = false;

				if (!e.fired && pos.x >= e.x - 0.05f && pos.x <= e.x + 40.f &&
					std::fabs(pos.y - e.y) < 60.f) {
					this->fireClick(e.kind);
					e.fired = true;
				}
			}
		}

		// the click the bot is trying before the last death spot
		if (g_fix.active && g_fix.who == who && g_fix.mode == mode) {
			float trig = g_fix.baseTrig - static_cast<float>(g_fix.k - 1) * g_fix.vx;

			if (g_fix.fired && pos.x < trig - 5.f) g_fix.fired = false;

			if (!g_fix.fired && pos.x >= trig - 0.05f && pos.x <= trig + 40.f) {
				this->fireClick(g_fix.kind);
				g_fix.fired = true;
			}

			// passed the death spot alive: the click worked
			if (pos.x > g_fix.deathX + 60.f) g_fix.active = false;
		}
	}

	// called when the player is really about to die
	void learnFromDeath() {
		if (g_deathGuard > 0) return;
		if (!this->assistAllowed()) return;
		g_deathGuard = 60;

		auto mod = Mod::get();
		auto f = m_fields.self();
		int n = static_cast<int>(f->hist.size());

		if (n < 3) {
			g_cur.clear();
			g_newAttempt = true;
			return;
		}

		float deathX = f->hist[n - 1].x;
		float vx = f->hist[n - 1].x - f->hist[n - 2].x;
		if (vx < 0.01f) vx = 0.5f;

		// this attempt got further than any before: it becomes the best run
		bool frontier = false;
		if (deathX > g_bestX + 5.f) {
			std::vector<Ev> nb;
			for (auto& e : g_best) {
				if (e.x < g_startX - 1.f) nb.push_back(e);
			}
			for (auto& e : g_cur) nb.push_back(e);
			g_best = nb;
			g_bestX = deathX;
			frontier = true;
		}

		int who = this->whoAmI();
		int mode = this->modeId();
		int maxBack = static_cast<int>(mod->getSettingValue<int64_t>("max-back-steps"));

		bool retry = g_fix.active && g_fix.who == who && g_fix.mode == mode &&
			deathX <= g_fix.deathX + 15.f && deathX >= g_fix.deathX - 120.f;

		if (retry) {
			// died at the same spot again: click one step earlier
			g_fix.k++;
			g_fix.fired = false;
			if (g_fix.k > maxBack) {
				if (!g_fix.flipped && g_fix.kind != 2) {
					g_fix.kind = 1 - g_fix.kind;
					g_fix.flipped = true;
					g_fix.k = 1;
				} else {
					g_fix.active = false;
				}
			}
		} else if (frontier) {
			// died further than ever: start with a click on the last step
			g_fix.active = true;
			g_fix.flipped = false;
			g_fix.fired = false;
			g_fix.who = who;
			g_fix.mode = mode;
			g_fix.kind = this->pickKind();
			g_fix.k = 1;
			g_fix.deathX = deathX;
			g_fix.baseTrig = f->hist[n - 2].x;
			g_fix.vx = vx;
		}

		g_cur.clear();
		g_newAttempt = true;
	}
};

class $modify(AssistLayer, PlayLayer) {
	struct Fields {
		CCLabelBMFont* label = nullptr;
		int tick = 0;
	};

	void destroyPlayer(PlayerObject* player, GameObject* object) {
		if (player && object != m_anticheatSpike) {
			static_cast<AssistPlayer*>(player)->learnFromDeath();
		}
		PlayLayer::destroyPlayer(player, object);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		auto mod = Mod::get();
		auto f = m_fields.self();
		bool show = mod->getSettingValue<bool>("assist-enabled") &&
			mod->getSettingValue<bool>("show-hud");

		if (!f->label) {
			if (!show || !m_uiLayer) return;
			f->label = CCLabelBMFont::create("", "bigFont.fnt");
			f->label->setScale(0.3f);
			f->label->setAnchorPoint({ 0.f, 1.f });
			auto win = CCDirector::get()->getWinSize();
			f->label->setPosition({ 6.f, win.height - 6.f });
			m_uiLayer->addChild(f->label, 100);
		}

		f->label->setVisible(show);
		if (!show) return;

		f->tick++;
		if (f->tick % 10 != 0) return;

		std::string t = "AI best x: " + std::to_string(static_cast<int>(g_bestX)) +
			" | clicks: " + std::to_string(g_best.size());
		if (g_fix.active) {
			t += " | trying " + std::to_string(g_fix.k) + " steps early";
		}
		f->label->setString(t.c_str());
	}
};

class $modify(AssistPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto spr1 = ButtonSprite::create("Assist");
		spr1->setScale(0.7f);
		auto btn1 = CCMenuItemSpriteExtra::create(
			spr1, this, menu_selector(AssistPause::onAssist)
		);

		auto spr2 = ButtonSprite::create("Reset AI");
		spr2->setScale(0.7f);
		auto btn2 = CCMenuItemSpriteExtra::create(
			spr2, this, menu_selector(AssistPause::onReset)
		);
		btn2->setPosition({ 0.f, -28.f });

		auto menu = CCMenu::create();
		menu->addChild(btn1);
		menu->addChild(btn2);

		auto win = CCDirector::get()->getWinSize();
		menu->setPosition({ win.width - 60.f, win.height - 30.f });
		this->addChild(menu);
	}

	void onAssist(CCObject*) {
		openSettingsPopup(Mod::get());
	}

	void onReset(CCObject*) {
		resetLearning();
	}
};
