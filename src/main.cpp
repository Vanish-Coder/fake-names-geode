#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/CCMenuItemLabel.hpp>
#include <Geode/utils/NodeIDs.hpp>

using namespace geode::prelude;

// Constants for layout (easy to tweak)
constexpr float POPUP_WIDTH = 280.0f;
constexpr float POPUP_HEIGHT = 180.0f;
constexpr float INPUT_WIDTH = 200.0f;
constexpr float INPUT_HEIGHT = 30.0f;
constexpr int MAX_NAME_LENGTH = 50;
constexpr int MAX_AUTHOR_LENGTH = 30;

// Structure to store override data for a level
struct LevelOverride {
    std::string name;
    std::string author;
};

// Global variable to track if our popup is currently open
static bool s_popupOpen = false;

// Persistence functions
std::string getLevelKey(int levelID) {
    if (levelID == 0) {
        return "local_level";
    }
    return "level_" + std::to_string(levelID);
}

LevelOverride loadLevelOverride(int levelID) {
    std::string key = getLevelKey(levelID);
    LevelOverride override;
    
    // Load name override
    auto nameValue = Mod::get()->getSavedValue<std::string>(key + "_name", "");
    override.name = nameValue;
    
    // Load author override
    auto authorValue = Mod::get()->getSavedValue<std::string>(key + "_author", "");
    override.author = authorValue;
    
    log::debug("Loaded override for level {}: name='{}', author='{}'", 
               levelID, override.name, override.author);
    
    return override;
}

void saveLevelOverride(int levelID, const LevelOverride& override) {
    std::string key = getLevelKey(levelID);
    
    // Save name override (empty string means no override)
    Mod::get()->setSavedValue(key + "_name", override.name);
    
    // Save author override (empty string means no override)
    Mod::get()->setSavedValue(key + "_author", override.author);
    
    log::debug("Saved override for level {}: name='{}', author='{}'", 
               levelID, override.name, override.author);
}

void clearLevelOverride(int levelID) {
    std::string key = getLevelKey(levelID);
    
    // Clear overrides by setting to empty strings
    Mod::get()->setSavedValue(key + "_name", "");
    Mod::get()->setSavedValue(key + "_author", "");
    
    log::debug("Cleared override for level {}", levelID);
}

// Custom popup for editing level names
class EditNamePopup : public geode::Popup {
protected:
    int m_levelID;
    CCTextInputNode* m_nameInput;
    CCTextInputNode* m_authorInput;
    
    bool init(int levelID, const std::string& currentName, const std::string& currentAuthor) {
        if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT))
            return false;
        
        m_levelID = levelID;
        
        // Set title
        this->setTitle("Edit Level Names");
        
        // Add instruction text
        auto instruction = CCLabelBMFont::create(
            "Press 'O' to open this popup", 
            "goldFont.fnt"
        );
        instruction->setScale(0.5f);
        m_mainLayer->addChildAtPosition(instruction, Anchor::Top, ccp(0, -25));
        
        // Create name input
        auto nameLabel = CCLabelBMFont::create("Level Name:", "bigFont.fnt");
        nameLabel->setScale(0.5f);
        m_mainLayer->addChildAtPosition(nameLabel, Anchor::Center, ccp(-INPUT_WIDTH/2 - 20, 20));
        
        m_nameInput = CCTextInputNode::create(INPUT_WIDTH, INPUT_HEIGHT, "", "bigFont.fnt");
        m_nameInput->setString(currentName);
        m_nameInput->setMaxLabelLength(MAX_NAME_LENGTH);
        m_nameInput->setPlaceholderEnabled(true);
        m_nameInput->setPlaceholderLabelColor({150, 150, 150});
        m_mainLayer->addChildAtPosition(m_nameInput, Anchor::Center, ccp(20, 20));
        
        // Create author input
        auto authorLabel = CCLabelBMFont::create("Author Name:", "bigFont.fnt");
        authorLabel->setScale(0.5f);
        m_mainLayer->addChildAtPosition(authorLabel, Anchor::Center, ccp(-INPUT_WIDTH/2 - 20, -20));
        
        m_authorInput = CCTextInputNode::create(INPUT_WIDTH, INPUT_HEIGHT, "", "bigFont.fnt");
        m_authorInput->setString(currentAuthor);
        m_authorInput->setMaxLabelLength(MAX_AUTHOR_LENGTH);
        m_authorInput->setPlaceholderEnabled(true);
        m_authorInput->setPlaceholderLabelColor({150, 150, 150});
        m_mainLayer->addChildAtPosition(m_authorInput, Anchor::Center, ccp(20, -20));
        
        // Create simple menu items for buttons
        auto saveBtn = CCMenuItemLabel::create(
            CCLabelBMFont::create("Save", "bigFont.fnt"),
            this,
            menu_selector(EditNamePopup::onSave)
        );
        saveBtn->setScale(0.8f);
        m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Center, ccp(-40, -POPUP_HEIGHT/2 + 25));
        
        auto resetBtn = CCMenuItemLabel::create(
            CCLabelBMFont::create("Reset", "bigFont.fnt"),
            this,
            menu_selector(EditNamePopup::onReset)
        );
        resetBtn->setScale(0.8f);
        m_buttonMenu->addChildAtPosition(resetBtn, Anchor::Center, ccp(40, -POPUP_HEIGHT/2 + 25));
        
        return true;
    }
    
    void onSave(cocos2d::CCObject* sender) {
        LevelOverride override;
        override.name = m_nameInput->getString();
        override.author = m_authorInput->getString();
        
        saveLevelOverride(m_levelID, override);
        
        // Close popup
        this->close();
        s_popupOpen = false;
        
        log::debug("Saved name override for level {}", m_levelID);
    }
    
    void onReset(cocos2d::CCObject* sender) {
        clearLevelOverride(m_levelID);
        
        // Reset input fields
        m_nameInput->setString("");
        m_authorInput->setString("");
        
        log::debug("Reset name override for level {}", m_levelID);
    }
    
    void onClose(cocos2d::CCObject* sender) override {
        Popup::onClose(sender);
        s_popupOpen = false;
    }
    
