# 交接文档 —— 给下一个开发 agent

> 这份文档回答三个问题：**项目现在到哪了、有哪些只有踩过才知道的坑、接下来该做什么。**
> 写于 2026-11-05，交接时 `main` 分支 HEAD = `61d09da`，全部验证绿灯。

---

## 0. 项目速览

**Muvik**：面向 Steam Deck / 通用掌机 / 桌面的音视频播放器，
C++17 + [Borealis](https://github.com/xfangfang/borealis) + libmpv。
GPL-3.0 开源（2026-10-29 起，此前闭源商业），**最终目标是发布**。

范围共识（用户原话定义，不要自行扩大或收窄）：
手柄 + 触控优先；常见视频/字幕格式与内嵌字幕；音乐播放 + 后台播放；
MX Player 式媒体文件夹扫描；创意工坊式插件（视频源 + 刮削源）；
**不假定是 Steam Deck、不限制屏幕尺寸**（含 Android、开源掌机）。

平台现状：macOS arm64 是唯一可实测环境；Linux/SteamOS 有容器构建；Android 目录存在但未验证。

---

## 1. 必读文档地图（按优先级）

| 文档 | 作用 | 什么时候读 |
| :--- | :--- | :--- |
| **`AGENTS.md`** | **必读**。构建方法、架构、**26 条陷阱清单**（全部是实际踩过的）、验证证据 | 接手第一天通读 |
| `RELEASE-BLOCKERS.md` | 「现在能不能发布？」——阻断项清单与守门检查 | 规划发布时 |
| `docs/UI-DESIGN.md` | 唯一视觉标准：色板/间距/字号 token、三套主题（深/OLED/浅） | 做任何 UI 改动前 |
| `docs/使用指南.md` | 面向用户的按键/手势/功能说明 | 改交互时核对口径 |
| `external/borealis/PATCHES.md` | 我们对 borealis 的 5 处修改及理由 | 升级 borealis 前 |
| `web/ui-review/` | 浏览器里的可交互 UI 原型（1:1 逻辑像素），产品方向的来源 | 讨论新界面时 |
| `docs/HANDOVER.md` | 本文档 | 你正在读 |

**历史会话**：更早的开发记录在 `~/.dsh/sessions/--Users-yiuf-code-muvik--/`
（DeepSeek harness，`session.v4.jsonl.zstd`，`zstd -d` 解开）。云端 conversation_search 查不到这部分。

---

## 2. 当前进度快照（2026-11-05）

### 最近提交

| 提交 | 内容 |
| :--- | :--- |
| `61d09da` | 浅色主题：kLight 色板 + 设置页外观切换（六项原型功能收尾） |
| `1d9d0ac` | 播放页 OSD 压成两行布局（底栏 238 → 168） |
| `215b7b3` | 媒体库视图菜单 + 平铺网格 + 搜索 + 抽帧管线（原型第二阶段落地） |
| `d94577f` | 媒体库加文件夹层：四宫格目录卡 + 最近添加 |
| `aaf987c` | 实机对齐原型第一步：侧栏收窄到 160，四页改为媒体库/文件/历史/设置 |

### 验证状态（交接时全绿）

- `verify_ui.py`：basic 3 项 / tabs 2 项 / functional 9 项 / osd 5 项 全过
- `check_docs.py`：文档与实现一致 47/47
- `release_check.py`：**29 / 30**（唯一红项是口径过时的许可证检查，见 §6 P0）
- 功能冒烟（手柄路径）：进文件夹 / 压入播放页 / 返回 全过；冷缓存 6 连过

### 本阶段落地的六项原型功能（全部完成）

1. **视图菜单**（分组/内容/排序/筛选 + 升降序，持久化到 `library_view.conf`）—— 页内浮层 `ViewSheet`，不是 Activity
2. **平铺网格**（`LibraryGroup::Flat`，绕过文件夹层直接渲染全部媒体）
3. **搜索**（媒体库页顶搜索框，实时过滤）
4. **抽帧缩略图**（可选链接 FFmpeg → avformat seek 至时长 1/10（上限 120s）→ decode → swscale → RGBA 缓存；编译宏 `MUVIK_HAVE_FFMPEG`）
5. **OSD 两行布局**（主控块一行 + 进度条一行，`toolCY = h-124`、`barCY = h-58`，底栏高度 238 → 168）
6. **浅色主题**（kLight 色板 + `ThemeKind::Light` + 设置页「外观 → 主题」切换；OSD 恒暗）

---

## 3. 关键设计决定（改动前先理解为什么）

### 浮层架构：页内 Box，绝不用 Activity

borealis 的 Activity 栈**只渲染栈顶**，pushActivity 做浮层会得到纯黑背景。
`ViewSheet`（`src/view/view_sheet.cpp`）是页内绝对定位 Box：铺满宿主 + 自绘遮罩 +
`removeView(this, false)` 防自删。以后所有「在当前页上盖一层」的需求都走这个模式。

### 主题解析链

`MUVIK_THEME` 环境变量 > `<configDir>/theme` 文件（设置页写入，重启生效）> 默认 Dark。
`ui::palette()` **只跟 `resolveThemeKind()` 走**，不读 borealis 的 ThemeVariant；
`applyMuvikTheme()` 把选中的色板**同时写进 borealis 深色和浅色两个主题**，
防止系统浅色模式把界面染回 Switch 青绿。三套色板：kDark / kOled / kLight（在 `src/ui/design.cpp`）。

### 自绘 vs borealis 组件的分界

文字/颜色/间距一律走 `ui::palette()` / `ui::space` / `ui::radius` / `ui::font`，
禁止字面量（AGENTS.md 硬性规则 3）。borealis 侧边栏/按钮的度量覆盖集中在
`src/theme/deck_theme.cpp`（侧栏 160px 等，数值与 web/ui-review 原型一一对应）。

---

## 4. 本阶段新踩的坑（已沉淀为 AGENTS.md 陷阱 19–26）

这六条+两小条是上一阶段（媒体库/视图菜单/浅色主题）开发中实锤的，
**每一条都造成过真实的挂死、死循环或静默失效**：

1. **Activity 栈只渲染栈顶** → 浮层用页内绝对定位 Box（陷阱 19）
2. **`isViewDead` 地址登记表假阳性**：`deadViews` 只增不减，堆地址复用导致
   giveFocus 静默失败；已改 View 构造时 `unmarkViewDead(this)`（陷阱 20）
3. **递归锁死锁**：`requestBatch` 持锁调 `filePath()`；持锁只能调 `*Unlocked()` 函数（陷阱 21）
4. **抽帧失败重建死循环**：失败 → 重建 → 重试；`g_failed` 兜底（陷阱 22）
5. **ScrollingFrame 抢焦点**：未布局视图（0×0）被判出界；borealis 已加 `focusUnlaidOut`（陷阱 23）
6. **自绘按钮宽度粗估**：中文胶囊全截成「按文…」；必须 `ui::textWidth` 实测（陷阱 24）
7. **回调里 hide() 自己 = UAF**：`removeView(this, false)`（陷阱 25）
8. **抓帧时序**：`MUVIK_FRAMEGRAB_AT_MS` 控制抓帧时刻，产物是 PPM 要转 PNG（陷阱 26）

borealis 的对应修改（unmarkViewDead / focusUnlaidOut / ViewSheet 相关）都在
`external/borealis/PATCHES.md` 有记录，**升级 borealis 时别丢**。

---

## 5. 日常验证工作流（改完代码跑这个）

```bash
# 1. 编译
cmake --build build -j8

# 2. UI 回归（basic/tabs/functional/osd 最常用；全部组见 --list）
python3 tests/tools/verify_ui.py --only basic
python3 tests/tools/verify_ui.py --only functional
python3 tests/tools/verify_ui.py --only osd

# 3. 文档一致性（改了 UI 或文档必跑）
python3 tests/tools/check_docs.py

# 4. 发布守门（dev 模式豁免许可证项）
python3 tests/tools/release_check.py --dev

# 5. 视觉改动：抓帧目检（产物是 PPM，先转 PNG 再看）
MUVIK_THEME=light MUVIK_OPEN_SHEET=1 \
MUVIK_FRAMEGRAB=/tmp/x.ppm MUVIK_FRAMEGRAB_AT_MS=2500 ./build/muvik
ffmpeg -y -i /tmp/x.ppm /tmp/x.png
```

常用自检环境变量（全集 `grep -rhoE 'MUVIK_[A-Z_]+' src include | sort -u`）：
`MUVIK_OPEN_SHEET`（呼出视图菜单）、`MUVIK_OPEN_SETTINGS`（切设置页）、
`MUVIK_CLICK_FIRST_VIDEO`（注入点击第一项）、`MUVIK_SEARCH=关键词`、
`MUVIK_THEME=dark|oled|light`、`MUVIK_OSD_OFF=1`（像素检查时关 OSD）。

**验证纪律**（AGENTS.md 陷阱 10 的展开）：「功能正常」不能只看退出码和像素统计，
要抓帧目检 + 走真实输入路径（手柄按键/触摸手势），构造出来的捷径测不到真问题。

---

## 6. 待办与优先级

### P0 — 发布口径修正（小改动，先做）

- [ ] **`tests/tools/release_check.py` 的许可证检查口径过时**：还在检查「产物可闭源分发」，
  而项目已是 GPL-3.0，「必须 LGPL 版 libmpv」**已作废**（AGENTS.md 许可证章节、
  RELEASE-BLOCKERS.md 开头都明说了）。应改为「依赖链许可证与 GPL-3.0 分发合规」。
  连带核对 `tests/tools/license_audit.py` 的结论文案。
  这是当前 `release_check.py` 29/30 里唯一红项，**不是真阻断**。

### P1 — 真正的发布阻断项（RELEASE-BLOCKERS.md，需要外部条件）

- [ ] **源码公开**（GPL-3.0 §6）：仓库 `github.com/yiuf63/muvik` 还是私有
- [ ] **Flatpak 构建**：需要 flatpak-builder + 网络（`flatpak/` 目录已有骨架）
- [ ] **Steam Deck 实机验证**：无实机，所有 SteamOS 结论是静态推导
- [ ] **人工验收**：发布前整体过一遍

### P2 — 功能与平台

- [ ] Android 构建/运行验证（`android/`，构建前提见 AGENTS.md「其它平台」三条隐藏坑）
- [ ] 开源掌机（wiliwili 对齐的平台列表）适配验证
- [ ] 创意工坊式插件生态继续完善（视频源 + 刮削源已有 ABI 与示例）
- [ ] 后台音乐播放体验打磨（now_playing_bar 已有雏形）

---

## 7. 给接手 agent 的三句话

1. **先读 AGENTS.md 全文再动手**——26 条陷阱每条都花过真实排查时间，别再踩一遍。
2. **任何 UI 改动以 `docs/UI-DESIGN.md` + `web/ui-review/` 原型为准**，改完抓帧目检，
   不确定就问用户，用户偏好「简洁、面向行动、可直接执行的下一步」。
3. **所有改动往「能发布」的方向靠**：口径统一、验证可跑、阻断项收敛；顺手把新坑写回 AGENTS.md。
