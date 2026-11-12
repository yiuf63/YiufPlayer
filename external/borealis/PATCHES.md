# Borealis 修改记录

本目录下的 Borealis 为 Muvik 定制版本，基于
https://github.com/xfangfang/borealis 修改。

依据 Apache License 2.0 第 4(b) 条，此处声明对原始代码的修改内容。

原始许可证见同目录 `LICENSE`。

---

## 修改 1：GLFW 无显示器时的空指针崩溃

**文件**：`library/lib/platforms/glfw/glfw_video.cpp`

**问题**：`glfwGetVideoMode()` 在无可用显示器时返回 `NULL`（例如显示器休眠、
无头会话、远程桌面断开）。原代码未做判空即解引用 `mode->redBits` 等字段，
导致启动阶段直接 `SIGSEGV`（实测 macOS：`KERN_INVALID_ADDRESS at 0x100`）。

**修改**：在 `GLFWVideoContext::GLFWVideoContext()` 中增加判空，并提供
1920x1080@60 的默认 `GLFWvidmode` 作为回退值。

## 修改 2：全屏切换的空指针崩溃

**文件**：`library/lib/platforms/glfw/glfw_video.cpp`

**问题**：`GLFWVideoContext::fullScreen()` 在两个分支中同样对
`glfwGetVideoMode()` 的返回值未判空。

**修改**：
- 进入全屏时若无可用显示器，记录警告并保持窗口模式返回。
- 退出全屏时若无合法 `mode`，仅恢复窗口而不访问 `mode->width` 等字段。

---

以上修改均为健壮性加固，未改变 Borealis 的公开 API 或行为语义。

## 修改 3：新增 `postDrawHook` 帧末绘制回调

**文件**：
- `library/include/borealis/core/application.hpp`
- `library/lib/core/application.cpp`

**背景**：Borealis 的绘制流程是
`nvgBeginFrame() → view->frame() → nvgEndFrame()`。
nanoVG 是**命令缓冲**，`nvgEndFrame()` 时才真正提交 GL 命令。
因此若在 `view->draw()` 内直接调用 OpenGL 渲染（例如 libmpv 的
`mpv_render_context_render`），其输出会被随后提交的 nanoVG 命令覆盖，
表现为该区域一片纯色。

**修改**：在 `Application` 中新增静态成员 `postDrawHook`（`std::function<void()>`），
在 `nvgEndFrame()` 之后、`VideoContext::endFrame()`（交换缓冲）之前调用。

```cpp
nvgEndFrame(Application::getNVGContext());

if (Application::postDrawHook)
    Application::postDrawHook();

Application::platform->getVideoContext()->endFrame();
```

**兼容性**：默认值为 `nullptr`，不设置时行为与原始版本完全一致，
对上游代码零影响。

**限制**：单个回调，只能服务一个需要帧末绘制的模块。
若将来需要多路同时输出，应改为回调列表。

---

## 修改 4：TabFrame 新增 `selectTab()` / `getActiveIndex()`

**文件**：
- `library/include/borealis/views/tab_frame.hpp`
- `library/lib/views/tab_frame.cpp`

**背景**：`TabFrame` 没有「以编程方式切换到第 N 个标签」的公开接口。
现有的 `focusTab(position)` 只是 `Application::giveFocus(sidebar->getItem(position))`，
而 `addTab` 注册的切换回调要求目标项**真正获得焦点**才会加载内容 ——
仅移动焦点在某些时序下不足以触发内容切换。

这导致无法用脚本验证各标签页的渲染，也无法在崩溃恢复后回到上次的标签。

**修改**：

```cpp
void selectTab(int position);   // 移动焦点到指定标签，触发内容加载
int  getActiveIndex() const;    // 返回当前标签序号，无激活时为 -1
```

`getActiveIndex()` 的值在 `addTab` 的内容创建回调里同步更新，
用私有成员 `activeIndex` 保存，不引入额外状态查询。

