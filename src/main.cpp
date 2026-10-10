#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <deque>
#include <vector>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;

// one recorded click (by the user)
struct UserEv {
	int who;     // 0 = player 1, 1 = player 2
	int mode;    // gamemode id
	float x;
	float y;
	int kind;    // 0 = release, 1 = push, 2 = tap
};

// a spot where the assist had to rescue the player
struct Spot {
	int who;
	int mode;
	float x;
	float y;
	int kind;
	float offset;      // how many units earlier to click here
	int tries;         // how many times it failed here
	bool solved;
	bool fired;
	bool hasSol;       // user's own clicks recorded for this spot
	std::vector<UserEv> sol;
	std::vector<char> solFired;
};

static std::vector<Spot> g_spots;
static std::vector<UserEv> g_user;
static std::vector<CCRect> g_hazards;
static PlayLayer* g_layer = nullptr;
static bool g_assistClick = false;
static int g_deathGuard = 0;

// 0 tries = base, then alternates earlier / later: +6, -6, +12, -12 ...
static float offsetFor(int tries, float base) {
	if (tries <= 0) return base;
	int k = (tries + 1) / 2;
	float sign = (tries % 2 == 1) ? 1.f : -1.f;
	float v = base + sign * 6.f * static_cast<float>(k);
	return std::max(-30.f, std::min(90.f, v));
}

static void rebuildHazards(float x) {
	g_hazards.clear();
	auto pl = PlayLayer::get();
	if (!pl || !pl->m_objects) return;
	for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
		if (!obj) continue;
		if (obj->m_objectType != GameObjectType::Hazard) continue;
		CCRect r = obj->getObjectRect();
		if (r.origin.x + r.size.width < x - 20.f || r.origin.x > x + 600.f) continue;
		g_hazards.push_back(r);
	}
}

class $modify(AssistPlayer, PlayerObject) {
public:
	struct Fields {
		std::deque<CCPoint> hist;
		int stuck = 0;
		float stuckX = 0.f;
		int releaseIn = 0;
		int cooldown = 0;
		int hazAge = 0;
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

	// clicks made by the assist itself (not recorded as user clicks)
	void fireClick(int kind) {
		auto f = m_fields.self();
		g_assistClick = true;
		if (kind == 0) {
			this->releaseButton(static_cast<PlayerButton>(1));
		} else if (kind == 1) {
			this->pushButton(static_cast<PlayerButton>(1));
		} else {
			this->pushButton(static_cast<PlayerButton>(1));
			f->releaseIn = m_isRobot ? 8 : 2;
		}
		g_assistClick = false;
	}

	// remember the user's own clicks
	void noteUserClick(int kind) {
		if (g_assistClick) return;
		if (!this->assistAllowed()) return;
		if (!Mod::get()->getSettingValue<bool>("assist-learn")) return;
		if (g_user.size() > 400) g_user.erase(g_user.begin());
		CCPoint p = this->getPosition();
		g_user.push_back(UserEv{ this->whoAmI(), this->modeId(), p.x, p.y, kind });
	}

	bool pushButton(PlayerButton b) {
		bool r = PlayerObject::pushButton(b);
		if (static_cast<int>(b) == 1) this->noteUserClick(1);
		return r;
	}

	bool releaseButton(PlayerButton b) {
		bool r = PlayerObject::releaseButton(b);
		if (static_cast<int>(b) == 1) this->noteUserClick(0);
		return r;
	}

	bool pathHits(const CCPoint& p, float vx, float vy, int steps, float half) {
		for (int k = 2; k <= steps; k += 2) {
			float px = p.x + vx * static_cast<float>(k);
			float py = p.y + vy * static_cast<float>(k);
			for (auto& r : g_hazards) {
				if (px + half > r.origin.x && px - half < r.origin.x + r.size.width &&
					py + half > r.origin.y && py - half < r.origin.y + r.size.height) {
					return true;
				}
			}
		}
		return false;
	}

	// look ahead for spikes / saws and click before reaching them
	void lookahead(const CCPoint& pos) {
		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("assist-lookahead")) return;

		auto f = m_fields.self();
		if (f->cooldown > 0) {
			f->cooldown--;
			return;
		}

		int n = static_cast<int>(f->hist.size());
		if (n < 3) return;

		float vx = f->hist[n - 1].x - f->hist[n - 2].x;
		float vy = f->hist[n - 1].y - f->hist[n - 2].y;
		if (vx < 0.01f) return;

		f->hazAge--;
		if (f->hazAge <= 0) {
			rebuildHazards(pos.x);
			f->hazAge = 16;
		}
		if (g_hazards.empty()) return;

		float look = static_cast<float>(mod->getSettingValue<double>("lookahead-distance"));
		int steps = std::min(200, static_cast<int>(std::ceil(look / vx)));
		float half = (m_isDart ? 5.f : 9.f) * std::max(0.5f, this->getScale());

		if (m_isDart) {
			if (std::fabs(vy) < 0.001f) return;
			if (this->pathHits(pos, vx, vy, steps, half) &&
				!this->pathHits(pos, vx, -vy, steps, half)) {
				bool wasHolding = (vy > 0.f) != m_isUpsideDown;
				this->fireClick(wasHolding ? 0 : 1);
				f->cooldown = 8;
			}
			return;
		}

		if (m_isShip || m_isSwing) return;

		bool needGround = !m_isBird;
		if (needGround && !m_isOnGround) return;

