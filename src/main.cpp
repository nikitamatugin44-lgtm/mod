#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include <optional>

using namespace geode::prelude;

struct SsbPoint {
    float percent = 0.f;
    float value = 100.f;
};

static std::vector<SsbPoint> loadPoints(bool sortPoints = true) {
    // Format: percent:value;percent:value. Old slot defaults migrate to two points.
    auto raw = Mod::get()->getSavedValue<std::string>("ssb-points", "25:125;35:100");
    std::vector<SsbPoint> points;
    std::stringstream all(raw);
    std::string part;
    while (std::getline(all, part, ';')) {
        auto split = part.find(':');
        if (split == std::string::npos) continue;
        try {
            float pct = std::stof(part.substr(0, split));
            float val = std::stof(part.substr(split + 1));
            if (!std::isfinite(pct) || !std::isfinite(val)) continue;
            pct = std::clamp(pct, 0.f, 100.f);
            val = std::clamp(val, 0.f, 300.f);
            points.push_back({pct, val});
        } catch (...) {}
        if (points.size() >= 50) break;
    }
    if (sortPoints) {
        std::stable_sort(points.begin(), points.end(), [](auto const& a, auto const& b) {
            return a.percent < b.percent;
        });
    }
    return points;
}

static void savePoints(std::vector<SsbPoint> const& points) {
    std::ostringstream out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i) out << ';';
        out << points[i].percent << ':' << points[i].value;
    }
    Mod::get()->setSavedValue("ssb-points", out.str());
}

// Stepped curve: default is 100% until the first point, then each point's value
// remains active until the next point is reached.
static float currentFactor() {
    auto mod = Mod::get();
    if (!mod->getSettingValue<bool>("ssb-enabled")) return 1.f;
    auto pl = PlayLayer::get();
    if (!pl) return 1.f;

    float pct = static_cast<float>(pl->getCurrentPercent());
    float value = 100.f;
    for (auto const& point : loadPoints()) {
        if (pct < point.percent) break;
        value = point.value;
    }
    return value / 100.f;
}

class SsbSettingsPopup : public Popup {
protected:
    ScrollLayer* m_scroll = nullptr;

    bool init() {
        if (!Popup::init(360.f, 280.f)) return false;
        this->setTitle("Fake SSB");

        auto mod = Mod::get();
        auto enabledText = mod->getSettingValue<bool>("ssb-enabled") ? "SSB: ON" : "SSB: OFF";
        auto enabledBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(enabledText, 90, true, "bigFont.fnt", "GJ_button_01.png", 25.f, 0.6f),
            this, menu_selector(SsbSettingsPopup::onToggleEnabled));
        enabledBtn->setPosition({-100.f, 103.f});

