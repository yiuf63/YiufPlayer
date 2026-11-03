# Borealis 修改记录

本目录下的 Borealis 为 DeckPlayer 定制版本，基于
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

以上修改均为健壮性加固或可选的扩展点，未改变 Borealis 现有公开 API 的语义。
