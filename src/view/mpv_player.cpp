#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "view/mpv_player.hpp"
#include "view/track_menu.hpp"
#include "activity/file_picker_activity.hpp"
#include "ui/design.hpp"
#include "ui/icons.hpp"
#include "utils/playback_history.hpp"
#include "utils/scanner.hpp"
#include "utils/i18n.hpp"
#include "utils/resource_locator.hpp"
#include <cmath>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <vector>

#define GLFW_INCLUDE_NONE
#if defined(ANDROID)
// Android 没有 GLFW。GL 函数指针通过 EGL 获取（NDK 标准做法），
// 这也是 mpv 的 render API 在 Android 上要求的取指针方式。
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#include <dlfcn.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <borealis/extern/glad/glad.h>
// nanovg 的纹理接口在 NANOVG_GL3 宏下才声明。
// borealis 用 GL3 后端（见其 CMake 的 USE_GL3 判断），这里保持一致。
#ifndef NANOVG_GL3
#define NANOVG_GL3 1
#endif
#include <borealis/extern/nanovg/nanovg_gl.h>

// nanovg 的 nvglCreateImageFromHandle* 系列声明被 #if defined NANOVG_GLES2/3
// 包裹，而那两个宏只在 borealis 的 .cpp 内定义，本翻译单元看不到。
// 函数本体在库里确实存在（只是符号名按后端不同），因此在文件作用域声明。
#if defined(USE_GLES2)
extern "C" int nvglCreateImageFromHandleGLES2(
    NVGcontext* ctx, unsigned int textureId, int w, int h, int flags);
#elif defined(USE_GLES3)
extern "C" int nvglCreateImageFromHandleGLES3(
    NVGcontext* ctx, unsigned int textureId, int w, int h, int flags);
#endif

namespace muvik {

// ─────────────────────────────────────────────────────────────
// OSD 视觉规格
//
// 重构说明（第二轮 UI 重做）：
//   第一版 OSD 是「两行文字按钮 + 一行手柄提示文字」，所有控件都是
//   带中文标签的方块，堆在画面底部，观感是「把菜单贴到视频上」。
//   现在改为电视端影音软件的通用语言：
//     · 顶部：返回按钮 + 片名 + 一行技术信息（分辨率 / 硬解）
//     · 底部：主控行（纯图标）+ 设置行（纯图标 + 倍速胶囊）+ 细进度条 + 时间
//     · 所有图标都是矢量路径（ui/icons），不依赖字形，任何平台一致
//   几何全部从「视口高度」推导，因此 16:10 / 16:9 / 4:3 都同一套比例。
// ─────────────────────────────────────────────────────────────
namespace osd {
// 字号（与 ui::font 分级对齐）
constexpr float FONT_TITLE    = 20.0f;  // 片名
constexpr float FONT_TIME     = 14.0f;  // 时间
constexpr float FONT_HUD      = 17.0f;  // 居中 HUD 文字
constexpr float FONT_HUD_ICON = 24.0f;  // HUD 图标

// 布局
constexpr float TOP_H          = 112.0f;  // 顶部压边高度
constexpr float BOTTOM_H       = 168.0f; // 底部压边高度
constexpr float BAR_H          = 5.0f;   // 进度条可视高度（细条更接近电视端）
constexpr float BAR_HIT_H      = 46.0f;  // 触摸判定高度（触屏下限）
constexpr float KNOB_R_DRAG    = 7.5f;   // 拖动时的滑块半径（静止不画滑块）
constexpr float TRANSPORT_D    = 64.0f;  // 播放/暂停主按钮直径
constexpr float SIDE_D         = 46.0f;  // 快退/快进按钮直径
constexpr float SKIP_D         = 40.0f;  // 上一集/下一集按钮直径
constexpr float SETTINGS_D     = 40.0f;  // 设置行按钮直径

// 内容左右安全边距：随宽度按比例，但设上下限（掌机窄屏不贴边，
// 桌面宽屏也不至于离得太远）。
inline float marginFor(float width) {
    return std::clamp(width * 0.042f, 24.0f, 56.0f);
}
// 底部两条水平基线：
//   工具条一条轴：主控块水平居中于画面中轴线，设置块右缘贴 w−m
//   进度条一条轴：左侧当前时间贴 m、右侧总时长贴 w−m，进度条居中延展
inline float barCenterY(float y, float h) { return y + h - 56.0f; }
inline float toolCenterY(float y, float h) { return y + h - 126.0f; }
// 主控块：居中于画面中轴线（播放/暂停及前后跳过按钮水平居中）
inline float transportCenterX(float x, float width) { return x + width * 0.5f; }
} // namespace osd

// 把 showHUD() 的历史字符串键映射到矢量图标。
// 以前这个参数根本没被使用（HUD 只画文字），现在图标是信息主体之一。
static ui::Icon hudIconFor(const std::string& key) {
    if (key == "play") return ui::Icon::Play;
    if (key == "pause") return ui::Icon::Pause;
    if (key == "replay") return ui::Icon::SeekBack;
    if (key == "fast_forward") return ui::Icon::SeekForward;
    if (key == "speed") return ui::Icon::Speed;
    if (key == "aspect_ratio") return ui::Icon::Aspect;
    if (key == "lock") return ui::Icon::Lock;
    if (key == "lock_open") return ui::Icon::Unlock;
    if (key == "volume_up" || key == "volume_down") return ui::Icon::Volume;
    if (key == "brightness_high" || key == "brightness_low") return ui::Icon::Brightness;
    if (key == "subtitles") return ui::Icon::Subtitles;
    if (key == "subtitles_off") return ui::Icon::SubtitlesOff;
    if (key == "timer") return ui::Icon::Clock;
    if (key == "error") return ui::Icon::Error;
    if (key == "playlist") return ui::Icon::Playlist;
    return ui::Icon::Info;
}

// ─────────────────────────────────────────────────────────────
// 长按加速（按住不动 → 临时提速，松手恢复）
// ─────────────────────────────────────────────────────────────
// 450ms 是常见的长按阈值：短于它会被误判成「手指还没抬起」，
// 长于它则用户会觉得按了没反应。
constexpr int   kHoldBoostDelayMs = 450;
// 手指移动超过这个距离就不算长按（已经在调音量/亮度或快进了）
constexpr float kHoldMoveSlopPx   = 12.0f;
// 长按时的目标倍速
constexpr double kHoldBoostSpeed  = 2.0;



static void* get_proc_address_mpv(void* fn_ctx, const char* name) {
    (void)fn_ctx;
#if defined(ANDROID)
    // Android 上必须用 eglGetProcAddress：GLES 的核心函数在 libGLESv3 里，
    // 扩展函数只有通过 EGL 解析才能拿到（dlsym(libGLESv3) 通常失败）。
    void* p = (void*)eglGetProcAddress(name);
    if (p) return p;
    // 少数核心函数不在 eglGetProcAddress 的返回里，回退到 dlsym。
    static void* libgles = dlopen("libGLESv3.so", RTLD_LAZY | RTLD_LOCAL);
    if (!libgles) libgles = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    return libgles ? dlsym(libgles, name) : nullptr;
#elif defined(__SDL2__)
    return SDL_GL_GetProcAddress(name);
#else
    glfwGetCurrentContext();
    return (void*)glfwGetProcAddress(name);
#endif
}

static void on_mpv_redraw(void* ctx) {
    (void)ctx;
    brls::sync([]() {});
}

MPVPlayerView::MPVPlayerView() {
    this->setFocusable(true);
    initMPV();

    // 背光能力只查一次：桌面 / macOS 返回 false，Steam Deck、安卓、Switch 返回 true。
    // 有就改系统背光，没有才用蒙层模拟（见 setBrightness 与 draw 里的判断）。
    if (auto* platform = brls::Application::getPlatform())
        backlight_capable_ = platform->canSetBacklightBrightness();

    last_user_action_time_ = std::chrono::steady_clock::now();

    auto handleLocked = [this]() -> bool {
        if (!touch_locked_) return false;
        auto now = std::chrono::steady_clock::now();
        lock_hint_until_ = now + std::chrono::milliseconds(2600);
        return true;
    };

    // 绑定基础播放与控制
    this->registerAction(i18n::tr("player.play_pause", "播放/暂停"), brls::ControllerButton::BUTTON_A, [this]([[maybe_unused]] brls::View* view) {
        auto now = std::chrono::steady_clock::now();
        if (touch_locked_) {
            const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_a_press_time_).count();
            if (dt_ms < 450) {
                toggleTouchLock();
                showOSD(true);
                last_user_action_time_ = now;
                return true;
            }
            last_a_press_time_ = now;
            lock_hint_until_ = now + std::chrono::milliseconds(2600);
            return true;
        }
        last_a_press_time_ = now;
        if (isOsdActive()) {
            activateOsdFocus();
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::PlayPause;
            togglePlayPause();
        }
        last_user_action_time_ = now;
        return true;
    }, false);

    this->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        // 若当前 OSD 正在显示，按 B 键首先关闭 OSD；OSD 已隐藏时按 B 才退出播放页
        if (isOsdActive()) {
            showOSD(false);
            return true;
        }
        // 退出当前视频播放页（立即返回上一页，无需淡出等待）
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return true;
    }, false);

    this->registerAction(i18n::tr("player.rewind", "快退"), brls::ControllerButton::BUTTON_LEFT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::LEFT);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
            seek(-10);
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, false, true);

    this->registerAction(i18n::tr("player.fast_forward", "快进"), brls::ControllerButton::BUTTON_RIGHT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::RIGHT);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
            seek(10);
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, false, true);

    this->registerAction(i18n::tr("player.volume_down", "音量减"), brls::ControllerButton::BUTTON_LT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        setVolume(volume_ - 5);
        return true;
    }, false, true);   // 第 5 个参数 = 按住连发

    this->registerAction(i18n::tr("player.volume_up", "音量加"), brls::ControllerButton::BUTTON_RT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        setVolume(volume_ + 5);
        return true;
    }, false, true);

    // 肩键：快退 15 秒 / 快进 15 秒（支持按住连续调节）
    this->registerAction(i18n::tr("player.rewind", "快退"), brls::ControllerButton::BUTTON_LB, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        seek(-15);
        return true;
    }, false, true);

    this->registerAction(i18n::tr("player.fast_forward", "快进"), brls::ControllerButton::BUTTON_RB, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        seek(15);
        return true;
    }, false, true);

    this->registerAction(i18n::tr("player.playlist", "播放列表"), brls::ControllerButton::BUTTON_UP, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::UP);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::PlayPause;
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, false);

    this->registerAction("控制栏", brls::ControllerButton::BUTTON_DOWN, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::DOWN);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, false);

    // 摇杆与方向导航映射：未唤出控制栏时唤出并聚焦对应控件，唤出后正常行进导航
    this->registerAction("", brls::ControllerButton::BUTTON_NAV_LEFT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::LEFT);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
            seek(-10);
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, true, true);

    this->registerAction("", brls::ControllerButton::BUTTON_NAV_RIGHT, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::RIGHT);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
            seek(10);
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, true, true);

    this->registerAction("", brls::ControllerButton::BUTTON_NAV_UP, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::UP);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::PlayPause;
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, true, false);

    this->registerAction("", brls::ControllerButton::BUTTON_NAV_DOWN, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        if (isOsdActive()) {
            navigateOsdFocus(brls::FocusDirection::DOWN);
        } else {
            showOSD(true);
            osd_focus_ = OsdFocusItem::ProgressBar;
        }
        last_user_action_time_ = std::chrono::steady_clock::now();
        return true;
    }, true, false);

    // 绑定高级功能按键
    this->registerAction(i18n::tr("player.audio_track", "音轨选择"), brls::ControllerButton::BUTTON_X, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        auto tracks = getTracks("audio");
        TrackSelectionDialog::showAudioDialog(tracks, [this](int track_id) {
            selectTrack("audio", track_id);
        });
        return true;
    }, false);

    this->registerAction(i18n::tr("player.sub_track", "字幕设置"), brls::ControllerButton::BUTTON_Y, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        auto tracks = getTracks("sub");
        TrackSelectionDialog::showSubtitleDialog(tracks, subtitle_delay_,
            [this](int track_id) { selectTrack("sub", track_id); },
            [this](float delay) { setSubtitleDelay(delay); },
            [this]() { promptLoadExternalSubtitle(); }
        );
        return true;
    }, false);

    // 按下右摇杆 (R3)：循环切换倍速 (1.0x -> 1.25x -> 1.5x -> 2.0x -> 0.75x)
    this->registerAction(i18n::tr("player.speed", "倍速切换"), brls::ControllerButton::BUTTON_RSB, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        cyclePlaybackSpeed();
        return true;
    }, false);

    // 按下左摇杆 (L3)：循环切换画面比例 (适应 -> 16:10 满屏 -> 16:9 -> 4:3)
    this->registerAction(i18n::tr("player.aspect_ratio", "比例切换"), brls::ControllerButton::BUTTON_LSB, [this, handleLocked]([[maybe_unused]] brls::View* view) {
        if (handleLocked()) return true;
        cycleAspectRatio();
        return true;
    }, false);

    // 全屏切换**不再占用任何手柄键**。
    //
    // 原因：原来绑在 BUTTON_BACK（手柄 Select）上，而 Android 的返回键
    // 在 SDL 里也映射到 BUTTON_BACK —— 于是用户按「返回」期望退出播放页，
    // 实际却在切全屏，返回事件永远到不了 Activity 栈。
    //
    // borealis 里 BUTTON_L3 这个枚举不存在（只有 LSB 且已被比例切换占用），
    // 所以不另找键位：全屏交给触摸控件与底部提示栏，
    // 返回键恢复成默认的 popActivity。
    //
    // 若将来需要在手柄上切全屏，应改到一个不与系统语义冲突的键。

    // 说明：这里**不能**用 registerClickAction。
    //
    // registerClickAction 内部是 registerAction(BUTTON_A, ...)，
    // 而 borealis 的 registerAction 按**按钮**去重（view.cpp:770-774）：
    // 后注册的会直接覆盖先注册的。上面第 111 行已经给 BUTTON_A 绑了
    // 「播放/暂停」，再用 registerClickAction 就会把它顶掉 ——
    // 实测手柄按 A 不再暂停，只是切换 OSD 显隐。
    //
    // 触屏的「单击切 OSD」已经在 TapGestureRecognizer 里处理
    // （见 setupGestures），手柄侧保留 A = 播放/暂停。
    // 因此这里不需要再注册任何 A 键动作。

    // 触控手势：双击播放/暂停、左右半屏滑动调节亮度与音量、水平滑动快进快退
    setupGestures();

    // 锁屏防误触：手柄 START 键切换（触控手势本身在锁屏后会被忽略）
    this->registerAction("触控锁定", brls::ControllerButton::BUTTON_START, [this]([[maybe_unused]] brls::View* view) {
        (void)view;
        toggleTouchLock();
        return true;
    }, false);

    // 视频渲染在帧末执行：mpv 画面直接渲染到默认帧缓冲中
    brls::Application::postDrawHook = [this]() {
        this->renderMPV();
    };

    // 手势自检：设置 MUVIK_GESTURE_TEST=1 后自动注入触摸序列，
    // 用于在无人交互的环境下验证手势是否真的生效。
    setupGestureSelfTest();
}