public:
    static EditNamePopup* create(int levelID, const std::string& currentName, const std::string& currentAuthor) {
        auto ret = new EditNamePopup();
        if (ret->init(levelID, currentName, currentAuthor)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// Helper function to check if text input is focused
bool isTextInputFocused() {
    auto director = CCDirector::sharedDirector();
    if (!director) return false;
    
    auto runningScene = director->getRunningScene();
    if (!runningScene) return false;
    
    // Check if any CCTextInputNode is currently focused
    // This is a simple check - in practice, we might need to check more specifically
    auto inputNodes = runningScene->getChildren();
    for (unsigned int i = 0; i < inputNodes->count(); i++) {
        auto node = static_cast<CCNode*>(inputNodes->objectAtIndex(i));
        if (auto input = typeinfo_cast<CCTextInputNode*>(node)) {
            if (input->isFocusedOnTarget()) {
                return true;
            }
        }
    }
    
    return false;
}

// Hook LevelInfoLayer to override displayed names
class $modify(FakeNamesLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;
        
        log::debug("LevelInfoLayer::init called for level ID: {}", 
                   level ? level->m_levelID : 0);
        
        // Ensure node IDs are provided
        NodeIDs::get()->provide(this);
        
        // Apply name override if it exists
        if (level) {
            auto override = loadLevelOverride(level->m_levelID);
            
            if (!override.name.empty()) {
                // Use the proper node ID from geode.node-ids
                auto levelNameLabel = this->getChildByID("title-label");
                
                if (auto label = typeinfo_cast<CCLabelBMFont*>(levelNameLabel)) {
                    label->setString(override.name.c_str());
                    log::debug("Overridden level name to: {}", override.name);
                } else {
                    log::debug("Could not find title-label to override");
                }
            }
            
            if (!override.author.empty()) {
                // Use the proper node ID from geode.node-ids
                auto creatorInfoMenu = this->getChildByID("creator-info-menu");
                CCNode* creatorNameNode = nullptr;
                
                if (creatorInfoMenu) {
                    creatorNameNode = creatorInfoMenu->getChildByID("creator-name");
                }
                
                if (auto label = typeinfo_cast<CCLabelBMFont*>(creatorNameNode)) {
                    label->setString(override.author.c_str());
                    log::debug("Overridden author name to: {}", override.author);
                    
                    // The creator info menu uses ColumnLayout, so it should handle
                    // repositioning automatically when the label content changes
                    if (creatorInfoMenu) {
                        creatorInfoMenu->updateLayout();
                    }
                } else {
                    log::debug("Could not find creator-name to override");
                }
            }
        }
        
        return true;
    }
};

// Keybind handler
$on_game(Loaded) {
    listenForKeybindSettingPresses("open-popup-keybind", [](Keybind const& keybind, bool down, bool repeat, double timestamp) {
        // Only fire on key down, not repeat
        if (!down || repeat) return;
        
        // Don't fire if popup is already open
        if (s_popupOpen) return;
        
        // Don't fire if text input is focused
        if (isTextInputFocused()) return;
        
        // Check if we're on a LevelInfoLayer
        auto director = CCDirector::sharedDirector();
        if (!director) return;
        
        auto runningScene = director->getRunningScene();
        if (!runningScene) return;
        
        // Try to find LevelInfoLayer in the current scene
        auto children = runningScene->getChildren();
        for (unsigned int i = 0; i < children->count(); i++) {
            auto node = static_cast<CCNode*>(children->objectAtIndex(i));
            if (auto levelInfoLayer = typeinfo_cast<LevelInfoLayer*>(node)) {
                // Found LevelInfoLayer, open popup
                if (levelInfoLayer->m_level) {
                    s_popupOpen = true;
                    
                    auto override = loadLevelOverride(levelInfoLayer->m_level->m_levelID);
                    
                    auto popup = EditNamePopup::create(
                        levelInfoLayer->m_level->m_levelID,
                        override.name,
                        override.author
                    );
                    
                    if (popup) {
                        popup->show();
                        log::debug("Opened edit popup for level {}", 
                                  levelInfoLayer->m_level->m_levelID);
                    } else {
                        s_popupOpen = false;
                    }
                }
                return;
            }
        }
        
        log::debug("Keybind pressed but no LevelInfoLayer found");
    });
}
