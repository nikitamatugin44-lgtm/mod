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

using namespace geode::prelude;

struct Pt {
	float pct;
	float speed;
};

static std::vector<Pt> g_points;
static bool g_reload = true;

// the table is saved as text: "25:125,35:100"
static std::vector<Pt> loadPoints() {
	std::vector<Pt> out;
	std::string s = Mod::get()->getSavedValue<std::string>("points", "25:125,35:100");

	size_t i = 0;
	while (i <= s.size()) {
		size_t j = s.find(',', i);
		if (j == std::string::npos) j = s.size();

		std::string item = s.substr(i, j - i);
		size_t c = item.find(':');
		if (c != std::string::npos) {
			float a = std::strtof(item.substr(0, c).c_str(), nullptr);
			float b = std::strtof(item.substr(c + 1).c_str(), nullptr);
			out.push_back(Pt{ a, b });
		}
		i = j + 1;
	}

	std::sort(out.begin(), out.end(), [](const Pt& x, const Pt& y) {
		return x.pct < y.pct;
	});
	return out;
}

// speed factor for the current level percent (1.0 = no effect)
static float currentFactor() {
	auto mod = Mod::get();
	if (!mod->getSettingValue<bool>("ssb-enabled")) return 1.f;

	auto pl = PlayLayer::get();
	if (!pl) return 1.f;

	if (g_reload) {
		g_points = loadPoints();
		g_reload = false;
	}
	if (g_points.empty()) return 1.f;

	float pct = pl->getCurrentPercent();

	int idx = -1;
	for (int i = 0; i < static_cast<int>(g_points.size()); i++) {
		if (g_points[i].pct <= pct) idx = i;
		else break;
	}
	if (idx < 0) return 1.f;

	float v = g_points[idx].speed;

	if (mod->getSettingValue<bool>("ssb-smooth") &&
		idx + 1 < static_cast<int>(g_points.size())) {
		auto& a = g_points[idx];
		auto& b = g_points[idx + 1];
		float span = b.pct - a.pct;
		if (span > 0.001f) {
			float t = (pct - a.pct) / span;
			v = a.speed + (b.speed - a.speed) * t;
		}
	}
	return v / 100.f;
}

// the table window
class PointsPopup : public Popup {
protected:
	std::vector<TextInput*> m_pct;
	std::vector<TextInput*> m_spd;

	bool init() {
		if (!Popup::init(340.f, 250.f)) return false;

		this->setTitle("Fake SSB points");

		auto sz = m_mainLayer->getContentSize();
		CCPoint c(sz.width * 0.5f, sz.height * 0.5f);

		auto h1 = CCLabelBMFont::create("Level %", "goldFont.fnt");
		h1->setScale(0.5f);
		h1->setPosition({ c.x - 70.f, c.y + 88.f });
		m_mainLayer->addChild(h1);

		auto h2 = CCLabelBMFont::create("Speed %", "goldFont.fnt");
		h2->setScale(0.5f);
		h2->setPosition({ c.x + 70.f, c.y + 88.f });
		m_mainLayer->addChild(h2);

		auto pts = loadPoints();

		for (int i = 0; i < 7; i++) {
			float y = c.y + 62.f - 22.f * static_cast<float>(i);

			auto a = TextInput::create(90.f, "%", "bigFont.fnt");
			a->setFilter("0123456789.");
			a->setScale(0.7f);
			a->setPosition({ c.x - 70.f, y });
			m_mainLayer->addChild(a);

			auto b = TextInput::create(90.f, "speed", "bigFont.fnt");
			b->setFilter("0123456789.");
			b->setScale(0.7f);
			b->setPosition({ c.x + 70.f, y });
			m_mainLayer->addChild(b);

			if (i < static_cast<int>(pts.size())) {
				a->setString(fmt::format("{:g}", pts[i].pct));
				b->setString(fmt::format("{:g}", pts[i].speed));
			}

			m_pct.push_back(a);
			m_spd.push_back(b);
		}

		auto hint = CCLabelBMFont::create(
			"Speed applies from this % until the next point. 100 = normal.",
			"bigFont.fnt"
		);
		hint->setScale(0.3f);
		hint->setPosition({ c.x, c.y - 102.f });
		m_mainLayer->addChild(hint);

		return true;
	}

	void saveFromInputs() {
		std::string out;
		for (size_t i = 0; i < m_pct.size(); i++) {
			std::string a = m_pct[i]->getString();
			std::string b = m_spd[i]->getString();
			if (a.empty() || b.empty()) continue;
			if (!out.empty()) out += ",";
			out += a + ":" + b;
		}
		Mod::get()->setSavedValue<std::string>("points", out);
		g_reload = true;
	}

public:
	static PointsPopup* create() {
		auto ret = new PointsPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}

	// the table is saved when the window is closed
	~PointsPopup() {
		this->saveFromInputs();
	}
};

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

		auto win = CCDirector::get()->getWinSize();

		// the icon (copy of logo.png in resources); text button if it is missing
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
			// remember where the other buttons are
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
				// the menu arranges its buttons by itself
				rightMenu->updateLayout();
			} else if (any) {
				// no layout: put it under the last button, or above if there is no room
				float gap = size + 6.f;
				CCPoint below = rightMenu->convertToWorldSpace(CCPoint(x, minY - gap));
				if (below.y > size * 0.6f) {
					btn->setPosition({ x, minY - gap });
				} else {
					btn->setPosition({ x, maxY + gap });
				}
			}
		} else {
			// right menu not found: own menu near the top right
			auto menu = CCMenu::create();
			menu->addChild(btn);
			menu->setPosition({ win.width - 110.f, win.height - 40.f });
			this->addChild(menu);
		}
	}

	void onSsb(CCObject*) {
		if (auto p = PointsPopup::create()) p->show();
	}
};