**兼容性**：纯新增接口，不影响现有调用方的行为。

---

## 修改 4b：`selectTab()` 把焦点继续送入内容区

**文件**：`library/lib/views/tab_frame.cpp`

**背景**：上游 `selectTab()` 只把焦点移到 `SidebarItem` 上，
而 `SidebarItem` 的 A 键动作是「向右导航」（`sidebar.cpp:72`）。
结果用户切到某标签后按 A，只是把焦点移进列表，**必须再按一次**
才真正选中条目 —— 表现为「点视频没反应」。

**修改**：内容加载后把焦点交给内容区的默认焦点。

```cpp
Application::giveFocus(item);
if (this->activeTab)
    if (View* target = this->activeTab->getDefaultFocus())
        Application::giveFocus(target);
```

**兼容性**：只影响 `selectTab()` 的调用方（本项目与验证脚本）。
侧边栏仍可用 LEFT 键返回。

---

## 修改 6：内存安全修复（`application.cpp` / `view.cpp` / `tap_gesture.cpp`）

**背景**：点击列表项偶发 `EXC_BAD_ACCESS`。根因分散在多处，
共同点是「事件/焦点持有的裸指针在视图销毁后仍被解引用」。

**修改**：

1. **已销毁视图登记表** `Application::markViewDead/isViewDead`
   - `View::~View()` 登记地址；手势回调、`giveFocus`、`processInput`
     解引用前查表。
   - 不能用 `view->getParent()` 之类的方法做检查 ——
     在已释放对象上调用任何方法都是未定义行为。

2. **`giveFocus(nullptr)` 现在真的会清空焦点**
   - 上游把赋值关在 `newFocus != nullptr` 分支里，
     导致 `View::~View()` 的焦点自查成为死代码，`currentFocus` 永久悬垂。
   - 同时 `oldFocus` 解引用前增加存活检查。

3. **`frame()` 与 `popActivity()` 的裸解引用**
   - `currentFocus->frameHighlight()`、`focusStack.back()`
     均可能指向已销毁视图，补判死。

4. **`processInput()` 复用触摸起点前查存活**
   - START 时保存的 `TouchState::view` 在 END 前可能已被
     `clearViews()` 销毁并 `delete`。

5. **新增 `Application::injectButton()`**
   - 供功能冒烟测试注入真实按键（走 `handleAction` 全链路）。

**兼容性**：全部为防御性改动，不改变正常路径行为。

---

## 修改 5：`Application::getTopActivity()`（`application.hpp`）

**日期**：随项目第 35 轮提交

**动机**：`Dialog::open()` 的实现是 `pushActivity(new Activity(dialog))`。
对话框打开后，原先的播放器视图**仍在 Activity 栈里**，其 `draw()` 仍在执行，
`postDrawHook` 依然生效。而 mpv 的画面是直接 glBlit 到帧缓冲、画满整个窗口的，
会在 `nvgEndFrame()` 之后把包括对话框在内的整套 UI 整屏盖掉。

实测：按 ↑ 打开播放列表，对话框确实已 `open()`（日志可见），
但画面亮像素仅 0.07%，与不开对话框时完全相同 —— 用户完全看不到任何反应。
音轨、字幕等所有播放器内对话框都受此影响。

修复需要在 `postDrawHook` 里判断「本视图所属 Activity 是否仍是栈顶」，
但 `activitiesStack` 是 private 成员，业务代码无法访问。

**修改**（纯新增只读接口）：

```cpp
static Activity* getTopActivity()
{
    return activitiesStack.empty() ? nullptr : activitiesStack.back();
}
```

**兼容性**：纯新增接口，不改变任何现有行为。

**验证**：`tests/tools/verify_ui.py --only dialog`
——有对话框 1.36% vs 无对话框 0.071%（19 倍差异），已能失败验证。

---

## 修改 7：EditTextDialog 内置手柄屏幕虚拟键盘（Full Controller Support）

