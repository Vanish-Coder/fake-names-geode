#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/PauseLayer.hpp>
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

// Icon detection next to the author name (in screen units)
constexpr float ICON_MAX_SIZE = 50.0f; // only nodes smaller than this count as icons
constexpr float ICON_MAX_GAP = 30.0f;  // how far from the name an icon can be

// Longest a fake name may be in list cells / pause screen before it is
// scaled down to fit (in screen units)
constexpr float MAX_LABEL_WIDTH = 180.0f;

// True while our popup is on screen, so it can't open twice.
static bool s_popupOpen = false;

struct LevelOverride {
    std::string name;
    std::string author;
};

// m_levelID is a SeedValueRSV, so read it with .value()
static int getLevelID(GJGameLevel* level) {
    return level ? level->m_levelID.value() : 0;
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

// ---- Label helpers (level info page) ----

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

// ---- Icon repositioning helpers ----

// Bounding box of a node in world (screen) coordinates.
static CCRect worldBounds(CCNode* n) {
    auto size = n->getContentSize();
    auto a = n->convertToWorldSpace(ccp(0, 0));
    auto b = n->convertToWorldSpace(ccp(size.width, size.height));
    return CCRect(
        std::min(a.x, b.x), std::min(a.y, b.y),
        std::abs(b.x - a.x), std::abs(b.y - a.y)
    );
}

// A small node (like the copyright icon) that sits next to the author name.
struct Neighbor {
    CCNode* node;
    CCPoint worldPos; // its position before the name changed
    bool onRight;     // true if it sits to the right of the name
};

// Finds small nodes right next to the name button among root's children.
static void collectNeighbors(CCNode* root, CCNode* nameNode, CCRect const& nameBox, std::vector<Neighbor>& out) {
    if (!root) return;
    auto children = root->getChildren();
    if (!children) return;

    float nameCenterY = nameBox.origin.y + nameBox.size.height / 2;
    float nameRight = nameBox.origin.x + nameBox.size.width;
    float nameLeft = nameBox.origin.x;

    for (unsigned int i = 0; i < children->count(); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (!child || child == nameNode || !child->isVisible()) continue;

        auto box = worldBounds(child);
        if (box.size.width <= 0 || box.size.width > ICON_MAX_SIZE || box.size.height > ICON_MAX_SIZE) continue;

        // Must be on roughly the same line as the name
        float centerY = box.origin.y + box.size.height / 2;
        if (std::abs(centerY - nameCenterY) > std::max(nameBox.size.height, box.size.height)) continue;

        float gapRight = box.origin.x - nameRight;
        float gapLeft = nameLeft - (box.origin.x + box.size.width);

        auto parent = child->getParent();
        if (!parent) continue;
        auto worldPos = parent->convertToWorldSpace(child->getPosition());

        if (gapRight >= -5.0f && gapRight <= ICON_MAX_GAP) {
            out.push_back({ child, worldPos, true });
        } else if (gapLeft >= -5.0f && gapLeft <= ICON_MAX_GAP) {
            out.push_back({ child, worldPos, false });
        }
    }
}

// Moves the neighbors by however far the name's edges moved.
static void shiftNeighbors(std::vector<Neighbor> const& list, CCRect const& before, CCRect const& after) {
    float rightDelta = (after.origin.x + after.size.width) - (before.origin.x + before.size.width);
    float leftDelta = after.origin.x - before.origin.x;

    for (auto const& n : list) {
        auto parent = n.node->getParent();
        if (!parent) continue;
        auto wp = n.worldPos;
        wp.x += n.onRight ? rightDelta : leftDelta;
        n.node->setPosition(parent->convertToNodeSpace(wp));
    }
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

    // Author name (and the icon next to it)
    if (force || !ov.author.empty()) {
        std::string original = level->m_creatorName;
        std::string text = ov.author.empty() ? original : ov.author;

        auto menu = layer->getChildByID("creator-info-menu");
        auto nameNode = menu ? menu->getChildByID("creator-name") : nullptr;

        if (nameNode) {
            // 1. Remember where the name and nearby icons are now
            auto before = worldBounds(nameNode);
            std::vector<Neighbor> neighbors;
            collectNeighbors(menu, nameNode, before, neighbors);
            collectNeighbors(layer, nameNode, before, neighbors);

            // 2. Change the text
            if (setLabelText(nameNode, text)) {
                menu->updateLayout();

                // 3. Move the icons by however far the name's edges moved
                shiftNeighbors(neighbors, before, worldBounds(nameNode));
                log::debug("Moved {} icon(s) next to the author name", neighbors.size());
            }
        } else {
            log::debug("Could not find creator-name");
        }
    }
}

// ---- Label helpers (list cells and pause screen) ----

// Searches root and all its descendants for a label whose text is exactly
// one of the given strings.
static CCLabelBMFont* findLabelWithText(CCNode* root, std::vector<std::string> const& texts) {
    if (!root) return nullptr;

    if (auto label = typeinfo_cast<CCLabelBMFont*>(root)) {
        std::string current = label->getString();
        for (auto const& t : texts) {
            if (current == t) return label;
        }
    }

    auto children = root->getChildren();
    if (!children) return nullptr;
    for (unsigned int i = 0; i < children->count(); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (auto found = findLabelWithText(child, texts)) return found;
    }
    return nullptr;
}

// Changes a label's text, shrinks it if the new text is too wide, and
// resizes the button around it if it lives inside one.
static void replaceLabelText(CCLabelBMFont* label, std::string const& text) {
    float oldWidth = label->getScaledContentSize().width;
    label->setString(text.c_str());

    float maxWidth = std::max(oldWidth, MAX_LABEL_WIDTH);
    float newWidth = label->getScaledContentSize().width;
    if (newWidth > maxWidth && newWidth > 0) {
        label->setScale(label->getScale() * maxWidth / newWidth);
    }

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(label->getParent())) {
        btn->setContentSize(label->getScaledContentSize());
        auto size = btn->getContentSize();
        label->setPosition(ccp(size.width / 2, size.height / 2));
    }
}