// 注入一次触摸事件：phase 表示按下/移动/抬起
static void injectTouch(brls::View* view, brls::TouchPhase phase,
                        float x, float y) {
    brls::TouchState ts;
    ts.fingerId = 0;
    ts.phase = phase;
    ts.position = brls::Point(x, y);
    ts.view = view;
    brls::MouseState ms;
    view->gestureRecognizerRequest(ts, ms, view);
}

void MPVPlayerView::setupGestureSelfTest() {
    if (!std::getenv("MUVIK_GESTURE_TEST")) return;

    brls::Logger::info("=== 手势自检开始 ===");
    brls::Logger::info("初始: osd={} playing={} vol={} bright={}%",
                       osd_visible_ ? "显示" : "隐藏",
                       is_playing_ ? "是" : "否", volume_,
                       (int)(brightness_ * 100));

    // 延迟执行：等界面布局完成（用 sync 排到下一帧之后）
    struct Step { int delay_ms; std::function<void()> action; const char* desc; };
    auto self = this;
    std::vector<Step> steps;

    // 1. 单击 -> OSD 应切换
    steps.push_back({200, [self]() {
        bool before = self->osd_visible_;
        injectTouch(self, brls::TouchPhase::START, 640, 360);
        injectTouch(self, brls::TouchPhase::END, 640, 360);
        brls::Logger::info("【单击】前 osd={} 后 osd={} -> {}",
                           before ? "显示":"隐藏",
                           self->osd_visible_ ? "显示":"隐藏",
                           before != self->osd_visible_ ? "PASS 已切换" : "FAIL 未变化");
    }, "单击"});

    // 2. 双击 -> 播放/暂停应切换
    steps.push_back({400, [self]() {
        bool before = self->is_playing_;
        injectTouch(self, brls::TouchPhase::START, 640, 360);
        injectTouch(self, brls::TouchPhase::END, 640, 360);
        injectTouch(self, brls::TouchPhase::START, 640, 360);
        injectTouch(self, brls::TouchPhase::END, 640, 360);
        bool after = self->is_playing_;
        brls::Logger::info("【双击】前 playing={} 后 playing={} -> {}",
                           before?"是":"否", after?"是":"否",
                           before != after ? "PASS 已切换" : "FAIL 未变化");
    }, "双击"});

    // 3. 右半屏向上滑 -> 音量应增大
    steps.push_back({400, [self]() {
        // 先把音量降到 50，否则在 100 上限时向上滑看不出变化
        self->setVolume(50);
        int before = self->volume_;
        float x = self->getWidth() * 0.75f;
        // Pan 的 delta = 上一帧位置 - 当前帧位置。
        // 向上滑（y 递减）时 delta.y 为正，代码里 ratio = -accum/300 会变负，
        // 说明「向上滑 = 减小音量」——需要确认这是否符合预期。
        injectTouch(self, brls::TouchPhase::START, x, 400);
        for (int i = 1; i <= 8; ++i)
            injectTouch(self, brls::TouchPhase::STAY, x, 400 - i * 25.0f);
        injectTouch(self, brls::TouchPhase::END, x, 200);
        int after = self->volume_;
        brls::Logger::info("【右半屏上滑】前 vol={} 后 vol={} -> {}",
                           before, after,
                           after > before ? "PASS 已增大" : "FAIL 未变化");
    }, "音量"});

    // 4. 左半屏向下滑 -> 亮度应降低
    steps.push_back({400, [self]() {
        float before = self->brightness_;
        float x = self->getWidth() * 0.25f;
        injectTouch(self, brls::TouchPhase::START, x, 200);
        for (int i = 1; i <= 8; ++i)
            injectTouch(self, brls::TouchPhase::STAY, x, 200 + i * 25.0f);
        injectTouch(self, brls::TouchPhase::END, x, 400);
        float after = self->brightness_;
        brls::Logger::info("【左半屏下滑】前 bright={}% 后 bright={}% -> {}",
                           (int)(before*100), (int)(after*100),
                           after < before ? "PASS 已降低" : "FAIL 未变化");
    }, "亮度"});

    // 5. 水平滑动 -> 应触发 seek
    //
    // 注意：分两步。第一步只注入手势，第二步（由后台线程延后调度）
    // 才读取结果。绝不能在同一段主线程代码里 sleep 等待 ——
    // 那会冻结事件循环，mpv 的属性事件无处投递，
    // 看起来就像「seek 没生效」（实测踩过）。
    static int64_t seek_before = 0;
    steps.push_back({400, [self]() {
        seek_before = self->current_position_;
        injectTouch(self, brls::TouchPhase::START, 200, 360);
        for (int i = 1; i <= 10; ++i)
            injectTouch(self, brls::TouchPhase::STAY, 200 + i * 60.0f, 360);
        injectTouch(self, brls::TouchPhase::END, 800, 360);
    }, "快进(注入)"});

    steps.push_back({700, [self]() {
        // 从 mpv 直接读真实位置，排除本地缓存字段的干扰
        double mpv_pos = -1;
        char* p = mpv_get_property_string(self->mpv_, "time-pos");
        if (p) { mpv_pos = atof(p); mpv_free(p); }
        brls::Logger::info("【水平滑动】前 pos={}s mpv_time-pos={}s -> {}",
                           seek_before, mpv_pos,
                           mpv_pos > 1.0 ? "PASS 已跳转" : "FAIL 未跳转");
    }, "快进(校验)"});

    // 6. 长按加速：按住不动 → 倍速临时提升，松手恢复
    //
    // 分三步（按下 / 校验并松手 / 校验恢复），原因同第 5 项：
    // 提速发生在真实时间流逝之后，不能在同一段主线程代码里等待。
    steps.push_back({400, [self]() {
        // 用画面正中央，避开底部按钮行与进度条判定区
        injectTouch(self, brls::TouchPhase::START,
                    self->getWidth() * 0.5f, self->getHeight() * 0.5f);
        brls::Logger::info("【长按加速】已按下，等待阈值…");
    }, "长按(按下)"});

    steps.push_back({900, [self]() {
        double sp = 1.0;
        char* p = mpv_get_property_string(self->mpv_, "speed");
        if (p) { sp = atof(p); mpv_free(p); }
        brls::Logger::info("【长按加速】按住后 speed={} -> {}",
                           sp, sp > 1.5 ? "PASS 已提速" : "FAIL 未提速");
        injectTouch(self, brls::TouchPhase::END,
                    self->getWidth() * 0.5f, self->getHeight() * 0.5f);
    }, "长按(校验并松手)"});

    steps.push_back({600, [self]() {
        double sp = 1.0;
        char* p = mpv_get_property_string(self->mpv_, "speed");
        if (p) { sp = atof(p); mpv_free(p); }
        brls::Logger::info("【长按恢复】松手后 speed={} -> {}",
                           sp, sp < 1.5 ? "PASS 已恢复" : "FAIL 未恢复");
    }, "长按(恢复校验)"});

    // 7. 按钮按下态：手指落在按钮上应立刻高亮，松手清除。
    //
    // 这一项是为了防住一类「静默失效」：代码看着有按下态处理，
    // 但 borealis 把按下报成 UNSURE 而不是 START（见 setupGestures 的说明），
    // 于是整段逻辑一次都没执行过 —— 界面上表现为「按钮点了没反应」。
    steps.push_back({400, [self]() {
        self->osd_visible_ = true;
        self->last_user_action_time_ = std::chrono::steady_clock::now();
    }, "按下态(显示OSD)"});

    steps.push_back({500, [self]() {
        if (self->hit_rects_.empty()) {
            brls::Logger::info("【按钮按下态】hit_rects_ 为空 -> FAIL 未布局");
            return;
        }
        const auto& r = self->hit_rects_.front();
        injectTouch(self, brls::TouchPhase::START, r.x + r.w / 2, r.y + r.h / 2);
        const bool during = self->has_locked_action_;
        // 抬手位置放在画面左上角（不在任何按钮上），避免真的触发按钮动作
        injectTouch(self, brls::TouchPhase::END, 4, 4);
        const bool after = self->has_locked_action_;
        brls::Logger::info("【按钮按下态】按下中={} 松手后={} -> {}",
                           during ? "高亮" : "无", after ? "高亮" : "无",
                           (during && !after) ? "PASS 有按下反馈" : "FAIL 无按下反馈");
    }, "按下态(校验)"});

    // 后台线程负责计时（sleep 不在主线程），动作本身投递到主线程执行
    std::thread([steps]() mutable {
        for (auto& st : steps) {
            std::this_thread::sleep_for(std::chrono::milliseconds(st.delay_ms));
            brls::sync([action = st.action]() { action(); });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        brls::sync([]() {
            brls::Logger::info("=== 手势自检结束 ===");
            brls::Application::quit();
        });
    }).detach();
}

bool MPVPlayerView::ensureVideoTarget(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (vid_fbo_ != 0 && vid_tex_w_ == w && vid_tex_h_ == h) return true;

    releaseVideoTarget();

    glGenTextures(1, &vid_tex_);
    glBindTexture(GL_TEXTURE_2D, vid_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &vid_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, vid_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, vid_tex_, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        brls::Logger::error("MPVPlayerView: FBO 不完整 0x{:x}", (unsigned)status);
        releaseVideoTarget();
        return false;
    }

    vid_tex_w_ = w;
    vid_tex_h_ = h;
    nvg_image_ = -1;  // 纹理变了，nanoVG 句柄需重建

    // nanoVG 把 GL 纹理包装成图像的接口按后端分三种实现：
    //   GL3    —— 桌面 OpenGL 3.x（macOS/Steam Deck 用的这个）
    //   GLES2  —— OpenGL ES 2.0（PowerVR GE8300 这类掌机）
    //   GLES3  —— OpenGL ES 3.0
    // 写死 GL3 版本会导致 GLES 构建在链接期报
    //   Undefined symbols: _nvglCreateImageFromHandleGL3
    // 因此按编译宏分派。宏由 borealis 的 commonOption.cmake 定义。
#if defined(USE_GLES2)
    nvg_image_ = nvglCreateImageFromHandleGLES2(
        brls::Application::getNVGContext(), vid_tex_, w, h, 0);
#elif defined(USE_GLES3)
    nvg_image_ = nvglCreateImageFromHandleGLES3(
        brls::Application::getNVGContext(), vid_tex_, w, h, 0);
#else
    nvg_image_ = nvglCreateImageFromHandleGL3(
        brls::Application::getNVGContext(), vid_tex_, w, h, 0);
#endif
    if (nvg_image_ <= 0) {
        brls::Logger::warning("MPVPlayerView: nvglCreateImageFromHandle 失败");
        nvg_image_ = -1;
    }
    return true;
}

void MPVPlayerView::releaseVideoTarget() {
    if (vid_fbo_) { glDeleteFramebuffers(1, &vid_fbo_); vid_fbo_ = 0; }
    if (vid_tex_) { glDeleteTextures(1, &vid_tex_); vid_tex_ = 0; }
    vid_tex_w_ = vid_tex_h_ = 0;
    nvg_image_ = -1;
}

void MPVPlayerView::renderMPV() {
    if (!mpv_gl_ || !pending_render_) return;
    pending_render_ = false;

    const float w = last_draw_.w, h = last_draw_.h;
    if (w <= 0.0f || h <= 0.0f) return;

    // 取当前视口的原点即可；宽度高度用下面算出的物理像素值。
    // 曾经这里还算过 fb_w/fb_h，但换算方式错了（见下方说明），已废弃。
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);

    // 坐标换算：三套坐标系容易混淆，这里一次说清。
    //
    //   · last_draw_      —— **逻辑**坐标（borealis 布局空间，最大 1280x906）
    //   · windowWidth/Height —— **物理**像素（SDL 传的是 drawable size，
    //                          Android 上是 3392x2400）
    //   · windowScale     —— 物理 / 逻辑 = 3392 / 1280 = 2.65
    //   · contentHeight   —— **逻辑**高度 = 2400 / 2.65 = 906
    //
    // GL 的 glViewport 与 mpv 的 FBO 都要物理像素，所以逻辑坐标一律乘
    // windowScale。
    //
    // 反面教材（原来这里是这么写的）：
    //     sx = fb_w / windowWidth
    // macOS 上 windowScale=1，物理==逻辑，sx 恰好等于 1，看不出问题；
    // Android 上 fb_w 和 windowWidth 都是 3392，sx 仍然是 1，
    // 于是逻辑坐标被当成物理像素用 —— 视频只占屏幕左上角 1/2.65，
    // 表现为「画面缩在左下角、控件在左上角」。
    const float scale = brls::Application::windowScale > 0.0f
                            ? brls::Application::windowScale : 1.0f;

    // Y 轴翻转的基准必须是**逻辑**高度（contentHeight），
    // 不能用 windowHeight —— 后者是物理像素，与逻辑坐标相减毫无意义。
    // GL 原点在左下、UI 原点在左上，所以要用 contentHeight - y - h。
    const float logical_h = brls::Application::contentHeight > 0.0f
                                ? brls::Application::contentHeight
                                : (float)brls::Application::windowHeight / scale;

    const int px = (int)(last_draw_.x * scale);
    const int py = (int)((logical_h - last_draw_.y - h) * scale);
    const int pw = (int)(w * scale);
    const int ph = (int)(h * scale);

    // 直接把视频渲染到默认帧缓冲的目标区域。
    // 注意：这会覆盖该区域内的 UI（OSD / 标题栏）。
    mpv_opengl_fbo fbo{(int)vp[0], pw, ph, 0};
    int flip_y = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    // nanoVG 会设置 scissor / blend / depth 等状态，mpv 期望的是干净状态。
    //
    // **关键：必须关闭混合（GL_BLEND）。**
    // 视频是要「覆盖」目标像素，不是和底下的 UI 叠加。
    // 之前这里写了 glEnable(GL_BLEND) —— 于是 mpv 输出的每个像素都和
    // 上一帧残留的 UI 做 alpha 混合，画面呈现重影/杂色，
    // 也就是用户看到的「花屏」。
    // 桌面（macOS）上 mpv 输出的 alpha 恒为 1，混合结果等于覆盖，
    // 所以完全看不出问题；Android 的硬解路径会给出带 alpha 的帧。
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // 像素打包/解包的对齐也必须复位：
    // nanoVG 上传字体图集时会改这些状态，残留会导致纹理上传错位，
    // 画面表现为逐像素的彩色噪点（实测正是这个症状）。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);

    glViewport(px, py, pw, ph);
    mpv_render_context_render(mpv_gl_, params);

    glViewport(vp[0], vp[1], vp[2], vp[3]);

    drawOverlay();


}

