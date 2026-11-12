# 发布阻断项清单 / Release Blockers

> **这份文档回答一个问题：现在能不能发布？**
>
> 答案是：**不能。** 下面是必须由**有网络环境的人**完成的事项。
> 每一项目前都有自动化检查守着，做完之后检查会自己变绿。

---

## 项目规模

| 类别 | 规模 |
| :--- | :--- |
| 应用代码 | 7867 行（C++17 / Objective-C++） |
| 测试与工具 | 3009 行（C++ 单测 + Python 脚本） |
| 插件示例 | 1220 行（C ABI 参考实现） |
| 文档 | 3036 行 |
| 对 Borealis 的修改 | 5 处（全部记录在 `external/borealis/PATCHES.md`） |

---

## 当前状态速览

本项目以 **GPL-3.0** 分发（2026-10-29 决定，此前是闭源商业）。
因此「必须自建 LGPL 版 libmpv」**已不再是阻断项**——链接常见的 GPL 版 FFmpeg 完全合法。

| 类别 | 状态 | 阻断发布 |
| :--- | :--- | :---: |
| 功能实现 | 全部完成并有实测证据 | — |
| 自动化测试 | 8 套单测 + 端到端 + 47 项文档一致性 + 30 项发布检查 + 16 项目标覆盖（各项数以脚本输出为准） | — |
| **源码可获取（GPL-3.0 §6）** | **未公开**（当前是私有仓库） | **是** |
| **Flatpak 构建** | **未执行**（需 flatpak-builder + 网络） | **是** |
| **Steam Deck 实机** | **未验证** | **是** |
| 人工验收 | 未进行 | 是 |

一条命令看当前状态：

```bash
python3 tests/tools/release_check.py        # 发布模式，退出码 0 才能发
```

---

## 阻断项 1：源码必须可获取（GPL-3.0 §6）【必须】

### 为什么阻断

GPL-3.0 允许**收费分发**，但要求同时提供完整对应源码。
当前仓库 `https://github.com/yiuf63/muvik` 是**私有**的，
外部使用者拿不到源码 —— 只要对外分发二进制就违规。

> 注意：本项目此前是闭源商业授权，这条要求当时不存在；
> 改成 GPL-3.0 之后它才成为发布的前提条件。

### 怎么做

```bash
gh repo edit yiuf63/muvik --visibility public   # 或 --accept-visibility-change-consequences
```

如果暂时不想公开仓库，就**不要对外分发二进制**，只在内部自测。

### 验收标准

```bash
gh repo view yiuf63/muvik --json visibility    # 必须是 public
# 且产物依赖能在公开的构建里复现：
python3 tests/tools/license_audit.py                 # 列出产物实际链接的库
```

---

## 可选：构建 LGPL 版 libmpv（不是阻断项）

GPL-3.0 之下链接 GPL 版 FFmpeg 合法，所以这一步**只在想让产物更小
（不带 `--enable-gpl` 组件）时才需要**，不做也能发布。

### 怎么做

```bash
# 需要网络（下载 FFmpeg 与 mpv 源码）+ meson + ninja + pkg-config
./scripts/build-lgpl-libmpv.sh /opt/muvik-lgpl

# 用产出的 LGPL 版重新构建
cmake -B build-release \
      -DCMAKE_BUILD_TYPE=Release \
      -DMPV_ROOT=/opt/muvik-lgpl \
      -DCMAKE_PREFIX_PATH=/opt/muvik-lgpl
cmake --build build-release -j
```

### 验收标准（只在做这一步时才需要）

```bash
python3 tests/tools/license_audit.py --strict     # 退出码 0（产物不含 GPL 组件）
python3 tests/tools/release_check.py              # 退出码 0
./build-release/muvik                       # 能正常播放
```

**注意**：脚本已内置两道防线，不要绕过它们——

- `configure` 明确带 `--disable-gpl --disable-version3 --disable-nonfree`
- pkg-config 被锁定在 `$PREFIX` 内（`PKG_CONFIG_LIBDIR`），
  一旦 LGPL FFmpeg 缺失会**立刻失败**，而不是静默链回系统 GPL 版

如果构建成功但 `license_audit.py --strict` 仍报 GPL，说明有别的路径混进来了，
先查 `otool -L`（macOS）或 `ldd`（Linux）的完整依赖链。