		if (this->pathHits(pos, vx, needGround ? 0.f : vy, steps, half)) {
			this->fireClick(2);
			f->cooldown = 15;
		}
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto f = m_fields.self();
		CCPoint pos = this->getPosition();
		auto mod = Mod::get();

		// new level / new PlayLayer: forget everything
		auto pl = PlayLayer::get();
		if (pl != g_layer) {
			g_layer = pl;
			g_spots.clear();
			g_user.clear();
			g_hazards.clear();
		}

		if (g_deathGuard > 0) g_deathGuard--;

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
				g_assistClick = true;
				this->releaseButton(static_cast<PlayerButton>(1));
				g_assistClick = false;
			}
		}

		if (!this->assistAllowed()) return;

		// keep only the user's clicks that are behind the player in this attempt
		g_user.erase(
			std::remove_if(g_user.begin(), g_user.end(), [&](const UserEv& e) {
				return e.x < pos.x - 400.f || e.x > pos.x + 50.f;
			}),
			g_user.end()
		);

		if (mod->getSettingValue<bool>("assist-learn")) {
			float margin = static_cast<float>(mod->getSettingValue<double>("learn-margin"));
			int who = this->whoAmI();
			int mode = this->modeId();

			for (auto& s : g_spots) {
				if (s.who != who || s.mode != mode) continue;

				// behind the spot again: allow re-checking it
				if (pos.x < s.x - 150.f) s.solved = false;

				// passed the spot alive: remember the user's clicks around it
				if (pos.x > s.x + 100.f && !s.solved) {
					s.solved = true;
					if (!s.hasSol) {
						std::vector<UserEv> sol;
						for (auto& e : g_user) {
							if (e.who == who && e.mode == mode &&
								e.x >= s.x - 80.f && e.x <= s.x + 80.f) {
								sol.push_back(e);
							}
						}
						if (!sol.empty()) {
							s.sol = sol;
							s.solFired.assign(sol.size(), 1);
							s.hasSol = true;
						}
					}
				}

				if (!s.hasSol) {
					// the assist's own click, with the adaptive offset
					float fx = s.x - s.offset;
					if (s.fired && pos.x < fx - 5.f) s.fired = false;
					if (!s.fired && pos.x >= fx && pos.x <= fx + 40.f &&
						std::fabs(pos.y - s.y) < 35.f + std::fabs(s.offset)) {
						this->fireClick(s.kind);
						s.fired = true;
					}
				} else {
					// replay the user's own clicks
					float shift = s.offset - margin;
					for (size_t i = 0; i < s.sol.size(); i++) {
						float tx = s.sol[i].x - shift;
						if (s.solFired[i] && pos.x < tx - 5.f) s.solFired[i] = 0;
						if (!s.solFired[i] && pos.x >= tx && pos.x <= tx + 40.f &&
							std::fabs(pos.y - s.sol[i].y) < 35.f + std::fabs(shift)) {
							this->fireClick(s.sol[i].kind);
							s.solFired[i] = 1;
						}
					}
				}
			}
		}

		this->lookahead(pos);
	}

	bool tryRescue() {
		auto mod = Mod::get();
		if (!this->assistAllowed()) return false;

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

		int kind = flipMode ? (wasHolding ? 0 : 1) : 2;
		this->fireClick(kind);

		// remember this spot, or adapt it if we were already here
		if (mod->getSettingValue<bool>("assist-learn")) {
			float margin = static_cast<float>(mod->getSettingValue<double>("learn-margin"));
			int who = this->whoAmI();
			int mode = this->modeId();

			Spot* nearSpot = nullptr;
			for (auto& s : g_spots) {
				if (s.who == who && s.mode == mode && std::fabs(s.x - target.x) < 40.f) {
					nearSpot = &s;
					break;
				}
			}

			if (nearSpot) {
				nearSpot->tries++;
				nearSpot->offset = offsetFor(nearSpot->tries, margin);
				nearSpot->hasSol = false;
				nearSpot->sol.clear();
				nearSpot->solFired.clear();
				nearSpot->solved = false;
				nearSpot->fired = true;
			} else if (g_spots.size() < 300) {
				Spot s;
				s.who = who;
				s.mode = mode;
				s.x = target.x;
				s.y = target.y;
				s.kind = kind;
				s.offset = margin;
				s.tries = 0;
				s.solved = false;
				s.fired = true;
				s.hasSol = false;
				g_spots.push_back(s);
			}
		}

		return true;
	}

	// the player is really going to die: change the timing of nearby spots
	void onRealDeath() {
		if (g_deathGuard > 0) return;
		if (!this->assistAllowed()) return;

		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("assist-learn")) return;

		g_deathGuard = 40;

		float margin = static_cast<float>(mod->getSettingValue<double>("learn-margin"));
		float dx = this->getPositionX();
		int who = this->whoAmI();
		int mode = this->modeId();

		for (auto& s : g_spots) {
			if (s.who != who || s.mode != mode) continue;
			if (dx >= s.x - 120.f && dx <= s.x + 120.f) {
				s.tries++;
				s.offset = offsetFor(s.tries, margin);
				s.hasSol = false;
				s.sol.clear();
				s.solFired.clear();
				s.solved = false;
			}
		}
	}
};

class $modify(AssistLayer, PlayLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* object) {
		if (player && object != m_anticheatSpike) {
			auto ap = static_cast<AssistPlayer*>(player);
			if (ap->tryRescue()) {
				return; // cancel the death
			}
			ap->onRealDeath();
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
