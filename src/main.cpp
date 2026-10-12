#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <string>
#include "shared.hpp"

using namespace geode::prelude;

float g_animFactor = 1.f;
float g_partFactor = 1.f;

// one row of the table: from this level percent on, until the next row
struct Pt {
	float pct;
	float speed;   // x movement (SSB)
	float jump;    // cube jump height
	float grav;    // gravity
	float anim;    // animation speed
	float part;    // particle speed
};

// the values that are active right now, as factors (1.0 = normal)
struct Cur {
	float speed = 1.f;
	float jump = 1.f;
	float grav = 1.f;
	float anim = 1.f;
	float part = 1.f;
};

static std::vector<Pt> g_points;
static bool g_enabled = true;
static bool g_methodSpeed = false;
static bool g_smooth = false;
static bool g_reload = true;
static Cur g_now;

static std::vector<std::string> splitStr(const std::string& s, char d) {
	std::vector<std::string> out;
	size_t i = 0;
	while (true) {
		size_t j = s.find(d, i);
		if (j == std::string::npos) {
			out.push_back(s.substr(i));
			break;
		}
		out.push_back(s.substr(i, j - i));
		i = j + 1;
	}
	return out;
}

// rows are saved as text: "25:125:100:100:100:100,35:100:100:100:100:100"
static std::vector<Pt> parsePoints(const std::string& s) {
	std::vector<Pt> out;
	for (auto& row : splitStr(s, ',')) {
		auto f = splitStr(row, ':');
		if (f.size() < 2 || f[0].empty()) continue;

		float v[6] = { 0.f, 100.f, 100.f, 100.f, 100.f, 100.f };
		for (size_t i = 0; i < 6 && i < f.size(); i++) {
			if (!f[i].empty()) v[i] = std::strtof(f[i].c_str(), nullptr);
		}
		out.push_back(Pt{ v[0], v[1], v[2], v[3], v[4], v[5] });
	}

	std::sort(out.begin(), out.end(), [](const Pt& a, const Pt& b) {
		return a.pct < b.pct;
	});
	return out;
}

static void loadConfig() {
	auto mod = Mod::get();
	g_enabled = mod->getSavedValue<bool>("enabled", true);
	g_methodSpeed = mod->getSavedValue<bool>("method-speed", false);
	g_smooth = mod->getSavedValue<bool>("smooth", false);
	g_points = parsePoints(mod->getSavedValue<std::string>(
		"points", "25:125:100:100:100:100,35:100:100:100:100:100"
	));
	g_reload = false;
}

// the values for the current level percent
static Cur computeNow() {
	Cur c;
	if (g_reload) loadConfig();
	if (!g_enabled) return c;

	auto pl = PlayLayer::get();
	if (!pl || g_points.empty()) return c;

	float pct = pl->getCurrentPercent();

	int idx = -1;
	for (int i = 0; i < static_cast<int>(g_points.size()); i++) {
		if (g_points[i].pct <= pct) idx = i;
		else break;
	}
	if (idx < 0) return c;

	Pt p = g_points[idx];

	if (g_smooth && idx + 1 < static_cast<int>(g_points.size())) {
		const Pt& n = g_points[idx + 1];
		float span = n.pct - p.pct;
		if (span > 0.001f) {
			float t = (pct - p.pct) / span;
			p.speed += (n.speed - p.speed) * t;
			p.jump  += (n.jump  - p.jump)  * t;
			p.grav  += (n.grav  - p.grav)  * t;
			p.anim  += (n.anim  - p.anim)  * t;
			p.part  += (n.part  - p.part)  * t;
		}
	}

	c.speed = p.speed / 100.f;
	c.jump  = p.jump  / 100.f;
	c.grav  = p.grav  / 100.f;
	c.anim  = p.anim  / 100.f;
	c.part  = p.part  / 100.f;
	return c;
}

// the settings window with the table
class SsbPopup : public Popup {
protected:
	std::vector<std::vector<TextInput*>> m_cells;
	ButtonSprite* m_methodSpr = nullptr;

	bool init() {
		if (!Popup::init(420.f, 270.f)) return false;

		this->setTitle("Fake SSB");

		if (g_reload) loadConfig();

		// ---- switches ----
		auto en = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(SsbPopup::onEnable), 0.6f
		);
		en->toggle(g_enabled);
		m_buttonMenu->addChildAtPosition(en, Anchor::Center, ccp(-175.f, 88.f));