// Finds the level's name and author labels anywhere under root by matching
// their text against the level's real name and creator, then swaps in the
// saved overrides. Used for list cells and the pause screen.
static void applyToLabels(CCNode* root, GJGameLevel* level) {
    if (!root || !level) return;

    int levelID = getLevelID(level);
    if (levelID <= 0) return; // local/unsaved levels are not supported

    auto ov = loadLevelOverride(levelID);
    if (ov.name.empty() && ov.author.empty()) return;

    std::string realName = level->m_levelName;
    std::string realAuthor = level->m_creatorName;

    if (!ov.name.empty() && !realName.empty()) {
        if (auto label = findLabelWithText(root, { realName })) {
            replaceLabelText(label, ov.name);
        } else {
            log::debug("Could not find level name label for level {}", levelID);
        }
    }

    if (!ov.author.empty() && !realAuthor.empty()) {
        if (auto label = findLabelWithText(root, { realAuthor, "By " + realAuthor })) {
            // Keep a "By " prefix if the original label had one
            bool hasPrefix = std::string(label->getString()).rfind("By ", 0) == 0;
            replaceLabelText(label, hasPrefix ? "By " + ov.author : ov.author);
        } else {
            log::debug("Could not find author label for level {}", levelID);
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

// ---- Hook: level info page ----
class $modify(FakeNamesLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        log::debug("LevelInfoLayer::init for level {}", getLevelID(level));
        applyOverrides(this, level, false);

        return true;
    }
};

// ---- Hook: level cells in lists (saved, created, search, ...) ----
class $modify(FakeNamesLevelCell, LevelCell) {
    void loadCustomLevelCell() {
        LevelCell::loadCustomLevelCell();
        applyToLabels(this, m_level);
    }
};

// ---- Hook: pause screen ----
class $modify(FakeNamesPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto playLayer = PlayLayer::get();
        if (playLayer) {
            applyToLabels(this, playLayer->m_level);
        }
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