// 在视频之上绘制 OSD 层。
// 使用一次独立的 nanoVG 帧：nvgBeginFrame -> 绘制 -> nvgEndFrame。
void MPVPlayerView::drawOverlay() {
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg) return;

    const auto& r = last_draw_;
    if (r.w <= 0.0f || r.h <= 0.0f) return;

    nvgBeginFrame(vg, brls::Application::windowWidth,
                  brls::Application::windowHeight,
                  brls::Application::windowScale);
    nvgScale(vg, brls::Application::windowScale, brls::Application::windowScale);

    brls::Activity* mine = this->getParentActivity();
    brls::Activity* top = brls::Application::getTopActivity();
    bool has_overlay_activity = (mine && top && top != mine);

    if (!has_overlay_activity) {
        drawHud(vg, r.x, r.y, r.w, r.h);
    } else {
        // 当有压栈的 Activity（如播放列表、音轨/字幕选择或退出对话框）时：
        // 视频画面在底层保持渲染（不黑屏、不闪屏），将顶层 Activity 及其半透明 backdrop 遮罩
        // 绘制在视频之上，确保对话框卡片、按钮和高亮完整可见。
        brls::FrameContext frameContext;
        frameContext.pixelRatio = (float)brls::Application::windowWidth / (float)brls::Application::windowHeight;
        frameContext.vg         = vg;
        frameContext.fontStash  = brls::Application::getFontStash();
        frameContext.theme      = brls::Application::getTheme();

        brls::View* topView = top->getContentView();
        if (topView) {
            topView->frame(&frameContext);
        }

        brls::View* focus = brls::Application::getCurrentFocus();
        if (focus && !brls::Application::isViewDead(focus) &&
            brls::Application::getInputType() != brls::InputType::TOUCH) {
            focus->frameHighlight(&frameContext);
        }
    }

    nvgEndFrame(vg);
}

// OSD / HUD 层绘制。参数为视频区域的左上角与尺寸。
//
// 【重要】本函数内所有文字都必须走 ui::text()（内部用 getDefaultFont()），
// 不能写 nvgFontFace(vg, "sans")。
//   "sans" 是 nanoVG 官方示例里的字体名，本项目从未注册过它；
//   fontstash 查不到会返回 -1，nvgText 随即直接 return，
//   结果是**整块 OSD 一个字都画不出来**，只看到一排空色块。
//   这个 bug 曾存在很久而所有自动检查都通过（它们只看视频画面）。
//   现在由 `verify_ui.py --only osd` 用纯黑视频做底来守卫。
void MPVPlayerView::drawHud(NVGcontext* vg, float x, float y, float width, float height) {
    // 亮度蒙层模拟：仅在平台没有背光接口时使用（桌面 / macOS）。
    // Deck、安卓这类能真正改背光的设备上，再叠一层黑只会二次压暗。
    if (!backlight_capable_ && brightness_ < 0.99f) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, (1.0f - brightness_) * 0.8f));
        nvgFill(vg);
    }

    auto now = std::chrono::steady_clock::now();

    // ── 居中 HUD 提示（音量 / 亮度 / 倍速 / 跳转结果）──
    if (now < hud_expire_time_) {
        // 尾部 260ms 淡出：到期时「啪」地消失也是「直挺挺」的一种
        const float fade = std::clamp(
            std::chrono::duration<float, std::milli>(hud_expire_time_ - now).count()
                / 260.0f, 0.0f, 1.0f);
        const bool has_bar = hud_progress_ >= 0.0f;
        const float text_w = ui::textWidth(vg, osd::FONT_HUD, hud_text_);
        const float box_w  = std::clamp(text_w + 32.0f + 26.0f + 32.0f, 180.0f, width - 100.0f);
        const float box_h  = has_bar ? 74.0f : 52.0f;
        const float box_x  = x + (width - box_w) * 0.5f;
        // 略微偏上（距顶 38% 高度），避开画面正中心的人脸与焦点区
        const float box_y  = y + height * 0.38f - box_h * 0.5f;
        const float mid_y  = has_bar ? box_y + 26.0f : box_y + box_h * 0.5f;

        // 半透明暗色毛玻璃底色：由原先 236（92.5% 不透明）改为 130（51% 半透明），通透不挡画面
        ui::fillRoundRect(vg, box_x, box_y, box_w, box_h, 16.0f,
                          nvgRGBA(16, 20, 26, (int)(130 * fade)));
        ui::strokeRoundRect(vg, box_x, box_y, box_w, box_h, 16.0f,
                            nvgRGBA(255, 255, 255, (int)(38 * fade)), 1.2f);
        ui::drawIcon(vg, hudIconFor(hud_icon_), box_x + 30.0f, mid_y, osd::FONT_HUD_ICON,
                     nvgRGBA(26, 159, 255, (int)(255 * fade)));
        ui::text(vg, box_x + 54.0f, mid_y, osd::FONT_HUD,
                 nvgRGBA(255, 255, 255, (int)(255 * fade)),
                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                 ui::ellipsize(vg, osd::FONT_HUD, hud_text_, box_w - 84.0f));

        if (has_bar) {
            const float bar_w = box_w - 60.0f;
            const float bar_x = box_x + 30.0f;
            const float bar_y = box_y + box_h - 20.0f;
            ui::fillRoundRect(vg, bar_x, bar_y, bar_w, 4.0f, 2.0f,
                              nvgRGBA(255, 255, 255, (int)(42 * fade)));
            ui::fillRoundRect(vg, bar_x, bar_y,
                              bar_w * std::clamp(hud_progress_, 0.0f, 1.0f), 4.0f, 2.0f,
                              nvgRGBA(26, 159, 255, (int)(255 * fade)));
        }
    }

    // ── 锁定态提示：按键或轻点屏幕后短暂出现解锁提示 ──
    if (touch_locked_ && now < lock_hint_until_) {
        const float fade = std::clamp(
            std::chrono::duration<float, std::milli>(lock_hint_until_ - now).count()
                / 260.0f, 0.0f, 1.0f);
        const std::string label = i18n::tr("player.lock_hint", "屏幕已锁定 · 连按两次 A 键或双击屏幕解锁");
        const float tw = ui::textWidth(vg, 14.0f, label);
        const float pw = tw + 74.0f;
        const float px = x + (width - pw) * 0.5f;
        const float py = y + 18.0f;
        ui::fillRoundRect(vg, px, py, pw, 44.0f, 22.0f,
                          nvgRGBA(18, 20, 24, (int)(226 * fade)));
        ui::strokeRoundRect(vg, px, py, pw, 44.0f, 22.0f,
                            nvgRGBA(255, 255, 255, (int)(26 * fade)));
        ui::drawIcon(vg, ui::Icon::Unlock, px + 30.0f, py + 22.0f, 18.0f,
                     nvgRGBA(26, 159, 255, (int)(255 * fade)));
        ui::text(vg, px + 50.0f, py + 22.5f, 14.0f,
                 nvgRGBA(255, 255, 255, (int)(240 * fade)),
                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, label);
    }

    // ── OSD 显隐状态机 ──
    constexpr int64_t kOsdIdleTimeoutMs = 4500;
    const auto idle_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_user_action_time_).count();
    // 测试用：MUVIK_OSD_OFF 强制隐藏 OSD，
    // 便于脚本单独验证「字幕是否被控制栏遮挡」。
    static const bool osd_force_off = std::getenv("MUVIK_OSD_OFF") != nullptr;
    const bool osd_shown = !osd_force_off && !touch_locked_
                        && osd_visible_ && idle_ms < kOsdIdleTimeoutMs;

    // ── OSD 渐显动画 ──
    // 目标翻转时记下起点 alpha 与时刻；每帧按耗时插值（帧率无关）。
    // 顶部栏随淡出向上滑、底部栏向下滑——电视端播放器的标准动效。
    if (osd_shown != osd_anim_target_) {
        osd_anim_target_ = osd_shown;
        osd_alpha_from_  = osd_alpha_;
        osd_anim_t0_     = now;
    }
    constexpr float kOsdAnimMs = 240.0f;
    const float anim_t = std::clamp(
        std::chrono::duration<float, std::milli>(now - osd_anim_t0_).count()
            / kOsdAnimMs, 0.0f, 1.0f);
    // 缓出曲线：线性插值在收尾处会有「突然停住」的机械感
    const float eased = 1.0f - (1.0f - anim_t) * (1.0f - anim_t);
    const float osd_target_alpha = osd_shown ? 1.0f : 0.0f;
    osd_alpha_ = osd_alpha_from_ + (osd_target_alpha - osd_alpha_from_) * eased;

    // OSD 不可见时**必须清空命中区**。
    // 否则 hit_rects_ 保留上一次可见时的坐标：用户点屏幕空白处
    // 会命中「看不见的按钮」并触发动作，表现为行为诡异、点了没反应。
    if (!osd_shown) {
        hit_rects_.clear();
        if (!seek_dragging_) {
            seek_bar_ = SeekBarRect{};
        }
    }

    if (osd_alpha_ <= 0.01f) return;

    const float a = osd_alpha_;
    const float top_slide = (1.0f - a) * -18.0f;
    const float bot_slide = (1.0f - a) *  18.0f;
    const float m = osd::marginFor(width);

    // ═══════════════ 顶部：返回 + 片名 + 技术信息 ═══════════════
    ui::scrimTop(vg, x, y + top_slide, width, osd::TOP_H, a);

    const float clock_w = 74.0f;
    const float title_x = x + m + 52.0f;
    ui::textClipped(vg, title_x, y + 38.0f + top_slide, osd::FONT_TITLE,
                    nvgRGBA(255, 255, 255, (int)(252 * a)),
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, video_title_,
                    width - (title_x - x) - m - clock_w);

    // 顶部格式徽标（微胶囊排版，告别技术调试串）
    float cur_bx = title_x;
    const float badge_y = y + 57.0f + top_slide;
    const float badge_h = 20.0f;
    const auto& pal = ui::palette();
    for (const auto& badge : format_badges_) {
        const float tw = ui::textWidth(vg, 11.0f, badge.text);
        const float bw = tw + 14.0f;
        const NVGcolor bg = badge.is_accent ? ui::rgba(pal.accent, 0.22f * a)
                                            : nvgRGBA(255, 255, 255, (int)(22 * a));
        const NVGcolor border = badge.is_accent ? ui::rgba(pal.accent, 0.70f * a)
                                                : nvgRGBA(255, 255, 255, (int)(45 * a));
        const NVGcolor fg = badge.is_accent ? pal.accent
                                            : nvgRGBA(255, 255, 255, (int)(220 * a));
        ui::fillRoundRect(vg, cur_bx, badge_y, bw, badge_h, 4.0f, bg);
        ui::strokeRoundRect(vg, cur_bx, badge_y, bw, badge_h, 4.0f, border, 1.0f);
        ui::text(vg, cur_bx + bw * 0.5f, badge_y + badge_h * 0.5f + 0.5f, 11.0f, fg,
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, badge.text);
        cur_bx += bw + 6.0f;
    }

    // 右上角时钟：等宽数字排版，平稳无抖动
    ui::tabularText(vg, x + width - m, y + 38.0f + top_slide, osd::FONT_TIME,
                    nvgRGBA(255, 255, 255, (int)(150 * a)),
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, getCurrentTimeStr());

    // ═══════════════ 底部：压边 + 进度条 + 时间 ═══════════════
    ui::scrimBottom(vg, x, y + height - osd::BOTTOM_H + bot_slide, width,
                    osd::BOTTOM_H, a);

    const float bar_cy = osd::barCenterY(y, height) + bot_slide;

    // 拖动中的预览值优先，否则用真实播放位置
    const double shown_pos   = seek_dragging_ ? seek_preview_ : current_position_;
    const double shown_total = seek_dragging_ && seek_preview_total_ > 0
                                   ? seek_preview_total_ : total_duration_;

    const std::string pos_str   = MediaScanner::formatDuration((int64_t)shown_pos);
    const std::string total_str = MediaScanner::formatDuration((int64_t)shown_total);

    // 时间显示宽度自适应（等宽排版，至少预留 64px，带时分秒时不挤压）
    const float time_w = std::max(ui::tabularTextWidth(vg, osd::FONT_TIME, total_str), 64.0f);
    constexpr float kTimeGap = 16.0f; // 时间文字与进度条两端留出的安全间距

    // 进度条两端让出时间显示区域，时间与进度条位于同一垂直中心线
    const float bar_x   = x + m + time_w + kTimeGap;
    const float bar_w   = width - (m + time_w + kTimeGap) * 2.0f;
    const float bar_top = bar_cy - osd::BAR_H * 0.5f;

    seek_bar_.x     = bar_x;
    seek_bar_.y     = bar_top;
    seek_bar_.w     = bar_w;
    seek_bar_.h     = osd::BAR_H;
    seek_bar_.hit_h = osd::BAR_HIT_H;
    seek_bar_.hit_y = bar_cy - osd::BAR_HIT_H * 0.5f;

    const bool is_bar_focused = (osd_focus_ == OsdFocusItem::ProgressBar && a > 0.05f);

    const float track_h = osd::BAR_H;
    const float track_top = bar_cy - track_h * 0.5f;

    // 进度条槽底色（去除整个长条的外发光蓝框，聚焦时仅高亮进度圆点）
    ui::fillRoundRect(vg, bar_x, track_top, bar_w, track_h, track_h * 0.5f,
                      nvgRGBA(255, 255, 255, (int)(52 * a)));

    float knob_x = bar_x;
    if (shown_total > 0) {
        const float progress = (float)std::clamp(shown_pos / shown_total, 0.0, 1.0);
        const float active_w = bar_w * progress;
        knob_x = bar_x + active_w;

        ui::fillRoundRect(vg, bar_x, track_top, std::max(active_w, track_h),
                          track_h, track_h * 0.5f,
                          nvgRGBA(26, 159, 255, (int)(255 * a)));

        // 滑块原点：普通状态为细腻白点；手柄聚焦或拖动时原点放大并带光晕指示
        if (seek_dragging_ || is_bar_focused) {
            ui::fillCircle(vg, knob_x, bar_cy, osd::KNOB_R_DRAG + 5.0f,
                           nvgRGBA(26, 159, 255, (int)(110 * a)));
            ui::fillCircle(vg, knob_x, bar_cy, osd::KNOB_R_DRAG + 2.5f,
                           nvgRGBA(102, 192, 244, (int)(220 * a)));
            ui::fillCircle(vg, knob_x, bar_cy, osd::KNOB_R_DRAG,
                           nvgRGBA(255, 255, 255, (int)(255 * a)));
        } else {
            ui::fillCircle(vg, knob_x, bar_cy, 4.0f,
                           nvgRGBA(255, 255, 255, (int)(220 * a)));
        }
    }

    // 拖动或手柄聚焦进度条时，在滑块上方显示精致紧凑的目标时间胶囊，
    // 高度与位置严格经过几何计算，绝不与上方主控按钮重叠遮挡。
    if (seek_dragging_ || is_bar_focused) {
        const std::string label = MediaScanner::formatDuration((int64_t)shown_pos);
        const float tw = ui::tabularTextWidth(vg, 12.0f, label);
        const float pw = tw + 16.0f;
        const float ph = 22.0f;
        float px = std::clamp(knob_x - pw * 0.5f, x + m, x + width - m - pw);
        const float py = bar_cy - 33.0f;
        ui::fillRoundRect(vg, px, py, pw, ph, ph * 0.5f,
                          nvgRGBA(18, 20, 24, (int)(240 * a)));
        ui::strokeRoundRect(vg, px, py, pw, ph, ph * 0.5f,
                          nvgRGBA(26, 159, 255, (int)(180 * a)));
        ui::tabularText(vg, px + pw * 0.5f, py + ph * 0.5f, 12.0f,
                        nvgRGBA(255, 255, 255, (int)(255 * a)),
                        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
    }

    // 时间：与进度条同一条基线。等宽数字排版，秒数流逝平滑无抖。
    const float time_cy = bar_cy;
    ui::tabularText(vg, x + m, time_cy, osd::FONT_TIME,
                    nvgRGBA(255, 255, 255, (int)(232 * a)),
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, pos_str);
    ui::tabularText(vg, x + width - m, time_cy, osd::FONT_TIME,
                    nvgRGBA(255, 255, 255, (int)(150 * a)),
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, total_str);

    // 图标按钮行（主控 + 设置），同时登记触摸命中区
    drawTouchControls(vg, x, y, width, height, bot_slide);
}

