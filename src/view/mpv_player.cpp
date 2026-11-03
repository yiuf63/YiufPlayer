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
#include "utils/playback_history.hpp"
#include "utils/scanner.hpp"
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

namespace deck_player {

// ─────────────────────────────────────────────────────────────
// OSD 视觉规格
//
// 为什么要集中定义：重构前 OSD 里散落着 22/21/20/17/15 五种字号
// 和 35/30 两种边距，彼此不成体系，观感就是「样式不对、随手拼的」。
// 这里的数值与 docs/UI-DESIGN.md 第 5 节一一对应，改样式只改这一处。
// ─────────────────────────────────────────────────────────────
namespace osd {
// 字号
constexpr float FONT_TITLE   = 20.0f;  // 片名
constexpr float FONT_INFO    = 17.0f;  // 右上角状态、时间
constexpr float FONT_HINT    = 15.0f;  // 辅助提示
constexpr float FONT_HUD     = 21.0f;  // 居中 HUD 文字
constexpr float FONT_ICON    = 22.0f;  // HUD 图标

// 间距（统一为 35，锁定图标也对齐这条线）
constexpr float MARGIN       = 35.0f;

// 布局
constexpr float TOP_H        = 65.0f;   // 顶部渐变栏高
// 底部渐变栏高度。实测需要容纳：
//   进度条(8) + 时间文字(20) + 间隔 + 主控行(58) + 间隔 + 功能行(58) + 底部留白
// = 约 215px。原来写 110 只够一行，导致功能行整个画到视频画面上。
constexpr float BOTTOM_H     = 215.0f;
constexpr float BAR_H        = 8.0f;    // 进度条可视高度
constexpr float BAR_HIT_H    = 44.0f;   // 进度条触摸判定高度（触屏下限）
constexpr float BAR_BOTTOM_PAD = 14.0f; // 进度条距底部的留白
constexpr float BAR_RADIUS   = 4.0f;    // 进度条圆角
constexpr float KNOB_R      = 10.0f;   // 滑块半径
constexpr float KNOB_R_DRAG = 12.0f;   // 拖动时放大，给反馈
constexpr float BTN_MIN      = 44.0f;   // 按钮最小边长（触屏下限）
constexpr float BTN_MAX      = 62.0f;
constexpr float BTN_DIVISOR  = 22.0f;   // 按宽度自适应：1280/22 ≈ 58
constexpr float LOCK_SIZE    = 50.0f;   // 锁定图标
} // namespace osd



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

    last_user_action_time_ = std::chrono::steady_clock::now();

