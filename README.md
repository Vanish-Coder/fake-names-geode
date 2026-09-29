# Fake Names

A Geometry Dash mod that lets you override the displayed level name, author name, and level statistics of any level locally. Think "Fake Rate," but for names.

**Mod ID**: `vanishcoder.fakenames`

## Features

- **Level Name Override**: Change the displayed name of any level
- **Author Name Override**: Change the displayed creator name of any level
- **Persistence**: Your overrides are saved and remembered across game sessions
- **Keybind Access**: Press `O` (default) to open the edit popup for the current level
- **Client-Side Only**: Never modifies real save data or uploads anything to GD servers
- **Level Stats Override**: Changes the displayed likes and downloads of any level

## Requirements

- Geometry Dash on macOS (Steam) with Geode mod loader installed
- Xcode Command Line Tools
- CMake
- Geode CLI

## Building on macOS - FOR DEVS ONLY

### Prerequisites

1. **Install Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Install CMake**:
   ```bash
   brew install cmake
   ```

3. **Install Geode CLI**:
   Follow the instructions at [https://docs.geode-sdk.org/getting-started/geode-cli/](https://docs.geode-sdk.org/getting-started/geode-cli/)

### Build Steps

1. **Clone the repository**:
   ```bash
   git clone https://github.com/Vanish-Coder/fake-names-geode.git
   cd fake-names-geode
   ```

3. **Build the mod**:
   ```bash
   geode build
   ```

   If you have issues with the default build, try with Ninja:
   ```bash
   geode build --ninja
   ```

4. **Install the mod**:
   ```bash
   geode package install
   ```

   This will automatically install the built `.geode` file to your Geometry Dash mods folder.

### Manual Installation

If automatic installation doesn't work, you can manually install the mod:

1. Find the built `.geode` file in the `build` folder
2. Copy it to your Geometry Dash mods folder:
   - macOS: `~/Library/Application Support/Geometry Dash/geode/mods/`
3. Restart Geometry Dash

## Usage

1. Open any level in Geometry Dash
2. Press `O` to open the edit popup
3. Enter your custom level name and/or author name
4. Click "Save" to apply the override
5. Click "Reset" to clear the override for that level

The changes are applied immediately and will persist across game sessions.

## Configuration

The keybind can be changed in the mod settings:
1. Open Geode mod settings
2. Find "Fake Names" in the mod list
3. Change the "Open Edit Popup" keybind to your preferred key

## Unverified Items

The following items could not be verified during development and may need adjustment:

1. **Level Name Label Node Path**: The code uses a best-effort approach to find the level name label in LevelInfoLayer. The actual node structure may vary, and you may need to adjust the label detection logic based on the actual game structure.

2. **Author Label Node Path**: Similar to the level name label, the author label detection uses heuristics and may need adjustment.

3. **Icon Repositioning**: The code attempts to reposition icons next to the author label when the author name changes, but the exact node structure and positioning logic may need refinement.

4. **Text Input Focus Detection**: The current implementation checks for focused text inputs in the scene, but may not catch all cases. You may need to refine this based on actual testing.

5. **Level Cell Hooks**: The mod currently only hooks LevelInfoLayer. Hooking level cells in browse/search/saved lists was implemented, but could be slightly more unstable.

## Debug Logging

The mod includes debug logging at key points:
- When overrides are loaded/saved
- When hooks are triggered
- When the popup is opened/closed
- When labels are overridden

You can view these logs in the Geode log file to verify that the mod is working correctly.

## Layout Constants

Layout constants are defined at the top of `src/main.cpp` for easy tweaking:

```cpp
constexpr float POPUP_WIDTH = 280.0f;
constexpr float POPUP_HEIGHT = 180.0f;
constexpr float INPUT_WIDTH = 200.0f;
constexpr float INPUT_HEIGHT = 30.0f;
constexpr int MAX_NAME_LENGTH = 50;
constexpr int MAX_AUTHOR_LENGTH = 30;
```

## License

This mod is provided as-is under the MIT License.

## Credits

- Built with [Geode SDK](https://geode-sdk.org/)
- Inspired by similar mods like "Fake Rate"