void MPVPlayerView::blitVideoTexture(int dst_x, int dst_y, int dst_w, int dst_h) {
    if (vid_fbo_ == 0 || dst_w <= 0 || dst_h <= 0) return;

    GLint prev_read = 0, prev_draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, vid_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // 注意：blit 会覆盖目标区域（含 UI）。因此只在视频区域内，
    // 且 UI 恰好绘制在该区域上时会丢失。
    glBlitFramebuffer(0, 0, vid_tex_w_, vid_tex_h_,
                      dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw);
}

// 触摸控件绘制。位置同时记录到 hit_rects_，供 hitTouchControls 命中测试。
//
// 布局（全部是矢量图标，没有一个中文标签方框）：
//   顶部左上   返回（圆形玻璃按钮）
//   主控行     上一集 / 快退10 / 播放暂停（实心强调色大圆）/ 快进10 / 下一集
//   设置行     字幕 / 音轨 / 倍速(胶囊) / 画面 / 列表 / 锁定 —— 右对齐
//
// 为什么分成两行：主控是「看片时随时会用」的高频操作，放低、放大、
// 居中，拇指最容易够到；设置是低频操作，放上一行、缩小、靠右，
// 不抢视觉重心。这也是 Plex / Apple TV / Emby 的一致做法。
void MPVPlayerView::drawTouchControls(NVGcontext* vg, float x, float y,
                                      float width, float height, float dy) {
    hit_rects_.clear();
    const float a = osd_alpha_;
    if (a <= 0.01f) return;

    const NVGcolor white = nvgRGBA(255, 255, 255, 255);
    const float m = osd::marginFor(width);
    const float top_slide = (1.0f - a) * -18.0f;

    auto pressed = [this](TouchAction ac) {
        return has_locked_action_ && locked_action_ == ac;
    };

    // 统一登记命中区：按钮的**可点范围**比视觉略大 4px，
    // 触屏上「差一点没点中」比「点到了但没反馈」更让人恼火。
    auto hit = [&](float cx, float cy, float d, TouchAction ac) {
        const float pad = 4.0f;
        hit_rects_.push_back({cx - d * 0.5f - pad, cy - d * 0.5f - pad,
                              d + pad * 2.0f, d + pad * 2.0f, ac});
    };

    // 玻璃质感圆按钮：深色半透明底 + 细亮描边。
    // 早期版本用「半透明白底 8%」，在亮画面上等于没画——控件直接糊进视频里，
    // 只能靠图标的白线勉强辨认。压暗底色后图标才有稳定对比度。
    auto ghost = [&](float cx, float cy, float d, ui::Icon ic, float iconSize,
                     TouchAction ac, OsdFocusItem f_item = OsdFocusItem::None) {
        const bool p = pressed(ac);
        const bool f = (osd_focus_ == f_item);

        NVGcolor bg = f ? nvgRGBA(26, 159, 255, (int)(65 * a))
                        : nvgRGBA(8, 10, 14, (int)((p ? 214 : 156) * a));
        NVGcolor border = f ? nvgRGBA(26, 159, 255, (int)(255 * a))
                            : nvgRGBA(26, 159, 255, (int)((p ? 200 : 40) * a));
        float stroke_w = f ? 2.5f : (p ? 1.6f : 1.0f);

        if (f) {
            ui::strokeCircle(vg, cx, cy, d * 0.5f + 3.0f,
                             nvgRGBA(102, 192, 244, (int)(140 * a)), 2.5f);
        }

        ui::drawIconButton(vg, ic, cx, cy, d, iconSize, bg, white, border, stroke_w);
        hit(cx, cy, d, ac);
    };

    // ── 返回按钮（顶部左侧）──
    {
        const float cx = x + m + 19.0f;
        const float cy = y + 44.0f + top_slide;
        const bool p = pressed(TouchAction::Back);
        const bool f = (osd_focus_ == OsdFocusItem::Back);

        NVGcolor bg = f ? nvgRGBA(26, 159, 255, (int)(65 * a))
                        : nvgRGBA(8, 10, 14, (int)((p ? 214 : 156) * a));
        NVGcolor border = f ? nvgRGBA(26, 159, 255, (int)(255 * a))
                            : nvgRGBA(255, 255, 255, (int)((p ? 120 : 40) * a));
        float stroke_w = f ? 2.5f : 1.0f;

        if (f) {
            ui::strokeCircle(vg, cx, cy, 22.0f,
                             nvgRGBA(102, 192, 244, (int)(140 * a)), 2.5f);
        }

        ui::drawIconButton(vg, ui::Icon::Back, cx, cy, 38.0f, 19.0f,
                           bg, white, border, stroke_w);
        hit_rects_.push_back({cx - 23.0f, cy - 23.0f, 46.0f, 46.0f,
                              TouchAction::Back});
    }

    // ── 主控块：水平居中对齐画面中心，播放按钮位于正中 ──
    const float tcx = osd::transportCenterX(x, width);
    const float tcy = osd::toolCenterY(y, height) + dy;

    ghost(tcx - 122.0f, tcy, osd::SKIP_D, ui::Icon::Prev, 20.0f, TouchAction::Prev, OsdFocusItem::Prev);
    ghost(tcx - 67.0f, tcy, osd::SIDE_D, ui::Icon::SeekBack, 21.0f,
          TouchAction::SeekBack, OsdFocusItem::SeekBack);

    // 播放/暂停：整屏唯一的实心强调色，视线自然落在它上面
    {
        const bool p = pressed(TouchAction::PlayPause);
        const bool f = (osd_focus_ == OsdFocusItem::PlayPause);
        const NVGcolor fill = p ? nvgRGBA(70, 185, 255, (int)(255 * a))
                                : nvgRGBA(26, 159, 255, (int)(255 * a));

        if (f) {
            ui::strokeCircle(vg, tcx, tcy, osd::TRANSPORT_D * 0.5f + 4.0f,
                             nvgRGBA(102, 192, 244, (int)(230 * a)), 3.0f);
            ui::strokeCircle(vg, tcx, tcy, osd::TRANSPORT_D * 0.5f + 7.5f,
                             nvgRGBA(26, 159, 255, (int)(110 * a)), 2.5f);
        }

        ui::fillCircle(vg, tcx, tcy, osd::TRANSPORT_D * 0.5f, fill);
        ui::drawIcon(vg, is_playing_ ? ui::Icon::Pause : ui::Icon::Play, tcx, tcy,
                     28.0f, nvgRGBA(10, 22, 36, (int)(255 * a)), 2.2f);
        hit(tcx, tcy, osd::TRANSPORT_D, TouchAction::PlayPause);
    }

    ghost(tcx + 67.0f, tcy, osd::SIDE_D, ui::Icon::SeekForward, 21.0f,
          TouchAction::SeekForward, OsdFocusItem::SeekForward);
    ghost(tcx + 122.0f, tcy, osd::SKIP_D, ui::Icon::Next, 20.0f, TouchAction::Next, OsdFocusItem::Next);

    // ── 设置块：右对齐 w − m，与主控块同一条轴 ──
    const float scy = tcy;
    float right = x + width - m;

    auto place = [&](ui::Icon ic, TouchAction ac, float iconSize, OsdFocusItem f_item) {
        const float cx = right - osd::SETTINGS_D * 0.5f;
        ghost(cx, scy, osd::SETTINGS_D, ic, iconSize, ac, f_item);
        right = cx - osd::SETTINGS_D * 0.5f - 12.0f;
    };

    place(ui::Icon::Lock, TouchAction::Lock, 18.0f, OsdFocusItem::Lock);
    place(ui::Icon::Playlist, TouchAction::Playlist, 19.0f, OsdFocusItem::Playlist);
    place(ui::Icon::Aspect, TouchAction::Aspect, 19.0f, OsdFocusItem::Aspect);

    // 倍速胶囊：数值本身就是信息，做成胶囊比做成图标更直观
    {
        char spd[16];
        const double rounded = std::round(playback_speed_ * 10.0) / 10.0;
        if (std::abs(rounded - std::round(rounded)) < 0.05)
            std::snprintf(spd, sizeof(spd), "%d×", (int)std::round(rounded));
        else
            std::snprintf(spd, sizeof(spd), "%.1f×", rounded);

        const float pill_w = 78.0f;
        const float pill_h = 34.0f;
        const float px = right - pill_w;
        const bool p = pressed(TouchAction::Speed);
        const bool f = (osd_focus_ == OsdFocusItem::Speed);

        NVGcolor bg = f ? nvgRGBA(26, 159, 255, (int)(65 * a))
                        : nvgRGBA(8, 10, 14, (int)((p ? 214 : 156) * a));
        NVGcolor border = f ? nvgRGBA(26, 159, 255, (int)(255 * a))
                            : nvgRGBA(26, 159, 255, (int)((p ? 200 : 90) * a));
        float stroke_w = f ? 2.5f : 1.0f;

        if (f) {
            ui::strokeRoundRect(vg, px - 3.0f, scy - pill_h * 0.5f - 3.0f,
                                pill_w + 6.0f, pill_h + 6.0f, (pill_h + 6.0f) * 0.5f,
                                nvgRGBA(102, 192, 244, (int)(140 * a)), 2.5f);
        }

        ui::fillRoundRect(vg, px, scy - pill_h * 0.5f, pill_w, pill_h,
                          pill_h * 0.5f, bg);
        ui::strokeRoundRect(vg, px, scy - pill_h * 0.5f, pill_w, pill_h,
                            pill_h * 0.5f, border, stroke_w);
        ui::drawIcon(vg, ui::Icon::Speed, px + 18.0f, scy, 16.0f,
                     nvgRGBA(26, 159, 255, (int)(240 * a)), 1.8f);
        ui::text(vg, px + 32.0f, scy + 0.5f, 14.0f,
                 nvgRGBA(255, 255, 255, (int)(240 * a)),
                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, spd);
        hit_rects_.push_back({px - 4.0f, scy - pill_h * 0.5f - 4.0f,
                              pill_w + 8.0f, pill_h + 8.0f, TouchAction::Speed});
        right = px - 12.0f;
    }

    place(ui::Icon::Audio, TouchAction::AudioTrack, 19.0f, OsdFocusItem::AudioTrack);
    const bool subs_on = selected_sid_ > 0;
    place(subs_on ? ui::Icon::Subtitles : ui::Icon::SubtitlesOff,
          TouchAction::SubtitleTrack, 19.0f, OsdFocusItem::SubtitleTrack);
}