    // 绑定基础播放与寻迹控制
    this->registerAction("播放/暂停", brls::ControllerButton::BUTTON_A, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        togglePlayPause();
        return true;
    }, false);

    this->registerAction("快退", brls::ControllerButton::BUTTON_LEFT, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        seek(-10);
        return true;
    }, false);

    this->registerAction("快进", brls::ControllerButton::BUTTON_RIGHT, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        seek(10);
        return true;
    }, false);

    this->registerAction("大幅快退", brls::ControllerButton::BUTTON_LT, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        seek(-30);
        return true;
    }, false);

    this->registerAction("大幅快进", brls::ControllerButton::BUTTON_RT, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        seek(30);
        return true;
    }, false);

    this->registerAction("上一集", brls::ControllerButton::BUTTON_LB, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        if (on_prev_next_cb_) on_prev_next_cb_(false);
        return true;
    }, false);

    this->registerAction("下一集", brls::ControllerButton::BUTTON_RB, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        if (on_prev_next_cb_) on_prev_next_cb_(true);
        return true;
    }, false);

    this->registerAction("播放列表", brls::ControllerButton::BUTTON_UP, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        if (on_show_playlist_cb_) on_show_playlist_cb_();
        return true;
    }, false);

    // 绑定高级功能按键
    this->registerAction("音轨选择", brls::ControllerButton::BUTTON_X, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        auto tracks = getTracks("audio");
        TrackSelectionDialog::showAudioDialog(tracks, [this](int track_id) {
            selectTrack("audio", track_id);
        });
        return true;
    }, false);

    this->registerAction("字幕设置", brls::ControllerButton::BUTTON_Y, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        auto tracks = getTracks("sub");
        TrackSelectionDialog::showSubtitleDialog(tracks, subtitle_delay_,
            [this](int track_id) { selectTrack("sub", track_id); },
            [this](float delay) { setSubtitleDelay(delay); },
            [this]() { promptLoadExternalSubtitle(); }
        );
        return true;
    }, false);

    // 按下右摇杆 (R3)：循环切换倍速 (1.0x -> 1.25x -> 1.5x -> 2.0x -> 0.75x)
    this->registerAction("倍速切换", brls::ControllerButton::BUTTON_RSB, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
        cyclePlaybackSpeed();
        return true;
    }, false);

    // 按下左摇杆 (L3)：循环切换画面比例 (适应 -> 16:10 满屏 -> 16:9 -> 4:3)
    this->registerAction("比例切换", brls::ControllerButton::BUTTON_LSB, [this]([[maybe_unused]] brls::View* view) {
        if (touch_locked_) return false;
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

    // 视频渲染必须在 nanoVG 提交完成之后进行，否则会被覆盖
    brls::Application::postDrawHook = [this]() {
        // 关键：Dialog 在 borealis 里是「压栈的新 Activity」
        //   void Dialog::open() { Application::pushActivity(new Activity(this)); }
        //
        // 对话框打开后，本视图仍在栈中、draw() 仍在被调用、postDrawHook
        // 依然生效。若此时照常渲染 mpv 画面（它画满整个窗口），会把包括
        // 对话框在内的整套 UI 整屏盖掉——实测播放列表完全看不见。
        //
        // 判据：本视图所属的 Activity 必须就是栈顶 Activity，才渲染视频。
        // 这样任何压栈的对话框/二级页面都会自动让出画面。
        brls::Activity* mine = this->getParentActivity();
        brls::Activity* top = brls::Application::getTopActivity();
        if (mine && top && top != mine) {
            return;   // 被对话框或其它 Activity 覆盖，不画视频
        }
        this->renderMPV();
    };

    // 手势自检：设置 DECKPLAYER_GESTURE_TEST=1 后自动注入触摸序列，
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
    if (!std::getenv("DECKPLAYER_GESTURE_TEST")) return;

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

    // 这里**不要**自己设 glViewport。
    //
    // 视口由平台层负责：SDL 后端在 sdlWindowFramebufferSizeCallback 里
    // 已经用物理像素调用过 glViewport(0, 0, fWidth, fHeight)，
    // 而 nanoVG 的 GL 后端根本不碰视口（glnvg__renderViewport 只记录
    // gl->view，从不读取它）。
    //
    // 之前这里写 glViewport(0,0,windowWidth,windowHeight) 是错的：
    // windowWidth 已经是**物理像素**（见 Application::onWindowResized），
    // 再乘 windowScale 会放大到屏幕外，OSD 就只剩左上角一块。
    //
    // nvgBeginFrame 的 windowWidth/Height 参数只是记录用途，
    // 真正的逻辑→物理换算由紧随其后的 nvgScale 完成，
    // 与 borealis 主循环（application.cpp:803-804）保持一致。
    // 与 borealis 主循环保持一致：
    //   nvgBeginFrame(windowWidth(物理), windowHeight(物理), scaleFactor)
    //   nvgScale(windowScale)
    // 这样 View 用逻辑坐标（contentWidth=1280）绘制，nvgScale 之后
    // 自动映射到物理像素（3392）。**两处必须完全一致**，
    // 否则 OSD 与主 UI 会落在不同的坐标空间里。
    nvgBeginFrame(vg, brls::Application::windowWidth,
                  brls::Application::windowHeight,
                  brls::Application::windowScale);
    nvgScale(vg, brls::Application::windowScale, brls::Application::windowScale);

    drawHud(vg, r.x, r.y, r.w, r.h);

    nvgEndFrame(vg);
}

// OSD / HUD 层绘制。参数为视频区域的左上角与尺寸。
//
// 【重要】本函数内所有文字都必须用 nvgFontFaceId(getDefaultFont())，
// 不能写 nvgFontFace(vg, "sans")。
//   "sans" 是 nanoVG 官方示例里的字体名，本项目从未注册过它；
//   fontstash 查不到会返回 -1，nvgText 随即直接 return，
//   结果是**整块 OSD 一个字都画不出来**，只看到一排空色块。
//   这个 bug 曾存在很久而所有自动检查都通过（它们只看视频画面）。
//   现在由 `verify_ui.py --only osd` 用纯黑视频做底来守卫。
// 主字体 resources/font/font.ttf 已含 2 万汉字，中英文都能渲染。
void MPVPlayerView::drawHud(NVGcontext* vg, float x, float y, float width, float height) {
    // 亮度蒙层模拟
    if (brightness_ < 0.99f) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, (1.0f - brightness_) * 0.8f));
        nvgFill(vg);
    }

    // 3. 锁屏指示器 (防误触图标)
    if (touch_locked_) {
        float lock_x = x + osd::MARGIN;
        float lock_y = y + (height - osd::LOCK_SIZE) / 2.0f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, lock_x, lock_y, osd::LOCK_SIZE, osd::LOCK_SIZE, osd::LOCK_SIZE * 0.5f);
        nvgFillColor(vg, nvgRGBA(30, 30, 30, 200));
        nvgFill(vg);

        nvgFontSize(vg, osd::FONT_ICON);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(241, 196, 15, 255));
        nvgText(vg, lock_x + osd::LOCK_SIZE * 0.5f, lock_y + osd::LOCK_SIZE * 0.5f, "[锁]", nullptr);
    }

    // 4. 居中 HUD 悬浮提示框
    auto now = std::chrono::steady_clock::now();
    if (now < hud_expire_time_) {
        float hud_w = 300.0f;
        float hud_h = 76.0f;
        float hud_x = x + (width - hud_w) / 2.0f;
        float hud_y = y + (height - hud_h) / 2.0f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, hud_x, hud_y, hud_w, hud_h, 16.0f);
        nvgFillColor(vg, nvgRGBA(18, 18, 18, 230));
        nvgFill(vg);

        nvgFontSize(vg, osd::FONT_HUD);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgText(vg, hud_x + hud_w / 2.0f, hud_y + (hud_progress_ >= 0 ? 26.0f : 38.0f), hud_text_.c_str(), nullptr);

        if (hud_progress_ >= 0.0f) {
            float bar_w = hud_w - 48.0f;
            float bar_h = 6.0f;
            float bar_x = hud_x + 24.0f;
            float bar_y = hud_y + 52.0f;

            nvgBeginPath(vg);
            nvgRoundedRect(vg, bar_x, bar_y, bar_w, bar_h, 3.0f);
            nvgFillColor(vg, nvgRGBA(70, 70, 70, 220));
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, bar_x, bar_y, bar_w * std::clamp(hud_progress_, 0.0f, 1.0f), bar_h, 3.0f);
            nvgFillColor(vg, nvgRGBA(0, 168, 255, 255));
            nvgFill(vg);
        }
    }

    // 5. 易用 OSD 控制层（顶部标题栏 + 底部控制栏）
    auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - last_user_action_time_).count();
    // 测试用：DECKPLAYER_OSD_OFF 强制隐藏 OSD，
    // 便于脚本单独验证「字幕是否被控制栏遮挡」。
    static const bool osd_force_off = std::getenv("DECKPLAYER_OSD_OFF") != nullptr;
    // 5 秒对触摸操作太短：用户点一下想看看控制栏，
    // 还没看清就消失了，观感是「根本没有播放控件」。
    // 延长到 8 秒，并在任何触摸/按键后重新计时（见 last_user_action_time_）。
    const bool osd_shown = !osd_force_off && !touch_locked_
                        && osd_visible_ && idle_seconds < 8;

    // OSD 不可见时**必须清空命中区**。
    // 否则 hit_rects_ 保留上一次可见时的坐标：用户点屏幕空白处
    // 会命中「看不见的按钮」并触发动作，表现为行为诡异、点了没反应。
    if (!osd_shown) {
        hit_rects_.clear();
        // 进度条同理：OSD 隐藏后不能还能拖动看不见的进度条。
        // 拖动中不打断（否则手一松就跳不动了）。
        if (!seek_dragging_) {
            seek_bar_ = SeekBarRect{};
        }
    }

    if (osd_shown) {
        // --- 顶部状态栏 ---
        float top_h = osd::TOP_H;
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, top_h);
        NVGpaint top_paint = nvgLinearGradient(vg, x, y, x, y + top_h, nvgRGBA(0, 0, 0, 200), nvgRGBA(0, 0, 0, 0));
        nvgFillPaint(vg, top_paint);
        nvgFill(vg);

        // 视频标题
        nvgFontSize(vg, osd::FONT_TITLE);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgText(vg, x + osd::MARGIN, y + 32.0f, video_title_.c_str(), nullptr);

        // 系统当前时间与便携状态
        std::string top_info = "⚡ 硬件解码  |  " + getCurrentTimeStr();
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(200, 200, 200, 240));
        nvgText(vg, x + width - osd::MARGIN, y + 32.0f, top_info.c_str(), nullptr);

        // --- 底部控制条 ---
        float bottom_h = osd::BOTTOM_H;
        nvgBeginPath(vg);
        nvgRect(vg, x, y + height - bottom_h, width, bottom_h);
        NVGpaint bottom_paint = nvgLinearGradient(vg, x, y + height - bottom_h, x, y + height, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 230));
        nvgFillPaint(vg, bottom_paint);
        nvgFill(vg);

        // 触控大进度条
        float bar_margin = osd::MARGIN;
        // 进度条贴近底部：底部留白 14，其上依次是时间文字与两行按钮。
        // 这样它与按钮行完全不重叠（原来两者竖直重叠 38px，
        // 主控按钮直接把进度条盖住，进度条既看不见也拖不动）。
        float bar_y = y + height - osd::BAR_BOTTOM_PAD - osd::BAR_H;
        float bar_w = width - bar_margin * 2;
        float bar_h = osd::BAR_H;

        // 记录命中区供拖动使用。
        // 可视高度只有 8px，手指按不准，因此判定区以可视条为中心
        // 向上延伸到 44px —— 这是触屏可用性的下限。
        seek_bar_.x     = x + bar_margin;
        seek_bar_.y     = bar_y;
        seek_bar_.w     = bar_w;
        seek_bar_.h     = bar_h;
        seek_bar_.hit_h = std::max(osd::BAR_HIT_H, bar_h);
        seek_bar_.hit_y = bar_y - (seek_bar_.hit_h - bar_h) * 0.5f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + bar_margin, bar_y, bar_w, bar_h, osd::BAR_RADIUS);
        nvgFillColor(vg, nvgRGBA(80, 80, 80, 200));
        nvgFill(vg);

        // 拖动中显示手指位置，而不是当前播放位置，
        // 否则用户拖动时滑块不动，完全失去反馈。
        const double shown_pos = seek_dragging_ ? seek_preview_ : current_position_;
        const double shown_total = seek_dragging_ && seek_preview_total_ > 0
                                       ? seek_preview_total_ : total_duration_;
        if (shown_total > 0) {
            float progress = (float)shown_pos / shown_total;
            float active_w = bar_w * std::clamp(progress, 0.0f, 1.0f);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + bar_margin, bar_y, active_w, bar_h, osd::BAR_RADIUS);
            nvgFillColor(vg, nvgRGBA(0, 168, 255, 255));
            nvgFill(vg);

            // 抓取滑块球
            nvgBeginPath(vg);
            // 滑块直径 20（原 18）：触屏上更容易看见与抓住
            nvgCircle(vg, x + bar_margin + active_w, bar_y + bar_h / 2.0f,
                      seek_dragging_ ? osd::KNOB_R_DRAG : osd::KNOB_R);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgFill(vg);
        }

        // 时间标签 (当前 / 总共)
        std::string time_text = MediaScanner::formatDuration((int64_t)shown_pos)
                              + " / " + MediaScanner::formatDuration((int64_t)shown_total);
        nvgFontSize(vg, osd::FONT_INFO);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
        nvgText(vg, x + bar_margin, bar_y + 16.0f, time_text.c_str(), nullptr);

        // 底部直观功能键提示
        std::ostringstream btn_oss;
        btn_oss << "A 播放  |  X 音轨  |  Y 字幕  |  LT/RT 快退快进  |  R3 倍速 (" << playback_speed_ << "x)  |  L3 比例";
        std::string ctrl_hint = btn_oss.str();

        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGBA(150, 150, 150, 200));
        nvgFontSize(vg, osd::FONT_HINT);
        nvgText(vg, x + width - bar_margin, y + height - 132.0f, ctrl_hint.c_str(), nullptr);

        // ---- 触摸控件（触屏用户唯一的操作入口）----
        //
        // 背景：OSD 原本只画进度条 + 一行「A 播放 | X 音轨 | …」的手柄提示。
        // 对触屏用户来说，那行字只说明「能做什么」，**点不了任何东西**，
        // 观感就是「几乎没有播放功能」。
        // 这里补一排真正可点的按钮，位置记录到 hit_rects_ 供触摸命中测试。
        drawTouchControls(vg, x, y, width, height);
    }

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
// 分两行：
//   主控行（下）—— 上一集 / 快退 / 播放暂停 / 快进 / 下一集
//   功能行（上）—— 音轨 / 字幕 / 倍速 / 比例 / 锁定
// 这样常用操作在拇指最容易够到的位置，功能项在上方不抢眼。
void MPVPlayerView::drawTouchControls(NVGcontext* vg, float x, float y,
                                      float width, float height) {
    hit_rects_.clear();

    // 尺寸按逻辑宽度自适应：1280 宽下约 58px，小屏不小于 44px。
    const float btn = std::clamp(width / osd::BTN_DIVISOR, osd::BTN_MIN, osd::BTN_MAX);
    const float gap = btn * 0.30f;

    struct Item { const char* label; TouchAction action; bool wide; };

    // 主控行：居中，间距更大
    const Item primary[] = {
        {"上一集", TouchAction::Prev,        true},
        {"-10s",   TouchAction::SeekBack,    true},
        {"播放",   TouchAction::PlayPause,   true},
        {"+10s",   TouchAction::SeekForward, true},
        {"下一集", TouchAction::Next,        true},
    };
    // 功能行：右对齐，尺寸一致
    const Item secondary[] = {
        {"列表", TouchAction::Playlist,      true},
        {"音轨", TouchAction::AudioTrack,    true},
        {"字幕", TouchAction::SubtitleTrack, true},
        {"倍速", TouchAction::Speed,         true},
        {"画面", TouchAction::Aspect,        true},
        {"锁定", TouchAction::Lock,          true},
    };

    auto widthOf = [&](const Item& it) { return it.wide ? btn * 1.75f : btn; };

    auto drawRow = [&](const Item* items, int n, float row_y, bool centered) {
        float total_w = 0.0f;
        for (int i = 0; i < n; ++i) total_w += widthOf(items[i]);
        total_w += gap * (n - 1);

        float cx = centered ? (x + (width - total_w) * 0.5f)
                            : (x + width - osd::MARGIN - total_w);
        if (cx < x + osd::MARGIN) cx = x + osd::MARGIN;

        for (int i = 0; i < n; ++i) {
            const float bw = widthOf(items[i]);
            const bool focused = has_locked_action_ && (locked_action_ == items[i].action);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, row_y, bw, btn, btn * 0.22f);
            // 锁定态用强调色描边更醒目；播放/暂停用实心强调色突出主操作
            const bool is_primary_action = (items[i].action == TouchAction::PlayPause);
            nvgFillColor(vg, is_primary_action
                                 ? nvgRGBA(255, 159, 69, 235)
                                 : nvgRGBA(28, 30, 36, 215));
            nvgFill(vg);
            nvgStrokeColor(vg, focused ? nvgRGBA(255, 192, 122, 255)
                                       : nvgRGBA(255, 159, 69, 140));
            nvgStrokeWidth(vg, focused ? 2.5f : 1.2f);
            nvgStroke(vg);

            nvgFontSize(vg, btn * 0.32f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, is_primary_action ? nvgRGBA(20, 22, 26, 255)
                                               : nvgRGBA(242, 242, 245, 245));
            nvgText(vg, cx + bw / 2.0f, row_y + btn / 2.0f, items[i].label, nullptr);

            hit_rects_.push_back({cx, row_y, bw, btn, items[i].action});
            cx += bw + gap;
        }
    };

    // 按钮行自下而上排列，全部位于进度条**上方**。
    // 顺序（从下往上）：主控行 -> 功能行。
    // 主控行贴着手最容易够到的位置，功能行在其上方。
    const float primary_y   = y + height - osd::BAR_BOTTOM_PAD - osd::BAR_H
                              - 8.0f - 20.0f - btn;      // 进度条 + 时间文字 + 间隔
    const float secondary_y = primary_y - btn - gap;

    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    drawRow(secondary, (int)(sizeof(secondary)/sizeof(secondary[0])), secondary_y, false);
    drawRow(primary,   (int)(sizeof(primary)/sizeof(primary[0])),   primary_y,   true);
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
            if (status.state == brls::GestureState::START) {
                has_locked_action_ = false;
                for (const auto& r : hit_rects_) {
                    if (status.position.x >= r.x && status.position.x <= r.x + r.w &&
                        status.position.y >= r.y && status.position.y <= r.y + r.h) {
                        locked_action_ = r.action;
                        has_locked_action_ = true;
                        break;
                    }
                }
                if (has_locked_action_) return;
            }
            if (status.state == brls::GestureState::END) {
                has_locked_action_ = false;
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

            if (gesture_double_tap_pending_ &&
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_touch_down_time_).count() < 300) {
                // 双击
                gesture_double_tap_pending_ = false;
                if (!touch_locked_) togglePlayPause();
            } else {
                // 单击
                gesture_double_tap_pending_ = true;
                last_touch_down_time_ = now;
                if (!touch_locked_) {
                    osd_visible_ = !osd_visible_;
                    last_user_action_time_ = now;
                }
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
                    // PanGesture 的 delta 定义为「上一帧位置 - 当前帧位置」。
                    // 因此向上滑（y 递减）时 delta.y 为【正】，
                    // 直接用累加值除以灵敏度即为正向增量，无需取反。
                    // （此前误当作「当前 - 上一帧」而取了负号，导致
                    //   向上滑反而降低音量/亮度，方向完全相反。）
                    float ratio = gesture_accumulatedY_ / 300.0f;
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
    if (std::getenv("DECKPLAYER_NO_HWDEC")) {
        mpv_set_option_string(mpv_, "hwdec", "no");
        brls::Logger::info("MPVPlayerView: 硬解已禁用（DECKPLAYER_NO_HWDEC）");
    } else {
        mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    }
    mpv_set_option_string(mpv_, "vo", "libmpv");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "sub-auto", "fuzzy");
    mpv_set_option_string(mpv_, "sub-font", "sans-serif");
    mpv_set_option_string(mpv_, "sub-font-size", "46");
    // 字幕默认贴在画面最底部，会被底部的 OSD 控制栏（110px）遮住，
    // 实测有 OSD 时字幕白像素从 3676 掉到 236，基本不可读。
    // 抬高字幕位置并留出底部边距，使其始终位于控制栏之上。
    mpv_set_option_string(mpv_, "sub-pos", "80");
    mpv_set_option_string(mpv_, "sub-margin-y", "36");
    mpv_set_option_string(mpv_, "sub-border-size", "2.5");
    mpv_set_option_string(mpv_, "sub-shadow-offset", "1.5");
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

    last_eof_time_ = std::chrono::steady_clock::now();  // 为防抖窗口起点
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    int rc = mpv_command_async(mpv_, 0, cmd);
    brls::Logger::info("MPVPlayerView: loadfile 返回 {}", rc);

    if (resume_position > 0) {
        seekTo(resume_position);
        showHUD("replay", "已自动接续播放: " + MediaScanner::formatDuration(resume_position));
    }
    is_playing_ = true;
}

