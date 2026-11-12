#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include "utils/frame_grabber.hpp"
#include <borealis/core/logger.hpp>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <vector>
#include <string>
#include <borealis/core/application.hpp>

// glad 已经在 borealis 内部被引入；这里直接使用它的 GL 函数指针。
#include <borealis/extern/glad/glad.h>

namespace muvik {

namespace {

// 抓取延迟：给 UI 足够的帧数完成初始化与首次布局
constexpr int kGrabAfterFrames = 45;

std::string g_path;
int  g_count      = 1;      // 要抓几帧
int  g_interval   = 30;     // 帧间隔
bool g_exit_after = true;   // 抓完是否退出
int  g_grabbed    = 0;      // 已抓帧数
int  g_start      = kGrabAfterFrames;  // 首次抓帧的起始帧

// 时间模式（可选）。帧号模式受启动期影响不可预测：
// mpv 初始化完成前主循环空转到数百 fps，「第 N 帧」与
// 「播放后第 N 秒」没有稳定映射——这正是 verify_ui 里
// 「时序相关像素检查偶发失败」的根源之一。
// 按墙钟毫秒锚定才是确定性的。
int  g_start_ms    = -1;    // MUVIK_FRAMEGRAB_AT_MS，<0 = 帧号模式
int  g_interval_ms = -1;    // MUVIK_FRAMEGRAB_INTERVAL_MS
std::chrono::steady_clock::time_point g_t0{};
bool g_t0_set = false;

int envInt(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try { return std::stoi(v); } catch (...) { return fallback; }
}


bool isEnabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("MUVIK_FRAMEGRAB");
        if (!env || !*env) return false;
        g_path = env;
        g_count = envInt("MUVIK_FRAMEGRAB_COUNT", 1);
        if (g_count < 1) g_count = 1;
        g_interval = envInt("MUVIK_FRAMEGRAB_INTERVAL", 30);
        if (g_interval < 1) g_interval = 1;
        g_exit_after = envInt("MUVIK_FRAMEGRAB_EXIT", 1) != 0;
        g_start = envInt("MUVIK_FRAMEGRAB_START", kGrabAfterFrames);
        if (g_start < 0) g_start = 0;
        g_start_ms    = envInt("MUVIK_FRAMEGRAB_AT_MS", -1);
        g_interval_ms = envInt("MUVIK_FRAMEGRAB_INTERVAL_MS", -1);
        return true;
    }();
    return enabled;
}

// 给多帧输出拼上序号后缀：/tmp/a.ppm -> /tmp/a_0.ppm
std::string numberedPath(const std::string& base, int index) {
    if (g_count <= 1) return base;
    auto slash = base.find_last_of('/');
    auto dot = base.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return base + "_" + std::to_string(index);
    }
    return base.substr(0, dot) + "_" + std::to_string(index) + base.substr(dot);
}

} // namespace

bool FrameGrabber::enabled() {
    return isEnabled();
}

std::string FrameGrabber::outputPath() {
    isEnabled();
    return g_path;
}

bool FrameGrabber::writePPM(const std::string& path, int width, int height,
                            const unsigned char* rgb_flipped) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        brls::Logger::error("FrameGrabber: 无法写入 {}", path);
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb_flipped),
              static_cast<std::streamsize>(width) * height * 3);
    out.close();
    brls::Logger::info("FrameGrabber: 已保存 {}x{} 到 {}", width, height, path);
    return true;
}

bool FrameGrabber::tick(int frame_index) {
    if (!isEnabled()) return false;

    // 单帧模式：等 UI 稳定后抓一帧
    // 多帧模式：按 interval 连续抓，用于验证视频画面是否在变化
    if (g_start_ms >= 0) {
        // 时间模式：锚定主循环第一帧的墙钟时刻
        if (!g_t0_set) { g_t0 = std::chrono::steady_clock::now(); g_t0_set = true; }
        const long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_t0).count();
        const int interval = g_interval_ms > 0 ? g_interval_ms : 30;
        const long target = (long)g_start_ms + (long)g_grabbed * interval;
        if (elapsed < target) return false;
    } else {
        const int target = g_start + g_grabbed * g_interval;
        if (frame_index < target) return false;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    int w = viewport[2];
    int h = viewport[3];
    if (w <= 0 || h <= 0) {
        brls::Logger::warning("FrameGrabber: 视口尺寸无效 {}x{}", w, h);
        return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        brls::Logger::warning("FrameGrabber: glReadPixels 出错 0x{:x}", (unsigned)err);
        return false;
    }

    // GL 的原点在左下，图像文件需要从上到下，翻转行序
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = pixels.data() + static_cast<size_t>(h - 1 - y) * w * 4;
        unsigned char* dst = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }

    std::string out_path = numberedPath(g_path, g_grabbed);
    bool ok = writePPM(out_path, w, h, rgb.data());
    if (ok) ++g_grabbed;

    if (g_grabbed >= g_count) {
        // 抓够了。单帧模式或多帧模式都可选择退出
        return g_exit_after;
    }
    return false;
}

} // namespace muvik