        auto method = mod->getSettingValue<std::string>("ssb-method");
        auto methodText = "Method: " + method;
        auto methodBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(methodText.c_str(), 120, true, "bigFont.fnt", "GJ_button_02.png", 25.f, 0.55f),
            this, menu_selector(SsbSettingsPopup::onToggleMethod));
        methodBtn->setPosition({55.f, 103.f});

        auto topMenu = CCMenu::create();
        topMenu->setPosition({m_size.width / 2.f, m_size.height / 2.f});
        topMenu->addChild(enabledBtn);
        topMenu->addChild(methodBtn);
        m_mainLayer->addChild(topMenu);

        auto headers = CCNode::create();
        auto pLabel = CCLabelBMFont::create("Percent", "bigFont.fnt");
        pLabel->setScale(0.42f);
        pLabel->setPosition({m_size.width / 2.f - 95.f, m_size.height / 2.f + 67.f});
        m_mainLayer->addChild(pLabel);
        auto vLabel = CCLabelBMFont::create("Value %", "bigFont.fnt");
        vLabel->setScale(0.42f);
        vLabel->setPosition({m_size.width / 2.f + 5.f, m_size.height / 2.f + 67.f});
        m_mainLayer->addChild(vLabel);

        auto addSpr = ButtonSprite::create("+ Add point", 100, true, "bigFont.fnt", "GJ_button_01.png", 25.f, 0.55f);
        auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(SsbSettingsPopup::onAddPoint));
        addBtn->setPosition({m_size.width / 2.f, 20.f});
        auto bottomMenu = CCMenu::create();
        bottomMenu->setPosition({0.f, 0.f});
        bottomMenu->addChild(addBtn);
        m_mainLayer->addChild(bottomMenu);

        this->rebuildRows();
        return true;
    }

    void rebuildRows() {
        if (m_scroll) {
            m_scroll->removeFromParentAndCleanup(true);
            m_scroll = nullptr;
        }
        auto points = loadPoints();
        auto rect = CCRect(34.f, 47.f, m_size.width - 68.f, 125.f);
        m_scroll = ScrollLayer::create(rect, true, true);
        if (!m_scroll) return;
        m_mainLayer->addChild(m_scroll);

        float rowHeight = 28.f;
        float contentHeight = std::max(125.f, static_cast<float>(points.size()) * rowHeight + 8.f);
        m_scroll->m_contentLayer->setContentSize({rect.size.width, contentHeight});

        for (size_t i = 0; i < points.size(); ++i) {
            float y = contentHeight - 18.f - static_cast<float>(i) * rowHeight;
            auto const point = points[i];

            auto percentInput = TextInput::create(70.f, "0-100", "bigFont.fnt");
            percentInput->setString(std::to_string(static_cast<int>(std::round(point.percent))), false);
            percentInput->setPosition({75.f, y});
            percentInput->setCallback([i](std::string const& text) {
                auto value = parseNumber(text, 0.f, 100.f);
                auto pts = loadPoints(false);
                if (i >= pts.size() || !value) return;
                pts[i].percent = *value;
                savePoints(pts);
            });
            m_scroll->m_contentLayer->addChild(percentInput);

            auto valueInput = TextInput::create(70.f, "0-300", "bigFont.fnt");
            valueInput->setString(std::to_string(static_cast<int>(std::round(point.value))), false);
            valueInput->setPosition({170.f, y});
            valueInput->setCallback([i](std::string const& text) {
                auto value = parseNumber(text, 0.f, 300.f);
                auto pts = loadPoints(false);
                if (i >= pts.size() || !value) return;
                pts[i].value = *value;
                savePoints(pts);
            });
            m_scroll->m_contentLayer->addChild(valueInput);

            auto delSpr = ButtonSprite::create("X", 30, true, "bigFont.fnt", "GJ_button_06.png", 20.f, 0.55f);
            auto delBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(SsbSettingsPopup::onDeletePoint));
            delBtn->setTag(static_cast<int>(i));
            delBtn->setPosition({rect.size.width - 18.f, y});
            auto rowMenu = CCMenu::create();
            rowMenu->setPosition({0.f, 0.f});
            rowMenu->addChild(delBtn);
            m_scroll->m_contentLayer->addChild(rowMenu);
        }
    }

    static std::optional<float> parseNumber(std::string const& text, float min, float max) {
        try {
            size_t used = 0;
            float value = std::stof(text, &used);
            if (used != text.size() || !std::isfinite(value)) return std::nullopt;
            return std::clamp(value, min, max);
        } catch (...) { return std::nullopt; }
    }

    void onToggleEnabled(CCObject*) {
        auto mod = Mod::get();
        mod->setSettingValue("ssb-enabled", !mod->getSettingValue<bool>("ssb-enabled"));
        this->removeFromParentAndCleanup(true);
        SsbSettingsPopup::create()->show();
    }

    void onToggleMethod(CCObject*) {
        auto mod = Mod::get();
        auto current = mod->getSettingValue<std::string>("ssb-method");
        mod->setSettingValue("ssb-method", current == "Shape" ? "Speed" : "Shape");
        this->removeFromParentAndCleanup(true);
        SsbSettingsPopup::create()->show();
    }

    void onAddPoint(CCObject*) {
        auto points = loadPoints();
        if (points.size() >= 50) {
            FLAlertLayer::create("Fake SSB", "You can add up to 50 points.", "OK")->show();
            return;
        }
        points.push_back({0.f, 100.f});
        std::stable_sort(points.begin(), points.end(), [](auto const& a, auto const& b) { return a.percent < b.percent; });
        savePoints(points);
        this->rebuildRows();
    }

    void onDeletePoint(CCObject* sender) {
        auto points = loadPoints();
        auto index = static_cast<size_t>(sender->getTag());
        if (index < points.size()) points.erase(points.begin() + index);
        savePoints(points);
        this->rebuildRows();
    }

public:
    static SsbSettingsPopup* create() {
        auto ret = new SsbSettingsPopup();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

class $modify(SsbPlayer, PlayerObject) {
public:
    void update(float dt) {
        static int tick = 0;
        static float cached = 1.f;
        if ((tick++ % 4) == 0) cached = currentFactor();
        float factor = cached;

        if (factor == 1.f) {
            PlayerObject::update(dt);
            return;
        }
        if (Mod::get()->getSettingValue<std::string>("ssb-method") == "Speed") {
            PlayerObject::update(dt * factor);
            return;
        }
        float oldX = this->getPositionX();
        PlayerObject::update(dt);
        float dx = this->getPositionX() - oldX;
        if (dx <= 0.f || dx > 50.f) return;
        this->setPosition(CCPoint(oldX + dx * factor, this->getPositionY()));
    }
};

class $modify(SsbPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        // Put resources/logo.png in the mod's resources directory.
        auto spr = CCSprite::create("logo.png");
        if (!spr) {
            spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        }
        if (!spr) {
            spr = CCSprite::create();
        }
        spr->setScale(0.45f);
        spr->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, 360.f)));

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SsbPause::onSsb));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 60.f, win.height - 30.f});
        this->addChild(menu);
    }

    void onSsb(CCObject*) {
        SsbSettingsPopup::create()->show();
    }
};
