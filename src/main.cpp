#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

// ---- Layout constants (easy to tweak) ----
constexpr float POPUP_WIDTH = 280.0f;
constexpr float POPUP_HEIGHT = 350.0f;
constexpr float INPUT_WIDTH = 220.0f;
constexpr size_t MAX_NAME_LENGTH = 50;
constexpr size_t MAX_AUTHOR_LENGTH = 30;
constexpr size_t MAX_DOWNLOADS_LENGTH = 10;
constexpr size_t MAX_LIKES_LENGTH = 10;

// Icon detection next to the author name (in screen units)
constexpr float ICON_MAX_SIZE = 50.0f; // only nodes smaller than this count as icons
constexpr float ICON_MAX_GAP = 30.0f;  // how far from the name an icon can be

// Longest a fake name may be in list cells / pause screen before it is
// scaled down to fit (in screen units)
constexpr float MAX_LABEL_WIDTH = 180.0f;

// In list cells, which part of the author name stays fixed when the text
// changes. true = left edge (left-aligned names), false = centre.
constexpr bool CELL_KEEP_LEFT_EDGE = true;

// True while our popup is on screen, so it can't open twice.
static bool s_popupOpen = false;

struct LevelOverride {
    std::string name;
    std::string author;
    std::string downloads;
    std::string likes;
    std::string listDownloads;
    std::string listLikes;
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
    ov.downloads = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "downloads"), std::string());
    ov.likes = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "likes"), std::string());
    ov.listDownloads = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "listDownloads"), std::string());
    ov.listLikes = Mod::get()->getSavedValue<std::string>(keyFor(levelID, "listLikes"), std::string());
    return ov;
}

static void saveLevelOverride(int levelID, LevelOverride const& ov) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), ov.name);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), ov.author);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "downloads"), ov.downloads);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "likes"), ov.likes);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listDownloads"), ov.listDownloads);
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listLikes"), ov.listLikes);
    log::debug("Saved override for level {}: name='{}' author='{}' downloads='{}' likes='{}' listDownloads='{}' listLikes='{}'", levelID, ov.name, ov.author, ov.downloads, ov.likes, ov.listDownloads, ov.listLikes);
}