---

## 阻断项 2：补全 Flatpak manifest 的哈希 【必须】

### 为什么阻断

`flatpak/org.muvik.Muvik.yaml` 里有两处占位符：

```
sha256: REPLACE_WITH_REAL_SHA256_OF_FFMPEG_7.1
sha256: REPLACE_WITH_REAL_SHA256_OF_MPV_0.39.0
```

`flatpak-builder` 会因校验失败中止。这是**刻意留空**的——
不想让手写的错误哈希被误当成真值。

### 怎么做

```bash
./scripts/update-flatpak-sources.sh    # 拉取源码并填入真实 sha256
flatpak-builder --user --install --force-clean build-flatpak \
    flatpak/org.muvik.Muvik.yaml
```

### 验收标准

```bash
# 1. 无占位符残留
grep -c "REPLACE_WITH_REAL" flatpak/*.yaml     # 必须为 0

# 2. 记录 Flatpak 内 FFmpeg 的实际许可证
#    不要用 strings 抓 "GPL version" —— FFmpeg 源码里也有这个字面量，
#    会误判。正确做法是调用官方 API avutil_license()。
#    GPL-3.0 之下 GPL 或 LGPL 都可接受，但必须**如实写进 THIRD-PARTY-NOTICES.md**，
#    因为分发时要按 §6 提供对应源码。
flatpak run --command=sh org.muvik.Muvik -c \
  'python3 -c "
import ctypes, glob
so = glob.glob(\"/app/lib/libavutil.so*\")[0]
lib = ctypes.CDLL(so)
lib.avutil_license.restype = ctypes.c_char_p
print(lib.avutil_license().decode())
"'
# 把输出结果登记到 THIRD-PARTY-NOTICES.md

# 3. 能启动并播放
flatpak run org.muvik.Muvik /path/to/video.mp4
```

---

## 阻断项 3：Steam Deck / SteamOS 实机验证 【必须】

### 为什么阻断

本项目**从未在 Steam Deck 上运行过**。macOS 上的所有结论都是推导，
以下各项在 Linux/SteamOS 上**完全未验证**：

| 项 | 风险 |
| :--- | :--- |
| 手柄按键映射 | macOS 用 GLFW 手柄后端，SteamOS 走 Steam Input，映射可能不同 |
| 触控手势 | Steam Deck 触屏的坐标/压力与 macOS 不同 |
| GL 渲染路径 | macOS 是 Metal/OpenGL 兼容层，SteamOS 是原生 Mesa GL |
| 资源路径 | 已在 macOS 上验证过「从任意目录启动」，但 Steam 启动器的工作目录行为未知 |
| 音频后端 | macOS 用 CoreAudio，SteamOS 用 PulseAudio/PipeWire |
| 硬件解码 | 未验证 VAAPI |
| 休眠唤醒 | 掌机频繁睡眠，唤醒后 mpv/GL 上下文是否存活未知 |

### 怎么做

```bash
# 1. 装完后先看资源定位
MUVIK_FRAMEGRAB=/tmp/g.ppm \
MUVIK_FRAMEGRAB_START=120 \
  ./muvik
# 日志里应有：资源目录: /path/to/resources

# 2. 跑端到端验证（Linux 上帧缓冲读取走同一套 glReadPixels）
python3 tests/tools/verify_ui.py

# 3. 手柄逐项试：A / 十字键 / LT RT / LB RB / X / Y / R3 / L3 / ↑ / START
#    对照 docs/使用指南.md 的按键表

# 4. 触控手势：单击 / 双击 / 左半屏上下滑 / 右半屏上下滑 / 水平滑
```

### 验收标准

- `verify_ui.py` 在实机上全绿（或至少 `basic/tabs/video/subtitle/audio/gestures` 全绿）
- 手柄 14 个按键全部符合文档
- 五项触控手势全部符合文档
- 连续播放 1 小时不崩溃、内存不持续增长
- 合盖休眠 → 唤醒后能继续播放

---

## 阻断项 4：人工验收 【必须】

以下是我**无法自动验证**的，需要人实际操作：