**文件**：
- `library/include/borealis/views/edit_text_dialog.hpp`
- `library/lib/views/edit_text_dialog.cpp`
- `library/lib/platforms/glfw/glfw_ime.cpp`
- `library/lib/platforms/sdl/sdl_ime.cpp`
- `library/lib/platforms/desktop/steam_deck.cpp`

**背景**：
在掌机或大屏电视连接手柄的场景下，原版 `EditTextDialog` 仅包含文本显示框与按键提示，缺少屏幕软键盘控件，完全依赖实体键盘或外部系统输入法。在无键盘的纯手柄环境下（如客厅电视、Steam Deck 桌面模式或非 Steam 环境），用户无法完成任何文本输入（搜索元数据、配置 WebDAV 路径等），无法满足 Steam 官方“完全支持控制器”（Full Controller Support）的硬性准则。此外，原版 `steam_deck.cpp` 硬编码了 `/home/deck/.steam/steam.pipe` 路径，在非 deck 用户名或非 SteamOS 环境下均会静默失效。

**修改**：
1. **多模式屏幕虚拟键盘**：在 `EditTextDialog` 内部集成 QWERTY 软键盘容器，提供三种布局模式（小写字母/数字、大写字母/数字、特殊符号与 Web 常用短语），每个按键均为可聚焦视图。
2. **手柄专属键位映射**：
   - 方向键 / 左摇杆：在虚拟键盘键位间离散导航焦点；
   - Ⓐ 键：输入高亮键位的字符；
   - Ⓑ 键：取消并关闭对话框；
   - Ⓧ 键：退格删除（全局生效，无需移动焦点）；
   - Ⓨ 键：快捷插入空格（全局生效）；
   - LB / RB 键：左/右移动文本光标；
   - START 键（☰）/ 点击「完成」：提交输入。
3. **IME 抽象桥接**：在 `GLFWImeManager` 与 `SDLImeManager` 中对接软键盘派发的 `insertTextEvent`、`clearEvent` 与 `cursorMoveEvent`，实体键盘输入、剪贴板粘贴与手柄软键盘无缝并存。
4. **Steam Pipe 多路径兼容**：在 `steam_deck.cpp` 中自适应探测 `$HOME/.steam/steam.pipe`、`$HOME/.local/share/Steam/steam.pipe`、`/home/deck/.steam/steam.pipe` 与 `/tmp/steam.pipe`，并在管道不可达时自动回退为命令行拉起 Steam OSK。

**兼容性**：不破坏实体键盘原有按键输入与粘贴逻辑，对上层业务调用完全透明。

---

## 修改 8：滚动视图焦点自愈与空焦点死锁修复（Focus Self-Healing & Natural Scrolling Focus Fix）

**文件**：
- `library/lib/views/scrolling_frame.cpp`
- `library/lib/views/tab_frame.cpp`
- `library/lib/core/application.cpp`
- `library/include/borealis/core/logger.hpp`

**背景**：
在手柄控制模式下，原版存在多处致命的焦点丢失逻辑缺陷，导致控件完全无法被手柄选中：
1. **findTopMostFocusableView 误判不可聚焦元素**：原版仅校验 `inscribed(frame)`，未校验 `focusCheck->isFocusable()`。当列表顶部为不可聚焦元素（如标题栏 `SectionHeader` 或空白提示 `EmptyState`）时，会将其误判为焦点返回。随后的 `giveFocus` 将其子树默认焦点（nullptr）赋给 `currentFocus`，导致焦点直接被置空。
2. **ScrollingFrame 自然滚动边界过度约束**：原版使用全包围 `inscribed()`，导致横向百分比卡片由于滚动条宽度或亚像素舍入被判定为「出界」，每帧强行触发焦点重夺循环。
3. **Application::navigate 无焦点自愈能力**：原版 `navigate()` 当 `currentFocus == nullptr` 时直接退出，一旦任何界面切换或销毁导致焦点短暂置空，手柄方向键和摇杆立刻变砖，永久卡死在无焦点死角。
4. **giveFocus 传入非聚焦视图清空焦点**：原版在传入非空但不可聚焦的视图时会将 `currentFocus` 冲为 nullptr。