static void clearLevelOverride(int levelID) {
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "name"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "author"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "downloads"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "likes"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listDownloads"), std::string());
    Mod::get()->setSavedValue<std::string>(keyFor(levelID, "listLikes"), std::string());
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

// Finds small nodes right next to the name among root's children.
// deep = true also searches inside larger containers (used for list cells,
// where icons can be nested a few levels down).
static void collectNeighbors(
    CCNode* root, CCNode* nameNode, CCRect const& nameBox,
    std::vector<Neighbor>& out, bool deep = false
) {
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

        // Too big to be an icon: it may be a container holding icons
        if (box.size.width > ICON_MAX_SIZE || box.size.height > ICON_MAX_SIZE) {
            if (deep) collectNeighbors(child, nameNode, nameBox, out, true);
            continue;
        }
        if (box.size.width <= 0) continue;

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
        std::string text = ov.author.empty() ? original : "By " + ov.author;

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

    // Downloads
    if (force || !ov.downloads.empty()) {
        std::string original = std::to_string(level->m_downloads);
        std::string text = ov.downloads.empty() ? original : ov.downloads;
        auto downloadsLabel = layer->getChildByID("downloads-label");
        if (downloadsLabel) {
            if (auto label = typeinfo_cast<CCLabelBMFont*>(downloadsLabel)) {
                float oldScale = label->getScale();
                label->setString(text.c_str());
                float newWidth = label->getScaledContentSize().width;
                if (newWidth > 60.0f && newWidth > 0) {
                    label->setScale(oldScale * 60.0f / newWidth);
                }
            } else if (!setLabelText(downloadsLabel, text)) {
                log::debug("Could not set downloads-label text");
            }
        } else {
            log::debug("Could not find downloads-label");
        }
    }

    // Likes
    if (force || !ov.likes.empty()) {
        std::string original = std::to_string(level->m_likes);
        std::string text = ov.likes.empty() ? original : ov.likes;
        auto likesLabel = layer->getChildByID("likes-label");
        if (likesLabel) {
            if (auto label = typeinfo_cast<CCLabelBMFont*>(likesLabel)) {
                float oldScale = label->getScale();
                label->setString(text.c_str());
                float newWidth = label->getScaledContentSize().width;
                if (newWidth > 60.0f && newWidth > 0) {
                    label->setScale(oldScale * 60.0f / newWidth);
                }
            } else if (!setLabelText(likesLabel, text)) {
                log::debug("Could not set likes-label text");
            }
        } else {
            log::debug("Could not find likes-label");
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

// After a name changed width, pins one edge (or the centre) of it back to
// where it was and moves nearby icons along with the name's edges.
static void keepNameAnchored(CCNode* nameNode, CCRect const& before, std::vector<Neighbor> const& neighbors) {
    auto after = worldBounds(nameNode);

    float dx;
    if (CELL_KEEP_LEFT_EDGE) {
        dx = before.origin.x - after.origin.x;
    } else {
        dx = (before.origin.x + before.size.width / 2) - (after.origin.x + after.size.width / 2);
    }

    // Shift the name so the chosen point stays where it was
    if (auto parent = nameNode->getParent()) {
        auto wp = parent->convertToWorldSpace(nameNode->getPosition());
        wp.x += dx;
        nameNode->setPosition(parent->convertToNodeSpace(wp));
    }

    // Then move the icons according to the name's corrected edges
    CCRect corrected(after.origin.x + dx, after.origin.y, after.size.width, after.size.height);
    shiftNeighbors(neighbors, before, corrected);
    log::debug("Re-aligned author name, moved {} icon(s)", neighbors.size());
}

// Finds the level's name and author labels anywhere under root by matching
// their text against the level's real name and creator, then swaps in the
// saved overrides. Used for list cells and the pause screen.
static void applyToLabels(CCNode* root, GJGameLevel* level) {
    if (!root || !level) return;

    int levelID = getLevelID(level);
    if (levelID <= 0) return; // local/unsaved levels are not supported

    auto ov = loadLevelOverride(levelID);
    if (ov.name.empty() && ov.author.empty() && ov.listDownloads.empty() && ov.listLikes.empty()) return;

    std::string realName = level->m_levelName;
    std::string realAuthor = level->m_creatorName;
    std::string realDownloads = std::to_string(level->m_downloads);
    std::string realLikes = std::to_string(level->m_likes);

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

            // The node that actually sits in the layout: the button around
            // the label if there is one, otherwise the label itself
            CCNode* nameNode = label;
            if (typeinfo_cast<CCMenuItemSpriteExtra*>(label->getParent())) {
                nameNode = label->getParent();
            }

            // 1. Remember where the name and nearby icons are now
            auto before = worldBounds(nameNode);
            std::vector<Neighbor> neighbors;
            collectNeighbors(root, nameNode, before, neighbors, true);

            // 2. Change the text
            replaceLabelText(label, hasPrefix ? "By " + ov.author : ov.author);

            // 3. Put the name back in place and move the icons with it
            keepNameAnchored(nameNode, before, neighbors);
        } else {
            log::debug("Could not find author label for level {}", levelID);
        }
    }

    if (!ov.listDownloads.empty() && !realDownloads.empty()) {
        if (auto label = findLabelWithText(root, { realDownloads })) {
            replaceLabelText(label, ov.listDownloads);
        } else {
            log::debug("Could not find downloads label for level {}", levelID);
        }
    }

    if (!ov.listLikes.empty() && !realLikes.empty()) {
        if (auto label = findLabelWithText(root, { realLikes })) {
            replaceLabelText(label, ov.listLikes);
        } else {
            log::debug("Could not find likes label for level {}", levelID);
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
    TextInput* m_downloadsInput = nullptr;
    TextInput* m_likesInput = nullptr;
    TextInput* m_listDownloadsInput = nullptr;
    TextInput* m_listLikesInput = nullptr;
    ScrollLayer* m_scrollLayer = nullptr;

    bool init(CCNode* layer, GJGameLevel* level) {
        if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

        m_layer = layer;
        m_level = level;
        m_levelID = getLevelID(level);

        this->setTitle("Edit Level Names");

        auto current = loadLevelOverride(m_levelID);

        // Create scroll layer
        m_scrollLayer = ScrollLayer::create({ 0, 0, POPUP_WIDTH, POPUP_HEIGHT - 60 });
        m_scrollLayer->m_contentLayer->setContentSize({ POPUP_WIDTH, 400.0f });
        m_mainLayer->addChildAtPosition(m_scrollLayer, Anchor::Center, ccp(0, -10));

        float yOffset = 360.0f;

        // Level name
        auto nameLabel = CCLabelBMFont::create("Level name", "goldFont.fnt");
        nameLabel->setScale(0.5f);
        nameLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(nameLabel);

        m_nameInput = TextInput::create(INPUT_WIDTH, std::string(level->m_levelName));
        m_nameInput->setMaxCharCount(MAX_NAME_LENGTH);
        m_nameInput->setString(current.name);
        m_nameInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_nameInput);
        yOffset -= 50;

        // Author name
        auto authorLabel = CCLabelBMFont::create("Author name", "goldFont.fnt");
        authorLabel->setScale(0.5f);
        authorLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(authorLabel);

        m_authorInput = TextInput::create(INPUT_WIDTH, std::string(level->m_creatorName));
        m_authorInput->setMaxCharCount(MAX_AUTHOR_LENGTH);
        m_authorInput->setString(current.author);
        m_authorInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_authorInput);
        yOffset -= 50;

        // Downloads (main page)
        auto downloadsLabel = CCLabelBMFont::create("Downloads (Main Page)", "goldFont.fnt");
        downloadsLabel->setScale(0.5f);
        downloadsLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(downloadsLabel);

        m_downloadsInput = TextInput::create(INPUT_WIDTH, "");
        m_downloadsInput->setMaxCharCount(MAX_DOWNLOADS_LENGTH);
        m_downloadsInput->setString(current.downloads);
        m_downloadsInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_downloadsInput);
        yOffset -= 50;

        // Likes (main page)
        auto likesLabel = CCLabelBMFont::create("Likes (Main Page)", "goldFont.fnt");
        likesLabel->setScale(0.5f);
        likesLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(likesLabel);

        m_likesInput = TextInput::create(INPUT_WIDTH, "");
        m_likesInput->setMaxCharCount(MAX_LIKES_LENGTH);
        m_likesInput->setString(current.likes);
        m_likesInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_likesInput);
        yOffset -= 50;

        // Downloads (list cells)
        auto listDownloadsLabel = CCLabelBMFont::create("Downloads (List Cells)", "goldFont.fnt");
        listDownloadsLabel->setScale(0.5f);
        listDownloadsLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(listDownloadsLabel);

        m_listDownloadsInput = TextInput::create(INPUT_WIDTH, "");
        m_listDownloadsInput->setMaxCharCount(MAX_DOWNLOADS_LENGTH);
        m_listDownloadsInput->setString(current.listDownloads);
        m_listDownloadsInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_listDownloadsInput);
        yOffset -= 50;

        // Likes (list cells)
        auto listLikesLabel = CCLabelBMFont::create("Likes (List Cells)", "goldFont.fnt");
        listLikesLabel->setScale(0.5f);
        listLikesLabel->setPosition(ccp(POPUP_WIDTH / 2, yOffset));
        m_scrollLayer->m_contentLayer->addChild(listLikesLabel);

        m_listLikesInput = TextInput::create(INPUT_WIDTH, "");
        m_listLikesInput->setMaxCharCount(MAX_LIKES_LENGTH);
        m_listLikesInput->setString(current.listLikes);
        m_listLikesInput->setPosition(ccp(POPUP_WIDTH / 2, yOffset - 23));
        m_scrollLayer->m_contentLayer->addChild(m_listLikesInput);

        // Buttons (outside scroll layer)
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
        ov.downloads = m_downloadsInput->getString();
        ov.likes = m_likesInput->getString();
        ov.listDownloads = m_listDownloadsInput->getString();
        ov.listLikes = m_listLikesInput->getString();
        saveLevelOverride(m_levelID, ov);
        applyOverrides(m_layer.data(), m_level.data(), true);
        this->keyBackClicked(); // closes the popup, same as pressing back
    }

    void onReset(CCObject*) {
        clearLevelOverride(m_levelID);
        m_nameInput->setString("");
        m_authorInput->setString("");
        m_downloadsInput->setString("");
        m_likesInput->setString("");
        m_listDownloadsInput->setString("");
        m_listLikesInput->setString("");
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
