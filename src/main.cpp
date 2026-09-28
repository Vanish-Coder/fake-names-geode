#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

// ---- Layout constants (easy to tweak) ----
constexpr float POPUP_WIDTH = 280.0f;
constexpr float POPUP_HEIGHT = 190.0f;
constexpr float INPUT_WIDTH = 220.0f;
constexpr size_t MAX_NAME_LENGTH = 50;
constexpr size_t MAX_AUTHOR_LENGTH = 30;

// True while our popup is on screen, so it can't open twice.
static bool s_popupOpen = false;

struct LevelOverride {
    std::string name;
    std::string author;
};

// m_levelID may be a plain int or a wrapper type depending on version;
// this handles both.
template <class T>
static int toInt(T const& v) {
    if constexpr (requires { v.value(); }) {
        return static_cast<int>(v.value());
    } else {
        return static_cast<int>(v);
    }
}

static int getLevelID(GJGameLevel* level) {
    return level ? toInt(level->m_levelID) : 0;
}

// ---- Persistence ----
static std::string keyFor(int levelID, char const* field) {
    return fmt::format("level_{}_{}", levelID, field);
}

static LevelOverride loadLevelOverride(int levelID) {
    LevelOverride ov;
    ov.name = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "name"), std::string());
    ov.author = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "author"), std::string());
    return ov;
}

static void saveLevelOverride(int levelID, LevelOverride const& ov) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), ov.name);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), ov.author);
    log::debug("Saved override for level {}: name='{}' author='{}'", levelID, ov.name, ov.author);
}

static void clearLevelOverride(int levelID) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), std::string());
    log::debug("Cleared override for level {}", levelID);
}

// ---- Label helpers ----

// Sets the text of a label, or of the label inside a button.
static bool setLabelText(CCNode* node, std::string const& text) {
    if (!node) return false;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
        label->setString(text.c_str());
        return true;
    }

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(btn->getNormalImage())) {
            label->setString(text.c_str());
            // Resize the button to fit the new text and re-center the label.
            btn->setContentSize(label->getScaledContentSize());
            auto size = btn->getContentSize();
            label->setPosition(ccp(size.width / 2, size.height / 2));
            return true;
        }
    }
    return false;
}

// Applies saved overrides to the info layer.
// force = false: only touch labels that have an override (used at startup).
// force = true: also restore the original text when there is no override
//               (used after Save / Reset).
static void applyOverrides(CCNode* layer, GJGameLevel* level, bool force) {
    if (!layer || !level) return;

    int levelID = getLevelID(level);
    if (levelID <= 0) return; // local/unsaved levels are not supported

    auto ov = loadLevelOverride(levelID);

    // Level name
    if (force || !ov.name.empty()) {
        std::string original = level->m_levelName;
        std::string text = ov.name.empty() ? original : ov.name;
        if (!setLabelText(layer->getChildByID("title-label"), text)) {
            log::debug("Could not find title-label");
        }
    }

    // Author name
    if (force || !ov.author.empty()) {
        std::string original = level->m_creatorName;
        std::string text = ov.author.empty() ? original : ov.author;

        auto menu = layer->getChildByID("creator-info-menu");
        if (menu && setLabelText(menu->getChildByID("creator-name"), text)) {
            // Let the menu's layout re-space the author label and any icon
            // next to it.
            menu->updateLayout();
        } else {
            log::debug("Could not find creator-name");
        }
    }
}

// ---- Edit popup ----
class EditNamePopup : public Popup {
protected:
    Ref<CCNode> m_layer;
    Ref<GJGameLevel> m_level;
    int m_levelID = 0;
    TextInput* m_nameInput = nullptr;
    TextInput* m_authorInput = nullptr;

    bool init(CCNode* layer, GJGameLevel* level) {
        if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

        m_layer = layer;
        m_level = level;
        m_levelID = getLevelID(level);

        this->setTitle("Edit Level Names");

        auto current = loadLevelOverride(m_levelID);

        // Level name
        auto nameLabel = CCLabelBMFont::create("Level name", "goldFont.fnt");
        nameLabel->setScale(0.5f);
        m_mainLayer->addChildAtPosition(nameLabel, Anchor::Center, ccp(0, 45));

        m_nameInput = TextInput::create(INPUT_WIDTH, std::string(level->m_levelName));
        m_nameInput->setMaxCharCount(MAX_NAME_LENGTH);
        m_nameInput->setString(current.name);
        m_mainLayer->addChildAtPosition(m_nameInput, Anchor::Center, ccp(0, 22));

        // Author name
        auto authorLabel = CCLabelBMFont::create("Author name", "goldFont.fnt");
        authorLabel->setScale(0.5f);
        m_mainLayer->addChildAtPosition(authorLabel, Anchor::Center, ccp(0, -12));

        m_authorInput = TextInput::create(INPUT_WIDTH, std::string(level->m_creatorName));
        m_authorInput->setMaxCharCount(MAX_AUTHOR_LENGTH);
        m_authorInput->setString(current.author);
        m_mainLayer->addChildAtPosition(m_authorInput, Anchor::Center, ccp(0, -35));

        // Buttons
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Save"), this, menu_selector(EditNamePopup::onSave)
        );
        m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Bottom, ccp(-50, 25));

        auto resetBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Reset"), this, menu_selector(EditNamePopup::onReset)
        );
        m_buttonMenu->addChildAtPosition(resetBtn, Anchor::Bottom, ccp(50, 25));

        return true;
    }

    void onSave(CCObject*) {
        LevelOverride ov;
        ov.name = m_nameInput->getString();
        ov.author = m_authorInput->getString();
        saveLevelOverride(m_levelID, ov);
        applyOverrides(m_layer.data(), m_level.data(), true);
        this->keyBackClicked(); // closes the popup, same as pressing back
    }

    void onReset(CCObject*) {
        clearLevelOverride(m_levelID);
        m_nameInput->setString("");
        m_authorInput->setString("");
        applyOverrides(m_layer.data(), m_level.data(), true);
    }

public:
    static EditNamePopup* create(CCNode* layer, GJGameLevel* level) {
        auto ret = new EditNamePopup();
        if (ret->init(layer, level)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    ~EditNamePopup() {
        s_popupOpen = false;
    }
};

// ---- Hook: apply overrides when the level info page opens ----
class $modify(FakeNamesLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        log::debug("LevelInfoLayer::init for level {}", getLevelID(level));
        applyOverrides(this, level, false);

        return true;
    }
};

// ---- Keybind: open the edit popup ----
$on_game(Loaded) {
    listenForKeybindSettingPresses(
        "open-popup-keybind",
        [](Keybind const& keybind, bool down, bool repeat, double timestamp) {
            if (!down || repeat) return;
            if (s_popupOpen) return;

            auto scene = CCDirector::sharedDirector()->getRunningScene();
            if (!scene) return;

            auto layer = scene->getChildByType<LevelInfoLayer>(0);
            if (!layer || !layer->m_level) {
                log::debug("Keybind pressed but no LevelInfoLayer is open");
                return;
            }

            if (getLevelID(layer->m_level) <= 0) {
                log::debug("Local levels are not supported");
                return;
            }

            auto popup = EditNamePopup::create(layer, layer->m_level);
            if (popup) {
                s_popupOpen = true;
                popup->show();
                log::debug("Opened edit popup");
            }
        }
    );
}