**修改**：
1. **findTopMostFocusableView 深度寻焦**：增加 `!focusCheck->isFocusable()` 循环下探，只有找到真正可聚焦控件才返回；未找到时回退到 `contentView->getDefaultFocus()`，绝不返回不可聚焦控件。
2. **ScrollingFrame 垂直可见性判定**：垂直滚动容器只判定垂直边界（带 1px 浮点容差），并在 `getDefaultFocus()` 中优先返还内容区可聚焦首项。
3. **navigate 焦点全自动自愈**：当 `currentFocus == nullptr` 时，方向键与摇杆导航自动从栈顶 Activity 获取默认焦点接管，彻底杜绝死锁。
4. **giveFocus 防御性守卫**：传入非空但不可聚焦视图时不破坏已有焦点。
5. **popActivity 焦点栈健全性加固**：清理已销毁的失效焦点栈项时防止 double pop，且在焦点栈为空或失效时，向待展示 Activity 请求 `getDefaultFocus()` 自动接管，确保退出弹窗或二级页面后焦点必能复位。
6. **Logger 日志即时刷新**：`fflush(logOut)` 扩大至全平台，使 systemd/journal 管道日志无延迟输出。

---

## 修改 9：TabFrame 标签文本动态更新（setTabLabel）

**文件**：
- `library/include/borealis/views/tab_frame.hpp`
- `library/lib/views/tab_frame.cpp`

**背景**：
在运行时动态切换语言（i18n）时，`TabFrame` 左侧侧边栏已添加的标签文本需要随之刷新。原版没有暴露修改已添加条目文本的接口。

**修改**：
- 增加 `TabFrame::setTabLabel(int position, const std::string& label)`，通过 `sidebar->getItem(position)->setLabel(label)` 动态更新侧边栏对应标签的显示文本。

---

## 修改 10：侧边栏与单元格标签单行约束与越界保护（singleLine 保护）

**文件**：
- `library/lib/views/sidebar.cpp`
- `library/lib/views/cells/cell_detail.cpp`
- `library/lib/views/cells/cell_radio.cpp`

**背景**：
1. `SidebarItem`、`DetailCell` 和 `RadioCell` 的高度是固定行高（`@style/brls/sidebar/item_height`，如 56px），且无剪裁区域。
2. 原始 XML 模板中子项 `Label` 均未声明 `singleLine="true"`。
3. 当多语言（如英文 History、Settings 或长标题）文本宽度超过条目可用可用宽度时，Label 测算判定 `isWrapping = true`，自动将超长文字换行至第 2 行。
4. 由于固定行高并未随之扩展，折到第 2 行的文字直接在当前卡片下方绘制，竖向侵入下方下一个条目（例如 History 折下来的字母 'y' 绘制在 Settings 区域内）。

**修改**：
- 在 `sidebarItemXML` 中的 `brls/sidebar/item_label` 显式增加 `singleLine="true"`。
- 在 `detailCellXML` 中的 `title` 与 `detail` 显式增加 `singleLine="true"`。
- 在 `radioCellXML` 中的 `title` 显式增加 `singleLine="true"`。
- 结合 Borealis Label 的 `autoAnimate` 机制：当文本超出可用宽度时，失焦状态安全显示省略号（`...`），获得焦点时自动平滑左右跑马灯滚动，从根本上杜绝竖向折行越界重叠。

---

## 修改 11：支持全量 16 个 GLFW 控制器插槽与加载 Steam `SDL_GAMECONTROLLERCONFIG`

**文件**：
- `library/lib/platforms/glfw/glfw_input.cpp`

