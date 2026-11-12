#include "utils/display_mode.hpp"
#include "utils/app_paths.hpp"
#include "utils/i18n.hpp"

#include <cstdlib>
#include <fstream>
#include <cctype>

#if !defined(TESTING_WITHOUT_BOREALIS)
#include <borealis/core/application.hpp>
#include <borealis/core/video.hpp>
#include <borealis/core/input.hpp>
#include <borealis/core/logger.hpp>
#endif

namespace muvik {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) start++;
    return s.substr(start);
}

void logInfo(const std::string& msg) {
#if !defined(TESTING_WITHOUT_BOREALIS)
    brls::Logger::info("{}", msg);
#else
    (void)msg;
#endif
}

} // namespace

bool DisplayMode::resolveFullscreen() {
    // 1. 显式环境变量：MUVIK_WINDOW（窗口测试/指定分辨率）强制窗口模式
    if (std::getenv("MUVIK_WINDOW")) {
        logInfo("DisplayMode: 检测到 MUVIK_WINDOW，使用窗口模式");
        return false;
    }

    // 2. 环境变量 MUVIK_FULLSCREEN（用于 CI、测试脚本或临时调试覆盖）
    if (const char* env = std::getenv("MUVIK_FULLSCREEN")) {
        std::string s = trim(env);
        if (s == "0" || s == "false" || s == "no" || s == "window" || s == "off") {
            logInfo("DisplayMode: MUVIK_FULLSCREEN=" + std::string(env) + "，使用窗口模式");
            return false;
        }
        if (s == "1" || s == "true" || s == "yes" || s == "fullscreen" || s == "on") {
            logInfo("DisplayMode: MUVIK_FULLSCREEN=" + std::string(env) + "，使用全屏模式");
            return true;
        }
    }

    // 3. 用户配置文件（~/.config/muvik/fullscreen）
    std::ifstream f(AppPaths::configFile("fullscreen"));
    if (f) {
        std::string line;
        if (std::getline(f, line)) {
            std::string s = trim(line);
            if (s == "0" || s == "false" || s == "window" || s == "off") {
                logInfo("DisplayMode: 配置文件设置窗口模式");
                return false;
            }
            if (s == "1" || s == "true" || s == "fullscreen" || s == "on") {
                logInfo("DisplayMode: 配置文件设置全屏模式");
                return true;
            }
        }
    }

    // 4. 默认：全屏（符合掌机 / 主机 / 电视沉浸式影音体验）
    logInfo("DisplayMode: 默认采用全屏模式");
    return true;
}

bool DisplayMode::isFullscreen() {
#if !defined(TESTING_WITHOUT_BOREALIS)
    return VideoContext::FULLSCREEN;
#else
    return false;
#endif
}

void DisplayMode::setFullscreen(bool fullscreen, bool persist) {
#if !defined(TESTING_WITHOUT_BOREALIS)
    if (isFullscreen() == fullscreen) {
        if (persist) {
            std::ofstream f(AppPaths::configFile("fullscreen"), std::ios::trunc);
            if (f) f << (fullscreen ? "fullscreen\n" : "window\n");
        }
        return;
    }

    auto* platform = brls::Application::getPlatform();
    if (platform && platform->getVideoContext()) {
        platform->getVideoContext()->fullScreen(fullscreen);
    } else {
        VideoContext::FULLSCREEN = fullscreen;
    }
#endif

    if (persist) {
        AppPaths::ensureDir(AppPaths::configDir());
        std::ofstream f(AppPaths::configFile("fullscreen"), std::ios::trunc);
        if (f) {
            f << (fullscreen ? "fullscreen\n" : "window\n");
        }
    }
}

void DisplayMode::toggleFullscreen(bool persist) {
    const bool target = !isFullscreen();
    setFullscreen(target, persist);
#if !defined(TESTING_WITHOUT_BOREALIS)
    brls::Application::notify(target ? i18n::tr("display.fullscreen_on", "已切换为全屏模式")
                                     : i18n::tr("display.fullscreen_off", "已切换为窗口模式"));
#endif
}

void DisplayMode::setupGlobalKeyBindings() {
#if !defined(TESTING_WITHOUT_BOREALIS)
    auto* platform = brls::Application::getPlatform();
    if (!platform || !platform->getInputManager()) return;

    platform->getInputManager()->getKeyboardKeyStateChanged()->subscribe([](brls::KeyState state) {
        if (!state.pressed) return;

        bool toggle = false;

        // F11 键（掌机 / Windows / Linux 通用）
        if (state.key == brls::BRLS_KBD_KEY_F11) {
            toggle = true;
        }
        // Alt + Enter（经典多媒体全屏切换）
        else if (state.key == brls::BRLS_KBD_KEY_ENTER && (state.mods & 0x0004)) {
            toggle = true;
        }
#if defined(__APPLE__)
        // macOS: Cmd + F (Super = 0x0008)
        else if (state.key == brls::BRLS_KBD_KEY_F && (state.mods & 0x0008)) {
            toggle = true;
        }
#endif

        if (toggle) {
            toggleFullscreen(true);
        }
    });
#endif
}

int DisplayMode::getTargetFps() {
    // 1. 显式环境变量 MUVIK_FPS 优先覆盖
    if (const char* env = std::getenv("MUVIK_FPS")) {
        int v = std::atoi(env);
        return v >= 0 ? v : 0;
    }

    // 2. 用户配置文件 ~/.config/muvik/fps_limit
    std::ifstream f(AppPaths::configFile("fps_limit"));
    if (f) {
        std::string line;
        if (std::getline(f, line)) {
            std::string s = trim(line);
            if (!s.empty()) {
                int v = std::atoi(s.c_str());
                if (v >= 0) return v;
            }
        }
    }

    // 3. 默认：0（跟随屏幕刷新率 / 垂直同步，不强加软上限）
    // 这样 Steam Deck LCD (60Hz)、Steam Deck OLED (90Hz)、外接高刷屏 (120Hz/144Hz) 都能原生跑满
    return 0;
}

void DisplayMode::setTargetFps(int fps, bool persist) {
    if (persist) {
        AppPaths::ensureDir(AppPaths::configDir());
        std::ofstream f(AppPaths::configFile("fps_limit"), std::ios::trunc);
        if (f) {
            f << fps << "\n";
        }
    }
}

std::string DisplayMode::getFpsLimitLabel(int fps) {
    if (fps <= 0) return i18n::tr("fps.auto", "自动（跟随屏幕 / 垂直同步）");
    if (fps == 30) return i18n::tr("fps.30", "30 FPS（极致省电）");
    if (fps == 40) return i18n::tr("fps.40", "40 FPS（掌机平衡）");
    if (fps == 48) return i18n::tr("fps.48", "48 FPS（24fps 影视匹配）");
    if (fps == 60) return i18n::tr("fps.60", "60 FPS（标准 LCD）");
    if (fps == 90) return i18n::tr("fps.90", "90 FPS（Steam Deck OLED）");
    if (fps == 120) return i18n::tr("fps.120", "120 FPS（外接高刷）");
    if (fps == 144) return i18n::tr("fps.144", "144 FPS（电竞高刷）");
    return std::to_string(fps) + " FPS";
}

} // namespace muvik
