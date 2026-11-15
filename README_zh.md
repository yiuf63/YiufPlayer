# Yiuf Player 🎬🎮

**专为 Steam Deck、掌机与大屏设计的沉浸式手柄优先媒体播放器。**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: Linux | macOS | Android](https://img.shields.io/badge/Platform-SteamOS%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-green.svg)](RELEASE-BLOCKERS.md)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-orange.svg)](CMakeLists.txt)
[![Binary Size](https://img.shields.io/badge/Binary-6.3MB-brightgreen.svg)](build-linux/muvik)

[English](README.md) | [简体中文](README_zh.md)

---

## 🌟 为什么选择 Yiuf Player？

在 **Steam Deck**、**ROG Ally**、**Legion Go** 等掌上游戏机，或是客厅 HTPC 与 Android 平板/TV 上，使用手柄看电影、追番或听歌一直缺乏顺手优雅的体验：

* **传统桌面播放器（VLC / MPC / IINA）**：完全为键鼠操作设计。用手柄摇杆或触控板去对准几像素宽的进度条与下拉菜单，体验极为别扭繁琐。
* **重量级家庭影院套件（Kodi）**：最初作为完整操作系统设计，包含数百兆历史包袱，冷启动耗时数秒，在掌机只想随手看一集离线番剧时显得沉重缓慢。其触控与软键盘体验也与现代掌机系统严重割裂。
* **流媒体客户端（Plex / Emby）**：在飞机、高铁等无网络离线环境下，直接播放 SD 卡中的本地资源门槛高，且常绑定账号登录。

**Yiuf Player** 将主机级 60FPS 流畅渲染引擎 [Borealis](https://github.com/dragonflylee/borealis) 与业界顶尖的解码内核 [libmpv](https://mpv.io) 深度融合，专为掌机场景量身定做：

* **秒级瞬启**：纯原生 C++17 二进制文件（< 7 MB），冷启动用时 **< 100 ms**，常驻内存仅约 40 MB。
* **原生手柄与掌机优先**：为十字键、双摇杆、肩键、扳机键与完整触控手势全面优化，告别模拟鼠标。
* **深度契合 SteamOS**：自动识别 MicroSD 存储卡、呼出 SteamDeck 官方原生屏幕键盘，附带无缝整合的 Steam 封面网格图。
* **开源纯净**：基于 GNU GPL-3.0 协议完全开源，无任何遥测或强制登录。
* **QuickJS 沙箱扩展**：支持用普通 JavaScript 编写网络视频源与刮削器插件，无需编译 C++ 即可扩展功能。

---

## ✨ 核心特性

### 🎮 手柄与掌机操控系统
* **全手柄按键导航**：十字键、双摇杆、ABXY 动作键、肩键及线性扳机键（LT / RT）全链路自动焦点寻路。
* **自适应按键图元**：根据连接的手柄类型自动切换 **Xbox / Steam Deck**、**PlayStation** 与 **Nintendo Switch** 提示图标。
* **SteamOS 原生软键盘**：在搜索与输入框按 A 键直接呼出 SteamOS 系统原生软键盘（`steam://open/keyboard?Mode=0`）。

### 👆 触控与手势交互
* **双击屏幕**：播放 / 暂停切换。
* **单击屏幕**：显示 / 隐藏 OSD 控制栏与状态指示。
* **左半屏垂直滑动**：无级平滑调节屏幕硬件背光 / 亮度。
* **右半屏垂直滑动**：无级平滑调节音频音量（0% – 100%）。
* **水平横向滑动**：毫秒级精准时间轴搜寻，带实时预览气泡。
* **长按屏幕**：临时 2.0x 倍速快进（跳过片头利器），松手立即恢复原速。
* **触控锁定**：双击 A 键或快捷切换防误触锁，防止躺姿手掌压屏误触。

### 📦 全能全格式解码（基于 libmpv）
* **视频格式**：MP4, MKV, WebM, MOV, M4V, TS, M2TS, AVI, FLV, WMV, VOB, RMVB, 3GP, OGV, ISO 等，支持全平台硬件解码加速（VAAPI / VideoToolbox / MediaCodec）。
* **音频格式**：FLAC, WAV, APE, ALAC, AIFF, DSD (DSF/DFF), MP3, M4A, AAC, OGG, OPUS, WMA 等无损与高解析度音频。
* **外挂与内嵌字幕**：SRT, ASS, SSA, WebVTT, PGS 图形字幕, SUB/IDX, SMI, LRC 等，支持像素级 libass 样式特效与毫秒级字幕时间轴平移校准。

### 🎧 独立后台音频播放引擎
* 音乐与播客由全局独立的 `AudioPlayer` 引擎调度，离开播放页面或在媒体库浏览时音乐持续播放不中断。
* 纯音频播放模式采用 `vid=no`，0% GPU / GL 额外功耗，自动抑制 macOS App Nap 并支持设备熄屏待机播放。
* 主界面常驻全局「正在播放」轻量迷你控制条。

### 🌐 原生内置 NAS 与 WebDAV 网络串流（开箱即用）
* **原生网络存储连接**：无需编写或编译任何插件，界面一键直连群晖 Synology、威联通 QNAP、Nextcloud 或 AList 服务器。
* **AList 聚合网盘无缝支持**：通过 AList 的 WebDAV 接口（默认端口 `:5244/dav`），直接秒播挂载在 115、夸克、阿里云盘、百度网盘与 OneDrive 上的海量影视资源。
* **硬件加速流式播放**：基于 `libmpv` 强大的网络串流引擎，支持毫秒级时间轴跳跃拖拽、外挂与内嵌字幕加载以及断点记忆。

### 🧩 QuickJS 沙箱插件扩展（刮削器与视频源）
* 运行于隔离安全的 16MB QuickJS JavaScript 独立沙箱。
* **刮削插件**：联网搜索并获取影视元数据、剧情简介、评分及海报封面（如 TMDB、Bangumi、豆瓣）。
* **视频源插件**：扩展挂载远程 WebDAV、局域网 NAS 或流媒体服务器。
* 完备的宿主通信 API：`host.http`（自定义 HTTP 头与超时的网络请求）、`host.getConfig`、`host.storage`（插件独立 KV 存储）、`host.crypto`（MD5/SHA256/Base64）。详见开发文档 [`docs/PLUGIN-DEV.md`](docs/PLUGIN-DEV.md)。

---

## 🕹️ 手柄默认键位表（Xbox / Steam Deck 标准）

| 按键 | 视频播放界面 | 媒体库 / 文件浏览界面 |
| :--- | :--- | :--- |
| **A** | 播放 / 暂停（确认 OSD 控件） | 进入目录 / 播放选中项 |
| **B** | 退出播放（立即返回媒体库） | 返回上一层目录 / 取消 |
| **十字键 左 / 右** | 快退 / 快进（-10s / +10s） | 网格光标移动 |
| **LT / RT（扳机键）**| 音量减 / 音量加（-5% / +5%） | 列表快速翻页 |
| **LB / RB（肩键）**  | 跳转（-15s / +15s） | 切换上一个 / 下一个标签页 |
| **十字键 上** | 打开剧集播放列表对话框 | 光标跳到顶部搜索按钮 |
| **十字键 下** | 聚焦到底部 OSD 控制栏 | 向下滚动浏览 |
| **X** | 打开音轨选择菜单 | 触发影视元数据刮削 |
| **Y** | 打开字幕选择与样式菜单 | 重新扫描当前媒体库 |
| **R3（按下右摇杆）** | 循环切换播放倍速（`1.0x → 1.25x → 1.5x → 2.0x → 0.75x`） | - |
| **L3（按下左摇杆）** | 循环切换画面比例（原始比例 → 16:10 铺满 → 16:9 → 4:3 → 智能裁切） | - |
| **START** | 开关触控锁 | - |

---

## 📥 安装与运行

预编译发布安装包可在 [GitHub Releases](https://github.com/yiuf63/YiufPlayer/releases) 直接下载：

### Steam Deck (SteamOS)
1. 将 Steam Deck 切换至 **桌面模式（Desktop Mode）**。
2. 下载 `yiufplayer-linux-x86_64.tar.gz` 并解压至 `~/yiufplayer`。
3. 运行 `python3 add_to_steam.py`，脚本将自动在 Steam 库中注册 Yiuf Player 非 Steam 游戏快捷方式，并自动设置官方海报及封面网格图。
4. 切回 **游戏模式（Gaming Mode）** —— 在非 Steam 游戏库中点击 Yiuf Player 即可享受 60FPS 掌机大屏播放！

### Linux 桌面与其它 Linux 掌机（Arch / Ubuntu / Fedora）
解压 `yiufplayer-linux-x86_64.tar.gz` 并运行 `./yiufplayer`。请确保系统已安装 `libmpv`、`ffmpeg` 和 `libcurl`：
```bash
# Ubuntu / Debian
sudo apt install libmpv2 libavcodec-dev libcurl4

# Arch / Manjaro
sudo pacman -S mpv ffmpeg curl
```

### macOS (Apple Silicon arm64)
下载 `yiufplayer-macos-arm64.zip` 解压后直接运行 `./yiufplayer`。如需从源码编译，可通过 Homebrew 安装依赖：
```bash
brew install mpv ffmpeg fmt tinyxml2
```

### Android (arm64-v8a)
下载 `yiufplayer-android-arm64.apk`，可直接安装于 Android 平板、安卓掌机（如 Odin、Retroid Pocket）及 Android TV 智能电视。

---

## 🛠️ 从源码构建

### 环境要求
* 支持 C++17 的现代编译器（GCC 9+、Clang 10+ 或 AppleClang）
* CMake 3.18+
* `libmpv` 开发库与头文件
* OpenGL 或 OpenGL ES 3.0+
* `libcurl`

### 本地编译步骤

```bash
# 克隆仓库
git clone https://github.com/yiuf63/YiufPlayer.git
cd YiufPlayer

# 配置与编译
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# 启动运行
./build/yiufplayer
```

### Linux 容器化交叉构建（Steam Deck 目标）
```bash
docker build --platform linux/amd64 -f Dockerfile.linux -t yiufplayer-linux .
docker run --rm -v "$PWD:/src" -w /src yiufplayer-linux bash scripts/build-linux.sh
```

---

## 🧪 自动化测试与质量保障

Yiuf Player 包含完善的质量验证套件，覆盖业务逻辑、内存安全与界面渲染：

```bash
# 运行 16 组独立单元测试
./tests/run_tests.sh

# 运行端到端 UI 与无头 GL 帧缓冲冒烟测试
python3 tests/tools/verify_ui.py --only functional

# 运行文档与代码一致性核对
python3 tests/tools/check_docs.py
```

---

## 📄 开源许可证与致谢

Yiuf Player 采用 **[GNU General Public License v3.0](LICENSE)** 协议分发。

### 第三方组件声明
* **[Borealis](https://github.com/dragonflylee/borealis)**：专为游戏手柄设计的 Apache-2.0 UI 引擎。
* **[libmpv](https://mpv.io)**：LGPL-2.1+ / GPL-3.0 跨平台音视频播放核心。
* **[QuickJS](https://bellard.org/quickjs/)**: Fabrice Bellard 开发的 MIT 许可极简 JavaScript 引擎。
* **[FFmpeg](https://ffmpeg.org)**：LGPL-2.1+ / GPL-3.0 顶级多媒体框架。
* 完整第三方授权与声明清单见 [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)。

### 特别鸣谢
衷心感谢 **[wiliwili](https://github.com/xfangfang/wiliwili)**、**[Kodi](https://kodi.tv)** 与 **[mpv](https://mpv.io)** 项目，它们为掌机与手柄交互范式、现代化播放器架构提供了宝贵的灵感与参考。