void MPVPlayerView::togglePlayPause() {
    if (!mpv_) return;
    int pause_val = is_playing_ ? 1 : 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &pause_val);
    is_playing_ = !is_playing_;
    last_user_action_time_ = std::chrono::steady_clock::now();
    showHUD(is_playing_ ? "play" : "pause", is_playing_ ? "播放" : "暂停");
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
            mpv_set_property_string(mpv_, "video-aspect-override", "-1");
            prompt = "画面比例: 原始自适应";
            break;
        case AspectRatioMode::Stretch_16_10:
            mpv_set_property_string(mpv_, "video-aspect-override", "1.6");
            prompt = "画面比例: 16:10 满屏 (Steam Deck 专用)";
            break;
        case AspectRatioMode::Fit_16_9:
            mpv_set_property_string(mpv_, "video-aspect-override", "1.777777");
            prompt = "画面比例: 16:9 标准宽屏";
            break;
        case AspectRatioMode::Fit_4_3:
            mpv_set_property_string(mpv_, "video-aspect-override", "1.333333");
            prompt = "画面比例: 4:3 复古动画/电视";
            break;
        case AspectRatioMode::ZoomCrop:
            mpv_set_property_string(mpv_, "panscan", "1.0");
            prompt = "画面比例: 裁切铺满全屏";
            break;
    }
    showHUD("aspect_ratio", prompt);
}

