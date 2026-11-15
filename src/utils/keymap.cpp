#include <string>
#include "utils/keymap.hpp"
#include "utils/app_paths.hpp"
#include "utils/resource_locator.hpp"
#include "utils/input_settings.hpp"
#include "utils/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

#if !defined(TESTING_WITHOUT_BOREALIS)
#if defined(__SDL2__) || defined(ANDROID)
#include <SDL2/SDL.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <nanovg.h>
#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/font.hpp>
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

#if defined(TESTING_WITHOUT_BOREALIS)
static KeymapStyle g_mock_style = KeymapStyle::Auto;
#endif

} // namespace

KeymapStyle Keymap::getStyle() {
#if defined(TESTING_WITHOUT_BOREALIS)
    if (g_mock_style != KeymapStyle::Auto) return g_mock_style;
#endif

    // 1. 显式环境变量覆盖（MUVIK_KEYMAP=xbox / ps / switch / auto）
    if (const char* env = std::getenv("MUVIK_KEYMAP")) {
        std::string s = trim(env);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        if (s == "xbox" || s == "deck" || s == "1") return KeymapStyle::Xbox;
        if (s == "ps" || s == "playstation" || s == "sony" || s == "2") return KeymapStyle::PlayStation;
        if (s == "switch" || s == "nintendo" || s == "3") return KeymapStyle::Switch;
        if (s == "auto" || s == "0") return KeymapStyle::Auto;
    }

    // 2. 配置文件 ~/.config/muvik/keymap
    std::ifstream f(AppPaths::configFile("keymap"));
    if (f) {
        std::string line;
        if (std::getline(f, line)) {
            std::string s = trim(line);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            if (s == "xbox" || s == "deck" || s == "1") return KeymapStyle::Xbox;
            if (s == "ps" || s == "playstation" || s == "sony" || s == "2") return KeymapStyle::PlayStation;
            if (s == "switch" || s == "nintendo" || s == "3") return KeymapStyle::Switch;
            if (s == "auto" || s == "0") return KeymapStyle::Auto;
        }
    }

    // 默认自动识别
    return KeymapStyle::Auto;
}

void Keymap::setStyle(KeymapStyle style, bool persist) {
#if defined(TESTING_WITHOUT_BOREALIS)
    g_mock_style = style;
#endif

    if (persist) {
        AppPaths::ensureDir(AppPaths::configDir());
        std::ofstream f(AppPaths::configFile("keymap"), std::ios::trunc);
        if (f) {
            switch (style) {
                case KeymapStyle::Xbox:        f << "xbox\n"; break;
                case KeymapStyle::PlayStation: f << "ps\n"; break;
                case KeymapStyle::Switch:      f << "switch\n"; break;
                case KeymapStyle::Auto:
                default:                       f << "auto\n"; break;
            }
            logInfo(std::string("Keymap: 已保存按键提示偏好: ") + styleToString(style));
        }
    }
}

KeymapStyle Keymap::resolveActiveStyle() {
    KeymapStyle pref = getStyle();
    if (pref != KeymapStyle::Auto) {
        return pref;
    }

    // 1. TrimUI 掌机硬件：机身物理印刷为 Switch 布局，即使驱动层模拟了 Xbox 360 手柄也优先按 Switch 显示
    if (AppPaths::isTrimUI()) {
        return KeymapStyle::Switch;
    }

    // 2. 如果开启了 Switch 确认键反转，底栏也优先匹配 Switch 风格
    if (InputSettings::isSwapAB()) {
        return KeymapStyle::Switch;
    }

#if !defined(TESTING_WITHOUT_BOREALIS)
#if defined(__SDL2__) || defined(ANDROID)
    int num_joysticks = SDL_NumJoysticks();
    for (int jid = 0; jid < num_joysticks && jid < 16; ++jid) {
        const char* name = SDL_JoystickNameForIndex(jid);
        if (name) {
            std::string s(name);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });

            if (s.find("playstation") != std::string::npos ||
                s.find("dualshock") != std::string::npos ||
                s.find("dualsense") != std::string::npos ||
                s.find("ps4") != std::string::npos ||
                s.find("ps5") != std::string::npos ||
                s.find("sony") != std::string::npos) {
                return KeymapStyle::PlayStation;
            }

            if (s.find("switch") != std::string::npos ||
                s.find("joy-con") != std::string::npos ||
                s.find("pro controller") != std::string::npos ||
                s.find("nintendo") != std::string::npos ||
                s.find("trimui") != std::string::npos) {
                return KeymapStyle::Switch;
            }

            if (s.find("xbox") != std::string::npos ||
                s.find("x-box") != std::string::npos ||
                s.find("microsoft") != std::string::npos ||
                s.find("steam") != std::string::npos ||
                s.find("xinput") != std::string::npos) {
                return KeymapStyle::Xbox;
            }
        }
    }