// 屏幕 x 坐标 → 目标播放时间（秒）。
// 用于进度条拖动。超出范围自动夹紧到 [0, 总时长]。
double MPVPlayerView::seekBarTimeAt(float px) const {
    if (!seek_bar_.valid())
        return 0.0;
    const double total = seek_dragging_ && seek_preview_total_ > 0
                             ? seek_preview_total_ : (double)total_duration_;
    if (total <= 0.0)
        return 0.0;
    float ratio = (px - seek_bar_.x) / seek_bar_.w;
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    return ratio * total;
}

bool MPVPlayerView::hitTouchControls(float px, float py) {
    // 渐隐过半的按钮视为不可点（看得见轮廓，但避免误触正在消失的控件）
    if (osd_alpha_ < 0.5f) return false;
    for (const auto& r : hit_rects_) {
        if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h) {
            performTouchAction(r.action);
            return true;
        }
    }
    return false;
}

void MPVPlayerView::performTouchAction(TouchAction action) {
    last_user_action_time_ = std::chrono::steady_clock::now();
    switch (action) {
        case TouchAction::PlayPause:
            togglePlayPause();
            break;
        case TouchAction::SeekBack:
            seek(-10);
            break;
        case TouchAction::SeekForward:
            seek(10);
            break;
        case TouchAction::Prev:
        case TouchAction::Next: {
            // 上下集由 PlayerActivity 注入的回调处理（它才知道播放列表）
            if (!on_prev_next_cb_) {
                brls::Application::notify("没有可切换的剧集");
                break;
            }
            on_prev_next_cb_(action == TouchAction::Next);
            break;
        }
        case TouchAction::AudioTrack: {
            auto tracks = getTracks("audio");
            if (tracks.empty()) {
                brls::Application::notify("没有可切换的音轨");
                break;
            }
            TrackSelectionDialog::showAudioDialog(tracks, [this](int track_id) {
                selectTrack("audio", track_id);
            });
            break;
        }
        case TouchAction::SubtitleTrack: {
            auto tracks = getTracks("sub");
            if (tracks.empty()) {
                brls::Application::notify("没有可切换的字幕");
                break;
            }
            TrackSelectionDialog::showSubtitleDialog(tracks, subtitle_delay_,
                [this](int track_id) { selectTrack("sub", track_id); },
                [this](float delay) { setSubtitleDelay(delay); },
                [this]() { promptLoadExternalSubtitle(); });
            break;
        }
        case TouchAction::Aspect:
            cycleAspectRatio();
            break;
        case TouchAction::Speed:
            cyclePlaybackSpeed();
            break;
        case TouchAction::Lock:
            toggleTouchLock();
            break;
        case TouchAction::Playlist:
            if (on_show_playlist_cb_)
                on_show_playlist_cb_();
            break;
        case TouchAction::Back:
            brls::Application::popActivity();
            break;
    }
}

void MPVPlayerView::setupGestures() {
    // 单击：显示/隐藏 OSD；双击：播放/暂停
    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (soundToPlay) *soundToPlay = brls::SOUND_CLICK;

            // 按下态：手指落在按钮上时立刻高亮，松手清除。
            // 原实现里 has_locked_action_ 声明后从未被赋值，
            // 按钮永远画成静态的，点下去没有任何反馈 ——
            // 触屏用户会以为「点了没反应」。
            // 手指按下：borealis 的 TapGestureRecognizer 在未开启
            // forceRecognision 时把「按下」报成 **UNSURE**，
            // GestureState::START 在该识别器里根本不会被派发
            // （见 tap_gesture.hpp 的注释：START: unsupported）。
            // 因此必须把 UNSURE 一并当作按下，否则下面这段按下态逻辑
            // —— 以及长按加速的计时 —— 永远都不会执行。
            const bool finger_down =
                status.state == brls::GestureState::START ||
                status.state == brls::GestureState::UNSURE;

            if (finger_down) {
                has_locked_action_ = false;
                for (const auto& r : hit_rects_) {
                    if (status.position.x >= r.x && status.position.x <= r.x + r.w &&
                        status.position.y >= r.y && status.position.y <= r.y + r.h) {
                        locked_action_ = r.action;
                        has_locked_action_ = true;
                        break;
                    }
                }
                // 长按加速的起点：只有没落在按钮上时才计时，
                // 否则按住「倍速」按钮会同时触发长按加速，两个行为打架。
                touch_down_ = !has_locked_action_;
                press_moved_ = false;
                press_down_time_ = std::chrono::steady_clock::now();
                if (has_locked_action_) return;
            }
            if (status.state == brls::GestureState::END) {
                has_locked_action_ = false;
                touch_down_ = false;
                endHoldBoost();
            }

            if (status.state != brls::GestureState::END) return;

            auto now = std::chrono::steady_clock::now();

            // 先做控件命中测试：点在按钮上就直接执行，不再当作
            // 「单击切换 OSD」或「双击播放/暂停」。
            // 不先判断的话，点「字幕」按钮会同时触发 OSD 切换，
            // 按钮效果被吞掉。
            if (status.state == brls::GestureState::END &&
                !hit_rects_.empty() && !touch_locked_) {
                // 用结束点的位置判断（点按过程中手指几乎不动）
                if (hitTouchControls(status.position.x, status.position.y)) {
                    gesture_double_tap_pending_ = false;
                    return;
                }
            }

            // 双击判定：**时间 + 位置**双重条件。
            //
            // 原实现只比时间（间隔 < 300ms），不看两次点击的位置。
            // 后果：用户先点左上角、300ms 内再点右下角，会被判成双击
            // 而切换播放/暂停 —— 这是纯误触，在触屏上很容易发生。
            //
            // 现在要求两次落点相距 < 80 逻辑像素（约一个拇指宽度），
            // 超出就当作两次独立单击。
            const float kDoubleTapSlopPx = 80.0f;
            const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_touch_down_time_).count();
            const float dx = status.position.x - last_touch_pos_.x;
            const float dy = status.position.y - last_touch_pos_.y;
            const bool near_prev = (dx * dx + dy * dy) <
                                   (kDoubleTapSlopPx * kDoubleTapSlopPx);

            // 锁定态：OSD 不显示，但**必须**给用户一条出路。
            // 原实现里锁屏后单击/双击全部被吞掉，触屏用户只能靠手柄
            // 或重启才能解锁——等于把自己锁在门外。
            // 现在：单击 → 顶部弹出「双击解锁」提示；双击 → 解锁。
            if (touch_locked_) {
                if (gesture_double_tap_pending_ && dt_ms < 300 && near_prev) {
                    gesture_double_tap_pending_ = false;
                    toggleTouchLock();
                    osd_visible_ = true;
                    last_user_action_time_ = now;
                } else {
                    gesture_double_tap_pending_ = true;
                    last_touch_down_time_ = now;
                    last_touch_pos_ = status.position;
                    lock_hint_until_ = now + std::chrono::milliseconds(2600);
                }
                return;
            }

            if (gesture_double_tap_pending_ && dt_ms < 300 && near_prev) {
                // 双击：切换播放/暂停。
                // 注意第一次单击已经切换过 OSD 了，这里把它切回来，
                // 否则双击会在控制栏上闪现一下（观感很毛）。
                gesture_double_tap_pending_ = false;
                toggleOSD();   // 抵消第一次单击的效果
                togglePlayPause();
                last_user_action_time_ = now;
            } else {
                // 单击
                gesture_double_tap_pending_ = true;
                last_touch_down_time_ = now;
                last_touch_pos_ = status.position;
                toggleOSD();
            }
        }));

    // 垂直滑动：左半屏调节亮度，右半屏调节音量
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            (void)soundToPlay;
            if (touch_locked_) return;

            float view_w = this->getWidth();
            if (view_w <= 0.0f) return;

            switch (status.state) {
                case brls::GestureState::START: {
                    // 起点落在进度条判定区内时，这次手势属于「拖动进度」，
                    // 不能同时调亮度/音量。
                    if (seek_dragging_ ||
                        (seek_bar_.valid() &&
                         status.startPosition.x >= seek_bar_.x &&
                         status.startPosition.x <= seek_bar_.x + seek_bar_.w &&
                         status.startPosition.y >= seek_bar_.hit_y &&
                         status.startPosition.y <= seek_bar_.hit_y + seek_bar_.hit_h)) {
                        current_gesture_ = GestureMode::None;
                        return;
                    }
                    gesture_active_ = true;
                    // 以起点 X 坐标判断落在哪个半屏
                    bool left_half = status.startPosition.x < view_w / 2.0f;
                    current_gesture_ = left_half ? GestureMode::Brightness : GestureMode::Volume;
                    brightness_start_ = brightness_;
                    volume_start_ = volume_;
                    gesture_accumulatedY_ = 0.0f;
                    break;
                }
                case brls::GestureState::STAY: {
                    gesture_accumulatedY_ += status.delta.y;
                    // 手指已经明显移动 → 不再是「长按」，而是调亮度/音量
                    if (std::abs(gesture_accumulatedY_) > kHoldMoveSlopPx) {
                        press_moved_ = true;
                    }
                    // PanGesture 的 delta 定义为「上一帧位置 - 当前帧位置」。
                    // 因此向上滑（y 递减）时 delta.y 为【正】，
                    // 直接用累加值除以灵敏度即为正向增量，无需取反。
                    // （此前误当作「当前 - 上一帧」而取了负号，导致
                    //   向上滑反而降低音量/亮度，方向完全相反。）
                    // 灵敏度按**屏幕高度的比例**计算，而不是固定 300px。
                    //
                    // 原来写死 300：在 1280x906 的界面上，手指只滑三分之一屏
                    // 就能把音量从 0 拉到 100%，几乎无法精调；而在小屏上
                    // 又需要滑很多次才到头。同一个手势在不同设备上手感差异巨大。
                    //
                    // 现在：滑过约 1.2 个屏高 = 满量程，与尺寸无关。
                    const float view_h = this->getHeight() > 0.0f
                                             ? this->getHeight() : 906.0f;
                    const float sensitivity = view_h * 1.2f;
                    float ratio = gesture_accumulatedY_ / sensitivity;
                    if (current_gesture_ == GestureMode::Brightness) {
                        setBrightness(brightness_start_ + ratio);
                    } else if (current_gesture_ == GestureMode::Volume) {
                        setVolume(volume_start_ + (int)(ratio * 100.0f));
                    }
                    last_user_action_time_ = std::chrono::steady_clock::now();
                    break;
                }
                case brls::GestureState::END: {
                    gesture_active_ = false;
                    current_gesture_ = GestureMode::None;
                    break;
                }
                default:
                    break;
            }
        },
        brls::PanAxis::VERTICAL));

    // 水平滑动：快进/快退，带实时时间预览
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            (void)soundToPlay;
            if (touch_locked_) return;

            float view_w = this->getWidth();
            if (view_w <= 0.0f || total_duration_ <= 0) return;

            // ── 进度条拖动优先 ──
            // 判定：起点落在进度条的加高命中区内。
            // 这是播放器最基础的交互，必须比「全屏横滑 seek」优先，
            // 否则用户在进度条上横向拖动会变成全屏 seek，条本身纹丝不动。
            const bool on_seek_bar =
                seek_bar_.valid() &&
                status.startPosition.x >= seek_bar_.x &&
                status.startPosition.x <= seek_bar_.x + seek_bar_.w &&
                status.startPosition.y >= seek_bar_.hit_y &&
                status.startPosition.y <= seek_bar_.hit_y + seek_bar_.hit_h;

            if (on_seek_bar || seek_dragging_) {
                switch (status.state) {
                    case brls::GestureState::START:
                        seek_dragging_ = true;
                        seek_preview_total_ = (double)total_duration_;
                        seek_preview_ = seekBarTimeAt(status.position.x);
                        last_user_action_time_ = std::chrono::steady_clock::now();
                        break;
                    case brls::GestureState::STAY:
                        // 只更新预览，不真的 seek。
                        // 边拖边 seek 会让解码器疲于奔命、画面卡顿，
                        // 松手时再一次性跳转。
                        seek_preview_ = seekBarTimeAt(status.position.x);
                        last_user_action_time_ = std::chrono::steady_clock::now();
                        break;
                    case brls::GestureState::END:
                        if (seek_dragging_) {
                            seekTo((int64_t)seek_preview_);
                            showHUD("replay",
                                    MediaScanner::formatDuration((int64_t)seek_preview_));
                            seek_dragging_ = false;
                        }
                        break;
                    default:
                        break;
                }
                return;
            }

            switch (status.state) {
                case brls::GestureState::START: {
                    gesture_active_ = true;
                    current_gesture_ = GestureMode::Seek;
                    seek_start_pos_ = current_position_;
                    gesture_accumulatedX_ = 0.0f;
                    gesture_preview_position_ = current_position_;
                    break;
                }
                case brls::GestureState::STAY: {
                    gesture_accumulatedX_ += status.delta.x;
                    if (std::abs(gesture_accumulatedX_) > kHoldMoveSlopPx) {
                        press_moved_ = true;
                    }
                    // PanGesture 的 delta = 「上一帧 - 当前帧」，与直觉相反：
                    // 向右滑时 delta.x 为负。取负号才是「向右 = 前进」。
                    const float moved = -gesture_accumulatedX_;
                    // 划过整屏宽度对应半个影片时长（至少 30 秒），
                    // 保证短片也能滑出可见跨度、长片又能保持精度。
                    double span = std::max((double)total_duration_ * 0.5, 30.0);
                    int64_t delta = (int64_t)(moved / view_w * span);
                    int64_t target = seek_start_pos_ + delta;
                    if (target < 0) target = 0;
                    if (target > total_duration_) target = total_duration_;
                    gesture_preview_position_ = target;

                    float progress = total_duration_ > 0
                        ? (float)target / (float)total_duration_ : 0.0f;
                    showHUD("fast_forward",
                        MediaScanner::formatDuration(target) + " / " +
                        MediaScanner::formatDuration(total_duration_), progress);
                    last_user_action_time_ = std::chrono::steady_clock::now();
                    break;
                }
                case brls::GestureState::END: {
                    if (gesture_preview_position_ != seek_start_pos_) {
                        seekTo(gesture_preview_position_);
                    }
                    gesture_active_ = false;
                    current_gesture_ = GestureMode::None;
                    break;
                }
                default:
                    break;
            }
        },
        brls::PanAxis::HORIZONTAL));
}

