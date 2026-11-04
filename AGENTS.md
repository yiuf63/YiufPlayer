# AGENTS.md

本文件面向在此仓库工作的 AI agent（以及人类开发者），记录**不能靠读代码猜出来**的约定与陷阱。
纯代码风格类的细节请直接阅读现有源码。

---

## 项目是什么

**DeckPlayer** —— 面向 Steam Deck 与通用掌机/桌面的音视频播放器。

- **技术栈**：C++17 + [Borealis](https://github.com/xfangfang/borealis) UI 框架 + libmpv 解码内核
- **分发形态**：**GPL-3.0 开源**（2026-10 起从闭源改为 GPL-3.0，见「许可证」一节）
- **平台**：Linux / SteamOS（目标）、macOS（当前唯一可实测的开发环境）
- **测试环境**：macOS arm64。**没有 Steam Deck 实机**，所有 SteamOS 相关结论均为静态推导

---

## 构建

```bash
# 本地开发（链接系统 mpv）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./build/deck-player
```

构建时 `license_probe` 会打印所用 FFmpeg 的版本与许可证，**不会阻断构建**
（本项目是 GPL-3.0，链接 GPL 版 FFmpeg 同样合法）。

可选：若希望产物更小（不带 `--enable-gpl` 组件），可先跑
`scripts/build-lgpl-libmpv.sh`，再用 `-DMPV_ROOT=<前缀>` 指定。

### 其它平台

```bash
# Linux / SteamOS：容器构建（推荐，产物落 build-linux/）
docker build --platform linux/amd64 -f Dockerfile.linux -t deckplayer-linux .
docker run --rm -v "$PWD:/src" -w /src deckplayer-linux
# 或 Linux 主机上直接：./scripts/build-linux.sh（见 docs/LINUX-BUILD.md）
# Linux 必须 -DUSE_SYSTEM_GLFW=OFF：borealis 用的是定制 GLFW 分支，
# 系统 libglfw3 没有 GLFW_SOFT_FULLSCREEN / GLFW_TOUCH / GLFW_IME 符号。

# Android（仅 arm64-v8a）
cd android && ./gradlew assembleDebug
```

**Android / Linux 构建的隐藏前提**（CI 上踩过，本地因为有文件看不出来）：

1. `libromfs-generator` 是把资源编译进二进制的**宿主机工具**，不入库。
   必须先现编，再准备资源，顺序不能反：
   ```bash
   bash scripts/build-libromfs-generator.sh && bash scripts/prepare-romfs.sh
   ```
   macOS 编出来的是 Mach-O，Linux runner 上跑不了，所以每个平台都要现编。
2. `external/borealis/library/lib/extern/SDL` 是未随附的 submodule，
   Android 构建必须有它（见 `android/README.md`）。
3. 仓库 `.gitignore` 曾把 `*.so` 排除，导致 Android 的 11 个预编译库没入库；
   改动 `.gitignore` 后要确认 `git ls-files android/app/src/main/jniLibs` 非空。

### 测试

```bash
./tests/run_tests.sh          # 全部单元测试
./tests/run_tests.sh -v       # 详细输出（默认只看汇总）
./tests/run_tests.sh parser   # 按名字过滤
```

自建测试框架，无 gtest/catch2 依赖。新测试需在 `tests/run_tests.sh` 的
`TESTS` 数组登记。详见 `tests/README.md`。

---

## 许可证：GPL-3.0

**本项目以 GPL-3.0 分发，全文见根目录 `LICENSE`。**
此前的闭源约束（不得链接 GPL 组件、必须自建 LGPL 版 libmpv）**已全部取消**。

### 为什么改成 GPL-3.0

最初是闭源商业软件，因此每个架构都必须自建 LGPL 版 libmpv + FFmpeg，
这是移植到掌机的最大成本。改为 GPL-3.0 后：

- 可直接链接 Homebrew / 发行版 / Flathub 的常见 mpv 与 FFmpeg
- 可与同为 GPL 的项目互借实现（但**不要整段照抄**，见下）
- 移植到新架构时不再需要单独的许可证合规构建

**代价**：衍生作品也必须 GPL-3.0 开源。原先「收费闭源 MOD」的方案
在法律上难以成立（通过 ABI 加载的插件通常被视为同一程序），已放弃。

### 组件许可

| 组件 | 许可证 | 与本项目的关系 |
| :--- | :--- | :--- |
| libmpv | LGPL-2.1+ 或其依赖决定的 GPL | 动态链接；GPL 与 LGPL 均合法 |
| FFmpeg | 视构建而定（常见为 GPL-3） | 链接 GPL 版合法 |
| Borealis | Apache-2.0 | 兼容 GPL-3.0；修改已声明于 `PATCHES.md` |
| libcurl | MIT/X | 兼容 |
| fmt/glfw/nanovg/yoga/tinyxml2 | MIT/BSD/zlib | 兼容 |

### `cmake/license_probe.cpp` 仍然保留

它**不再阻断构建**，只打印所用 FFmpeg 的版本与许可证。

保留原因：GPL-3.0 §6 要求分发二进制时提供对应源码，
所以必须能查出产物实际链接的是哪个 FFmpeg 构建。
`tests/tools/license_audit.py` 顺着依赖闭包做同样的核对。

### 借鉴其他 GPL 项目的红线

可以**读**同类开源项目（如 wiliwili）学习做法，但：

- 不要整段复制它的代码进本仓库（会引入来源不清的著作权问题）
- 需要复用时，只移植**思路**，按本项目风格重写，并在注释中注明参考来源
- 本项目是 Steam Deck/掌机播放器，与 B 站客户端 wiliwili 的领域不同

## 架构

```
src/
├── main.cpp                    入口：资源定位 → Borealis 初始化 → 主循环（含音频 tick）
├── activity/                   一个 Activity ≈ 一个全屏页面
│   ├── file_browser_activity  主界面（4 个 Tab：存储位置/视频库/音乐库/播放历史）
│   ├── folder_picker_activity 目录选择器（borealis 无文件选择器，自建）
│   ├── player_activity        视频播放页（播放列表、自动下一集）
│   ├── plugin_browser_activity 插件视频源的目录浏览（逐层进入）
│   └── plugin_manager_activity 插件管理页（安装/卸载/配置）
├── model/                      数据与状态
│   ├── media_library          用户添加的媒体文件夹 + 扫描缓存（持久化）
│   ├── metadata_store         刮削元数据（独立于媒体库，原子写入）
│   └── storage_provider       IStorageProvider 接口（**旧 C++ ABI，已不用于插件加载**）
├── utils/
│   ├── app_paths              XDG 配置路径统一解析
│   ├── resource_locator       启动时定位资源目录（见「陷阱 1」）
│   ├── media_name_parser      文件名 → 刮削搜索词（已独立成模块以便单测）
│   ├── playback_history       播放进度持久化
│   └── scanner                格式白名单、目录遍历、外挂字幕匹配
├── view/
│   ├── mpv_player             核心：libmpv GL 渲染 + 全键位 + 手势 + OSD
│   ├── media_cell             列表卡片行 / 分组标题 / 空态（所有列表共用）
│   ├── now_playing_bar        后台音乐状态条
│   └── track_menu             音轨/字幕选择对话框
├── ui/                         视觉设计系统（**所有界面唯一 token 来源**）
│   ├── design                 色板 / 间距 / 圆角 / 字号 / 绘制基元
│   └── icons                  32 个矢量图标（nanoVG 路径，不依赖字体）
├── playback/
│   ├── audio_player           全局音乐引擎（生命周期独立于界面）
│   └── background_playback    macOS App Nap 抑制（.mm，Objective-C++）
└── plugin/
    ├── plugin_manager         插件加载、ABI 校验、宿主 API 实现
    ├── plugin_config_store    插件配置持久化（按插件 id 的键值对）
    └── plugin_installer       ZIP 安装（含 Zip Slip 防护）
```

### 两条播放路径（重要区别）

| 路径 | 类 | 生命周期 | 用途 |
| :--- | :--- | :--- | :--- |
| 视频 | `MPVPlayerView` | 绑定 View，界面销毁即停止 | 视频播放 |
| 音频 | `AudioPlayer`（单例） | 独立于界面，全局存活 | 音乐、后台播放 |

**不要把音乐播放改回 `MPVPlayerView`**，那会破坏后台播放。
`AudioPlayer` 用 `vid=no` 且不创建渲染上下文，因此不占 GL 资源。

---

## 陷阱清单（都是实际踩过的）

### 1. 资源路径是编译期相对路径

borealis 把 `BRLS_RESOURCES` 编译进二进制，当前值是 `"./resources/"`，
**相对于进程工作目录**。

后果：从任何非 `build/` 目录启动（桌面启动器、Flatpak `/app/bin`、Steam）
都找不到字体和图标，UI 会变成没有文字。

对策：`ResourceLocator::setup()` 在 `main()` 最前面执行，
按优先级查找资源目录并把**工作目录切到其父目录**。
新增启动相关代码时不要破坏这个顺序。

### 2. TabFrame 会 delete 旧 Tab

`borealis` 的 `TabFrame::addTab` 实现里：
```cpp
this->removeView(this->activeTab); // will call willDisappear and delete
```
切换标签页时旧 View 被**销毁**。任何指向它的成员指针都会悬垂。

对策：持有 Tab 内 View 的成员用 `DynamicBox` 包装（析构时自动置 nullptr），
见 `file_browser_activity.cpp` 里的 `DynamicBox` 类。

### 3. 后台线程回调必须检查存活标志

`brls::sync(lambda)` 只是把任务投递到主线程队列，**不保证活动还活着**。
lambda 捕获 `this` 时，活动可能已被 pop 并析构。

对策：使用 `shared_ptr<atomic<bool>>` 存活标志，且**必须是回调里第一个检查**：

```cpp
brls::sync([this, alive]() {
    if (!alive->load()) return;   // ← 必须最先，之后才能碰 this 的任何成员
    // ...
});
```

注意：**每次启动后台任务都要新建标志**。复用同一个标志会导致多个任务
互相覆盖，无法区分谁该失效。

**第 31 轮踩到的实际事故**：`FileBrowserActivity` 把标志存在单个成员
`alive_flag_` 里，`startLibraryScan()` 与 `scrapeItem()` 都往它赋值。
结果先启动的扫描任务，其标志被后启动的刮削任务覆盖，析构时只置 false 了
最后一个——**扫描回调在 Activity 销毁后仍然执行**，ASan 报：

```
heap-use-after-free in FileBrowserActivity::renderSourcesContent()
  <- refresh() <- startLibraryScan()::$_0::operator()
```

正确做法是用**容器保存所有活跃任务的标志**，析构时全部置 false：

```cpp
std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;

// 启动任务时
alive_flags_.push_back(alive);

// 析构时
for (auto& f : alive_flags_) if (f) f->store(false);
```

### 4. mpv 必须显式 observe_property

不调用 `mpv_observe_property()`，mpv **不会发送** `MPV_EVENT_PROPERTY_CHANGE`。
症状：进度条永远 0、时长永远 00:00、断点续播失效、播放历史不记录。

对策：`initMPV()` 里已订阅 `time-pos` / `duration` / `pause`。
新增需要读取的属性时记得一并订阅。

### 5. 插件 ABI 不能传 C++ 对象

**已实测**：插件按旧版结构体编译、主程序升级后新增一个字段，
运行时直接 `Bus error` 并产生无限循环。

`include/plugin/deck_plugin_abi.h` 里的规则不可协商：

- 只用 C 类型（`const char*`、`int64_t`、纯 C 结构体）
- 禁止 `std::string` / `std::vector` / 类 / 模板 / 异常跨 SO 边界
- 固定入口符号 `deck_player_plugin_entry_v1`
- `abi_version` 不匹配直接拒绝加载
- 结构体只允许在**尾部追加**字段

**破坏这些规则的代价是所有已发布第三方插件失效。** 改 ABI 前请三思。

### 6. 插件配置必须经 `config_json` 或 `get_config` 传入

宿主的 `g_config` 曾经是一个**从未被填充的空表**，导致 `init()` 永远收到
`nullptr`，插件只能靠环境变量配置。

现状：配置存在 `PluginConfigStore`（`~/.config/deck-player/plugin_config.dat`），
加载插件时以 JSON 形式传给 `init()`，同时 `host->get_config` 也会转发过去。

**新插件应优先读 `config_json`**，环境变量只作为调试后备。

### 7. `is_audio()` / `is_video()` 判空

`MediaItem::type` 有 `Unknown` 状态（如插件返回的占位条目）。
遍历视频/音频前应先判断类型，不要假设非此即彼。

### 8. 跨两次调用的快照比较不成立

`MediaLibrary::getItems()` 返回**值拷贝**，单次结果内部自洽。
但先 `getItems()` 再 `getVideos()` 再比较大小是**错的**——
两次调用之间后台扫描可能已替换数据集。

需要一致性视图时，在单次调用返回的容器内完成全部判断。

### 9. 切标签页后焦点停在侧边栏（「按 A 没反应」）

`TabFrame::selectTab()` 只把焦点给 `SidebarItem`，而 `SidebarItem` 的 A 键动作是
「向右导航」（`sidebar.cpp:72`），所以用户按 A 只是把焦点移进列表，**要再按一次**才
真的选中条目。表现就是「点了没用」。

已在 borealis 里改成 `selectTab` 末尾继续 `giveFocus(activeTab->getDefaultFocus())`。
改动焦点相关代码时不要退回旧行为。

### 10. 验证「功能正常」不能只看退出码和像素

崩溃（退出码 / ASan）和渲染（抓帧）都过了，仍然可能出现「按 A 打不开视频」——
65 项自动检查全绿而功能全废，本项目真的发生过。

功能层必须有**注入操作 + 断言状态变化**的检查：`DECKPLAYER_SMOKE=1` 走
`Application::injectButton()` 全链路，断言 activity 栈深度变化，失败以退出码 1 结束。
新增这类检查后，要故意改坏源码确认它会红。

### 11. 测试钩子环境变量很多，别硬背

界面与播放的可验证性全靠 `DECKPLAYER_*` 钩子（抓帧、注入按键、指定起始标签、
打开对话框/播放列表/插件页、强制音轨字幕轨、模拟手势……）。完整清单以代码为准：

```bash
grep -rho 'getenv("DECKPLAYER_[A-Z_]*")' src | sort -u
```

### 12. TapGestureRecognizer 不派发 START（按下是 UNSURE）

`external/borealis/library/include/borealis/core/touch/tap_gesture.hpp` 明写
`START: unsupported`：未开启 `forceRecognision` 时，手指按下报成 **UNSURE**。
任何「按下态」「长按」逻辑只判 `GestureState::START` 就是死代码——
本项目曾因此「按钮按下态」看似已修、实际从未生效（2026-11-03 修复，见
`setupGestures()` 的注释）。

### 13. ffmpeg 不能继承父进程 stdin

脚本里调 ffmpeg 必须 `stdin=subprocess.DEVNULL`：ffmpeg 会读 stdin 收交互命令，
stdin 不关闭就**永久阻塞**（CPU 0%，像死锁）。`verify_ui.py` 曾因此整组挂死。

### 14. 像素检查里能关的图层就关掉

OSD 自动隐藏要 8 秒，半透明渐变会压在字幕上，把纯白像素压到 200 上下，
导致「>245」判据在 OSD 显示期间必挂。判「字幕是否渲染」时两组抓帧都加
`DECKPLAYER_OSD_OFF=1`，只比「字幕开/关」。UI 时序依赖是像素检查最大的不稳定源。

### 15. `NVGcolor` 是 0..1 的浮点，不是 0..255

直接写 `NVGcolor{0x18, 0x1B, 0x20, 1.0f}` 得到的是 `{24.0, 27.0, 32.0, 1.0}`，
三个通道都远超 1.0，**整片颜色变白**。`ui::design` 里有 `c8(r,g,b,a)` 负责除以 255，
自己写常量一律走它。

### 16. 文字必须用 `nvgFontFaceId(getDefaultFont())`

写 `nvgFontFace(vg, "sans")` 会拿到 `-1`，fontstash 找不到字体后 `nvgText` 直接 return，
**整块文字一个都不画**（历史 bug，见 docs/UI-DESIGN.md 附录 B1）。
现在统一走 `ui::text()` / `ui::textClipped()` / `ui::ellipsize()`，不要直接调 `nvgText`。

### 17. 列表行用 `MediaCell`，不要再用 Label 拼

`MediaCell` 里 `setHideHighlight(true)` 自己画选中态（底色 + 描边 + 左侧竖条）。
borealis 默认焦点框是整块白，在深色卡片上非常突兀；`deck_theme.cpp` 的
`highlight/padding` 也调成了 0，否则高亮框会比卡片小一圈。

新增界面时颜色/间距/圆角/字号**一律引用 `ui::palette()` / `ui::space` /
`ui::radius` / `ui::font`**，写字面量会让「一屏五种字号」的拼装观感回来。

### 18. `addMetric("brls/highlight/padding")` 是无效的

borealis 的高亮内缩来自 `View::highlightPadding` 成员（读 XML 属性
`highlightPadding`），不是 style 度量。塞进 `addMetric()` 不会有任何反应，
抓帧才会发现「对话框按钮的高亮依旧顶满整条 720px」。
`brls/highlight/corner_radius`、`brls/highlight/stroke_width` 则是**有效**的
（`view.cpp` 里确实从 style 取值）。对话框按钮的内缩只能改
`external/borealis/library/lib/views/dialog.cpp` 的 XML。

---

## 代码风格

- **缩进 4 空格**，无 tab
- **命名**：类型 `PascalCase`，函数/变量 `camelCase`，私有成员 `trailing_underscore_`，
  常量 `UPPER_SNAKE`（见现有代码）
- **命名空间**：全部业务代码在 `namespace deck_player`，文件系统别名 `namespace fs`
- **注释用中文**，与代码库保持一致
- 头文件用 `#pragma once`
- 新模块放在 `include/<层>/` 与 `src/<层>/` 对应位置

### 第三方头文件必须标记 SYSTEM

`CMakeLists.txt` 里对 `external/borealis/**` 的 include 用了 `SYSTEM` 关键字。
原因：borealis 头文件在 `-Wextra` 下会产生 300+ 条 `unused-parameter` 警告，
把项目自身的真实警告淹没。

**不要移除 SYSTEM 标记，也不要为了消警告去改 `external/` 里的代码**
（那会让后续同步上游变得很痛苦）。项目自身代码必须保持 **0 warning**。

---

## 已完成的验证 vs 未验证

诚实标注，避免后人误以为都测过：

### 已验证（有实际运行的证据）

- macOS 上构建、启动、资源加载、插件加载
- **界面渲染**（读 GL 帧缓冲验证，见下）
- **视频播放画面**（第 19 轮修复并验证，见下）
- **内嵌字幕渲染**（第 20 轮验证，见下）
- **音频播放与后台播放**（第 21 轮验证并修复三个缺陷，见下）
- **触控手势**（第 22 轮验证并修复方向错误，见下）
- **四个标签页界面 + 递归扫描**（第 23 轮验证，见下）
- **播放列表自动连播**（第 24 轮验证并修复三个缺陷，见下）
- **插件系统与创意工坊**（第 25 轮验证并修复浏览缺陷，见下）
- **刮削展示与 ZIP 安装**（第 26 轮验证并修复加载缺陷，见下）
- **端到端验证脚本**（第 27 轮，23 项自动检查 + 修复字幕遮挡，见下）
- **文档与实现一致性检查**（第 28 轮，38 项 + 修正两处文档错误，见下）
- **屏幕尺寸适配**（第 29 轮，5 种分辨率 + 宽高比验证，见下）
- **性能与 CPU 占用**（第 30 轮，修复 85%→4% 的空转缺陷，见下）
- **内存安全（ASan 全流程）**（第 31 轮，修复两个 use-after-free，见下）
- **长时间稳定性与线程安全**（第 32 轮，修死锁隐患 + 378 轮压力，见下）
- **发布前清单**（第 33 轮，27 项安装/合规检查 + 修两个打包缺陷，见下）
- **产物级许可证审计**（第 34 轮，依赖链 GPL 审计 + 修 LGPL 脚本隐患，见下）
- **播放器内对话框**（第 35 轮，修复对话框被视频盖住 + 新增播放列表界面，见下）
- **全页面对话框渲染**（第 36 轮，验证三类页面 + 修两处测试工具缺陷，见下）
- **字幕轨记忆**（第 38 轮，通读文档发现并修复「选过不记住」的缺陷，见下）
- **README 核对**（第 40 轮，发现倍速范围写错 + 把 README 纳入检查，见下）
- 8 个测试套件（断言数以 `./tests/run_tests.sh -v` 输出为准），含 Zip Slip 攻击防护的真实恶意包测试
- ASan + UBSan：全部测试与主程序运行无报告
- mpv 属性订阅、字幕切换、外部字幕加载（用真实 mkv 验证）
- 音频解码、播放列表连播、App Nap 抑制 API

### 界面渲染：已验证（第 18 轮）

**已验证界面能正常渲染。** 方法是读 GL 帧缓冲，绕开系统截图限制：

```bash
DECKPLAYER_FRAMEGRAB=/tmp/grab.ppm ./build/deck-player
python3 tests/tools/analyze_framegrab.py /tmp/grab.ppm
```

实测结果：1280x720，中性灰 98.2%（符合深色主题），
检测到标题栏文字、Tab 标签、列表项、上下分隔线，布局正常。

**为什么不用 `screencapture`**：macOS 需要屏幕录制权限，
在无权限 / 显示器休眠时会返回纯黑图（实测某次只有 1 种颜色），
曾导致误判为「界面没有渲染」。读帧缓冲不受此限制。

**关于切换标签页**：`onControllerButtonPressed` 无法可靠驱动标签切换
（该接口只在按键瞬时生效，实测画面差异仅来自动画相位）。
正确做法是 `TabFrame::selectTab(position)`，见第 23 轮的说明。

### 未验证（需外部条件）

| 项 | 缺什么 |
| :--- | :--- |
| LGPL 版 libmpv 构建 | 需要网络（沙箱无外网） |
| Flatpak 完整构建 | 需要 `flatpak-builder` + 网络 + runtime |
| 实际播放观感（画面/声音/手势流畅度） | 需要人上手 |
| SteamOS / Steam Deck 运行 | 需要实机 |
| ThreadSanitizer 数据竞争检测 | macOS 上 TSAN 跑不起来，需在 Linux 补做 |

**不要把这些当作"已经做好"。**

### 视频渲染：已验证（第 19 轮）

**视频能正常播放并渲染到屏幕。**

```bash
./build/deck-player /tmp/vtest.mp4        # 命令行直接播放
DECKPLAYER_FRAMEGRAB=/tmp/f.ppm \
DECKPLAYER_FRAMEGRAB_COUNT=4 \
DECKPLAYER_FRAMEGRAB_INTERVAL=100 \
DECKPLAYER_FRAMEGRAB_START=300 ./build/deck-player /tmp/vtest.mp4
```

实测（testsrc2 彩条视频，640x480）：

| 指标 | 修复前 | 修复后 |
| :--- | :--- | :--- |
| 彩色像素 | 0.15% | **73.62%** |
| 唯一颜色数 | 148 | **54041** |
| 帧间变化 | 0.10% | **11.48%** |

采样点为纯绿 `(0,255,1)`、洋红 `(255,0,254)`、青色 `(0,250,255)`，
与 testsrc2 图案一致；左右黑边说明 4:3 宽高比保持正确。

#### 根因：nanoVG 命令缓冲覆盖了 mpv 的 GL 输出

borealis 的绘制流程是：

```
videoContext->beginFrame()
glClear()
nvgBeginFrame()        ← 开始记录绘制命令（尚未提交 GPU）
  view->frame()        ← 若在此处 mpv_render_context_render，画面被写出
nvgEndFrame()          ← 此刻 nanoVG 才真正提交命令，覆盖掉 mpv 的画面
videoContext->endFrame()
```

**nanoVG 是命令缓冲**，`draw()` 期间调用 GL 渲染，其结果会被随后
`nvgEndFrame()` 提交的命令覆盖。表现为视频区域一片纯色
（当时是 `rgb(31,34,39)`，即 UI 背景色）。

#### 修复

在 borealis 中新增 `Application::postDrawHook`，在 `nvgEndFrame()`
之后、缓冲交换之前执行。`MPVPlayerView` 在 `draw()` 中只记录几何信息，
真正的渲染推迟到该钩子里完成。

改动位置：
- `external/borealis/library/include/borealis/core/application.hpp`（新增钩子）
- `external/borealis/library/lib/core/application.cpp`（调用钩子）
- `src/view/mpv_player.cpp`（`draw()` 记录几何 + `renderMPV()` 实际渲染）

**注意**：`postDrawHook` 是单个回调，只支持一个视频视图。若将来需要
多路视频同时输出，需改为回调列表。

#### 排查过程中排除的因素

| 排查项 | 结果 |
| :--- | :--- |
| mpv 能否解码 | ✓ duration=20 tracks=1 |
| 文件是否加载 | ✓ `MPV_EVENT_FILE_LOADED` 已触发 |
| 视频尺寸 / vo-configured / EOF | ✓ 640x480 / yes / no |
| render 是否被调用 | ✓ 每帧都调用 |
| `hwdec` / `keep-open` / `sub-auto` | ✓ 逐一排除 |
| FLIP_Y / FBO 物理像素 / glViewport | ✓ 都补了，但不是根因 |

**另一个教训**：`draw()` 里读 `height` 会读到 0（mpv 重配置的中间态），
同一帧稍后再读就是 640x480。**不要把属性读取当作渲染依据**。

#### 附带修复：OSD 在视频之上重绘

视频渲染到默认帧缓冲后会覆盖整个视图，导致标题栏、进度条、HUD 全部消失。
解决办法：`renderMPV()` 末尾调用 `drawOverlay()`，开一个独立的 nanoVG 帧
把 OSD 重新画在视频之上。

实测：顶部区域均值 55.4、底部 54.5（OSD 渐变生效），
中间 94.9（视频画面），彩色像素 71.73%。

### 反馈链路：功能冒烟测试（第 41 轮）

**「没崩」不等于「功能正常」。这是本项目最严重的一次验证方法论失败。**

#### 事故经过

用户连续报告点击视频闪退。我修了三轮崩溃（两轮修错指针，见下节），
第三轮之后**不再崩溃**，我宣布修好了。

用户实测反馈：

> 没有崩溃，但是点击任何视频都打不开，返回上级目录也是不行。

而当时 **65 项自动检查全部通过**——它们只测像素、退出码、ASan，
**从设计上就不可能发现「按钮点了没用」**。

#### 根因：切标签后焦点没进内容区

```cpp
// borealis tab_frame.cpp 上游实现
void TabFrame::selectTab(int position) {
    SidebarItem* item = this->sidebar->getItem(position);
    Application::giveFocus(item);      // ← 焦点停在侧边栏
}
```

而 `SidebarItem` 的 A 键动作是「**向右导航**」（`sidebar.cpp:72`）：

```cpp
this->registerAction("hints/ok", BUTTON_A, [](View* view) {
    Application::onControllerButtonPressed(BUTTON_NAV_RIGHT, false);
    return true;
});
```

于是用户按 A 只是把焦点移进列表，**必须再按一次**才真正选中条目。
表现为「点了没反应」。

修法：`selectTab` 里内容加载后把焦点继续交给 `activeTab->getDefaultFocus()`。

#### 新增：`DECKPLAYER_SMOKE` 功能冒烟

```bash
DECKPLAYER_SMOKE=1 DECKPLAYER_START_TAB=1 ./build/deck-player
echo $?    # 0=全通过，1=有断言失败
```

实现在 `src/selftest/functional_smoke.cpp`。设计要点：

**① 必须走真实按键路径，不能自己调 lambda**

```cpp
Application::injectButton(brls::BUTTON_A);   // 走 handleAction 全链路
```

只有这样才能验证「动作是否真的挂在焦点链上」。这正是原缺陷所在。

**② 断言可观测的状态变化，不是「有没有崩」**

```cpp
size_t before = stackDepth();
injectButton(BUTTON_A);
check("按 A 之后压入了播放页", stackDepth() > before);
```

**③ 失败以非零码退出**，供 CI 与 `verify_ui.py` 捕获。

#### 验证方式（必须做）

还原 `selectTab` 的焦点修复后，冒烟立即报：

```
✗ 按 A 之后压入了播放页  栈深 1 -> 1
✗ 播放页稳定存在  深度=1
失败 3 / 7
```

**这一步不能省。** 不做就不知道新测试有没有用——本项目已经踩过三次
「测试恒真」的坑（第 26 轮 ZIP、第 27 轮视频、第 32 轮并发）。

#### 教训

| 层次 | 检查手段 | 能发现什么 |
| :--- | :--- | :--- |
| 崩溃 | 退出码、ASan | 程序是否活着 |
| 渲染 | 像素统计、抓帧 | 界面是否画出来 |
| **功能** | **注入操作 + 断言状态** | **操作是否真的生效** |

前两层必要，但都不足以证明软件可用。
每加一类检查都要问：**它能发现哪一类用户抱怨？**

---

### README 从未被核对过（第 40 轮）

`check_docs.py` 从第 28 轮起只读 `docs/使用指南.md`。**README 完全没被检查过。**

结果 README 里写着：

```
- **多档倍速**：支持 0.5x ~ 3.0x 追剧循环调节
```

而代码是 `{1.0, 1.25, 1.5, 2.0, 0.75}`，即 **0.75x ~ 2.0x**。

用户照着 README 以为能调到 3 倍速，实际按 R3 循环五档就回到起点。

#### 为什么没被发现

| 检查 | 读的文档 |
| :--- | :--- |
| `check_docs.py`（旧） | 只读 `docs/使用指南.md` |
| — | README 无人核对 |

而且 README 的写法是自然语言「0.5x ~ 3.0x」，没有反引号包裹，
即使有人顺手 grep 也不容易对上。

#### 修复

1. README 改为显式序列：`1.0x → 1.25x → 1.5x → 2.0x → 0.75x`
2. `check_docs.py` 新增 README 核对：

```python
# 同一份 README 里可能多处提到倍速（特性列表 + 按键表），
# 每一处都必须与代码一致——不能只看第一处。
seqs = re.findall(r"([0-9.]+x(?:\s*(?:→|->)\s*[0-9.]+x)+)", text)
for one in seqs:
    got = [float(x) for x in re.findall(r"([0-9.]+)x", one)]
    if got != vals: ...
```

**验证方式**：改错任意一处，立即报
`README [1.0, 1.5, 2.0] / 代码 [1.0, 1.25, 1.5, 2.0, 0.75]`。

#### 这是同一类问题的第三次

| 轮次 | 漏掉的原因 |
| :--- | :--- |
| 28 | 只检查源码，不看文档 |
| 38 | 只核对字面量，核对不了「行为承诺」 |
| 40 | 只核对一份文档，漏了另一份 |

**教训**：每次扩大核对范围，都会立刻发现新的不一致。
「已经核对过了」不等于「全都核对过了」——要明确**核对了什么、没核对什么**。

### 通读用户指南发现的功能缺口（第 38 轮）

此前对文档只做**逐项数值核对**（`check_docs.py`），本轮第一次**从用户视角通读**。
结果发现一个文档承诺了、代码却没实现的功能。

#### 缺陷：选过的字幕轨不会被记住

用户指南第 197 行写着：

> 选过的字幕轨会被记住，下次播放同一文件时自动恢复。

实际测试（双字幕轨 MKV，英文 #1、中文 #2）：

```
第 1 次（强制选 #2）: 自动选中字幕轨 #2
                        历史记录: ...|2|1|0    ← sid=2 确实存了
第 2 次（正常打开）:   自动选中字幕轨 #1       ← 又变回第一条
```

**历史里存了 `sid`，但打开文件时从不读回来。**

`HistoryRecord` 有 `sid` / `aid` 字段，`updateProgress()` 也一直在写，
唯独 `openFile()` 里没有恢复逻辑，`selected_sid_` 只在用户手动选轨时
才被赋值（默认为 0，走「启用字幕」分支后固定选 `subs.front()`）。

#### 修复

```cpp
// openFile() 里
subtitle_chosen_ = false;
HistoryRecord rec;
if (PlaybackHistory::getInstance().getRecord(path, rec)) {
    selected_sid_ = rec.sid;
    selected_aid_ = rec.aid;
} else {
    selected_sid_ = 1;
    selected_aid_ = 1;
}
```

并在 `FILE_LOADED` 分支里优先用 `selected_sid_` 匹配实际轨道。

修复后：

```
第 1 次: 自动选中字幕轨 #2
第 2 次: 恢复轨道 sid=2 aid=1 -> 自动选中字幕轨 #2
```

#### 为什么之前的检查没抓到

`check_docs.py` 检查的是「文档里写的按键/数值/文件是否与代码一致」。
「选过的轨道会被记住」是一句**行为承诺**，没有可对照的字面量，
所以这项检查天然覆盖不到。

**教训**：文档核对有两层——

| 层次 | 工具 | 抓什么 |
| :--- | :--- | :--- |
| 数值/字面量 | `check_docs.py` | 按键名、秒数、文件名 |
| **行为承诺** | **人工通读 + 实测** | 「会自动恢复」「不会中断」这类描述 |

第二层目前无法自动化，只能靠人读。「会记住」「不会丢失」这类句子
最容易写成愿望而不是事实。

#### 补充的自动化检查

```bash
python3 tests/tools/verify_ui.py --only sid    # 2 项
```

- 可以选中非默认字幕轨
- 重新打开时恢复上次选择的轨道

**验证方式**：把恢复逻辑改回 `if (false && ...)`，
测试立即报「恢复为 #1（期望 #2）」。

#### 通读中确认无误的部分

- 水平滑动「整屏宽度 = 半个影片时长（最少 30 秒）」——
  代码是 `span = max(duration * 0.5, 30.0)`，一致
- 文件名清洗的三个示例，实测输出与文档**逐字相同**：
  ```
  The.Matrix.1999.1080p.BluRay.x264.mkv -> 「The Matrix」年份 1999
  流浪地球.2019.1080p.mkv              -> 「流浪地球」年份 2019
  [Group] Some Anime - 01 [1080p].mkv  -> 「Group Some Anime 01」年份 无
  ```
- 「音轨选择里有『静音』」——选择音轨 0 即静音，确实存在
- 断点续播的 95% / 30 秒双条件、15 秒跳转门槛

### 全页面对话框渲染验证（第 36 轮）

第 35 轮发现播放器内对话框被视频盖住后，本轮把**其余所有对话框**也验了一遍。

#### 结论：只有播放器内的受影响

| 页面 | 对话框 | 结果 |
| :--- | :--- | :--- |
| 播放器 | 播放列表 / 音轨 / 字幕 | 曾被视频盖住，第 35 轮已修 |
| 文件浏览器 | 移除文件夹确认 | **正常**（边缘 52.7→17.6，中心 52.9→57.6） |
| 插件管理 | 插件目录提示 | **正常**（边缘 56.8→17.6，中心 45.6→70.8） |

播放器之外的页面不使用 `postDrawHook`，所以没有这个问题。

#### 两种对话框的判据不同

**播放器内**（视频可能盖住 UI）：
```python
b_dlg > b_base * 3 and b_dlg > 0.3    # 亮像素必须显著增加
```
实测 1.36% vs 0.071%，19 倍。

**非播放器页**（对话框会压暗背景）：
```python
a_on < a_off * 0.6 and c_on > c_off   # 边缘变暗 且 中心变亮
```
对话框有半透明遮罩，背景变暗才是「真的显示了」。
只比亮像素会误判——文件浏览器那组亮像素反而**几乎不变**（3.01%→3.00%），
因为遮罩压暗和面板变亮互相抵消。

#### 新增测试钩子

```bash
DECKPLAYER_OPEN_PLAYLIST=1     # 播放器：打开播放列表
DECKPLAYER_OPEN_DIALOG=folder_options   # 文件浏览器：文件夹选项
DECKPLAYER_OPEN_DIALOG=media_info       # 文件浏览器：影片信息
DECKPLAYER_OPEN_DIALOG=1                # 插件管理页：插件目录
```

钩子内部统一用「后台线程 sleep + brls::sync」，且**延迟必须很短**：
早期写 300ms，而 framegrab 200 帧只需 0.2 秒，程序早就退出了，
测出来永远是「对话框没显示」——差点误判成 bug。

#### 顺便修的两处

**① `verify_ui.py` 失败时看不到是哪一项**
只打印「失败 1 / 55」，不知道是哪个。已加失败明细：

```
失败项明细：
  - [对话框] 文件浏览器对话框显示（背景被压暗）  边缘 52.8→52.7
```

**② `PluginManagerActivity` 析构没清存活标志**
新增测试钩子后需要，顺手补上（同第 31/32 轮的教训）。

#### 关于一次偶发失败

全量跑时出现过一次「失败 1 / 55」，之后连续 3 次全绿，
dialog/perf 组各单独压测 3–4 次也全绿，**没能复现**。

已加失败明细输出，下次出现可直接定位。**不排除是显示器休眠导致的抓帧异常**
（本项目历史上多次因此误判）。

### 播放器内对话框被视频盖住：修复（第 35 轮）

**按 ↑/X/Y 打开对话框时，用户什么都看不到。**

#### 现象

新增播放列表界面后，日志显示对话框确实打开了：

```
showPlaylistDialog: 列表 4 个
播放列表对话框已打开
```

但抓帧分析：

| | 亮像素 | 唯一色 | 中心均值 |
| :--- | :--- | :--- | :--- |
| 不打开对话框 | 0.071% | 334 | 85.2 |
| 打开对话框 | 0.071% | 334 | 85.2 |

**完全一样。** 对话框渲染了，但被盖住了。

#### 根因

两个机制撞在一起：

1. mpv 画面在 `postDrawHook`（`nvgEndFrame()` 之后）glBlit 到帧缓冲，**画满整个窗口**
2. `Dialog::open()` 的实现是 `Application::pushActivity(new Activity(dialog))`

对话框压栈后，**播放器视图仍在栈里**，`draw()` 仍在执行，
`postDrawHook` 依然生效 —— 于是每一帧都把对话框整屏盖掉。

#### 修复

在 `postDrawHook` 里判断本视图所属 Activity 是否仍是栈顶：

```cpp
brls::Activity* mine = this->getParentActivity();
brls::Activity* top = brls::Application::getTopActivity();
if (mine && top && top != mine) {
    return;   // 被对话框或其它 Activity 覆盖，不画视频
}
```

`activitiesStack` 是 private，因此在 borealis 里新增了只读接口
`getTopActivity()`（见 `external/borealis/PATCHES.md` 修改 5）。

修复后：有对话框 1.36% vs 无 0.071%，**相差 19 倍**。

#### 影响范围比想象的大

音轨选择（X）、字幕设置（Y）用的是同一个 `brls::Dialog` 机制，
**它们此前应该也是看不见的**——只是从来没有人验证过。

`verify_ui.py` 之前有 41 项检查，**没有任何一项打开过对话框**。

#### 教训

第 19 轮修「视频被 nanoVG 覆盖」时，我只验证了视频能显示，
没想过反过来「视频会不会覆盖别的 UI」。同一个 `postDrawHook`
既有「让视频显示出来」的正面作用，也有「盖住一切后绘制的 UI」的负面作用。

**新增一种全屏绘制手段时，必须问：它会不会盖住什么？**

### 产物级许可证审计 `license_audit.py`（第 34 轮）

**构建期探测不够，必须审计最终产物。**

#### 两种检查的区别

| 检查 | 时机 | 对象 |
| :--- | :--- | :--- |
| `cmake/license_probe.cpp` | 构建期 | 构建机上的 FFmpeg |
| `license_audit.py` | 构建后 | **产物实际依赖的那个 FFmpeg** |

构建期探测只能证明「编译时看到的是干净的」，但产物最终链到哪个
`libavutil` 取决于动态库解析。`license_audit.py` 顺着依赖闭包走到底，
对每个 `libavutil` 用 `ctypes` 直接调 `avutil_license()` 判定。

```bash
python3 tests/tools/license_audit.py           # 自测模式
python3 tests/tools/license_audit.py --strict  # 发布模式，GPL 则退出码 1
```

实测本机：

```
libavutil.61.dylib -> GPL version 3 or later
结论：存在 GPL 依赖，不可分发
```

检查项：libmpv 是否动态链接、`mpv_create` 是否只是未定义符号
（若是静态链接就不会出现在未定义表里）、依赖闭包里每个 avutil 的许可证。

#### 修复 LGPL 构建脚本的三个隐患

**① pkg-config 会静默回退到系统路径**

原脚本：

```bash
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}" meson setup ...
```

看起来是「优先用 $PREFIX」，但 **pkg-config 即使不设这个变量也会搜索
默认路径**（`/opt/homebrew/lib/pkgconfig` 等）。如果 $PREFIX 里的 LGPL
FFmpeg 没装好，mpv 会「成功」链上 Homebrew 的 GPL 版 —— **而且没有任何报错**。

这是本项目最危险的失败模式：构建成功、能跑，产物却是 GPL。

修复：清空默认搜索路径，只允许 $PREFIX：

```bash
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig"
```

这样 LGPL FFmpeg 缺失时构建会**立刻失败**，而不是静默降级。

**② 只验证了 FFmpeg，没验证 libmpv 实际链到哪个**

加了一段产物级复核：找出生成的 libmpv，`otool -L` / `ldd` 查它的
`libavutil` 路径，**必须位于 $PREFIX 内部**，否则直接退出并说明原因。

**③ 没有清理逻辑**

重复运行会复用已解压但可能半途失败的状态。

#### `release_check.py` 增加发布阻断项

`产物可闭源分发` 这一项**默认按失败处理**（不是警告）：

```
✗ 产物可闭源分发（发布阻断项）  (依赖链含 GPL 组件，发布前必须用 LGPL 版重建)
发布前检查未通过：1 / 30
```

早期我把它写成「✓ 警告：仅限自测」——那是错的。发布检查里出现
「能跑但只能自测」的东西必须让整体变红，否则 CI 会放过去。

日常开发用 `--dev` 豁免：

```bash
python3 tests/tools/release_check.py         # 发布模式，GPL 阻断，退出码 1
python3 tests/tools/release_check.py --dev   # 开发模式，退出码 0
```

### 发布前清单 `release_check.py`（第 33 轮）

**代码能跑 ≠ 能发布。** 本轮把「装出来能不能用」逐条验证了。

```bash
python3 tests/tools/release_check.py          # 27 项
python3 tests/tools/release_check.py --keep   # 保留安装目录供检查
```

#### 发现的三个真实问题

**① 图标路径尺寸与实际不符**

```cmake
install(FILES resources/icon/icon.png
        DESTINATION share/icons/hicolor/512x512/apps   # ← 声明 512
        RENAME org.deckplayer.DeckPlayer.png)
```

而 `icon.png` 实际是 **256×256**。按 freedesktop 规范，目录名必须与
图片真实尺寸一致，否则桌面环境会拒绝加载或把图标拉伸模糊。

已改为 `256x256`，并加了检查项（读 PNG 头验证宽高）。

**② `LICENSE-curl-MIT.txt` 没有随安装分发**

`THIRD-PARTY-NOTICES.md` 引用了它，但 `install(FILES ...)` 里漏了。
libcurl 是 MIT 许可，**要求随附版权声明**——这是合规问题，不是小事。

**③ 安装规则本身没错，但无法在 macOS 上验证**

安装规则写在 `if (UNIX AND NOT APPLE)` 里，macOS 上什么都不装。
`release_check.py` 的处理办法是：临时把条件改成 `if (UNIX OR APPLE)`，
安装、验证、**然后在 finally 里还原**。

**这个「临时改条件」非常危险**：如果脚本异常退出没还原，就会把
macOS 专用的构建产物混进 Linux 发布。因此用 `try/finally` 保证还原。

#### 验证覆盖

| 类别 | 检查项 |
| :--- | :--- |
| 安装 | 可执行文件、资源目录、字体、图标资源都已安装 |
| 桌面集成 | desktop 必填项、`Exec` 指向真实文件、`Icon` 指向真实图标 |
| 图标规范 | **路径尺寸 = 图片真实尺寸** |
| AppStream | metainfo 可解析、必填字段、`launchable` 与 desktop 同名 |
| 许可证 | `THIRD-PARTY-NOTICES` 引用的每个 LICENSE 文件都随包分发 |
| 安装后启动 | 从**任意目录**启动安装后的二进制，能定位资源并渲染界面 |

最后一项最关键：模拟的是用户真实场景——从桌面启动器点图标，
工作目录不是源码目录。

#### 验证方式

改错图标路径 → 立即报 `路径 512x512 / 图片 256x256`；
删掉 curl 许可证分发 → 立即报 `缺失 ['LICENSE-curl-MIT.txt']`。
两种都能被准确捕获，恢复后全绿。

### 长时间稳定性与线程安全（第 32 轮）

**修掉一个会在 UI 层卡死的死锁隐患，并做了循环压力验证。**

#### 缺陷 ①：`PlaybackHistory` 完全没有加锁

```cpp
std::unordered_map<std::string, HistoryRecord> records_;   // 裸表
```

`updateProgress()` 由主循环调用（音频 tick、播放进度），
`getRecord()` / `getRecentList()` 由 UI 线程调用，**两者并发访问同一张
unordered_map**。并发写会破坏哈希桶链表，症状是偶发崩溃或读到脏数据。

修复：加 `std::recursive_mutex`。

**为什么必须是 recursive**：`updateProgress()` 持锁时会调用 `save()`，
而 `save()` 也要加锁。普通 `std::mutex` 会**自死锁**（同一线程重入）。

```cpp
mutable std::recursive_mutex mutex_;
```

#### 缺陷 ②：`PluginBrowserActivity` 同样是单标志

与第 31 轮修的 `FileBrowserActivity` 完全相同的模式：

```cpp
alive_flag_ = alive;   // 每次刷新都覆盖，先前任务的标志永远为 true
```

连续按 Y 刷新时，先前的回调会在 Activity 销毁后继续执行。
一并改成 `std::vector<shared_ptr<atomic<bool>>> alive_flags_`。

**这个教训值得重复**：同一个 bug 模式在两个 Activity 里各出现一次，
说明当时只修了发现的那一处，没有全局搜索同类写法。

#### 循环压力测试：378 轮全部正常

20 分钟内反复「启动 → 切标签/播放/退出」，每轮约 3 秒：

```
总轮数: 378
非零退出: 0
崩溃: 0
```

覆盖面：四个标签页轮换、音频播放、视频播放、启动即退出。

单次运行不崩溃说明不了什么，**反复启动才能暴露「只有特定顺序才触发」
的问题**——第 31 轮那两个 use-after-free 正是这类。

已加入验证脚本：

```bash
python3 tests/tools/verify_ui.py --only soak    # 默认 12 轮
DECKPLAYER_SOAK_ROUNDS=50 python3 tests/tools/verify_ui.py --only soak
```

#### 长时间单次播放：内存稳定

30 秒/集的四集连播，运行 6.5 分钟：

```
t=30s  RSS=188MB  CPU=4.3%    ← 首集加载，峰值
t=60s  RSS=119MB  CPU=4.6%    ← 缓存回收后
t=240s RSS=119MB  CPU=3.1%
t=390s RSS=119MB  CPU=2.7%    ← 完全稳定，零增长
```

播完 4 集后停留在播放器界面，内存不再变化。

#### 一个诚实的说明：并发测试不等于竞态检测

我给 `PlaybackHistory` 写了并发读写用例，但**验证时发现它测不出竞态**：

把锁完全去掉、把写入量从 2000 次提到 50000 次，测试依然全绿。
unordered_map 的数据竞争往往只是让个别条目丢失，不一定会当场崩溃。

已在测试文件里如实标注：

```cpp
// 注意：这是一条【冒烟测试】，不是真正的竞态检测。
// 实测把锁去掉后，即使 5 万次并发写入也不会让测试失败。
// 真正能发现竞态的是 ThreadSanitizer，
// 但本项目在 macOS 上跑不起来。
```

**不要因为它「通过」就认为没有数据竞争。** 这次的加锁修复是基于
代码审查得出的，不是测试验证的。

### 内存安全（ASan 全流程）：已验证并修复两个 use-after-free（第 31 轮）

**在 AddressSanitizer 构建下跑全部核心流程，抓出两个真实的内存错误。**

#### 缺陷 ①：`DynamicBox` 析构时写入已销毁的 Activity

第 23 轮为解决「TabFrame 会 delete 旧 Tab」引入的 RAII 包装：

```cpp
sources_container_ = new DynamicBox([this]() {
    sources_container_ = nullptr;    // ← 捕获 this
});
```

当 **Activity 先销毁、DynamicBox 后析构**时，这个 lambda 里的 `this`
已经是野指针。ASan 堆栈：

```
deck_player::FileBrowserActivity::createSourcesView()::$_0::operator()()
  <- std::function<void()>::operator()()
  <- deck_player::DynamicBox::~DynamicBox()
```

**修复**：回调只捕获「指针槽位的 shared_ptr」，不捕获 Activity。
槽位生命周期独立于 Activity，Activity 销毁后再执行也只是给
仍存活的槽位赋 nullptr：

```cpp
class DynamicBox : public brls::Box {
public:
    using Slot = std::shared_ptr<brls::Box*>;
    DynamicBox(Slot slot) : slot_(std::move(slot)) {}
    ~DynamicBox() override { if (slot_) *slot_ = nullptr; }
private:
    Slot slot_;
};
```

#### 缺陷 ②：存活标志被覆盖（见「陷阱 3」）

#### 为什么必须单独跑 ASan

Release 构建下这两个 use-after-free **完全看不出来**：内存还没被复用，
写进去也不会崩。只有 ASan 能稳定暴露。

已加入验证脚本：

```bash
python3 tests/tools/verify_ui.py --only asan    # 10 项
```

覆盖：启动、四个标签页、插件页、视频、字幕、音频、**手势+退出清理**
（后者正是暴露 DynamicBox 问题的路径）。

**验证方式**：把 `DynamicBox` 改回旧写法，ASan 组立即 FAIL
（rc=-6，1 个报告）；恢复后全绿。

构建 ASan 版本：

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1" \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -O1" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
```

#### ASan 报告被截断的坑

默认输出只打印 3 行（`ERROR` + `WRITE of size 8`），没有堆栈。
这是符号化过程本身崩溃导致的。解法：

```bash
ASAN_OPTIONS="fast_unwind_on_fatal=1:symbolize=0"
```

拿到裸地址后用 `atos` 手动符号化：

```bash
atos -o build-asan/deck-player -arch arm64 0x10008ebcc 0x10008eb0c ...
```

### 性能与 CPU 占用：已验证并修复严重缺陷（第 30 轮）

**发现并修复了一个会直接毁掉掌机续航的问题。**

#### 缺陷：主循环全速空转，CPU 稳定在 85%

纯启动、不做任何操作的实测：

```
空闲界面 CPU: 71.5%
再等 5 秒:    86.5%
```

对掌机来说这意味着一两小时耗尽电量、风扇狂转、机身发烫。

#### 根因：macOS 上 GLFW 的 vsync 不生效

borealis 靠 `swapInterval=1`（垂直同步）限帧。独立测试验证：

```bash
$ /tmp/vsync_test
swapInterval=1 -> 2856 FPS      # 窗口不可见
swapInterval=0 -> 1882 FPS

$ /tmp/vsync_vis                 # 窗口可见
swapInterval=1 -> 3354 FPS      # 依然不生效
swapInterval=0 -> 3615 FPS
```

**在 macOS 上垂直同步根本没有节流效果**，主循环跑满。

#### 修复：主动限帧

```cpp
const char* fps_env = std::getenv("DECKPLAYER_FPS");
const int target_fps = fps_env ? std::max(1, atoi(fps_env)) : 60;
const auto frame_budget = std::chrono::microseconds(1000000 / target_fps);

while (mainLoop()) {
    ...
    auto elapsed = std::chrono::steady_clock::now() - frame_start;
    if (elapsed < frame_budget)
        std::this_thread::sleep_for(frame_budget - elapsed);
    frame_start = std::chrono::steady_clock::now();
}
```

**效果**：

| 场景 | 修复前 | 修复后 |
| :--- | :--- | :--- |
| 空闲 CPU | 85.5% | **4.0%** |
| 播放 720p CPU | 86% | **5.9%** |
| 空闲内存 | 165 MB | 155 MB |
| 播放内存 | 205 MB | 188 MB |

降低约 **20 倍**。用户还可用 `DECKPLAYER_FPS=30` 进一步省电。

#### 内存：2 分钟无泄漏

连续播放 2 分钟，每 10 秒采样：

```
194 → 195 → 195 → 196 → 196 → 196 → 196 → 197 → 197 → 197 → 197 → 197 MB
```

120 秒仅增长 3 MB，属于缓存正常波动，无泄漏迹象。

#### 一个判断错误的纠正

我一度认为「最后一集播完进程不退出」是缺陷。加日志后确认
`popActivity()` 正常返回 `true`——播放器已关闭，**回到文件浏览器**
（命令行启动时 `main()` 先 push 了 `FileBrowserActivity`），
进程继续运行是**预期行为**，不是 bug。

**教训**：没有看界面栈就断言「应该退出」，是又一次凭直觉下结论。

### 屏幕尺寸适配：已验证（第 29 轮）

**界面布局与分辨率无关，视频宽高比在任何屏幕比例下都正确。**

新增 `DECKPLAYER_WINDOW=宽x高` 可强制窗口尺寸，便于验证：

```bash
DECKPLAYER_WINDOW=1280x800 ./build/deck-player   # Steam Deck 原生分辨率
```

实测五种尺寸（640×360 / 1024×600 / 1280×720 / 1280×800 / 1920×1080）：

| 尺寸 | 亮像素 | 布局形状与 1280×800 差异 |
| :--- | :--- | :--- |
| 1280×800（基准） | 2.83% | — |
| 1024×600 | 2.37% | 0.48 个百分点 |
| 640×360 | 2.88% | 0.58 个百分点 |
| 1920×1080 | 3.15% | 0.33 个百分点 |

「布局形状」= 把宽度归一化后切成 20 段，比较各段亮像素占比。
差异 < 1 个百分点说明相对布局完全一致，只是等比缩放。

#### 宽高比处理：精确正确

| 场景 | 实测 | 理论 |
| :--- | :--- | :--- |
| 4:3 视频 @ 1280×800 | 宽 1066 | 800×4/3 = 1066（偏差 0%） |
| 16:9 视频 @ 1280×800 | 高 719 | 1280×9/16 = 720，上下黑边各 40 |

#### 设置窗口尺寸的正确做法

**不能直接改 `Application::windowWidth/Height`** —— 会被 `createWindow()`
内部用 `VideoContext::sizeW/H` 覆盖（实测三种尺寸抓出来的帧都是 1280×720）。

正确做法是拿到 GLFW 窗口句柄：

```cpp
auto* vc = dynamic_cast<brls::GLFWVideoContext*>(
    brls::Application::getPlatform()->getVideoContext());
glfwSetWindowSize(vc->getGLFWWindow(), w, h);
brls::Application::setWindowSize(w, h);   // 同步 windowScale
```

#### 又一次「探测器误导」

我一开始用 24×64 的 ASCII 缩略图看 1024×600 的画面，判断
「布局错乱、右侧大片空白」。**这个判断是错的。**

改用「按宽度归一化后逐段比较」才发现两个尺寸的分布几乎相同
（3.20/4.82/0.74/2.98 对 3.78/5.31/1.05/2.78），布局一直是正常的。

**教训**：ASCII 缩略图分辨率太低，只能看「有没有东西」，
不能用来判断布局是否正确。要判断布局，必须用可量化的指标。

### 文档与实现一致性检查 `check_docs.py`（第 28 轮）

用户指南写错比代码 bug 更伤——用户照着做不通，会以为软件坏了。
本轮逐条核对文档，并固化成脚本：

```bash
python3 tests/tools/check_docs.py      # 38 项
python3 tests/tools/check_docs.py -v   # 附源码位置
```

#### 核对出并修正的两处错误

**① 「断点续播」说明写错了**

原文：「只有播放超过 15 秒的影片才会记录进度」。

实际代码：

```cpp
if (!rec.completed && rec.position > 15) resume_pos = rec.position;
```

进度**一直会记录**，15 秒是**重新打开时是否自动跳转**的门槛
（避免刚开头几秒的误触被当成断点）。已改正。

**② 数据文件表漏了 `plugin_config.dat`**

用户备份配置目录时容易漏掉插件服务器地址与账号密码。

#### 脚本的关键设计：必须读两边

第一版只检查源码，不读文档。把文档里的「快进 10 秒」改成「15 秒」，
**测试依然通过**——因为它根本没看文档。

正确做法是文档与代码双向对照：

```python
m = re.search(r"快退\s*/\s*快进\s*(\d+)\s*秒", doc_text)
doc_step = m.group(1)                       # 文档说几秒
check(f"seek(-{doc_step})" in src_text)     # 代码里是否一致
```

倍速序列、LT/RT 步长都改成这种双向比对。
验证方式：改文档后脚本必须变红（实测改 10→15 秒、删倍速项都能被抓到）。

**教训**：和第 27 轮的「视频检查是假的」是同一类问题——
**验证工具本身也要能被证伪**，只看单边的检查等于没检查。

### 端到端验证脚本 `verify_ui.py`（第 27 轮）

前九轮的验证都是零散的手工命令，本轮固化成可重复脚本：

```bash
python3 tests/tools/verify_ui.py            # 全部 23 项检查
python3 tests/tools/verify_ui.py --only video
python3 tests/tools/verify_ui.py --list
```

覆盖：基础渲染、四标签页、视频画面、内嵌字幕、音频/后台播放、
五项手势、自动连播、插件加载与页面。**实测 23 项全过，约 90 秒。**

#### 写这个脚本时发现的两个真实问题

**① 字幕被底部控制栏遮挡（已修复）**

字幕默认贴画面最底部，正好落在 110px 高的 OSD 控制栏下面：

| 状态 | 字幕白像素 |
| :--- | :--- |
| OSD 隐藏 | 3676（清晰可读） |
| OSD 显示 | **236**（几乎不可见） |

而 OSD 在操作后会显示数秒，用户看到字幕的时机恰好被压住。

修复：抬高字幕位置，避开控制栏。

```cpp
mpv_set_option_string(mpv_, "sub-pos", "80");
mpv_set_option_string(mpv_, "sub-margin-y", "36");
```

修复后 OSD 显示时字幕白像素 **236 → 3890**。

**② 检查本身可能是假的**

第一版 `check_video` 用固定阈值「彩色像素 > 20%」。
把 `mpv_render_context_render` 整个注释掉（视频完全不渲染）后，
**测试依然通过**——因为 UI 自身的进度条、按钮就有色彩。

改为与基线对比：先打开音频文件抓一张「纯 UI」的图，再和视频帧比：

```
彩色 71.5% vs 基线 1.4%
唯一色 67992 vs 基线 1165
```

这样破坏渲染后测试立刻变红（0.0% vs 基线 1.4%）。

字幕检查同理：加了 `DECKPLAYER_SUBTITLE_OFF` 开关做对照组，
有字幕 3890 / 关闭 210，两者相差近 20 倍才算通过。

**教训**：验证脚本必须自己也能失败。写完一定要故意破坏被测功能，
确认报告变红；否则只是在打印「通过」。

#### 脚本里踩的坑（都写进注释了）

| 现象 | 原因 |
| :--- | :--- |
| 日志里什么都看不到 | `subprocess` 用管道时 C 库全缓冲，进程被 kill 后缓冲丢失。必须 `stdbuf -o0` 或重定向到文件 |
| 连播检查只播 1 集 | `FRAMEGRAB_START` 是**帧数**不是秒；本机约 1200 帧/秒，900 帧只有 0.74 秒。要按墙钟时间等待 |
| 连播只播 1 集（续） | 测试素材全放在同一目录，`color.mp4` 等 20 秒视频被一起加入列表。系列素材要放独立子目录 |

### 刮削展示与 ZIP 安装：已验证并修复关键缺陷（第 26 轮）

**「装插件 → 刮削 → 界面显示」的用户旅程已闭环。**

实测（自动触发刮削）：

```
自动刮削: Interstellar.2014.1080p.mkv
→ metadata.dat: 演示影片 | 剧情简介 | 8.5 | 120分钟 | 剧情,科幻
→ 视频库条目: meta=标题=演示影片 评分=8.500000
```

#### 缺陷：ZIP 安装的插件永远不会被加载

安装器把插件解压到 `plugins/<包名>/` **子目录**，但扫描器只看顶层文件：

```cpp
for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;   // ← 目录被直接跳过
    ...
}
```

**后果**：用户从 ZIP 装的插件，装完重启也永远不出现。
这是「创意工坊」功能的一半失效。

**修复**：顶层文件和子目录都扫：

```cpp
if (entry.is_regular_file()) {
    tryLoad(entry.path());
} else if (entry.is_directory()) {
    for (const auto& sub : fs::directory_iterator(entry.path(), ec)) {
        if (sub.is_regular_file()) tryLoad(sub.path());
    }
}
```

实测修复后：`plugins 目录清空后安装，加载到 1 个插件`（修复前为 0）。

#### 测试有效性的一个教训

写这条回归测试时踩了三个坑，每个都会让测试「假通过」：

1. **断言太宽**：用 `n >= 1` 判断，但 `plugins/` 下本来就散装着别的插件，
   恒真。必须先把目录清空，让加载结果只可能来自本次安装。
2. **`TEST_END()` 有多个**：用字符串替换插入测试块，插到了错误的位置，
   新用例根本没执行（表现为测试仍是 20 项，没有新增）。
3. **用假 dylib 测加载**：原 `makeGoodZip()` 写的是
   `"fake dylib content"`，`dlopen` 报 “slice is not valid mach-o file”。
   这测的是二进制有效性，不是「扫描器有没有进子目录」。
   必须用真实编译产物构造测试包。

**验证方式**：还原旧扫描逻辑后测试变红，恢复后变绿。
不做这一步就不知道测试到底有没有用。

### 插件系统与创意工坊：已验证并修复关键缺陷（第 25 轮）

**配置 → 插件 → 远程目录 → 界面显示 的完整链路已打通。**

实测（本地 WebDAV 服务，真实 PROPFIND 请求）：

```
plugin: WebDAV 已初始化: http://127.0.0.1:18080 (带鉴权)
list_items('') -> status=0 count=1
  [0] 电影 (/电影)                       ← 目录，类型正确