#else
    // 自动模式：遍历已连接的手柄，探测设备名与布局
    for (int jid = 0; jid <= 15; ++jid) {
        if (glfwJoystickPresent(jid)) {
            const char* name = glfwGetGamepadName(jid);
            if (!name) name = glfwGetJoystickName(jid);
            if (name) {
                std::string s(name);
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });

                if (s.find("playstation") != std::string::npos ||
                    s.find("dualshock") != std::string::npos ||
                    s.find("dualsense") != std::string::npos ||
                    s.find("ps4") != std::string::npos ||
                    s.find("ps5") != std::string::npos ||
                    s.find("sony") != std::string::npos) {
                    return KeymapStyle::PlayStation;
                }

                if (s.find("switch") != std::string::npos ||
                    s.find("joy-con") != std::string::npos ||
                    s.find("pro controller") != std::string::npos ||
                    s.find("nintendo") != std::string::npos) {
                    return KeymapStyle::Switch;
                }

                if (s.find("xbox") != std::string::npos ||
                    s.find("x-box") != std::string::npos ||
                    s.find("microsoft") != std::string::npos ||
                    s.find("steam") != std::string::npos ||
                    s.find("xinput") != std::string::npos) {
                    return KeymapStyle::Xbox;
                }
            }
        }
    }
#endif
#endif

    // 如果用户开启了 Switch 确认键反转，底栏也优先匹配 Switch 风格
    if (InputSettings::isSwapAB()) {
        return KeymapStyle::Switch;
    }

    // 默认平台推导：SteamOS / PC / 桌面环境一律采用标准 Xbox/Steam 风格
    return KeymapStyle::Xbox;
}

std::string Keymap::resolveFontAssetPath(KeymapStyle style) {
    if (style == KeymapStyle::Auto) {
        style = resolveActiveStyle();
    }

    std::string rel_path;
    switch (style) {
        case KeymapStyle::PlayStation:
            rel_path = "font/keymap_ps.ttf";
            break;
        case KeymapStyle::Switch:
            rel_path = "font/switch_icons.ttf";
            break;
        case KeymapStyle::Xbox:
        default:
            rel_path = "font/keymap_xbox.ttf";
            break;
    }

    // 优先检查资源目录中的绝对路径
    const std::string& res_dir = ResourceLocator::resourceDir();
    if (!res_dir.empty()) {
        std::string abs_path = res_dir + "/" + rel_path;
        if (access(abs_path.c_str(), F_OK) == 0) {
            return abs_path;
        }
    }

    // 回退到相对路径
    std::string fallback = "./resources/" + rel_path;
    if (access(fallback.c_str(), F_OK) == 0) {
        return fallback;
    }

#if !defined(TESTING_WITHOUT_BOREALIS)
    return BRLS_ASSET("") + rel_path;
#else
    return rel_path;
#endif
}

void Keymap::applyKeymapFont() {
#if !defined(TESTING_WITHOUT_BOREALIS)
    std::string font_path = resolveFontAssetPath(getStyle());
    brls::FontLoader::USER_ICON_PATH = font_path;
    logInfo("Keymap: 初始化按键提示字体为: " + font_path + " (" + styleToShortString(resolveActiveStyle()) + ")");
#endif
}

bool Keymap::reloadKeymapFont() {
#if !defined(TESTING_WITHOUT_BOREALIS)
    std::string font_path = resolveFontAssetPath(getStyle());
    brls::FontLoader::USER_ICON_PATH = font_path;

    auto* vg = brls::Application::getNVGContext();
    if (!vg) return false;

    // 重新加载指定字体并绑定到 FONT_SWITCH_ICONS ("switch")
    bool ok = brls::Application::loadFontFromFile(brls::FONT_SWITCH_ICONS, font_path);
    if (ok) {
        nvgAddFallbackFontId(vg,
            brls::Application::getFont(brls::FONT_REGULAR),
            brls::Application::getFont(brls::FONT_SWITCH_ICONS));
        brls::Application::getGlobalHintsUpdateEvent()->fire();
        logInfo("Keymap: 热切换按键提示字体成功: " + font_path);
        return true;
    }
    return false;
#else
    return true;
#endif
}

std::string Keymap::styleToString(KeymapStyle style) {
    switch (style) {
        case KeymapStyle::Auto:        return i18n::tr("keymap.auto", "自动识别（跟随手柄）");
        case KeymapStyle::Xbox:        return i18n::tr("keymap.xbox", "Xbox / Steam Deck（A/B/X/Y）");
        case KeymapStyle::PlayStation: return i18n::tr("keymap.ps", "PlayStation（✕/◯/▢/△）");
        case KeymapStyle::Switch:      return i18n::tr("keymap.switch", "Nintendo Switch（Switch 风格）");
        default:                       return i18n::tr("keymap.unknown", "未知风格");
    }
}

std::string Keymap::styleToShortString(KeymapStyle style) {
    switch (style) {
        case KeymapStyle::Auto:        return i18n::tr("keymap.auto_short", "自动");
        case KeymapStyle::Xbox:        return i18n::tr("keymap.xbox_short", "Xbox / Steam");
        case KeymapStyle::PlayStation: return i18n::tr("keymap.ps_short", "PlayStation");
        case KeymapStyle::Switch:      return i18n::tr("keymap.switch_short", "Switch");
        default:                       return i18n::tr("keymap.unknown_short", "未知");
    }
}

} // namespace muvik
