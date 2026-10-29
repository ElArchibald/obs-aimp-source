# OBS AIMP Source (`obs-aimp-source`)

[English](README.md) • [Русский](README.ru.md)

[![OBS Studio](https://img.shields.io/badge/OBS%20Studio-33.0%2B-black.svg?logo=obsstudio)](https://obsproject.com/)
[![AIMP](https://img.shields.io/badge/AIMP-v4%20%7C%20v5-orange.svg)](https://www.aimp.ru/)
[![License](https://img.shields.io/badge/License-GPL%20v2.0-green.svg)](https://www.gnu.org/licenses/gpl-2.0.html)

A high-performance native plugin for **OBS Studio 33.0+** that delivers deep, seamless integration with the **AIMP** audio player: direct process audio capture, isolated audio monitoring, customizable track overlay with an external JSON theme engine, and an interactive Qt6 control dock.

Built on the modern official **obs-plugintemplate** with CMake Presets support.

---

## 🚀 Key Features

### 1. Direct Audio Capture (WASAPI Process Loopback)
- **Direct capture from `AIMP.exe`**: Captures audio at the session level using Windows Audio Session API (WASAPI Process Loopback). Eliminates the need for virtual audio cables (VB-Cable, VoiceMeeter) or desktop audio capture.
- **Studio Audio Quality**: Pristine 32-bit floating-point audio (IEEE 32-bit Float) with automatic sample rate matching (44.1 / 48 kHz, stereo and multichannel surround configurations).
- **Ultra-Low Latency**: Hardware-backed ~50 ms buffer with zero jitter and minimal CPU overhead.
- **Seamless Reconnection**: Automatically detects AIMP startup, shutdown, and restart, reconnecting to the audio stream instantly.

### 2. Track Overlay (Direct GPU Rendering)
- **Hardware-Accelerated Rendering**: Renders the track card directly inside the OBS Graphics pipeline (`gs_texture`, Premultiplied Alpha, custom blending).
- **Album Artwork**: Instantly retrieves album covers via Win32 `WM_COPYDATA` with smooth rounded corners. Displays a themed vector note fallback if no embedded artwork is present.
- **Smooth Marquee Scrolling**: Automatically scrolls lengthy track titles and artists with customizable scroll speed (px/sec).
- **Progress Bar & Timestamps**: Rounded gradient progress bar with elapsed and total duration indicators (`01:23 / 03:45`).
- **Auto-Hide on Stop / Pause**: Smoothly fades out (~250 ms fade) when playback is stopped or paused.
- **External Themes Engine**: Themes are loaded dynamically from `data/themes/` using standard `manifest.json` files. Includes 4 built-in themes:
  - 🌟 `default` (*Default Dark Glass*) — modern dark glassmorphism with AIMP signature orange accent (520×130).
  - ⚡ `neon` (*Neon Cyberpunk*) — vibrant cyberpunk / synthwave with dual neon glow (Cyan & Magenta, 520×130).
  - 💊 `compact` (*Streamer Compact Pill*) — ultra-compact horizontal pill widget (440×72) with smart curved-edge padding geometry and artist-time collision avoidance.
  - 💎 `card` (*Emerald Luxury Card*) — luxury card styling with deep emerald accents (520×130).
- **Customizable Source Properties**:
  - Full color overrides (background, border, title, artist, time, progress bar).
  - Font family and size overrides for every text element.

### 3. Interactive Control Dock (Qt6)
- **Status Monitoring**: Connection state indicator (*Connected*, *AIMP is not running*, *Playing*, *Paused*, *Stopped*).
- **Quick Launch**: "Launch AIMP" button with monochrome AIMP badge appears automatically when AIMP is closed (detects player path via Windows Registry).
- **Themed Media Icons**: Control buttons (`Previous`, `Play / Pause`, `Stop`, `Next`, `Shuffle`, `Repeat`, `Volume`, `Mute`, `Headphones`) dynamically adapt their tint to the active OBS Studio color scheme.
- **Interactive Seek Bar**: Click anywhere on the timeline or drag the slider to seek to any position in the track.
- **Playback Controls**: `Previous`, `Play / Pause`, `Stop`, `Next`, `Shuffle`, and `Repeat track`.
- **Player Volume**: Controls AIMP internal volume (0–100%) with a mute toggle.
- **OBS Source Volume**: Logarithmic volume fader in decibels (**dB**, -inf dB to 0.0 dB) controlling the OBS audio source with mute toggle.
- **Monitoring Controls**: Quick toggle for headphone monitoring, mixer integration, and dedicated monitoring volume slider in dB.
- **Native OBS Integration**: Dock automatically registers under OBS main menu: *Docks* → *AIMP Player Control*.

### 4. Isolated Audio Monitoring & Mixer Integration
- **Independent Monitoring**: Streamers can listen to music through headphones without broadcasting it to stream/recording, or balance personal volume independently from the stream mix.
- **Dedicated Mixer Companion Source**: Enabling *"Add monitoring to mixer"* registers a separate source in the OBS Audio Mixer with its own volume fader and volume meter.
- **Smart Context Menu**: Right-clicking "Properties" or "Filters" on the monitoring companion source in the mixer automatically opens the properties or filters of the main AIMP source.
- **Scene Collection Resilience**: Cleanly synchronizes state during scene collection switching, profile loading, and source renaming.
- **UIPI Bypass**: Protects Win32 message windows against User Interface Privilege Isolation (`ChangeWindowMessageFilterEx`) to ensure communication works even when OBS runs with elevated administrator privileges.

### 5. Full Internationalization (i18n)
- 100% of user-facing strings are localized:
  - `ru-RU.ini`: Russian localization
  - `en-US.ini`: English localization
- Fully synchronized keys and support for localized theme names via `locale.json` or `Theme.<id>`.

---

## 📖 User Guide (OBS Studio Workflow)

### 1. Adding the Source to a Scene:
1. In OBS Studio, locate the **Sources** dock and click **+**.
2. Select **AIMP Source** (*Источник AIMP*).
3. In the Properties window:
   - **Display**: Select an overlay theme, toggle auto-hide on stop/pause, and adjust marquee scroll speed.
   - **Audio**: Enable **Audio Capture** for stream/recording output. Enable **Monitoring** to listen through headphones and set your preferred monitoring volume.
   - **Colors & Fonts**: Optionally override colors and fonts on top of the selected theme.

### 2. Using the Control Dock:
1. In the OBS Studio menu bar, click **Docks** → select **AIMP Player Control**.
2. Dock the panel anywhere in your OBS workspace (e.g., next to the Audio Mixer, Scenes, or Preview).
3. The dock provides:
   - Current album cover, track title, and artist.
   - Interactive seek bar (click or drag to seek).
   - Track switching, playback controls, shuffle, and repeat toggles.
   - Player volume slider.
   - OBS source fader and monitoring volume fader in dB.

---

## 📂 Repository Structure

```
obs-aimp-source/
├── cmake/
│   ├── common/                   # Shared CMake modules (Findlibobs, Findobs-frontend-api, etc.)
│   └── windows/                  # Windows compiler and packaging configuration
├── data/
│   ├── icons/                    # Dock media player and audio control icons (11 icons)
│   ├── locale/                   # Localization files (en-US.ini, ru-RU.ini)
│   ├── themes/                   # Overlay themes
│   │   ├── default/              # Default Dark Glass
│   │   ├── neon/                 # Neon Cyberpunk
│   │   ├── compact/              # Streamer Compact Pill
│   │   └── card/                 # Emerald Luxury Card
│   ├── aimp_icon.png             # Full-color player icon (256×256)
│   ├── aimp_icon_mono.png        # Monochrome icon for UI buttons and Launch AIMP button (128×128)
│   ├── source_dark.svg/.png      # Source icon for OBS Dark theme
│   └── source_light.svg/.png     # Source icon for OBS Light theme
├── include/
│   ├── apiRemote.h               # AIMP Remote Control API header
│   └── obsconfig.h               # OBS configuration header for out-of-tree builds
├── obs-libs/                     # OBS import libraries (libobs, obs-frontend-api)
├── src/
│   ├── plugin-main.cpp           # OBS module entry point, mixer event filters
│   ├── plugin-support.h/.c.in    # Module metadata and logging helpers
│   ├── aimp-controller.hpp/.cpp  # Win32 IPC bridge to AIMP (Window messages, Shared Memory)
│   ├── aimp-audio.hpp/.cpp       # WASAPI Loopback process audio capture driver
│   ├── aimp-dock.hpp/.cpp        # Qt6 Control Dock and fader synchronization
│   ├── aimp-overlay.hpp/.cpp     # Overlay rendering, animation, and marquee engine
│   ├── aimp-overlay-theme.hpp/.cpp # Theme manager and JSON manifest parser
│   └── aimp-source.hpp/.cpp      # OBS source registration and property callbacks
├── .clang-format                 # OBS Project C++ code formatting standard
├── .gitignore                    # Git ignore rules
├── buildspec.json                # OBS Studio plugin metadata specification
├── CMakeLists.txt                # Root CMake build script
├── CMakePresets.json             # CMake build presets (MSVC 2026 / Ninja)
├── build.ps1                     # One-click Windows PowerShell build script
├── install.ps1                   # One-click plugin installer for OBS Studio
├── package.ps1                   # One-click ZIP packaging script with GitHub Releases upload support
├── README.md                     # English documentation
└── README.ru.md                  # Russian documentation
```

---

## 🛠️ Build and Installation

### Prerequisites:
- **OS**: Windows 10 / 11 (64-bit)
- **Compiler**: Visual Studio 2022 or 2026 (MSVC v143+) with C++17 support
- **Build System**: CMake 3.28+ and Ninja
- **Framework**: Qt 6.5+ (`Core`, `Gui`, `Widgets` components)
- **OBS Studio**: Version 33.0 or newer

### Quick Build (Local):
Run the build script from the project root:
```powershell
# Build with Ninja (default, output to build/):
.\build.ps1

# Or build with Visual Studio MSBuild (output to build_vs/):
.\build.ps1 -Generator VS
```
The script will:
1. Automatically detect and initialize the Visual Studio x64 developer environment (`vcvars64.bat`).
2. Configure the project with CMake (Ninja -> `build/` or Visual Studio -> `build_vs/`).
3. Compile the plugin using Ninja or MSBuild.

Alternatively, build directly using CMake Presets:
```powershell
# Ninja build:
cmake --preset windows-x64
cmake --build --preset windows-x64

# Visual Studio 2026 solution / MSBuild:
cmake --preset windows-vs
cmake --build --preset windows-vs
```

### Installation to OBS Studio:
Run:
```powershell
.\install.ps1
```
The compiled plugin `obs-aimp-source.dll`, debug symbols `.pdb`, locale files, and themes will be copied into:
`%OBS_DIR%\plugins\obs-aimp-source\`

*(If OBS Studio is running, the script safely hot-replaces the DLL without file locking errors).*

---

## 🎨 Creating a Custom Theme

Themes do not require recompilation. Simply create a new folder under `data/themes/<theme_id>/` and add a `manifest.json` file:

```json
{
  "id": "my_custom_theme",
  "name": "My Custom Theme",
  "author": "Streamer",
  "version": "1.0.0",
  "description": "My custom streamer overlay theme",
  "layout": {
    "width": 520,
    "height": 130,
    "cornerRadius": 16,
    "padding": 14,
    "paddingRight": 14,
    "coverSize": 102,
    "coverRadius": 10,
    "coverSpacing": 16,
    "progressBarHeight": 6,
    "progressBarRadius": 3,
    "progressBarBottomOffset": 14
  },
  "fonts": {
    "titleFont": "Segoe UI",
    "titleSize": 15,
    "titleWeight": 700,
    "artistFont": "Segoe UI",
    "artistSize": 12,
    "artistWeight": 500,
    "timeFont": "Consolas",
    "timeSize": 11,
    "timeWeight": 400
  },
  "colors": {
    "background": "#1A1B24E6",
    "border": "#3A3D5480",
    "borderWidth": 1.5,
    "titleText": "#FFFFFF",
    "artistText": "#A2A7BD",
    "timeText": "#8E93AB",
    "progressBackground": "#282B3C80",
    "progressFill": "#FF7A00",
    "progressFillEnd": "#FFA44D",
    "glowColor": "#FF7A0040"
  },
  "assets": {
    "placeholderCover": "placeholder.png"
  }
}
```

> [!NOTE]
> All dimensions are in pixels. The `paddingRight` layout field is optional: if omitted, it defaults to `padding`, and for pill-shaped overlays with large corner radiuses, the rendering engine automatically calculates safe right clearance to prevent text and progress bar clipping.

### Theme Localization (`locale.json`)

To localize the theme name and description in the OBS Studio interface, create a dedicated `locale.json` file inside your theme's folder:

```json
{
  "ru-RU": {
    "name": "Моя пользовательская тема",
    "description": "Описание темы на русском языке"
  },
  "en-US": {
    "name": "My Custom Theme",
    "description": "Custom theme description in English"
  }
}
```

The plugin automatically detects the user's active OBS Studio language and displays the localized theme name in the dropdown! (Dedicated `locale.ini` or a `locale/` subfolder are also supported).

---

## 📄 License

This project is licensed under the **GNU General Public License v2.0 (GPLv2)**. See [GNU GPL v2.0](https://www.gnu.org/licenses/gpl-2.0.html) for details.