void MPVPlayerView::updateTouchUI() {
    // 手势完成后的 UI 状态由 draw() 依据当前字段直接渲染
}

MPVPlayerView::~MPVPlayerView() {
    brls::Application::postDrawHook = nullptr;
    releaseVideoTarget();
    if (!current_file_path_.empty() && current_position_ > 0) {
        PlaybackHistory::getInstance().updateProgress(
            current_file_path_, current_position_, total_duration_,
            selected_sid_, selected_aid_
        );
    }
    destroyMPV();
}

void MPVPlayerView::initMPV() {
    mpv_ = mpv_create();
    if (!mpv_) return;

    // 针对 Steam Deck (AMD APU) 与现代 Linux 深度调优
    // Android 上硬解（MediaCodec → GLES 外部纹理 OES）需要
    // glEGLImageTargetTexture2DOES 等扩展函数，通过 eglGetProcAddress
    // 取到的指针在部分驱动上是空/无效的，表现为**花屏**。
    // 用环境变量可临时关闭以定位问题；默认仍启用硬解。
    if (std::getenv("MUVIK_NO_HWDEC")) {
        mpv_set_option_string(mpv_, "hwdec", "no");
        brls::Logger::info("MPVPlayerView: 硬解已禁用（MUVIK_NO_HWDEC）");
    } else {
        mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    }

    // 针对掌机与嵌入式多核 CPU (如 Cortex-A53 / x86) 开启多线程软解
    const int cpu_threads = std::clamp((int)std::thread::hardware_concurrency(), 2, 8);
    mpv_set_option_string(mpv_, "vd-lavc-threads", std::to_string(cpu_threads).c_str());

    // 影音同步机制：以音频时钟为主，避免因视频解码丢帧导致音频断续爆音
    mpv_set_option_string(mpv_, "video-sync", "audio");

    // 软解性能缓冲：在低功耗核心面临高码率突发时允许丢弃非关键帧追赶时钟，保证播放平滑
    mpv_set_option_string(mpv_, "framedrop", "decoder");

    // 快速解码模式：允许跳过非关键去块效应滤波，大幅减轻低算力芯片解码压力
    mpv_set_option_string(mpv_, "vd-lavc-fast", "yes");

    // 缓冲区大小上限：32MiB 防止 1GB 内存掌机（如 TrimUI Smart Pro）OOM，兼顾预读平滑
    mpv_set_option_string(mpv_, "demuxer-max-bytes", "33554432");

    mpv_set_option_string(mpv_, "vo", "libmpv");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "sub-auto", "fuzzy");
    mpv_set_option_string(mpv_, "sub-file-paths", "sub:subs:subtitles:字幕:Sub:Subs:Subtitles");

    // 针对嵌入式/无系统字体的掌机设备（如 TrimUI / 极简 Linux）配置内置字体目录与中文字体
    std::string font_dir = ResourceLocator::resourceDir() + "/font";
    std::error_code fec;
    if (std::filesystem::exists(font_dir, fec)) {
        mpv_set_option_string(mpv_, "sub-fonts-dir", font_dir.c_str());
    }
    mpv_set_option_string(mpv_, "sub-font", "Noto Sans SC");

    // 字幕字号按**屏幕高度比例**计算，而不是写死 46。
    //
    // 写死的后果（实测推算）：
    //   屏高  360 → 字幕占 12.8%（过大，遮挡画面）
    //   屏高  906 → 占 5.1%（合适）
    //   屏高 2400 → 占  1.9%（Android 平板上过小，看不清）
    // 同一个数值在不同设备上观感完全不同。
    //
    // mpv 的 sub-font-size 以视频高度的 1/100 为缩放基准，
    // 因此这里换算成「占屏高 5.5%」的等效字号。
    // 注意：取的是**逻辑高度**（contentHeight），不是物理像素。
    {
        const float logical_h = brls::Application::contentHeight > 0.0f
                                    ? brls::Application::contentHeight : 906.0f;
        const int sub_size = (int)std::clamp(logical_h * 0.055f, 18.0f, 96.0f);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", sub_size);
        mpv_set_option_string(mpv_, "sub-font-size", buf);
        brls::Logger::info("字幕字号: {}（屏高 {:.0f} 的 5.5%）", sub_size, logical_h);
    }

    // 字幕定位与排版：
    // sub-pos 设为 100 沉在画面最底部，配合合理的 sub-margin-y 避免顶边；
    // 允许在黑边中渲染字幕（sub-use-margins）。
    mpv_set_option_string(mpv_, "sub-pos", "100");
    mpv_set_option_string(mpv_, "sub-margin-y", "28");
    mpv_set_option_string(mpv_, "sub-use-margins", "yes");

    // ASS 特效字幕：仅按屏幕缩放，禁止强制覆盖 ASS 自带的排版坐标、样式与特效
    mpv_set_option_string(mpv_, "sub-ass-override", "scale");
    mpv_set_option_string(mpv_, "sub-ass-force-margins", "no");
    mpv_set_option_string(mpv_, "sub-ass-vsfilter-aspect-compat", "yes");

    // SRT 纯文本字幕：纯白文字 + 粗黑描边 + 柔和阴影，确保在任何复杂背景画面中清晰易读
    mpv_set_option_string(mpv_, "sub-color", "#FFFFFFFF");
    mpv_set_option_string(mpv_, "sub-border-color", "#FF000000");
    mpv_set_option_string(mpv_, "sub-shadow-color", "#80000000");
    mpv_set_option_string(mpv_, "sub-shadow-offset", "1.2");
    mpv_set_option_string(mpv_, "sub-codepage", "auto");

    // 描边同样按比例微调
    {
        const float logical_h = brls::Application::contentHeight > 0.0f
                                    ? brls::Application::contentHeight : 906.0f;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f",
                      std::clamp(logical_h * 0.003f, 2.0f, 6.0f));
        mpv_set_option_string(mpv_, "sub-border-size", buf);
    }
    mpv_set_option_string(mpv_, "ytdl", "no");

    if (mpv_initialize(mpv_) < 0) {
        destroyMPV();
        return;
    }

    // 订阅播放状态属性。必须显式调用 mpv_observe_property，否则 mpv 不会发送
    // MPV_EVENT_PROPERTY_CHANGE，进度、时长、暂停状态将永远停留在初始值，
    // 导致进度条、时间显示、断点续播与播放历史全部失效。
    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    // keep-open=yes 会【抑制】MPV_EVENT_END_FILE，导致「播完自动下一集」永远不触发。
    // 实测：keep-open=yes 时 5 秒内收不到 END_FILE；设为 no 才能收到。
    // 但我们又想保留最后一帧（避免播完立刻黑屏），所以订阅 eof-reached，
    // 在它为真时手动触发结束回调。
    mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "track-list", MPV_FORMAT_NONE);

    mpv_opengl_init_params gl_params{get_proc_address_mpv, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&mpv_gl_, mpv_, params) < 0) {
        destroyMPV();
        return;
    }

    mpv_render_context_set_update_callback(mpv_gl_, on_mpv_redraw, this);
}

void MPVPlayerView::destroyMPV() {
    if (mpv_gl_) {
        mpv_render_context_free(mpv_gl_);
        mpv_gl_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

void MPVPlayerView::openFile(const std::string& path, const std::string& title, int64_t resume_position) {
    if (!current_file_path_.empty() && current_position_ > 0) {
        PlaybackHistory::getInstance().updateProgress(
            current_file_path_, current_position_, total_duration_,
            selected_sid_, selected_aid_
        );
    }
    last_saved_position_ = resume_position;
    current_file_path_ = path;
    video_title_ = title.empty() ? std::filesystem::path(path).filename().string() : title;
    if (!mpv_) return;

    brls::Logger::info("MPVPlayerView: 打开文件 {}", path);
    eof_fired_ = false;      // 新文件：允许再次触发连播
    file_loaded_ = false;    // 在收到 FILE_LOADED 前忽略 eof-reached

    // 恢复上次选的字幕轨 / 音轨。
    // 之前只把 sid/aid 存进了历史，打开时却从不读回来 ——
    // 用户选了中文字幕，下次打开又变回第一条（实测复现）。
    subtitle_chosen_ = false;
    HistoryRecord rec;
    if (PlaybackHistory::getInstance().getRecord(path, rec)) {
        selected_sid_ = rec.sid;
        selected_aid_ = rec.aid;
        brls::Logger::info("MPVPlayerView: 恢复轨道 sid={} aid={}",
                           selected_sid_, selected_aid_);
    } else {
        selected_sid_ = 1;   // 默认：启用字幕（取第一条）
        selected_aid_ = 1;
    }

    pending_resume_position_ = resume_position;
    if (resume_position > 0) {
        std::string pos_str = std::to_string(resume_position);
        mpv_set_property_string(mpv_, "start", pos_str.c_str());
        brls::Logger::info("MPVPlayerView: 设置断点续播 start={}", pos_str);
    } else {
        mpv_set_property_string(mpv_, "start", "none");
    }

    last_eof_time_ = std::chrono::steady_clock::now();  // 为防抖窗口起点
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    int rc = mpv_command_async(mpv_, 0, cmd);
    brls::Logger::info("MPVPlayerView: loadfile 返回 {}", rc);

    is_playing_ = true;
}

void MPVPlayerView::togglePlayPause() {
    if (!mpv_) return;
    int pause_val = is_playing_ ? 1 : 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_val);
    is_playing_ = !is_playing_;
    last_user_action_time_ = std::chrono::steady_clock::now();
    showHUD(is_playing_ ? "play" : "pause", is_playing_ ? "播放" : "暂停");
    if (!is_playing_ && !current_file_path_.empty() && current_position_ > 0) {
        last_saved_position_ = current_position_;
        PlaybackHistory::getInstance().updateProgress(
            current_file_path_, current_position_, total_duration_,
            selected_sid_, selected_aid_
        );
    }
}

void MPVPlayerView::play() {
    if (!mpv_) return;
    int pause_val = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_val);
    is_playing_ = true;
    last_user_action_time_ = std::chrono::steady_clock::now();
}

void MPVPlayerView::pause() {
    if (!mpv_) return;
    int pause_val = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_val);
    is_playing_ = false;
    last_user_action_time_ = std::chrono::steady_clock::now();
    if (!current_file_path_.empty() && current_position_ > 0) {
        last_saved_position_ = current_position_;
        PlaybackHistory::getInstance().updateProgress(
            current_file_path_, current_position_, total_duration_,
            selected_sid_, selected_aid_
        );
    }
}

void MPVPlayerView::showOSD(bool show) {
    osd_visible_ = show;
    if (show) {
        last_user_action_time_ = std::chrono::steady_clock::now();
    }
}

void MPVPlayerView::toggleOSD() {
    auto now = std::chrono::steady_clock::now();
    constexpr int64_t kOsdIdleTimeoutMs = 4500;
    const auto idle_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_user_action_time_).count();
    const bool currently_shown = osd_visible_ && idle_ms < kOsdIdleTimeoutMs;
    if (currently_shown) {
        showOSD(false);
    } else {
        showOSD(true);
        osd_focus_ = OsdFocusItem::PlayPause;
    }
}

bool MPVPlayerView::isOsdActive() const {
    if (touch_locked_) return false;
    auto now = std::chrono::steady_clock::now();
    constexpr int64_t kOsdIdleTimeoutMs = 4500;
    const auto idle_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_user_action_time_).count();
    return osd_visible_ && idle_ms < kOsdIdleTimeoutMs && osd_alpha_ > 0.05f;
}

void MPVPlayerView::navigateOsdFocus(brls::FocusDirection direction) {
    static const std::vector<OsdFocusItem> kBottomRow = {
        OsdFocusItem::Prev,
        OsdFocusItem::SeekBack,
        OsdFocusItem::PlayPause,
        OsdFocusItem::SeekForward,
        OsdFocusItem::Next,
        OsdFocusItem::SubtitleTrack,
        OsdFocusItem::AudioTrack,
        OsdFocusItem::Speed,
        OsdFocusItem::Aspect,
        OsdFocusItem::Playlist,
        OsdFocusItem::Lock
    };

    switch (direction) {
        case brls::FocusDirection::LEFT: {
            if (osd_focus_ == OsdFocusItem::ProgressBar) {
                seek(-10);
            } else if (osd_focus_ == OsdFocusItem::Back) {
                // 保持
            } else {
                auto it = std::find(kBottomRow.begin(), kBottomRow.end(), osd_focus_);
                if (it != kBottomRow.end() && it != kBottomRow.begin()) {
                    osd_focus_ = *(it - 1);
                } else if (it == kBottomRow.end()) {
                    osd_focus_ = OsdFocusItem::PlayPause;
                }
            }
            break;
        }
        case brls::FocusDirection::RIGHT: {
            if (osd_focus_ == OsdFocusItem::ProgressBar) {
                seek(10);
            } else if (osd_focus_ == OsdFocusItem::Back) {
                // 保持
            } else {
                auto it = std::find(kBottomRow.begin(), kBottomRow.end(), osd_focus_);
                if (it != kBottomRow.end() && (it + 1) != kBottomRow.end()) {
                    osd_focus_ = *(it + 1);
                } else if (it == kBottomRow.end()) {
                    osd_focus_ = OsdFocusItem::PlayPause;
                }
            }
            break;
        }
        case brls::FocusDirection::UP: {
            if (osd_focus_ == OsdFocusItem::ProgressBar) {
                osd_focus_ = OsdFocusItem::PlayPause;
            } else if (osd_focus_ == OsdFocusItem::Back) {
                // 已经在顶部返回按钮，保持
            } else {
                osd_focus_ = OsdFocusItem::Back;
            }
            break;
        }
        case brls::FocusDirection::DOWN: {
            if (osd_focus_ == OsdFocusItem::Back) {
                osd_focus_ = OsdFocusItem::PlayPause;
            } else if (osd_focus_ == OsdFocusItem::ProgressBar) {
                // 已经在底部进度条，保持
            } else {
                osd_focus_ = OsdFocusItem::ProgressBar;
            }
            break;
        }
    }
}