**背景**：
1. **插槽上限截断（GAMEPADS_MAX = 5）**：
   原版 GLFW 输入管理器沿用了 Switch 平台的宏定义 `#define GAMEPADS_MAX 5`，轮询与状态合并仅处理 `0 <= i < 5`。
   在 Linux / Steam Deck 环境下，系统按照 `/dev/input/event*` 顺序扫描输入设备，由于触控屏、陀螺仪、虚拟输入设备等在早期被识别为具有 `EV_ABS` 的摇杆设备，外接手柄（蓝牙或 USB 接入的 Xbox、DualSense、Switch Pro、八位堂等）极易被分配到 `jid >= 5` 的插槽，导致被 Borealis 完全忽略而无法控制。
2. **缺少 Steam 动态控制器映射加载**：
   Steam 在游戏启动以及接入控制器时，会通过环境变量 `SDL_GAMECONTROLLERCONFIG` 动态注入手柄的 SDL 映射。原版仅加载静态 `gamecontrollerdb.txt`，未读取环境变量。若外接手柄或 Steam 虚拟控制器的 GUID 不在静态文件中，`glfwJoystickIsGamepad(jid)` 返回 false，所有按键均读出为 0。

**修改**：
1. 在 `GLFWInputManager` 构造函数中增加从环境变量读取并注入 `SDL_GAMECONTROLLERCONFIG`（通过 `glfwUpdateGamepadMappings`）。
2. 在 `glfwJoystickCallback` 的 `GLFW_CONNECTED` 事件中动态重新加载 `SDL_GAMECONTROLLERCONFIG`，确保热插拔手柄能即时获得按键映射。
3. 将手柄检测和状态轮询上限从硬编码的 `GAMEPADS_MAX`（5）扩展到 `GLFW_JOYSTICK_LAST`（15），遍历全量 16 个插槽并跳过未连接设备（`!glfwJoystickPresent(i)`）。
4. `getControllersConnectedCount()` 改为遍历全量插槽统计真实连接数，避免硬编码断言。
5. 在 `updateControllerState` 中为缺少 SDL Gamepad 映射的手柄（如特定八位堂、D-Input 模式、第三方摇杆）增加底层原始按键、十字方向键 (Hats) 与模拟轴 (Axes) 的通用回退解析，杜绝手柄完全失控的情况。
6. 在 `updateUnifiedControllerState` 中增加手柄输入（按键/摇杆）对 `Application::setActiveEvent(true)` 的调用，确保手柄操作能即时唤醒主循环并维持全帧率交互，不被节能休眠机制挂起。

---

## 修改 12：触摸与按键无缝平滑切换，杜绝手柄/遥控器吞键与失控

**文件**：
- `library/lib/core/application.cpp`

**背景**：
原版 Borealis（基于 Nintendo Switch 触屏+JoyCon 体验设计）在检测到触摸或鼠标指针移动时，会将 `InputType` 置为 `InputType::TOUCH`。
随后当用户按下手柄按键（`BUTTON_A`）或使用方向键导航（`navigate`）时，原版逻辑在 `handleAction` 与 `navigate` 中直接执行了 `return false;` 或 `return;`，将这一次输入指令完全丢弃（仅用来切换输入状态模式）。
在 macOS / 桌面多外设环境（如轻微鼠标抖动、触控板静电微电平）或 Android 电视遥控器交互场景中，这会导致手柄/遥控器的第一次甚至连续多次按键被静默丢弃（表现为按键无响应、手柄变砖、OSD 消失后按手柄无法唤醒）。

**修改**：
- 在 `Application::handleAction` 与 `Application::navigate` 中，当检测到从 `InputType::TOUCH` 切换为按键交互时，将输入模式恢复为 `InputType::GAMEPAD`，并直接向下继续分发执行该按键或导航动作，严禁丢弃用户按键。

---

以上修改均为健壮性加固或可选的扩展点，未改变 Borealis 现有公开 API 的语义。