| 项 | 怎么看 |
| :--- | :--- |
| 画面流畅度 | 播放 30fps / 60fps 视频，肉眼有无卡顿、撕裂 |
| 音画同步 | 说话场景是否对得上 |
| 字幕可读性 | 字号、位置、与画面亮度对比是否合适 |
| 手势手感 | 滑动灵敏度（当前按屏幕高度比例）是否顺手 |
| OSD 停留时长 | 操作后控制栏显示多久消失，是否合适 |
| 扫描大目录 | 添加 `~/Videos` 之类大目录，观察扫描耗时与进度提示 |
| 刮削真实效果 | 用真实影视文件测文件名解析是否准确 |
| 插件真实场景 | 用真实 WebDAV / 网盘插件浏览、播放 |

**这些不是"锦上添花"，是产品能不能用的判断依据。** 自动化只能证明「没坏」，
证明不了「好用」。

---

## 已完成、无需重复的部分

以下均有实测证据，发布前**不需要**重做（除非改了相关代码）：

| 项 | 证据 |
| :--- | :--- |
| 界面渲染 | `verify_ui.py` 基础组，读 GL 帧缓冲 |
| 视频播放 | 彩色像素 71.5% vs UI 基线 1.4%，唯一色 67542 vs 1317 |
| 内嵌字幕 | 有字幕 3890 白像素 vs 关闭 210 |
| 音频 / 后台播放 | 播放推进、进度落盘、App Nap 抑制 |
| 触控手势 | 五项注入测试全部 PASS |
| 四标签页 + 递归扫描 | 四页差异 6.0%–10.3%，扫到三层嵌套 |
| 自动连播 | 三集连续，每集播满时长 |
| 插件系统 | 真实 WebDAV PROPFIND，目录与视频列举正确 |
| ZIP 安装 | 真实编译产物构造的包，装完能被加载 |
| 性能 | 空闲 CPU 4.0%、播放 5.9%、内存 155MB |
| 内存安全 | ASan 全流程 10 项干净（修过 2 个 use-after-free） |
| 稳定性 | 378 轮启停无崩溃，6.5 分钟内存零增长 |
| 多分辨率 | 640×360 ~ 1920×1080 布局一致，4:3 视频零变形 |
| 对话框渲染 | 三个页面各自验证 |

---

## 发布前最后一遍检查

```bash
# 全部五套（各项数以脚本自身输出为准；下面只写组名）
./tests/run_tests.sh                    # 8 套单元测试
python3 tests/tools/verify_ui.py        # 端到端（渲染/播放/字幕/手势/插件…）
python3 tests/tools/check_docs.py       # 文档与实现一致性
python3 tests/tools/release_check.py    # 发布前清单（发布模式）
python3 tests/tools/goal_check.py       # 需求覆盖

# 许可证
python3 tests/tools/license_audit.py     # 列出产物实际依赖（GPL 下不再阻断）
```

**全部退出码为 0 才能发布。** 任何一项失败都不要跳过——
这些检查存在的意义就是拦下「看起来能用但不能发」的构建。

---

## 已知但暂不阻断的问题

| 问题 | 影响 | 说明 |
| :--- | :--- | :--- |
| `network_provider.dylib` 被拒绝加载 | 无 | 旧的 C++ ABI 插件，loader 正确拒绝。属预期行为 |
| ThreadSanitizer 未跑 | 未知 | macOS 上跑不起来，建议在 Linux 补做数据竞争检测 |
| 一次未复现的测试失败 | 未知 | 全量跑时出现过 1 次「失败 1 / 55」，之后 7 次全绿。已加失败明细输出 |

最后一项值得说明：**我没有把它当成已解决**。如果它再次出现，
`verify_ui.py` 现在会打印具体是哪一项、实际数值多少。

---

## 文档索引

| 需要什么 | 看哪里 |
| :--- | :--- |
| 用户怎么用 | `docs/使用指南.md` |
| 怎么构建 | `README.md` |
| 项目约定与陷阱 | `AGENTS.md` |
| 第三方许可证义务 | `THIRD-PARTY-NOTICES.md` |
| 对 Borealis 的修改 | `external/borealis/PATCHES.md` |
| Flatpak 怎么构建 | `flatpak/README.md` |
| 怎么给项目写测试 | `tests/README.md` |
| 怎么给项目写插件 | `examples/webdav_plugin/` |