void MPVPlayerView::activateOsdFocus() {
    switch (osd_focus_) {
        case OsdFocusItem::Back:
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            break;
        case OsdFocusItem::ProgressBar:
            togglePlayPause();
            break;
        case OsdFocusItem::Prev:
            performTouchAction(TouchAction::Prev);
            break;
        case OsdFocusItem::SeekBack:
            performTouchAction(TouchAction::SeekBack);
            break;
        case OsdFocusItem::PlayPause:
            performTouchAction(TouchAction::PlayPause);
            break;
        case OsdFocusItem::SeekForward:
            performTouchAction(TouchAction::SeekForward);
            break;
        case OsdFocusItem::Next:
            performTouchAction(TouchAction::Next);
            break;
        case OsdFocusItem::SubtitleTrack:
            performTouchAction(TouchAction::SubtitleTrack);
            break;
        case OsdFocusItem::AudioTrack:
            performTouchAction(TouchAction::AudioTrack);
            break;
        case OsdFocusItem::Speed:
            performTouchAction(TouchAction::Speed);
            break;
        case OsdFocusItem::Aspect:
            performTouchAction(TouchAction::Aspect);
            break;
        case OsdFocusItem::Playlist:
            performTouchAction(TouchAction::Playlist);
            break;
        case OsdFocusItem::Lock:
            performTouchAction(TouchAction::Lock);
            break;
        case OsdFocusItem::None:
            togglePlayPause();
            break;
    }
}

void MPVPlayerView::seek(int64_t seconds_offset) {
    if (!mpv_) return;
    std::string offset_str = (seconds_offset > 0 ? "+" : "") + std::to_string(seconds_offset);
    const char* cmd[] = {"seek", offset_str.c_str(), "relative", nullptr};
    mpv_command_async(mpv_, 0, cmd);

    last_user_action_time_ = std::chrono::steady_clock::now();
    std::string sign = seconds_offset > 0 ? "+" : "";
    showHUD("fast_forward", sign + std::to_string(seconds_offset) + "s", 
            total_duration_ > 0 ? (float)current_position_ / total_duration_ : 0.0f);
}

void MPVPlayerView::seekTo(int64_t target_seconds) {
    if (!mpv_) return;
    std::string target_str = std::to_string(target_seconds);
    const char* cmd[] = {"seek", target_str.c_str(), "absolute", nullptr};
    mpv_command_async(mpv_, 0, cmd);
}

void MPVPlayerView::cyclePlaybackSpeed() {
    static const double speeds[] = {1.0, 1.25, 1.5, 2.0, 0.75};
    static const size_t count = sizeof(speeds) / sizeof(speeds[0]);

    size_t next_idx = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::abs(playback_speed_ - speeds[i]) < 0.05) {
            next_idx = (i + 1) % count;
            break;
        }
    }
    setPlaybackSpeed(speeds[next_idx]);
}

void MPVPlayerView::setPlaybackSpeed(double speed) {
    playback_speed_ = speed;
    if (!mpv_) return;
    mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &playback_speed_);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << playback_speed_ << "x 倍速";
    showHUD("speed", oss.str());
}

// ── 长按加速 ────────────────────────────────────────────────
//
// 交互约定：手指按住画面不动 → 临时提速到 2x，松手立刻恢复原速。
// 与「R3 循环倍速」的区别是它是**临时**的，不覆盖用户设定的倍速。
//
// 三条必须同时满足才提速，缺一个都会变成误触：
//   1. 手指确实还按着（touch_down_）
//   2. 按下后没有明显移动（否则是在调音量/亮度或横滑快进）
//   3. 没落在 OSD 按钮/进度条上（否则与按钮点击打架）
void MPVPlayerView::updateHoldBoost() {
    if (!touch_down_ || hold_boosting_) return;
    if (touch_locked_ || press_moved_ || has_locked_action_) return;
    if (seek_dragging_ || !mpv_ || total_duration_ <= 0) return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - press_down_time_).count();
    if (elapsed < kHoldBoostDelayMs) return;

    beginHoldBoost();
}

void MPVPlayerView::beginHoldBoost() {
    // 已经是 2x 以上时再翻倍到 4x，否则「长按更快」在高速档下没有体感
    const double target = playback_speed_ < kHoldBoostSpeed
                              ? kHoldBoostSpeed
                              : std::min(playback_speed_ * 2.0, 8.0);
    speed_before_boost_ = playback_speed_;
    hold_boosting_ = true;
    setPlaybackSpeed(target);

    // setPlaybackSpeed 已经弹了 HUD，这里覆盖成更能说明状态的文案
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << target << "x 长按快进";
    showHUD("speed", oss.str());
    last_user_action_time_ = std::chrono::steady_clock::now();
}

void MPVPlayerView::endHoldBoost() {
    if (!hold_boosting_) return;
    hold_boosting_ = false;
    setPlaybackSpeed(speed_before_boost_);
}

double MPVPlayerView::getPlaybackSpeed() const {
    return playback_speed_;
}

void MPVPlayerView::cycleAspectRatio() {
    int next = (static_cast<int>(aspect_mode_) + 1) % 5;
    setAspectRatio(static_cast<AspectRatioMode>(next));
}

void MPVPlayerView::setAspectRatio(AspectRatioMode mode) {
    aspect_mode_ = mode;
    if (!mpv_) return;

    std::string prompt;
    switch (aspect_mode_) {
        case AspectRatioMode::Auto:
            mpv_set_property_string(mpv_, "panscan", "0.0");
            mpv_set_property_string(mpv_, "video-aspect-override", "-1");
            prompt = "画面比例: 原始自适应";
            break;
        case AspectRatioMode::Stretch_16_10:
            mpv_set_property_string(mpv_, "panscan", "0.0");
            mpv_set_property_string(mpv_, "video-aspect-override", "1.6");
            prompt = "画面比例: 16:10 满屏 (Steam Deck 专用)";
            break;
        case AspectRatioMode::Fit_16_9:
            mpv_set_property_string(mpv_, "panscan", "0.0");
            mpv_set_property_string(mpv_, "video-aspect-override", "1.777777");
            prompt = "画面比例: 16:9 标准宽屏";
            break;
        case AspectRatioMode::Fit_4_3:
            mpv_set_property_string(mpv_, "panscan", "0.0");
            mpv_set_property_string(mpv_, "video-aspect-override", "1.333333");
            prompt = "画面比例: 4:3 复古动画/电视";
            break;
        case AspectRatioMode::ZoomCrop:
            mpv_set_property_string(mpv_, "video-aspect-override", "-1");
            mpv_set_property_string(mpv_, "panscan", "1.0");
            prompt = "画面比例: 裁切铺满全屏";
            break;
    }
    showHUD("aspect_ratio", prompt);
}

void MPVPlayerView::toggleTouchLock() {
    touch_locked_ = !touch_locked_;
    if (touch_locked_) {
        showHUD("lock", i18n::tr("player.lock_hint", "屏幕已锁定 · 连按两次 A 键或双击屏幕解锁"));
    } else {
        showHUD("lock_open", i18n::tr("player.unlocked", "屏幕已解锁"));
    }
}

void MPVPlayerView::setVolume(int volume) {
    volume_ = std::clamp(volume, 0, 100);
    if (!mpv_) return;
    double vol = volume_;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &vol);
    showHUD("volume_up", "音量 " + std::to_string(volume_) + "%", volume_ / 100.0f);
}

int MPVPlayerView::getVolume() const {
    return volume_;
}

void MPVPlayerView::setBrightness(float brightness) {
    brightness_ = std::clamp(brightness, 0.1f, 1.0f);
    // 参考 wiliwili：能让系统改背光，就不要用黑色蒙层糊画面 —— 蒙层只是
    // 「把亮部压暗」，暗部细节一起被吃掉，而且没有任何一个像素真的变暗。
    // 只有在没有背光接口的平台（桌面 / macOS）才退回模拟。
    if (backlight_capable_) {
        if (auto* platform = brls::Application::getPlatform())
            platform->setBacklightBrightness(brightness_);
    }
    showHUD("brightness_high", "亮度 " + std::to_string((int)(brightness_ * 100)) + "%", brightness_);
}

float MPVPlayerView::getBrightness() const {
    return brightness_;
}

void MPVPlayerView::showHUD(const std::string& icon, const std::string& text, float progress) {
    hud_icon_ = icon;
    hud_text_ = text;
    hud_progress_ = progress;
    hud_expire_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
}

std::vector<TrackInfo> MPVPlayerView::getTracks(const std::string& type) {
    std::vector<TrackInfo> tracks;
    if (!mpv_) return tracks;

    mpv_node node;
    if (mpv_get_property(mpv_, "track-list", MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY) {
            for (int i = 0; i < node.u.list->num; i++) {
                if (node.u.list->values[i].format == MPV_FORMAT_NODE_MAP) {
                    mpv_node_list* map = node.u.list->values[i].u.list;
                    TrackInfo t;
                    for (int j = 0; j < map->num; j++) {
                        std::string key = map->keys[j];
                        if (key == "id" && map->values[j].format == MPV_FORMAT_INT64) {
                            t.id = (int)map->values[j].u.int64;
                        } else if (key == "type" && map->values[j].format == MPV_FORMAT_STRING) {
                            t.type = map->values[j].u.string;
                        } else if (key == "title" && map->values[j].format == MPV_FORMAT_STRING) {
                            t.title = map->values[j].u.string;
                        } else if (key == "lang" && map->values[j].format == MPV_FORMAT_STRING) {
                            t.lang = map->values[j].u.string;
                        } else if (key == "selected" && map->values[j].format == MPV_FORMAT_FLAG) {
                            t.selected = (map->values[j].u.flag != 0);
                        } else if (key == "codec" && map->values[j].format == MPV_FORMAT_STRING) {
                            t.codec = map->values[j].u.string;
                        } else if (key == "external" && map->values[j].format == MPV_FORMAT_FLAG) {
                            t.external = (map->values[j].u.flag != 0);
                        } else if (key == "external-filename" && map->values[j].format == MPV_FORMAT_STRING) {
                            t.external = true;
                        }
                    }
                    if (t.type == type) {
                        tracks.push_back(t);
                    }
                }
            }
        }
        mpv_free_node_contents(&node);
    }
    return tracks;
}

void MPVPlayerView::selectTrack(const std::string& type, int track_id, bool show_hud) {
    if (!mpv_) return;
    std::string prop = (type == "audio") ? "aid" : "sid";

    // 记录用户选择，便于下次续播时恢复
    if (type == "audio") selected_aid_ = track_id;
    else selected_sid_ = track_id;

    if (track_id <= 0) {
        mpv_set_property_string(mpv_, prop.c_str(), "no");
        if (show_hud) {
            showHUD("subtitles_off", type == "audio" ? "静音" : "关闭字幕");
        }
    } else {
        std::string val = std::to_string(track_id);
        mpv_set_property_string(mpv_, prop.c_str(), val.c_str());

        if (show_hud) {
            // 从轨道列表里取显示名，比裸轨道号友好
            std::string label = (type == "audio" ? "已选音轨: " : "已选字幕: ");
            for (const auto& t : getTracks(type)) {
                if (t.id == track_id) {
                    label += t.displayName();
                    break;
                }
            }
            showHUD("subtitles", label);
        }
    }

    // 立即落盘当前进度与轨道选择，避免退出时丢失
    if (!current_file_path_.empty()) {
        PlaybackHistory::getInstance().updateProgress(
            current_file_path_, current_position_, total_duration_,
            selected_sid_, selected_aid_);
    }
}

void MPVPlayerView::setSubtitleDelay(float seconds) {
    subtitle_delay_ = seconds;
    if (!mpv_) return;
    double val = seconds;
    mpv_set_property(mpv_, "sub-delay", MPV_FORMAT_DOUBLE, &val);
    std::ostringstream oss;
    oss << "字幕延时: " << (seconds >= 0 ? "+" : "") << seconds << "s";
    showHUD("timer", oss.str());
}

void MPVPlayerView::loadExternalSubtitle(const std::string& path) {
    if (!mpv_ || path.empty()) return;

    // mpv 的 sub-add 会把外挂字幕作为新的 external 轨道加入 track-list，
    // 因此无需重建播放器，可即时生效。
    const char* cmd[] = {"sub-add", path.c_str(), "select", nullptr};
    if (mpv_command(mpv_, cmd) < 0) {
        showHUD("error", "字幕加载失败，请确认格式受支持");
        return;
    }

    // 保持原有的字幕延时设置
    if (subtitle_delay_ != 0.0f) {
        double val = subtitle_delay_;
        mpv_set_property(mpv_, "sub-delay", MPV_FORMAT_DOUBLE, &val);
    }

    std::filesystem::path p(path);
    showHUD("subtitles", "已加载字幕: " + p.filename().string());
}

void MPVPlayerView::promptLoadExternalSubtitle() {
    // 用文件选择器，默认停在当前视频所在目录 —— 同名字幕 90% 的情况
    // 已经在那里了。原来的实现是弹 IME 让用户手打绝对路径：
    // 在掌机上敲 /run/media/mmcblk0p1/Videos/Movie.2024.2160p/movie.chs.ass
    // 不可能完成，等于没有这个功能。
    std::string dir;
    std::string prefix;
    if (!current_file_path_.empty()) {
        std::filesystem::path p(current_file_path_);
        dir = p.parent_path().string();
        prefix = p.stem().string();
    }

    auto* picker = FilePickerActivity::files(
        {".srt", ".ass", ".ssa", ".vtt", ".sup"}, dir, prefix, "选择字幕文件",
        [this](const std::string& path) { loadExternalSubtitle(path); });
    brls::Application::pushActivity(picker);
}