list_items('/电影') -> status=0 count=1
  [1] Demo Movie 2023.mp4 (/电影/...)    ← 视频，类型正确
```

#### 缺陷：`listVideoSourceItems` 漏调 `init`

这是**插件浏览功能完全不可用**的根因。

`loadAll()` 里正确地做了 `create_video_source()` → `init(配置)`，
但浏览目录走的 `listVideoSourceItems()` 只做了 `create`，**没有 init**：

```cpp
deck_video_source* vs = desc->create_video_source();
if (!vs) return false;
// ← 这里少了 init，插件拿不到 base_url
if (vs->list_items) { ... }   // 永远返回 DECK_ERR_NOT_AVAILABLE
```

表现：插件管理页能看到插件、显示"已配置"，但点进去永远空白。

**修复**：对新建实例补上同样的 `init`：

```cpp
if (vs->init) {
    const std::string plugin_id = fs::path(p.path).stem().string();
    const std::string cfg = PluginConfigStore::getInstance().toJson(plugin_id);
    g_current_plugin_id = plugin_id;
    deck_status st = vs->init(vs->impl, cfg.c_str(), &g_host_api);
    g_current_plugin_id.clear();
    if (st != DECK_OK) { ...; return false; }
}
```

#### 新增测试 `test_scraper`

覆盖两条路径：

| 用例 | 说明 |
| :--- | :--- |
| 刮削链路 | `scrapeSearch` → `scrapeForFile`，用 demo 插件离线数据 |
| 插件浏览 | `listVideoSourceItems`，需真实 WebDAV 服务 |

**测试自身会拉起 WebDAV 服务**（`tests/run_tests.sh` 里自动起停），
因此默认 `./tests/run_tests.sh` 就能跑完整链路，无需手工准备环境。

**已验证测试有效**：把 `init` 那段删掉后测试立即 FAIL，
恢复后 PASS。

#### 关于手工写配置文件

调试时**不要手写 `plugin_config.dat`**：程序退出时 `save()` 会用内存
里的内容覆盖文件，手写的内容会被冲掉（实测只剩一条 `url`）。
要么通过 UI/API 写入，要么在程序运行前写好且不触发保存。

### 播放列表自动连播：已验证并修复三个缺陷（第 24 轮）

**三集连续播放正常，每集播满时长、顺序正确。**

```
播放结束: 当前第 1/3 集 -> 自动切换下一集 -> 第 2 集   (3.13s)
播放结束: 当前第 2/3 集 -> 自动切换下一集 -> 第 3 集   (3.02s)
播放结束: 当前第 3/3 集                                (3.02s)
```

#### 缺陷 ①：`keep-open=yes` 抑制了 `MPV_EVENT_END_FILE`

这是连播**完全失效**的根因。

```bash
# 独立验证：keep-open=yes 下等 5 秒
$ /tmp/ko      # keep-open=yes -> "5 秒内没收到 END_FILE"
$ /tmp/ko2     # keep-open=no  -> "END_FILE reason=0 (EOF)"
```

而 `MPVPlayerView` 里正是 `keep-open=yes`（用于停住最后一帧，避免播完黑屏），
于是 `on_finish_cb_` 永远不被调用，自动下一集从未发生过。

**修复**：保留 `keep-open=yes`，改为订阅 `eof-reached` 属性：

```cpp
mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG);
```

属性为真时手动触发结束回调。这样既不黑屏，又能感知播放结束。

#### 缺陷 ②：`END_FILE` 的 reason 必须区分

`loadfile` 替换旧文件时会发 `END_FILE reason=2`（STOP），
这不是「播完了」。若不加区分地当作结束，连播会连锁触发、
把整个播放列表在几毫秒内跳完。

```cpp
const bool is_eof = (ef->reason == MPV_END_FILE_REASON_EOF);  // 0
if (is_eof && !eof_fired_) { ... }
```

#### 缺陷 ③：`pause` 状态会延续到下一个文件

`keep-open=yes` 在播放结束后会把 `pause` 置为 `yes`（停在最后一帧）。
这个状态**会延续到下一次 `loadfile`**，导致新一集加载完却一直暂停：

```
状态: file=ep2.mp4 pause=yes eof=no t=0.0/3.0   ← 卡在第 2 集
```

而且**必须在 `FILE_LOADED` 之后清除**，太早会被 `loadfile` 覆盖：

```cpp
// FILE_LOADED 分支内
mpv_set_property_string(mpv_, "pause", "no");
```

#### 顺带新增：同目录视频自动成列表

用命令行打开某一集时，同目录下其余视频会按文件名排序加入播放列表，
方便「打开一集就顺带播完整个目录」。

### 四个标签页界面：已验证（第 23 轮）

**存储位置 / 视频库 / 音乐库 / 播放历史 四个页面都能正常渲染。**

验证方式是用环境变量指定启动标签，逐个抓帧对比：

```bash
DECKPLAYER_START_TAB=1 DECKPLAYER_FRAMEGRAB=/tmp/t1.ppm \
DECKPLAYER_FRAMEGRAB_START=120 ./build/deck-player
```

四个页面的两两差异为 6.0%–10.3%，确认内容确实不同（不是同一屏）。
侧边栏高亮块随标签移动（存储位置时第 0 项最亮，视频库时第 1 项最亮）。

内容正确性用「配置数据 → 看界面行数变化」验证：

| 场景 | 内容区文本行 |
| :--- | :--- |
| 视频库（空） | 8 |
| 视频库（2 部电影 + 4 个嵌套视频） | 20 |
| 播放历史（2 条记录） | 2 个条目，各占一整行宽 |
| 音乐库（3 首歌） | 3 个条目 |

#### 递归扫描已验证

配置 `/tmp/libtest` 为媒体库后，缓存文件显示扫到了三层嵌套：

```
/tmp/libtest/Movie A 2020.mp4          (根目录)
/tmp/libtest/media/a.mp4               (第 1 层)
/tmp/libtest/media/sub/c.avi           (第 2 层)
/tmp/libtest/media/sub/subsub/d.webm   (第 3 层)
```

#### 解决：如何切换标签页（第 18 轮的遗留问题）

第 18 轮曾尝试用 `onControllerButtonPressed` 自动切标签，失败了。
正确做法是 `TabFrame::selectTab(position)`（本轮新增）。

原因在 `addTab` 的实现：

```cpp
sidebar->addItem(label, [this, creator](brls::View* view) {
    if (!view->isFocused()) return;   // 只有真正获得焦点才加载内容
    ...
});
```

`focusTab()` 只做 `giveFocus()`，不保证回调被触发。
`selectTab()` 封装了这一步并记录当前序号。

### 触控手势：已验证（第 22 轮）

**五项手势全部验证通过。**

验证方式是注入触摸事件直接驱动手势识别器，无需人工点击：

```bash
DECKPLAYER_GESTURE_TEST=1 ./build/deck-player /path/to/video.mp4
```

```
【单击】osd 显示 -> 隐藏                      PASS
【双击】playing 是 -> 否                       PASS
【右半屏上滑】vol 50 -> 100                    PASS
【左半屏下滑】bright 100% -> 41%               PASS
【水平滑动】mpv time-pos 0s -> 12s             PASS
```

#### 关键发现：PanGesture 的 delta 方向与直觉相反

`PanGestureRecognizer` 里：

```cpp
this->delta = this->position - position;   // 上一帧 - 当前帧
```

**delta = 上一帧位置 − 当前帧位置**，而不是常见约定。因此：

| 手势 | delta 符号 |
| :--- | :--- |
| 向右滑 | `delta.x` **为负** |
| 向上滑 | `delta.y` **为正** |

原实现三处都按相反方向处理，导致：

- 向上滑反而**降低**音量/亮度
- 向右滑反而**后退**（且目标被 clamp 到 0，看起来像完全没反应）

这是纯逻辑错误，代码看起来完全合理，**只有真正注入手势才能发现**。

#### 验证方法的要点

`brls::sync` 投递的动作需要延后执行，且**异步 API 的结果不能立即读取**：

```cpp
// seekTo 走 mpv_command_async，立即读 current_position_ 永远是旧值
seekTo(target);
// 必须等事件回来，或直接读 mpv 的 time-pos
char* p = mpv_get_property_string(mpv_, "time-pos");
```

测试里还应避免踩到边界：验证「向上滑增大音量」时，
若起始音量已是 100，怎么滑都不会变，会误判为失败。

**最容易踩的坑：绝不要在 `brls::sync` 的 lambda 里 `sleep`。**

```cpp
// 错误：冻结事件循环 500ms，mpv 的属性事件无处投递
brls::sync([self]() {
    injectTouch(...);
    std::this_thread::sleep_for(500ms);   // ← 主线程被卡住
    check(self->current_position_);       // ← 永远是旧值
});
```

正确做法是拆成两步，用**后台线程**计时：

```cpp
steps.push_back({0,   [self]() { injectTouch(...); }});          // 注入
steps.push_back({700, [self]() { check(...); }});                // 延后校验
std::thread([steps]{
    for (auto& st : steps) {
        sleep_for(st.delay);                 // sleep 在后台线程
        brls::sync([a = st.action]{ a(); }); // 只有动作回到主线程
    }
}).detach();
```

我曾因此误判「seek 手势无效」，实际是测试自己把事件循环堵死了。
与第 20 轮的字幕误判同类：**先怀疑探测器，再怀疑被测对象**。

### 音频播放与后台播放：已验证（第 21 轮）

**音乐能正常解码、推进、记录进度。**

实测（30 秒 mp3）：

```
直接播放音频: /tmp/audtest/song.mp3
macOS: 已抑制 App Nap，后台音频可继续播放
音频状态: pos=7s dur=30s playing=是 title=song.mp3
播放历史: song.mp3|1|30|...|1|1|0
```

**后台播放的架构保证**：
- `AudioPlayer` 是单例，生命周期独立于任何 Activity
- 其 `vid=no` 且不创建 GL 渲染上下文，故不占 GPU 资源
- `tick()` 在主循环里调用，不在界面里

#### 本轮修复的三个真实缺陷

**① 音频文件被错误路由到视频播放器**

`main.cpp` 的命令行播放逻辑对所有媒体都调用 `PlayerActivity`。
音频走了视频路径，退出界面音乐就停 —— 后台播放形同虚设。

修复：按 `MediaType::Audio` 分流到 `AudioPlayer::playPlaylist()`。

**② App Nap 恢复时崩溃（SIGTRAP）**

```
Foundation: _NSSwiftProcessInfo.endActivity(_:)
deck_player: BackgroundPlayback::end()
```

根因：本项目**未启用 ARC**，静态 `id g_activity` 是裸指针。
`beginActivityWithOptions` 返回的对象在 `@autoreleasepool` 结束后即被释放，
再用它调 `endActivity` 必然崩溃。

修复：`begin()` 里显式 `retain`，`end()` 里配对 `release`，
并先把成员置空再调用，避免重入。

**③ 短内容被误判为「已看完」**

```cpp
// 旧逻辑
completed = (position >= duration - 30 || position >= duration * 0.95);
```

当 `duration <= 30` 秒时，`duration - 30 <= 0`，**任何位置都满足**。
实测 30 秒的音频播 1 秒就被标记 `completed=1`，导致它永远无法断点续播。

修复：给「接近结尾」条件加上下限 —— 必须同时播过一半：

```cpp
const bool near_end = (position >= duration - 30) &&
                      (position >= duration / 2);
