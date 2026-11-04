# DeckPlayer 🎬🎮

**专为 Steam Deck 与 Linux 掌机打造的现代化、控制器优先（Controller-First）且深度支持触控（Touch-Friendly）的原生音视频播放器。**

---

## 🌟 为什么做 DeckPlayer？

在 Steam Deck 上，玩家如果想离线看电影、番剧、美剧或听广播剧，目前面临的尴尬局面是：
- **VLC / 传统桌面播放器**：完全是为鼠标键盘设计的桌面 UI，在 7/7.4 英寸屏幕下手柄极难点中微小的进度条与下拉菜单，必须用触控板模拟鼠标，体验割裂。
- **Kodi**：虽然有手柄支持，但启动缓慢、配置臃肿，为了看个简单的本地视频需要折腾繁杂的刮削和插件。
- **Plex / Jellyfin**：一旦处于飞机、高铁等无网络环境，无法直接播放 SD 卡中的离线媒体。

**DeckPlayer 结合了 [Borealis](https://github.com/dragonflylee/borealis) 主机级手柄/触控 UI 与 [libmpv](https://mpv.io) 最顶级的音视频解码内核，专门为 SteamOS 800p 屏幕与手柄量身打造。**

---

## ✨ 核心特性

- **🎮 原生手柄操作**：全界面焦点自动吸附与高亮，支持 D-Pad、双摇杆、ABXY、线性扳机（LT/RT）全键位平滑控制。
- **👆 深度触控手势（Touch Gestures）**：
  - **双击屏幕**：播放 / 暂停。
  - **单击屏幕**：显示 / 隐藏 OSD 播放控制条与状态信息。
  - **左半屏上下滑动**：无级调节画面亮度。
  - **右半屏上下滑动**：无级调节系统音量。
  - **全屏水平滑动**：毫秒级精准快进 / 快退并伴随实时时间预览卡片。
  - **按住屏幕不动**：临时 2 倍速快进，松手立刻恢复原速（跳过片头/广告时最常用）。
- **📦 全格式通吃（音视频与字幕格式大满贯）**：
  - **视频格式**：MP4, MKV, WebM, MOV, M4V, TS, M2TS, MTS, TP, AVI, FLV, F4V, WMV, VOB, MPG, MPEG, RMVB, RM, 3GP, ISO, OGV, DIVX 等。
  - **音频格式**：FLAC, WAV, APE, ALAC, AIFF, DSD, DFF, DSF, MP3, M4A, AAC, OGG, OPUS, WMA 等无损与高品质音频。
  - **字幕格式**：SRT, ASS, SSA, VTT, LRC (歌词同步), SUB, IDX, SUP (蓝光PGS图形字幕), SMI 等，原生渲染复杂特效。
- **⚡ 人性化易用特性**：
  - **多档倍速**：`1.0x → 1.25x → 1.5x → 2.0x → 0.75x` 循环切换（按下右摇杆 R3）。
  - **Steam Deck 屏幕满屏比例**：一键切换 16:10 铺满、16:9 标准、4:3 复古或裁切（按下左摇杆 L3 一键切换）。
  - **掌机触控防误触锁定**：提供锁屏模式，躺姿/外出移动看剧时避免手掌触碰误触。
  - **全盘聚合扫描 (Library)**：无需在 MicroSD 卡复杂的目录树里翻找，一键汇聚所有剧集与电影。
  - **播放列表**：按 ↑ 打开，显示每集进度，可直接跳集。
  - **字幕轨记忆**：选过的字幕轨 / 音轨会被记住，下次打开同一文件自动恢复。
- **💾 Steam Deck 存储一键直达**：
  - 自动探测并置顶 Steam Deck MicroSD 卡（`/run/media/mmcblk0p1` 或 `/run/media/*/*`）。
  - 自动识别用户媒体库（`~/Videos`、`~/Music`、`~/Downloads`、`Home`）。
  - 支持智能断点续播百分比徽章与连续播放。

### 音轨与字幕

- 内嵌字幕自动选中（含 ASS 特效、PGS 图形字幕）
- 外挂字幕按文件名自动匹配，支持 `影片名.zh.srt` 语言变体
- 播放中按 **X** 切换音轨、按 **Y** 打开字幕面板（可调时间轴、加载外部字幕）

### 音乐与后台播放

音乐走独立的 `AudioPlayer` 引擎，**退出播放器界面音乐不中断**，
主界面常驻「正在播放」状态条。macOS 上自动抑制 App Nap。

### 插件（创意工坊）

插件是纯 C ABI 的动态库（`.so` / `.dylib`），提供两类能力：

- **视频源**：接入 WebDAV / Emby / Jellyfin 等在线片库，逐层浏览播放
- **刮削器**：为本地影片补封面、简介、评分

支持从 ZIP 安装（有 Zip Slip 防护），也支持手动放入目录后热重载。
参考实现见 [`examples/webdav_plugin/`](examples/webdav_plugin/)。

---

## 📖 使用说明

**面向使用者的完整操作指南见 [`docs/使用指南.md`](docs/使用指南.md)**，
包含界面结构、全部按键与手势、媒体库添加、字幕与刮削、插件安装、
数据存放位置与常见问题。

下面只列最常用的手柄按键。

## 🕹️ 手柄快捷按键指南

| 按键 (Steam Deck) | 功能操作 |
| :--- | :--- |
| **A 键** | 播放 / 暂停（列表中为进入/确定） |
| **B 键** | 返回上一级 / 退出播放界面 |
| **D-Pad 左右 / 左摇杆** | 短距离快退（-10s） / 快进（+10s） |
| **LT / RT 线性扳机** | 长距离大幅度快退（-30s） / 快进（+30s） |
| **LB / RB 顶部肩键** | 切换播放列表上一集 / 下一集 |
| **X 键** | 快速弹出多音轨切换面板 |
| **Y 键** | 快速弹出字幕选择与时间轴微调面板 |
| **R3 (按下右摇杆)** | 一键循环切换播放倍速 (1.0x -> 1.25x -> 1.5x -> 2.0x -> 0.75x) |
| **L3 (按下左摇杆)** | 一键循环切换画面宽高比 (原始 -> 16:10满屏 -> 16:9 -> 4:3 -> 裁切铺满) |
| **START 键** | 锁定 / 解锁触控（防误触） |
| **BACK 键** | 全屏切换 |

> 以上为**播放界面内**的按键。在列表界面中，**X** 用于刮削影片信息、
> **Y** 用于重新扫描媒体库。完整说明见 [`docs/使用指南.md`](docs/使用指南.md)。

---

## 🛠️ 编译与构建

### 1. 依赖项
- C++17 编译器（GCC 9+ 或 Clang 10+）
- CMake 3.18+
- `libmpv-dev`
- `libgl-dev` 或 `libegl-dev`
- `borealis` UI 库

### 2. 本地构建

Borealis 已内置于 `external/borealis`，无需额外拉取子模块。

```bash
# 本地开发（链接系统 / Homebrew 的 mpv；本项目同为 GPL-3.0，链接它合法且产物可分发）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

# 运行
./build/deck-player
```

构建时会把 `resources/` 链接进构建目录，直接运行即可加载字体与图标。

> 构建时会打印所用的 FFmpeg/libmpv 版本与许可证，供发布时核对源码提供义务。
> 本项目以 GPL-3.0 分发，链接 GPL 或 LGPL 版 FFmpeg 均合法，构建不会因此中止。
> 若希望产物更小（不带 `--enable-gpl` 组件），可用
> `scripts/build-lgpl-libmpv.sh` 自建后再以 `-DMPV_ROOT=<前缀>` 指定。

### 3. Flatpak 构建（推荐用于 SteamOS）
```bash
flatpak-builder --user --install --force-clean build-flatpak flatpak/org.deckplayer.DeckPlayer.yaml
```
安装后，直接在 Steam 客户端点击 **“添加非 Steam 游戏”**，勾选 `DeckPlayer`，即可直接在 **Gaming Mode（掌机手柄模式）** 中无缝启动并畅快使用！

---

## 🧪 测试

```bash
./tests/run_tests.sh          # 全部单元测试
./tests/run_tests.sh -v       # 详细输出
./tests/run_tests.sh parser   # 按名字过滤
```

覆盖：文件名解析、元数据持久化（含换行转义回归）、媒体库扫描与过滤、
插件 ZIP 安装（**含 Zip Slip 攻击防护验证**）、并发读写。

### 端到端验证

单元测试验证不了「界面真的渲染了吗」「视频真的播出来了吗」。这三套工具补上：

```bash
# 界面与播放（渲染/播放/字幕/手势/插件/屏幕适配/性能/ASan/稳定性）
#   --list 看分组，--only <组> 只跑一组（如 --only video）
python3 tests/tools/verify_ui.py

# 文档与实现一致性（按键/数值/数据文件/环境变量/README）
python3 tests/tools/check_docs.py

# 发布前清单（30 项：安装路径/desktop/图标/许可证）
python3 tests/tools/release_check.py         # 发布模式，GPL 依赖会阻断
python3 tests/tools/release_check.py --dev   # 开发模式，仅警告

# 产物级许可证审计：顺着动态库依赖链查 GPL
python3 tests/tools/license_audit.py
```

`verify_ui.py` 用读取 GL 帧缓冲的方式验证画面，绕开 macOS 屏幕录制权限；
`--only video` 之类可以只跑某一组。

### 内存安全

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1" \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -O1" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
python3 tests/tools/verify_ui.py --only asan
```

详见 [`tests/README.md`](tests/README.md)。

---

## 🚦 发布状态

**当前不可发布。** 必须完成的事项见 [`RELEASE-BLOCKERS.md`](RELEASE-BLOCKERS.md)：

```bash
python3 tests/tools/release_check.py    # 退出码 0 才能发
```

---

## 📄 许可证与合规

DeckPlayer 以 **GNU GPL-3.0** 分发，全文见 [`LICENSE`](LICENSE)。

选择 GPL-3.0 的取舍：

- **可以**链接常见的 GPL 构建版 libmpv / FFmpeg，无需为每个架构单独构建 LGPL 版本。
- **可以**与同为 GPL-3.0 的项目互相借鉴实现。
- **代价**：衍生作品也必须以 GPL-3.0 开源；通过插件加载的闭源商业扩展
  在法律上难以成立，因此本项目不做收费闭源插件。

第三方组件清单与许可见 [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)，
许可证全文位于 `licenses/`。

**发布义务**：按 GPL-3.0 §6，分发二进制时须提供对应源码。
`python3 tests/tools/license_audit.py` 会列出产物的实际依赖，可用于核对。

---

## 📋 当前开发状态

已实现：
- 手柄全键位操作（A/B/十字键/LT/RT/LB/RB/X/Y/R3/L3/START），START 键切换触控锁
- 触控手势：双击播放暂停、单击 OSD、左右半屏分别调亮度与音量、水平滑动快进快退、
  长按临时 2x 快进
- 倍速（R3）与画面比例（L3）循环切换
- **OLED 真黑主题（A 屏黑）**：`DECKPLAYER_THEME=oled` 或配置 `theme` 文件切换，
  纯黑背景在省电与无光晕上适配 Steam Deck OLED 等 AMOLED 掌机
- **OSD 渐显动效**：控制栏 260ms 淡入淡出 + 上下滑移，HUD 提示尾部淡出；
  OSD 显示时字幕自动上移到控制栏上方
- **字幕完整支持**：
  - 内嵌字幕（容器内的 SRT / ASS / SSA / PGS 图形字幕）自动识别
  - 外挂字幕按同名字符串匹配自动加载（`sub-auto=fuzzy`，可匹配 `.zh.srt` 等变体）
  - 菜单中按「内嵌 / 外挂」分组展示，标注格式与是否位图字幕（PGS/SUB 保持原画质）
  - 运行中可随时 `sub-add` 加载额外字幕文件，即时生效
  - 字幕延时微调，并随播放进度一并持久化
- 音轨 / 字幕轨选择会记住，续播时自动恢复上次选择的轨道
- 断点续播与播放历史（属性订阅已修复，进度与时长真实可用）
- **媒体文件夹管理**：MX 式添加任意文件夹、递归扫描、结果持久化、
  冷启动免重扫、移除文件夹自动清理缓存（视/音频分列）
- 音乐库独立标签页，支持音频文件浏览
- **后台音乐播放**：独立于界面的全局音频引擎，退出播放器或切换标签页后
  音乐继续播放；主界面标题栏常驻「正在播放」条（曲名 / 进度 / 时间）；
  播放列表自动连播、断点续播、每 10 秒自动落盘进度
- macOS 上自动抑制 App Nap（Objective-C++ 调用 NSProcessInfo），
  窗口失焦也不会降频卡顿
- 存储位置自动探测（SteamOS /run/media 与 macOS /Volumes）
- 插件加载（dlopen，支持 .so/.dylib）

- **插件系统（创意工坊基础）**：
  - 纯 **C ABI** 接口，绝不跨动态库边界传递 C++ 对象
    （实测：仅新增一个结构体字段就会导致 Bus error）
  - 双扩展点：**视频源**（Emby/Jellyfin/自建站）+ **刮削源**（TMDB/豆瓣等）
  - 固定入口符号 `deck_player_plugin_entry_v1` + ABI 版本校验，不匹配直接拒绝
  - 插件目录 `~/.config/deck-player/plugins/`，支持 Y 键热重载
  - 插件管理界面：查看已装插件、能力标记、重新扫描
  - 宿主代为发起 HTTP 请求（libcurl），插件无需自带网络栈
  - **ZIP 包安装**：输入路径即可安装，内置 Zip Slip 防护（拒绝 `../` 与绝对路径）、
    路径穿越校验、同名不覆盖、失败自动回滚、卸载越界路径拒绝
  - **元数据刮削**：媒体库中对影片按 X 键自动刮削，
    从文件名智能提取片名与年份（清理 1080p/WEB-DL/x264 等技术标记），
    结果显示评分、年份、片长、类型与简介；元数据独立持久化（`metadata.dat`）
  - 示例插件：
    - `examples/webdav_plugin/` —— **可实际工作的 WebDAV 源**（含鉴权与 XML 解析，
      已用真实服务端验证）
    - `examples/demo_plugin/` —— 最小 ABI 演示

开发中：Steam Workshop 集成（需 Steam 应用 ID）。

> 注：macOS 上 Homebrew 的 mpv 是 **GPL 构建**，本项目同为 GPL-3.0，
> 因此链接它完全合法、产物可以分发——按 GPL-3.0 §6，分发时须提供对应源码
> （用 `python3 tests/tools/license_audit.py` 核对产物实际依赖）。
> 若希望产物不带 `--enable-gpl` 组件，可先用 `scripts/build-lgpl-libmpv.sh`
> 自建，再以 `-DMPV_ROOT=<前缀>` 指定。