void MPVPlayerView::updateFormatBadges() {
    format_badges_.clear();
    if (!mpv_) return;

    // 1. 分辨率徽标（4K / 1080P / 720P / 自定义）
    if (video_w_ >= 3800 || video_h_ >= 2100) {
        format_badges_.push_back({"4K", true});
    } else if (video_w_ >= 1900 || video_h_ >= 1060) {
        format_badges_.push_back({"1080P", false});
    } else if (video_w_ >= 1260 || video_h_ >= 700) {
        format_badges_.push_back({"720P", false});
    } else if (video_w_ > 0 && video_h_ > 0) {
        format_badges_.push_back({std::to_string(video_w_) + "×" + std::to_string(video_h_), false});
    }

    // 2. HDR / 色彩空间 / 10-BIT
    char* gamma = mpv_get_property_string(mpv_, "video-params/gamma");
    char* prim = mpv_get_property_string(mpv_, "video-params/primaries");
    char* pixfmt = mpv_get_property_string(mpv_, "video-params/pixelformat");
    bool is_hdr = false;
    if (gamma) {
        std::string g(gamma);
        if (g == "pq" || g == "hlg") is_hdr = true;
    }
    if (prim && std::string(prim).find("bt.2020") != std::string::npos) is_hdr = true;
    if (is_hdr) {
        format_badges_.push_back({"HDR", true});
    } else if (pixfmt) {
        std::string pf(pixfmt);
        if (pf.find("10") != std::string::npos || pf.find("12") != std::string::npos) {
            format_badges_.push_back({"10-BIT", false});
        }
    }
    if (gamma) mpv_free(gamma);
    if (prim) mpv_free(prim);
    if (pixfmt) mpv_free(pixfmt);

    // 3. 视频编码
    char* vcodec = mpv_get_property_string(mpv_, "video-format");
    if (!vcodec) vcodec = mpv_get_property_string(mpv_, "video-codec");
    if (vcodec) {
        std::string vc(vcodec);
        std::transform(vc.begin(), vc.end(), vc.begin(), ::toupper);
        if (vc == "H265" || vc == "HEVC") vc = "HEVC";
        else if (vc == "H264" || vc == "AVC" || vc == "AVC1") vc = "AVC";
        else if (vc.find("PRORES") != std::string::npos) vc = "PRORES";
        if (!vc.empty()) {
            format_badges_.push_back({vc, false});
        }
        mpv_free(vcodec);
    }

    // 4. 帧率
    double fps = 0.0;
    if (mpv_get_property(mpv_, "container-fps", MPV_FORMAT_DOUBLE, &fps) < 0 || fps <= 0.0) {
        mpv_get_property(mpv_, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &fps);
    }
    if (fps >= 115.0) {
        format_badges_.push_back({"120 FPS", false});
    } else if (fps >= 55.0) {
        format_badges_.push_back({"60 FPS", false});
    } else if (fps >= 48.0 && fps < 55.0) {
        format_badges_.push_back({"50 FPS", false});
    } else if (fps >= 23.5 && fps <= 24.5) {
        format_badges_.push_back({"24 FPS", false});
    }

    // 5. 声道数
    int64_t ach = 0;
    if (mpv_get_property(mpv_, "audio-params/channel-count", MPV_FORMAT_INT64, &ach) >= 0) {
        if (ach == 6) format_badges_.push_back({"5.1 CH", false});
        else if (ach >= 8) format_badges_.push_back({"7.1 CH", false});
        else if (ach == 2) format_badges_.push_back({"STEREO", false});
    }

    // 6. 硬解加速
    if (hwdec_active_) {
        format_badges_.push_back({"HW ACCEL", true});
    } else {
        format_badges_.push_back({"SW", false});
    }
}

void MPVPlayerView::checkMPVEvents() {
    if (!mpv_) return;

    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) break;

        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property* prop = (mpv_event_property*)event->data;
            if (std::string(prop->name) == "eof-reached" && prop->data) {
                // keep-open=yes 下不会有 END_FILE，靠这个属性判断播放结束。
                // 必须等文件真正加载完再接受信号：切换新文件时 mpv 会先回放
                // 上一个文件的 eof-reached=1，否则会瞬间把整个播放列表跳完。
                const int reached = *(int*)prop->data;
                if (reached && !eof_fired_ && file_loaded_) {
                    // 防抖：切换新文件时 mpv 可能立刻回报上一个文件的 eof=1，
                    // 若不加时间窗，整个播放列表会在同一帧内被跳完。
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_eof_time_).count() > 1500) {
                        eof_fired_ = true;
                        last_eof_time_ = now;
                        brls::Logger::info("播放结束（eof-reached）");
                        if (on_finish_cb_) on_finish_cb_();
                    }
                } else if (!reached) {
                    eof_fired_ = false;   // 新文件开始播放时复位
                }
            } else if (std::string(prop->name) == "time-pos" && prop->data) {
                current_position_ = (int64_t)*(double*)prop->data;
                // 每播放 5 秒或进度发生跳变时自动持久化历史进度，防止异常退出时丢失
                if (!current_file_path_.empty() && total_duration_ > 0 &&
                    std::abs(current_position_ - last_saved_position_) >= 5) {
                    last_saved_position_ = current_position_;
                    PlaybackHistory::getInstance().updateProgress(
                        current_file_path_, current_position_, total_duration_,
                        selected_sid_, selected_aid_
                    );
                }
            } else if (std::string(prop->name) == "duration" && prop->data) {
                total_duration_ = (int64_t)*(double*)prop->data;
            } else if (std::string(prop->name) == "pause" && prop->data) {
                is_playing_ = (*(int*)prop->data == 0);
            } else if (std::string(prop->name) == "track-list") {
                // 外挂字幕或异步探测的音轨/字幕轨加载完成后，在此补救处理
                if (!subtitle_chosen_ && file_loaded_ && selected_sid_ != 0) {
                    const bool subtitle_off = std::getenv("MUVIK_SUBTITLE_OFF") != nullptr;
                    if (!subtitle_off) {
                        auto subs = getTracks("sub");
                        if (!subs.empty()) {
                            int want = subs.front().id;
                            if (selected_sid_ > 0) {
                                for (const auto& t : subs) {
                                    if (t.id == selected_sid_) { want = t.id; break; }
                                }
                            }
                            selectTrack("sub", want, false);
                            subtitle_chosen_ = true;
                            brls::Logger::info("MPVPlayerView (track-list 更新): 自动选中字幕轨 #{}", want);
                        }
                    }
                }
                if (file_loaded_ && selected_aid_ > 1) {
                    // 若音轨偏好不是默认第 1 轨，且当前实际 aid != selected_aid_，检查新轨道是否匹配
                    int64_t current_aid = 0;
                    if (mpv_get_property(mpv_, "aid", MPV_FORMAT_INT64, &current_aid) >= 0 && current_aid != selected_aid_) {
                        auto audios = getTracks("audio");
                        for (const auto& t : audios) {
                            if (t.id == selected_aid_) {
                                selectTrack("audio", selected_aid_, false);
                                brls::Logger::info("MPVPlayerView (track-list 更新): 延迟匹配并恢复音轨 #{}", selected_aid_);
                                break;
                            }
                        }
                    }
                }
            }
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            brls::Logger::info("MPVPlayerView: 文件已加载");
            file_loaded_ = true;

            // OSD 顶部那行「1920×1080 · 硬件解码」的数据来源。
            // 只在这里读一次：mpv 属性每一帧读一次的开销远大于绘制本身。
            int64_t vw = 0, vh = 0;
            video_w_ = mpv_get_property(mpv_, "width", MPV_FORMAT_INT64, &vw) >= 0
                           ? (int)vw : 0;
            video_h_ = mpv_get_property(mpv_, "height", MPV_FORMAT_INT64, &vh) >= 0
                           ? (int)vh : 0;
            char* hwdec = mpv_get_property_string(mpv_, "hwdec-current");
            hwdec_active_ = hwdec && *hwdec && std::string(hwdec) != "no";
            if (hwdec) mpv_free(hwdec);
            updateFormatBadges();
            eof_fired_ = false;  // 新文件确认加载，允许后续再次触发连播

            // keep-open=yes 在播放结束后会把 pause 置为 yes（停在最后一帧）。
            // 该状态会延续到下一次 loadfile：新一集加载完但仍处于暂停，
            // 表现为「连播卡在第 2 集，时间永远是 0.0」。
            // 必须在 FILE_LOADED 之后清，太早会被 loadfile 覆盖。
            mpv_set_property_string(mpv_, "pause", "no");

            // 处理断点接续：在文件真正加载完毕后下发定位，并弹出 HUD 提示
            if (pending_resume_position_ > 0) {
                seekTo(pending_resume_position_);
                showHUD("replay", i18n::tr("player.resumed_at", "已自动接续播放: ") + MediaScanner::formatDuration(pending_resume_position_));
                pending_resume_position_ = 0;
                mpv_set_property_string(mpv_, "start", "none");
            }
            // 音频：恢复上次选择的音轨偏好或测试指定的音轨
            if (const char* fa = std::getenv("MUVIK_FORCE_AID")) {
                const int want_aid = std::atoi(fa);
                auto audios = getTracks("audio");
                bool found = false;
                for (const auto& t : audios) {
                    if (t.id == want_aid) { found = true; break; }
                }
                if (found) {
                    selectTrack("audio", want_aid, false);
                    char* actual = mpv_get_property_string(mpv_, "aid");
                    brls::Logger::info("测试：请求音轨 #{}，mpv 实际 aid={}（共 {} 条）",
                                       want_aid, actual ? actual : "?", audios.size());
                    mpv_free(actual);
                } else {
                    brls::Logger::warning("测试：音轨 #{} 不存在（共 {} 条）",
                                          want_aid, audios.size());
                }
            } else if (selected_aid_ > 0) {
                auto audios = getTracks("audio");
                bool found = false;
                for (const auto& t : audios) {
                    if (t.id == selected_aid_) { found = true; break; }
                }
                if (found) {
                    selectTrack("audio", selected_aid_, false);
                    brls::Logger::info("MPVPlayerView: 自动恢复音轨 #{}", selected_aid_);
                }
            }

            // mpv 默认不选中任何字幕轨（sid=no），需要显式选第一条。
            // 若不处理，内嵌字幕永远不会显示。
            // 测试用：强制不选字幕，便于脚本做「有/无字幕」对照
            const bool subtitle_off = std::getenv("MUVIK_SUBTITLE_OFF") != nullptr;
            if (subtitle_off) {
                mpv_set_property_string(mpv_, "sid", "no");
                subtitle_chosen_ = true;
            } else if (selected_sid_ == 0) {
                mpv_set_property_string(mpv_, "sid", "no");
                subtitle_chosen_ = true;
                brls::Logger::info("MPVPlayerView: 恢复历史状态：关闭字幕");
            } else if (!subtitle_chosen_) {
                auto subs = getTracks("sub");
                if (!subs.empty()) {
                    // 测试用：MUVIK_FORCE_SUB_SID=N 强制选指定轨，
                    // 用于验证「选过的字幕轨是否被记住」。
                    int want = subs.front().id;
                    if (const char* f = std::getenv("MUVIK_FORCE_SUB_SID")) {
                        want = std::atoi(f);
                        brls::Logger::info("测试：强制选择字幕轨 #{}", want);
                    } else if (selected_sid_ > 0) {
                        // 有历史记录时优先恢复它
                        for (const auto& t : subs) {
                            if (t.id == selected_sid_) { want = t.id; break; }
                        }
                    }
                    selectTrack("sub", want, false);
                    subtitle_chosen_ = true;
                    brls::Logger::info("MPVPlayerView: 自动选中字幕轨 #{}", want);
                }
            }
        } else if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
            brls::Logger::info("MPVPlayerView: 视频重配置");
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file* ef = (mpv_event_end_file*)event->data;

            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                brls::Logger::error("MPVPlayerView: 播放失败 (error code={})", ef->error);
                showHUD("alert", "播放失败: 无法打开文件");
            }

            // 只有 EOF 才代表「真的播完了」。
            // reason=STOP(2) 是我们自己 loadfile 替换旧文件造成的，
            // 若把它也当成结束，连播会连锁触发、把整个列表瞬间跳完。
            const bool is_eof = (ef->reason == MPV_END_FILE_REASON_EOF);
            if (is_eof && !eof_fired_) {
                eof_fired_ = true;
                last_eof_time_ = std::chrono::steady_clock::now();
                if (on_finish_cb_) on_finish_cb_();
            }
        }
    }
}

std::string MPVPlayerView::getCurrentTimeStr() const {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << tm_buf.tm_hour << ":"
        << std::setw(2) << std::setfill('0') << tm_buf.tm_min;
    return oss.str();
}

void MPVPlayerView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    checkMPVEvents();

    // 长按加速依赖「真实时间流逝」，不能靠手势回调计时：
    // 手指按住不动时 borealis 不会持续派发 STAY 事件，
    // 只有每帧都检查一次才能可靠触发。
    updateHoldBoost();

    // 1. mpv 视频画面渲染
    //
    // 关键点（缺一不可，否则画面不会出现在正确位置）：
    //   a) FBO 尺寸必须是【物理像素】，而 width/height 是逻辑像素，
    //      Retina 屏上有 ×2 的比例差；
    //   b) 必须用 glViewport 把渲染区域限制到本控件；
    //   c) 必须设置 FLIP_Y，因为 GL 与 nanoVG 的 Y 轴方向相反；
    //   d) 渲染完要把视口还原，否则会破坏后续 nanoVG 绘制。
    // 视频画面：mpv 先渲染到离屏纹理，再由 nanoVG 作为普通图层合成。
    //
    // 为什么不能直接渲染到默认帧缓冲：
    //   borealis 的流程是 nvgBeginFrame() -> view->frame() -> nvgEndFrame()，
    //   nanoVG 是命令缓冲，nvgEndFrame 才提交 GL 命令。若在 draw() 内直接
    //   mpv_render_context_render，输出会被随后提交的 nanoVG 覆盖。
    //   而若推迟到 nvgEndFrame 之后，视频又会盖住 OSD 与标题栏。
    //   只有「离屏渲染 + 纹理合成」两者兼得。
    // 视频画面采用「离屏纹理 + nanoVG 合成」。
    //
    // 但 mpv 的渲染必须推迟到 nvgEndFrame 之后，原因：
    //   draw() 期间 nanoVG 正在记录命令，此时直接 glBindFramebuffer /
    //   glViewport 会污染 nanoVG 的 GL 状态，导致它提交时用错 FBO，
    //   表现为「纹理合成命令无效，画面看不到视频」。
    //
    // 因此这里只记录几何参数，实际渲染与合成都在 postDraw 完成。
    if (mpv_gl_) {
        last_draw_ = {x, y, width, height};
        pending_render_ = true;
    }


    Box::draw(vg, x, y, width, height, style, ctx);
}

} // namespace muvik