completed = (duration > 0 && (near_end || position >= duration * 0.95));
```

已添加 `test_playback_history` 回归测试。验证方式：还原旧逻辑后
测试立即变红（2 项 FAIL），恢复后全绿。

**④ 静态析构顺序崩溃（SIGSEGV）**

```
AudioPlayer::~AudioPlayer() -> shutdown() -> PlaybackHistory::updateProgress()
-> unordered_map 访问 -> EXIT_BAD_ACCESS
```

`AudioPlayer` 与 `PlaybackHistory` 都是函数内静态对象，析构顺序不确定。
`main()` 已经显式调用过 `shutdown()`，析构里再调一次就会访问可能已销毁的
`PlaybackHistory`。

修复：`shutdown()` 加 `std::atomic<bool> shut_down_` 幂等标志；
析构函数只 `destroyMPV()`，不再碰其它静态对象。

### 字幕渲染：已验证（第 20 轮）

**内嵌字幕能正常渲染。**

实测（纯黑视频 + 内嵌 subrip 字幕）：

```
自动选中字幕轨 #1
字幕检查: sub-text='SUBTITLE TEST LINE ONE' 底部144行白像素=3676
```

3676 个白像素与独立测试程序的输出完全一致，说明字幕确实画进了视频帧。

#### 必须修复的前置问题：mpv 默认不选字幕

mpv 加载文件后 **`sid=no`**，**不会自动选中任何字幕轨**。
即使字幕轨存在、`sub-visibility=yes`，也不会显示。

对策：在 `MPV_EVENT_FILE_LOADED` 后显式选择第一条字幕轨：

```cpp
auto subs = getTracks("sub");
if (!subs.empty()) selectTrack("sub", subs.front().id);
```

注意 `mpv_set_option_string(mpv_, "sid", "auto")` **无效**，必须显式选。

#### 排查弯路（值得记录的教训）

我一度判定「字幕不显示」并写进了文档，**这个结论是错的**。

错误来源：用 `glReadPixels` 检查像素时，采样区域写成了
`py + ph/8`（GL 坐标偏下），而 GL 原点在**左下**，
该区域实际对应图像**顶部**，字幕在底部自然读不到。

修正为从 `py` 开始读 `ph/5` 行后，立刻读到 3676 个白像素。

**教训**：
1. GL 的 Y 轴与图像文件相反，采样时要格外小心；
2. 在写"这是一个缺陷"之前，先确认自己的探测手段是对的；
3. 用**已知正确的独立测试**做对照，能快速区分「被测对象坏了」
   还是「探测器坏了」。

---

## 相关文档

| 文件 | 内容 |
| :--- | :--- |
| `RELEASE-BLOCKERS.md` | **发布阻断项清单**（上线前必须完成什么） |
| `README.md` | 项目概览、特性、构建、测试入口 |
| `docs/使用指南.md` | **面向用户**的完整操作说明 |
| `THIRD-PARTY-NOTICES.md` | 第三方组件、许可证义务、发布检查清单 |
| `tests/README.md` | 测试框架说明与编写方法 |
| `flatpak/README.md` | Flatpak 构建步骤与合规注意事项 |
| `external/borealis/PATCHES.md` | 对 borealis 的修改记录（Apache-2.0 §4(b) 要求） |
| `examples/webdav_plugin/` | **可工作的插件参考实现**（含真实 WebDAV 验证） |
| `examples/demo_plugin/` | 最小演示插件（假数据，仅验证 ABI 链路） |
| `tests/tools/analyze_framegrab.py` | 帧缓冲分析：验证单帧 UI 是否渲染 |
| `tests/tools/verify_ui.py` | **端到端验证**（渲染/播放/字幕/手势/插件/屏幕/性能/ASan/稳定性/对话框/字幕轨记忆）；`--list` 看分组，`--only <组>` 只跑一组 |
| `tests/tools/check_docs.py` | **文档与实现一致性**（按键/数值/文件/环境变量/脚本路径/README） |
| `tests/tools/release_check.py` | **发布前清单**（安装/desktop/图标/许可证/产物审计）；`--dev` 开发模式只警告 |

> 上面三套的**项数不写死**：脚本每次运行都会打印自己的总数，
> 文档里写数字必然过期（实测已漂移过三次）。
| `tests/tools/license_audit.py` | **产物级许可证审计**（顺依赖链查 GPL） |
| `tests/tools/goal_check.py` | **目标符合性检查**（需求 → 代码位置） |
| `docs/AUDIT.md` | 子代理审计流程 + 每轮发现记录（**历史复盘在这里**） |
| `docs/UI-DESIGN.md` | UI/交互设计规格（尺寸、色板、OSD 布局） |
| `docs/LINUX-BUILD.md` | Linux / SteamOS 构建与运行时依赖 |
| `android/README.md` | Android 构建前置（NDK 版本、SDL2 源码） |

---

## 改动原则

1. **改动前先读相关模块**，特别是有「陷阱清单」标记的地方
2. **不要移除许可证检查、ABI 版本校验、Zip Slip 防护**——它们是有意为之
3. **不要假设有网络**：沙箱环境通常无外网，依赖下载的步骤会失败
4. **测试要测真实代码**，不要复刻一份实现来测（本项目踩过这个坑）
5. **验证要能失败**：写测试后故意改坏源码确认测试会红，否则不知道测试有没有用
6. 保持与现有代码风格一致
