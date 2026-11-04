/*
    Copyright 2019-2021 natinusala
    Copyright 2019 p-sam
    Copyright 2021 XITRIX

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <algorithm>

#include <functional>

#include <borealis/core/activity.hpp>
#include <borealis/core/audio.hpp>
#include <borealis/core/font.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/platform.hpp>
#include <borealis/core/style.hpp>
#include <borealis/core/theme.hpp>
#include <borealis/core/view.hpp>
#include <borealis/core/notification_manager.hpp>
#include <borealis/views/label.hpp>
#include <deque>
#include <vector>

#ifdef __WINRT__
#ifdef main
#undef main
#endif
#define main SDL_main
#endif

namespace brls
{

// Input types for entire app
enum class InputType
{
    // DeckPlayer 补丁：把「手柄」与「键盘」区隔开。
    //
    // 上游只有 GAMEPAD / TOUCH 两种，注释写明 "Gamepad or keyboard"，
    // 于是键盘操作也会被当作手柄：界面底部始终显示 A/B/X/Y 的手柄图标，
    // 与用户实际按的 Enter/Esc 对不上。
    //
    // 新增 KEYBOARD 后：
    //   - 键盘导航时 inputType 变为 KEYBOARD，提示栏显示键盘字符
    //   - 手柄仍有实体输入时切换回 GAMEPAD
    // 默认仍为 GAMEPAD，保持掌机上电即用的行为。
    GAMEPAD,  // 手柄
    KEYBOARD, // 物理键盘
    TOUCH,    // 触摸屏
};

class DebugLayer;
class EditTextDialog;

typedef std::function<View*(void)> XMLViewCreator;

class Application
{
  public:
    friend class EditTextDialog;

    static inline uint32_t ORIGINAL_WINDOW_WIDTH  = 1280;
    static inline uint32_t ORIGINAL_WINDOW_HEIGHT = 720;

    /**
     * Inits the borealis application.
     * Returns true if it succeeded, false otherwise.
     */
    static bool init();

    /**
     * Creates the application window with the given title.
     * Must be called after calling init().
     */
    static void createWindow(std::string title);

    /**
     * Application main loop iteration.
     * Must be called in an infinite loop until it returns false.
     */
    static bool mainLoop();

    static Platform* getPlatform();
    static AudioPlayer* getAudioPlayer();

    static NVGcontext* getNVGContext();
    inline static float contentWidth, contentHeight;

    inline static unsigned windowWidth, windowHeight;

    inline static int windowXPos, windowYPos;

    /**
     * Called by the video context when the content window is resized
     */
    static void onWindowResized(int width, int height);

    /**
     * Do not call this function, use it internally.
     * Called when the video context is ready (to setup the initial content scaling).
     */
    static void setWindowSize(int width, int height);

    /**
     * Called by the video context when the content window is Repositioned
     */
    static void onWindowReposition(int xPos, int yPos);

    /**
     * Do not call this function, use it internally.
     * Called when the video context is ready (to setup the initial window position).
     */
    static void setWindowPosition(int xPos, int yPos);

    static std::vector<Activity*> getActivitiesStack();

    /**
     * Pushes a view on this applications's view stack.
     *
     * The view will automatically be resized to take
     * the whole screen.
     *
     * The view will gain focus if applicable.
     *
     * The first activity to be pushed cannot be popped.
     */
    static void pushActivity(Activity* view, TransitionAnimation animation = TransitionAnimation::FADE);

    /**
     * Pops the last pushed activity from the stack
     * and gives focus back where it was before.
     *
     * return false if no actifity to pop.
     */
    static bool popActivity(
        TransitionAnimation animation = TransitionAnimation::FADE, std::function<void(void)> cb = [] {}, bool free = true);

    /**
     * Gives the focus to the given view
     * or clears the focus if given nullptr.
     */
    static void giveFocus(View* view);

    inline static Style getStyle()
    {
        return brls::getStyle();
    }

    static Theme getTheme();
    static ThemeVariant getThemeVariant();

    static ImeManager* getImeManager();

    // 帧末回调：在 nvgEndFrame 之后、交换缓冲之前执行。
    // 供需要在 nanoVG 之上绘制的模块使用（如 libmpv 视频渲染）。
    static inline std::function<void()> postDrawHook = nullptr;

    /**
     * Loads a font from a given file and stores it in the font stash.
     * Returns true if the operation succeeded.
     */
    static bool loadFontFromFile(std::string fontName, std::string filePath);

    /**
     * Loads a font from a given memory buffer and stores it in the font stash.
     * Returns true if the operation succeeded.
     */
    static bool loadFontFromMemory(std::string fontName, void* data, size_t size, bool freeData);

    /**
     * Returns the nanovg handle to the given font name, or FONT_INVALID if
     * no such font is currently loaded.
     */
    static int getFont(std::string fontName);

    static int getDefaultFont();

    static void notify(const std::string& text);

    static void onControllerButtonPressed(enum ControllerButton button, bool repeating);

    static void onKeyboardPressed(BrlsKeyCombination key, bool repeating);

    /**
     * "Crashes" the app (displays a fullscreen CrashFrame)
     */
    static void crash(std::string text);

    static void quit();

    /**
     * Blocks any and all user inputs
     */
    static void blockInputs(bool muteSounds = false);

    /**
     * Unblocks inputs after a call to
     * blockInputs()
     */
    static void unblockInputs();

    static bool isInputBlocks();

    static const ControllerState& getControllerState();

    static void setCommonFooter(std::string footer);
    static std::string* getCommonFooter();

    inline static float windowScale;

    /**
     * Sets whether BUTTON_START will globally be used to close the application.
     */
    static void setGlobalQuit(bool enabled);

    static void setFPSStatus(bool enabled);
    static bool getFPSStatus();
    static size_t getFPS();

    /**
     * Set the FPS limit
     * @param fps 0 to disable limit
     */
    static void setLimitedFPS(size_t fps);

    /**
     * Set the swap interval
     * Must be called after createWindow. On some platforms, it is impossible to set it back
     * to 0 after setting it to a non-zero values, so you can use
     * `VideoContext::swapInterval = 0;` (#include "borealis/core/video.hpp") before creating the window.
     *
     * Test results:
     *
     * ## Platforms that always vsync (interval: 1):
     * iOS18/GLES3/SDL
     * PS4/GLES2/SDL
     *
     * ## Platforms that only support enable and disable vsync (interval: 0,1):
     * Android9/GLES2/SDL
     * macOS13.0+/GL3.2/GLFW (will fixed to 120fps with vsync enabled, https://github.com/glfw/glfw/pull/2277)
     * Windows11/GL3.2/GLFW
     * Linux Wayland/GL3.2/GLFW
     *
     * ## Platforms that not support disable vsync (interval: 1,2,3,4):
     * Windows11/D3D11/GLFW
     * Switch/DEKO3D
     *
     * ## Platforms that support arbitrary values (interval: 0,1,2,3,4):
     * PsVita/GXM
     * PsVita/GLES2/SDL
     * macOS13.0+/GL3.2/SDL
     * Linux X11/GL3.2/GLFW
     * Linux X11/GL3.2/SDL
     * Linux Wayland/GL3.2/SDL
     * Switch/GL4.3/GLFW
     * Switch/GL4.3/SDL
     *
     * @param interval 0 to disable vsync
     */
    static void setSwapInterval(int interval);

    /**
     * If the value is set to true, the program will limit FPS to Application::DeactivatedFPS
     * after Application::DeactivatedTime milliseconds of inactivity.
     *
     * default is false;
     */
    static void setAutomaticDeactivation(bool value);
    static bool getAutomaticDeactivation();
    static bool hasActiveEvent();
    static void setActiveEvent(bool value);
    static void setDeactivatedTime(int millisecond);
    static void setDeactivatedFPS(int value);
    static int getDeactivatedFPS();
    static double getDeactivatedFrameTime();

    static GenericEvent* getGlobalFocusChangeEvent();
    static VoidEvent* getGlobalHintsUpdateEvent();
    static Event<InputType>* getGlobalInputTypeChangeEvent();
    static VoidEvent* getRunLoopEvent();
    static VoidEvent* getExitEvent();
    static VoidEvent* getExitDoneEvent();
    static VoidEvent* getWindowSizeChangedEvent();
    static VoidEvent* getWindowCreationDoneEvent();
    static VoidEvent* getWindowShouldCloseEvent();
    static Event<bool>* getWindowFocusChangedEvent();

    static View* getCurrentFocus();

    static std::string getTitle();

    /**
     * Registers a view to be created from XML. You must give the name of the XML node as well
     * as a function that creates the view.
     *
     * If you need attributes, register them with the given functions in the view
     * class constructor directly. They will be called one by one after the view is instantiated.
     *
     * You should not add any children in the function, it is already taken care of.
     */
    static void registerXMLView(std::string name, XMLViewCreator creator);

    static bool XMLViewsRegisterContains(std::string name);
    static XMLViewCreator getXMLViewCreator(std::string name);

    /**
     * Returns the current system locale.
     */
    static std::string getLocale();

    static void addToFreeQueue(View* view);

    /**
     * Returns the current input type.
     */
    inline static InputType getInputType()
    {
        return inputType;
    }

    inline static void enableDebuggingView(bool enable)
    {
        debuggingViewEnabled = enable;
    }

    inline static bool isDebuggingViewEnabled()
    {
        return debuggingViewEnabled;
    }

    static void setSwapInputKeys(bool swap);

    inline static bool isSwapInputKeys()
    {
        return swapInputKeys;
    }

    inline static void setDrawCoursor(bool draw)
    {
        drawCoursor = draw;
    }

    inline static bool isDrawCursor()
    {
        return drawCoursor;
    }

    inline static bool isHintsLiteMode()
    {
        return hintsLiteMode;
    }

    // Set hints lite mode, which will only show hints for A button and B button
    inline void static setHintsLiteMode(const bool value)
    {
        hintsLiteMode = value;
    }

    // DeckPlayer 补丁：已销毁视图的地址登记表。
    //
    // 问题：TapGestureRecognizer 的回调按值捕获了 View*，手势结束时无条件
    // 调用 giveFocus(view)。如果这次点击触发了界面切换（点列表项 push
    // 新 Activity、或切标签页导致旧 Tab 被 delete），该 View 已析构，
    // 之后再对它调用任何方法——**包括 getParent() 这种「检查」**——
    // 都是对已释放内存的访问，实测 SIGSEGV。
    //
    // 因此判断「还活着吗」不能靠问这个对象，只能靠外部登记：
    //   View 析构时把自己的地址放进 deadViews，giveFocus 先查这张表。
    // 地址复用会带来极低概率的误判（把新对象当已销毁），但由于只在
    // giveFocus 且该地址确实曾释放过时才跳过，影响可忽略。
    static bool isViewDead(const void* p)
    {
        if (p == nullptr) return false;
        auto& v = deadViews;
        const bool hit = std::find(v.begin(), v.end(), p) != v.end();
        // 诊断：统计命中的指针，判断是否存在地址复用误判。
        // 用 debug 而非 warning —— 这条结论已固化在上面的注释里，
        // 保留能力即可，不应在发布构建里刷屏让用户误以为出错。
        static int hit_count = 0;
        if (hit && hit_count < 30)
        {
            ++hit_count;
            brls::Logger::debug("isViewDead 命中: {} (表内 {} 项)",
                                p, v.size());
        }
        return hit;
    }
    static void markViewDead(const void* p)
    {
        if (p == nullptr) return;
        auto& v = deadViews;
        if (std::find(v.begin(), v.end(), p) == v.end())
        {
            v.push_back(p);
            // 防止无限增长：超过上限时丢弃最早的一半
            if (v.size() > 2048)
                v.erase(v.begin(), v.begin() + 1024);
        }
    }

    // DeckPlayer 补丁：供自动化冒烟测试注入按键。
    //
    // 直接暴露 handleAction 而不是让测试自己调 lambda：
    // 只有走完整按键路径，才能验证「动作是否真的挂在焦点链上」。
    // 这一点曾经出过事故——崩溃修好后，按 A 打不开视频，
    // 而所有「像素/进程」类检查依然全绿。
    static bool injectButton(ControllerButton button, bool repeating = false)
    {
        return handleAction(ACTION_GAMEPAD, button, repeating);
    }

    // DeckPlayer 补丁：由输入层回报「这次操作来自键盘还是手柄」。
    // 设置 actualInputType 是 setInputType 的 public 包装，
    // 供平台输入管理器（GLFW/SDL）调用，用于让提示栏显示正确的按键图标。
    static void reportInputSource(bool fromKeyboard)
    {
        setInputType(fromKeyboard ? InputType::KEYBOARD : InputType::GAMEPAD);
    }

    // DeckPlayer 补丁：返回当前栈顶 Activity。
    //
    // 用途：视频渲染发生在 postDrawHook（nvgEndFrame 之后），而 Dialog 是
    // 「压栈的新 Activity」。被对话框覆盖时，播放器视图仍在栈中、渲染钩子
    // 仍在生效，若不判断就会把对话框整屏盖掉。业务代码需要一个只读的
    // 栈顶查询来做这个判断，故新增此接口，不改动既有行为。
    static Activity* getTopActivity()
    {
        return activitiesStack.empty() ? nullptr : activitiesStack.back();
    }

    static void tryDeinitFirstResponder(View* view);

    static void addToWatchedKeys(const BrlsKeyCombination key);

    static void removeWatchedKeys(const BrlsKeyCombination key);

  private:
    inline static bool inited               = false;
    inline static bool quitRequested        = false;
    inline static bool debuggingViewEnabled = false;
    inline static bool swapInputKeys        = false;
    inline static bool drawCoursor          = false;

    inline static Platform* platform = nullptr;

    inline static std::string title;

    inline static FontStash fontStash;

    inline static std::vector<Activity*> activitiesStack;
    inline static std::vector<const void*> deadViews;  // DeckPlayer：已销毁视图地址
    inline static std::vector<View*> focusStack;
    inline static std::deque<View*> deletionPool;

    inline static View* currentFocus = nullptr;
    inline static std::vector<TouchState> currentTouchState;
    inline static MouseState currentMouseState;
    inline static NotificationManager* notificationManager;

    // Return true if input type was changed
    static bool setInputType(InputType type);

    inline static InputType inputType = InputType::GAMEPAD;
    inline static std::vector<BrlsKeyState> watchedKeys;
    inline static std::vector<BrlsKeyState> oldWatchedKeys;
    inline static std::unordered_map<int, int> watchedKeysMap;
    inline static ControllerState controllerState = {};

    inline static void processInput();
    inline static bool internalMainLoop();

    inline static void updateFPS();

    inline static unsigned blockInputsTokens = 0; // any value > 0 means inputs are blocked
    inline static bool muteSounds            = false;

    inline static std::string commonFooter;

    inline static bool globalQuitEnabled                = false;
    inline static ActionIdentifier gloablQuitIdentifier = ACTION_NONE;
    inline static bool globalFPSToggleEnabled           = false;
    inline static size_t globalFPS                      = 60;
    inline static Time limitedFrameTime                 = 0;
    inline static Time frameStartTime                   = 0;
    inline static bool hintsLiteMode                    = false;

    inline static bool deactivatedBehavior = false;
    inline static bool activeEvent         = false;
    inline static Time lastActiveTime      = 0;
    inline static int deactivatedFPS       = 5; // FPS 5
    inline static int deactivatedTime      = 5000000; // 5s

    inline static View* repetitionOldFocus = nullptr;

    inline static GenericEvent globalFocusChangeEvent;
    inline static VoidEvent globalHintsUpdateEvent;
    inline static Event<InputType> globalInputTypeChangeEvent;
    inline static VoidEvent runLoopEvent;
    inline static VoidEvent exitEvent;
    inline static VoidEvent exitDoneEvent;
    inline static VoidEvent windowSizeChangedEvent;
    inline static VoidEvent windowCreationDoneEvent;
    inline static VoidEvent windowShouldCloseEvent;
    inline static Event<bool> windowFocusChangedEvent;

    inline static std::unordered_map<std::string, XMLViewCreator> xmlViewsRegister;

    static void navigate(FocusDirection direction, bool repeating);

    static void frame();
    static void clear();
    static void exit();

    /**
     * Handles actions for the currently focused view and
     * the given button
     * Returns true if at least one action has been fired
     */
    static bool handleAction(ActionType type, int button, bool repeating);

    static void registerBuiltInXMLViews();

    inline static DebugLayer* debugLayer = nullptr;
};

} // namespace brls