void MPVPlayerView::toggleTouchLock() {
    touch_locked_ = !touch_locked_;
    showHUD(touch_locked_ ? "lock" : "lock_open", touch_locked_ ? "触控已锁定 (防误触)" : "触控已解锁");
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

void MPVPlayerView::selectTrack(const std::string& type, int track_id) {
    if (!mpv_) return;
    std::string prop = (type == "audio") ? "aid" : "sid";

    // 记录用户选择，便于下次续播时恢复
    if (type == "audio") selected_aid_ = track_id;
    else selected_sid_ = track_id;

    if (track_id <= 0) {
        mpv_set_property_string(mpv_, prop.c_str(), "no");
        showHUD("subtitles_off", type == "audio" ? "静音" : "关闭字幕");
    } else {
        std::string val = std::to_string(track_id);
        mpv_set_property_string(mpv_, prop.c_str(), val.c_str());

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
    // 弹出输入框让用户输入字幕文件路径。掌机无系统文件管理器，
    // 这里与「添加媒体文件夹」保持一致的交互方式。
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            if (text.empty()) return;
            std::error_code ec;
            if (!std::filesystem::exists(text, ec) ||
                !std::filesystem::is_regular_file(text, ec)) {
                showHUD("error", "字幕文件不存在");
                return;
            }
            loadExternalSubtitle(text);
        },
        "输入字幕文件路径", "例如 /path/to/movie.ass", 512);
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
            } else if (std::string(prop->name) == "duration" && prop->data) {
                total_duration_ = (int64_t)*(double*)prop->data;
            } else if (std::string(prop->name) == "pause" && prop->data) {
                is_playing_ = (*(int*)prop->data == 0);
            }
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            brls::Logger::info("MPVPlayerView: 文件已加载");
            file_loaded_ = true;
            eof_fired_ = false;  // 新文件确认加载，允许后续再次触发连播

            // keep-open=yes 在播放结束后会把 pause 置为 yes（停在最后一帧）。
            // 该状态会延续到下一次 loadfile：新一集加载完但仍处于暂停，
            // 表现为「连播卡在第 2 集，时间永远是 0.0」。
            // 必须在 FILE_LOADED 之后清，太早会被 loadfile 覆盖。
            mpv_set_property_string(mpv_, "pause", "no");
            // 音频：mpv 默认用第一条音轨。这里支持强制指定，便于验证
            // 「能否切换到非默认音轨」（多音轨文件在掌机上很常见，
            // 例如双语 BDRip 需要选日语或国语）。
            if (const char* fa = std::getenv("DECKPLAYER_FORCE_AID")) {
                const int want_aid = std::atoi(fa);
                auto audios = getTracks("audio");
                bool found = false;
                for (const auto& t : audios) {
                    if (t.id == want_aid) { found = true; break; }
                }
                if (found) {
                    selectTrack("audio", want_aid);
                    // 关键：报告 mpv 的**实际属性**，而不是「我刚请求了什么」。
                    // 否则测试只能证明代码被执行过，证明不了真的切过去了。
                    char* actual = mpv_get_property_string(mpv_, "aid");
                    brls::Logger::info("测试：请求音轨 #{}，mpv 实际 aid={}（共 {} 条）",
                                       want_aid, actual ? actual : "?", audios.size());
                    mpv_free(actual);
                } else {
                    brls::Logger::warning("测试：音轨 #{} 不存在（共 {} 条）",
                                          want_aid, audios.size());
                }
            }

            // mpv 默认不选中任何字幕轨（sid=no），需要显式选第一条。
            // 若不处理，内嵌字幕永远不会显示。
            // 测试用：强制不选字幕，便于脚本做「有/无字幕」对照
            const bool subtitle_off = std::getenv("DECKPLAYER_SUBTITLE_OFF") != nullptr;
            if (subtitle_off) {
                mpv_set_property_string(mpv_, "sid", "no");
                subtitle_chosen_ = true;
            } else if (selected_sid_ > 0 && !subtitle_chosen_) {
                auto subs = getTracks("sub");
                if (!subs.empty()) {
                    // 测试用：DECKPLAYER_FORCE_SUB_SID=N 强制选指定轨，
                    // 用于验证「选过的字幕轨是否被记住」。
                    int want = subs.front().id;
                    if (const char* f = std::getenv("DECKPLAYER_FORCE_SUB_SID")) {
                        want = std::atoi(f);
                        brls::Logger::info("测试：强制选择字幕轨 #{}", want);
                    } else if (selected_sid_ > 0 && selected_sid_ != 1) {
                        // 有历史记录时优先恢复它
                        for (const auto& t : subs) {
                            if (t.id == selected_sid_) { want = t.id; break; }
                        }
                    }
                    selectTrack("sub", want);
                    subtitle_chosen_ = true;
                    brls::Logger::info("MPVPlayerView: 自动选中字幕轨 #{}", want);
                }
            }
        } else if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
            brls::Logger::info("MPVPlayerView: 视频重配置");
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file* ef = (mpv_event_end_file*)event->data;

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

} // namespace deck_player