		auto enLabel = CCLabelBMFont::create("Enable", "bigFont.fnt");
		enLabel->setScale(0.4f);
		enLabel->setAnchorPoint({ 0.f, 0.5f });
		m_mainLayer->addChildAtPosition(enLabel, Anchor::Center, ccp(-158.f, 88.f));

		auto sm = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(SsbPopup::onSmooth), 0.6f
		);
		sm->toggle(g_smooth);
		m_buttonMenu->addChildAtPosition(sm, Anchor::Center, ccp(-70.f, 88.f));

		auto smLabel = CCLabelBMFont::create("Smooth", "bigFont.fnt");
		smLabel->setScale(0.4f);
		smLabel->setAnchorPoint({ 0.f, 0.5f });
		m_mainLayer->addChildAtPosition(smLabel, Anchor::Center, ccp(-53.f, 88.f));

		m_methodSpr = ButtonSprite::create(g_methodSpeed ? "Speed" : "Shape");
		m_methodSpr->setScale(0.6f);
		auto methodBtn = CCMenuItemSpriteExtra::create(
			m_methodSpr, this, menu_selector(SsbPopup::onMethod)
		);
		m_buttonMenu->addChildAtPosition(methodBtn, Anchor::Center, ccp(120.f, 88.f));

		// ---- table ----
		const char* heads[6] = { "Level%", "Speed%", "Jump%", "Grav%", "Anim%", "Part%" };
		const float xs[6] = { -150.f, -90.f, -30.f, 30.f, 90.f, 150.f };

		for (int c = 0; c < 6; c++) {
			auto h = CCLabelBMFont::create(heads[c], "goldFont.fnt");
			h->setScale(0.4f);
			m_mainLayer->addChildAtPosition(h, Anchor::Center, ccp(xs[c], 62.f));
		}

		auto pts = parsePoints(Mod::get()->getSavedValue<std::string>(
			"points", "25:125:100:100:100:100,35:100:100:100:100:100"
		));

		for (int r = 0; r < 7; r++) {
			std::vector<TextInput*> row;
			float y = 38.f - 21.f * static_cast<float>(r);

			for (int c = 0; c < 6; c++) {
				auto in = TextInput::create(50.f, c == 0 ? "%" : "100", "bigFont.fnt");
				in->setFilter("0123456789.");
				in->setScale(0.6f);
				m_mainLayer->addChildAtPosition(in, Anchor::Center, ccp(xs[c], y));

				if (r < static_cast<int>(pts.size())) {
					float v = pts[r].pct;
					if (c == 1) v = pts[r].speed;
					if (c == 2) v = pts[r].jump;
					if (c == 3) v = pts[r].grav;
					if (c == 4) v = pts[r].anim;
					if (c == 5) v = pts[r].part;
					in->setString(fmt::format("{:g}", v));
				}
				row.push_back(in);
			}
			m_cells.push_back(row);
		}

		auto hint = CCLabelBMFont::create(
			"Empty = 100. A row works from its Level% until the next row.",
			"bigFont.fnt"
		);
		hint->setScale(0.27f);
		m_mainLayer->addChildAtPosition(hint, Anchor::Center, ccp(0.f, -108.f));

		auto hint2 = CCLabelBMFont::create(
			"Speed = SSB, Jump = cube jump, Grav = gravity, Anim/Part = animation/particle speed.",
			"bigFont.fnt"
		);
		hint2->setScale(0.27f);
		m_mainLayer->addChildAtPosition(hint2, Anchor::Center, ccp(0.f, -120.f));

		return true;
	}

	void onEnable(CCObject* sender) {
		bool on = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
		Mod::get()->setSavedValue<bool>("enabled", on);
		g_reload = true;
	}

	void onSmooth(CCObject* sender) {
		bool on = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
		Mod::get()->setSavedValue<bool>("smooth", on);
		g_reload = true;
	}

	void onMethod(CCObject*) {
		bool speed = !Mod::get()->getSavedValue<bool>("method-speed", false);
		Mod::get()->setSavedValue<bool>("method-speed", speed);
		m_methodSpr->setString(speed ? "Speed" : "Shape");
		g_reload = true;
	}

	void saveFromInputs() {
		std::string out;
		for (auto& row : m_cells) {
			if (row[0]->getString().empty()) continue;
			if (!out.empty()) out += ",";
			for (size_t c = 0; c < row.size(); c++) {
				if (c > 0) out += ":";
				out += row[c]->getString();
			}
		}
		Mod::get()->setSavedValue<std::string>("points", out);
		g_reload = true;
	}

public:
	static SsbPopup* create() {
		auto ret = new SsbPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}

	// the table is saved when the window is closed
	~SsbPopup() {
		this->saveFromInputs();
	}
};

