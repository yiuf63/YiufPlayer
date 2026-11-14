# Muvik 🎬🎮

**A 10-foot, controller-first media player crafted for Steam Deck, handhelds, and large screens.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: Linux | macOS | Android](https://img.shields.io/badge/Platform-SteamOS%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-green.svg)](RELEASE-BLOCKERS.md)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-orange.svg)](CMakeLists.txt)
[![Binary Size](https://img.shields.io/badge/Binary-6.3MB-brightgreen.svg)](build-linux/muvik)

[English](README.md) | [简体中文](README_zh.md)

---

## 🌟 Why Muvik?

On gaming handhelds like **Steam Deck**, **ROG Ally**, **Legion Go**, or on living-room PCs and Android TV, enjoying local movies, anime, or background music with a gamepad has long been an awkward experience:

* **Traditional Desktop Players (VLC / MPC / IINA)**: Designed strictly for mouse and keyboard. Navigating tiny seekbars or dropdown menus with a controller joystick or trackpad is clunky and frustrating.
* **Heavy HTPC Stacks (Kodi)**: Originating as an all-in-one home theater operating system, Kodi carries hundreds of megabytes of legacy code, boots slowly (several seconds), and feels heavy when all you want is to play an offline episode in handheld mode. Its touch controls and virtual keyboard also feel disconnected from modern handheld OSs.
* **Cloud Streaming (Plex / Emby client)**: In offline environments like airplanes or bullet trains, playing media directly from an SD card requires cumbersome setups and often enforces account login.

**Muvik** bridges this gap by marrying the console-grade 60FPS UI of [Borealis](https://github.com/dragonflylee/borealis) with the industry-leading decoding powerhouse of [libmpv](https://mpv.io):

* **Instant Startup**: Native C++17 binary (< 7 MB), boots in **< 100 ms**, uses ~40 MB RAM.
* **Handheld & Controller First**: Built entirely for D-Pad, thumbsticks, triggers, and full touch gestures.
* **SteamOS Native**: Integrates seamlessly with Steam Deck — auto-detects MicroSD cards, invokes the native Steam virtual keyboard, and provides Steam grid art assets.
* **Pure & Free**: 100% open source under GNU GPL-3.0, zero tracking, zero mandatory cloud accounts.
* **QuickJS Sandboxed Extensibility**: Write lightweight JavaScript plugins for custom online video sources and metadata scrapers without compiling C++.

---

## ✨ Features

### 🎮 Gamepad & Handheld First
* **Full Controller Navigation**: Automatic focus traversal for D-Pad, dual analog sticks, ABXY buttons, shoulder bumpers, and analog triggers (LT / RT).
* **Adaptive Button Glyphs**: Automatically switches on-screen button prompts between **Xbox / Steam Deck**, **PlayStation**, and **Nintendo Switch** icon styles.
* **Native SteamOS Virtual Keyboard**: Pressing A on input fields invokes Steam Deck's official on-screen keyboard (`steam://open/keyboard?Mode=0`).

### 👆 Touch & Gesture Controls
* **Double Tap Screen**: Play / Pause toggle.
* **Single Tap Screen**: Show / Hide OSD control bar and status badges.
* **Vertical Swipe (Left Half)**: Smooth hardware backlight / brightness adjustment.
* **Vertical Swipe (Right Half)**: Smooth audio volume adjustment (0% – 100%).
* **Horizontal Swipe**: Millisecond-accurate timeline scrubbing with real-time time preview bubble.
* **Long Press**: Temporary 2.0x speed boost (ideal for skipping opening themes), returns to normal upon release.
* **Touch Lock**: Double-press A or toggle touch lock to prevent palm accidental touches while lying down.

### 📦 Universal Codec Support (Powered by libmpv)
* **Video Formats**: MP4, MKV, WebM, MOV, M4V, TS, M2TS, AVI, FLV, WMV, VOB, RMVB, 3GP, OGV, ISO, and more with full hardware decoding (VAAPI / VideoToolbox / MediaCodec).
* **Audio Formats**: FLAC, WAV, APE, ALAC, AIFF, DSD (DSF/DFF), MP3, M4A, AAC, OGG, OPUS, WMA, etc.
* **Subtitles**: SRT, ASS, SSA, WebVTT, PGS graphics subtitles, SUB/IDX, SMI, LRC with pixel-perfect libass styling and real-time subtitle sync adjustment.

### 🎧 Decoupled Background Audio Engine
* Music and podcasts run through a dedicated global `AudioPlayer` engine that stays alive even when leaving the player screen.
* Uses `vid=no` with 0% GPU / GL overhead, automatically suppresses macOS App Nap and supports standby playback.
* Persistent "Now Playing" mini-bar on the main interface.

### 🌐 Native NAS & WebDAV Streaming (Zero Setup Required)
* **Out-of-the-box Network Storage**: Connect directly to Synology DSM, QNAP QTS, Nextcloud, or AList right from the UI without compiling external plugins.
* **AList Cloud Aggregator**: Stream movies directly from 115, Quark, Baidu Netdisk, Aliyun Drive, Google Drive, or OneDrive via AList's WebDAV endpoint (`:5244/dav`).
* **Hardware-Accelerated Streaming**: Streams directly via `libmpv` with instant seeking, subtitle loading, and resume history.

### 🧩 QuickJS Sandboxed Plugins (Scrapers & Video Sources)
* Supports custom plugins written in plain JavaScript running inside an isolated 16MB QuickJS sandbox.
* **Scraper Plugins**: Search and fetch metadata, plot summaries, ratings, and posters (e.g., TMDB, Bangumi, Douban).
* **Video Source Plugins**: Connect to remote WebDAV, NAS, or streaming endpoints.
* Built-in host APIs: `host.http` (HTTP requests with headers/timeout), `host.getConfig`, `host.storage` (isolated persistence), `host.crypto` (MD5/SHA256/Base64). See [`docs/PLUGIN-DEV.md`](docs/PLUGIN-DEV.md).

---

## 🕹️ Controller Keymap (Default Xbox / Steam Deck)

| Button | In Player View | In Library / File View |
| :--- | :--- | :--- |
| **A** | Play / Pause (Confirm OSD control) | Enter directory / Play item |
| **B** | Exit playback (Instant return to library) | Back to upper directory / Cancel |
| **D-Pad Left / Right** | Quick seek (-10s / +10s) | Navigate grid |
| **LT / RT (Triggers)** | Volume down / up (-5% / +5%) | Page down / up |
| **LB / RB (Bumpers)** | Seek (-15s / +15s) | Previous / Next Tab |
| **D-Pad Up** | Open episode playlist dialog | Jump to search button |
| **D-Pad Down** | Focus OSD controls bar | Scroll down |
| **X** | Open Audio Track menu | Scrape movie metadata |
| **Y** | Open Subtitle menu | Rescan media library |
| **R3 (Right Stick Press)** | Cycle playback speed (`1.0x → 1.25x → 1.5x → 2.0x → 0.75x`) | - |
| **L3 (Left Stick Press)** | Cycle aspect ratio (Original → 16:10 Fill → 16:9 → 4:3 → Crop) | - |
| **START** | Toggle touch lock | - |

---

## 📥 Installation

Pre-compiled release packages are available on [GitHub Releases](https://github.com/yiuf63/muvik/releases):

### Steam Deck (SteamOS)
1. Switch Steam Deck to **Desktop Mode**.
2. Download `muvik-linux-x86_64.tar.gz` and extract it to `~/muvik`.
3. Run `python3 add_to_steam.py` to automatically register Muvik into your Steam Library with official boxart and banner grids.
4. Return to **Gaming Mode** — launch Muvik directly from your Non-Steam Games library!

### Linux Desktop / Handhelds (Arch / Ubuntu / Fedora)
Extract `muvik-linux-x86_64.tar.gz` and run `./muvik`. Ensure `libmpv`, `ffmpeg`, and `libcurl` are installed on your distribution:
```bash
# Ubuntu / Debian
sudo apt install libmpv2 libavcodec-dev libcurl4

# Arch / Manjaro
sudo pacman -S mpv ffmpeg curl
```

### macOS (Apple Silicon arm64)
Download `muvik-macos-arm64.zip`, extract, and launch `./muvik`. Alternatively build via Homebrew dependencies:
```bash
brew install mpv ffmpeg fmt tinyxml2
```

### Android (arm64-v8a)
Download and install `muvik-android-arm64.apk` directly on Android tablets, handhelds (Odin, Retroid), or Android TV.

---

## 🛠️ Building from Source

### Prerequisites
* C++17 compatible compiler (GCC 9+, Clang 10+, or AppleClang)
* CMake 3.18+
* `libmpv` development headers & library
* OpenGL or OpenGL ES 3.0+
* `libcurl`

### Build Steps

```bash
# Clone repository
git clone https://github.com/yiuf63/muvik.git
cd muvik

# Configure & build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Run
./build/muvik
```

### Linux Container Build (for Steam Deck target)
```bash
docker build --platform linux/amd64 -f Dockerfile.linux -t muvik-linux .
docker run --rm -v "$PWD:/src" -w /src muvik-linux bash scripts/build-linux.sh
```

---

## 🧪 Testing & Verification

Muvik features an extensive test suite verifying logic, memory safety, and visual rendering:

```bash
# Run all 15 unit test suites
./tests/run_tests.sh

# Run end-to-end UI and framebuffer smoke test
python3 tests/tools/verify_ui.py --only functional

# Check documentation and code consistency
python3 tests/tools/check_docs.py
```

---

## 📄 License & Acknowledgments

Muvik is released under the **[GNU General Public License v3.0](LICENSE)**.

### Third-Party Components & Notices
* **[Borealis](https://github.com/dragonflylee/borealis)**: Apache-2.0 UI engine for controller navigation.
* **[libmpv](https://mpv.io)**: LGPL-2.1+ / GPL-3.0 media playback core.
* **[QuickJS](https://bellard.org/quickjs/)**: MIT-licensed ultra-compact JavaScript engine by Fabrice Bellard.
* **[FFmpeg](https://ffmpeg.org)**: LGPL-2.1+ / GPL-3.0 multimedia framework.
* Complete notices: see [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

### Special Acknowledgments
Special thanks to **[wiliwili](https://github.com/xfangfang/wiliwili)**, **[Kodi](https://kodi.tv)**, and **[mpv](https://mpv.io)** for inspiring gamepad interaction paradigms and media playback architectures.