class $modify(SsbPlayer, PlayerObject) {
public:
	void update(float dt) {
		// do not recompute on every single step
		static int tick = 0;
		if ((tick++ % 4) == 0) {
			g_now = computeNow();
			g_animFactor = g_now.anim;
			g_partFactor = g_now.part;
		}

		float speed = g_now.speed;
		float grav = g_now.grav;

		if (speed == 1.f && grav == 1.f) {
			PlayerObject::update(dt);
			return;
		}

		// gravity: only for the duration of this step
		float savedGrav = m_gravityMod;
		if (grav != 1.f) m_gravityMod = savedGrav * grav;

		if (speed == 1.f) {
			PlayerObject::update(dt);
		} else if (g_methodSpeed) {
			// Speed: the whole player runs faster or slower
			PlayerObject::update(dt * speed);
		} else {
			// Shape: only the forward movement is stretched or squeezed
			float oldX = this->getPositionX();
			PlayerObject::update(dt);

			float dx = this->getPositionX() - oldX;
			if (dx > 0.f && dx <= 50.f) {
				this->setPosition(CCPoint(oldX + dx * speed, this->getPositionY()));
			}
		}

		m_gravityMod = savedGrav;
	}

	// jump height (cube only, jumps from the ground)
	void setYVelocity(double velocity, int type) {
		if (g_now.jump != 1.f && m_isOnGround &&
			!m_isShip && !m_isBird && !m_isBall && !m_isDart &&
			!m_isRobot && !m_isSpider && !m_isSwing) {
			velocity *= g_now.jump;
		}
		PlayerObject::setYVelocity(velocity, type);
	}
};

class $modify(SsbPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		// animations must run normally in the pause menu
		g_animFactor = 1.f;
		g_partFactor = 1.f;

		auto win = CCDirector::get()->getWinSize();

		// the logo (copy of logo.png in resources); text button if it is missing
		CCNode* icon = CCSprite::create("ssb.png"_spr);
		if (!icon) icon = ButtonSprite::create("SSB");

		// find the menu with the buttons on the right side
		CCMenu* rightMenu = nullptr;
		if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID("right-button-menu"))) {
			rightMenu = byId;
		} else if (auto kids = this->getChildren()) {
			int best = 0;
			for (auto obj : CCArrayExt<CCNode*>(kids)) {
				auto menu = typeinfo_cast<CCMenu*>(obj);
				if (!menu) continue;
				if (menu->getPositionX() < win.width * 0.6f) continue;
				int n = static_cast<int>(menu->getChildrenCount());
				if (n > best) {
					best = n;
					rightMenu = menu;
				}
			}
		}

		// size the icon like the buttons next to it
		float size = 45.f;
		if (rightMenu && rightMenu->getChildrenCount() > 0) {
			if (auto first = typeinfo_cast<CCNode*>(rightMenu->getChildren()->objectAtIndex(0))) {
				float h = first->getContentSize().height;
				if (h > 10.f) size = h;
			}
		}
		float ch = icon->getContentSize().height;
		if (ch > 1.f) icon->setScale(size / ch);

		auto btn = CCMenuItemSpriteExtra::create(
			icon, this, menu_selector(SsbPause::onSsb)
		);
		btn->setID("ssb-button"_spr);

		if (rightMenu) {
			float x = 0.f;
			float minY = 0.f;
			float maxY = 0.f;
			bool any = false;
			if (auto kids = rightMenu->getChildren()) {
				for (auto obj : CCArrayExt<CCNode*>(kids)) {
					if (!obj) continue;
					CCPoint p = obj->getPosition();
					if (!any) {
						x = p.x;
						minY = p.y;
						maxY = p.y;
						any = true;
					} else {
						minY = std::min(minY, p.y);
						maxY = std::max(maxY, p.y);
					}
				}
			}

			rightMenu->addChild(btn);

			if (rightMenu->getLayout()) {
				rightMenu->updateLayout();
			} else if (any) {
				float gap = size + 6.f;
				CCPoint below = rightMenu->convertToWorldSpace(CCPoint(x, minY - gap));
				if (below.y > size * 0.6f) {
					btn->setPosition({ x, minY - gap });
				} else {
					btn->setPosition({ x, maxY + gap });
				}
			}
		} else {
			auto menu = CCMenu::create();
			menu->addChild(btn);
			menu->setPosition({ win.width - 110.f, win.height - 40.f });
			this->addChild(menu);
		}
	}

	void onSsb(CCObject*) {
		if (auto p = SsbPopup::create()) p->show();
	}
};
