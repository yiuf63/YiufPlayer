#include "utils/input_settings.hpp"
#include "utils/app_paths.hpp"

#include <cstdlib>
#include <fstream>
#include <cctype>

#if !defined(TESTING_WITHOUT_BOREALIS)
#include <borealis/core/application.hpp>
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
static bool g_mock_swap_ab = false;
#endif

} // namespace

bool InputSettings::resolveSwapAB() {
    // 1. 显式环境变量覆盖（用于命令行测试或脚本调试）
    if (const char* env = std::getenv("MUVIK_SWAP_AB")) {
        std::string s = trim(env);
        if (s == "1" || s == "true" || s == "yes" || s == "on" || s == "switch") {
            logInfo("InputSettings: MUVIK_SWAP_AB=" + std::string(env) + "，开启 Switch 键位反转");
            return true;
        }
        if (s == "0" || s == "false" || s == "no" || s == "off" || s == "xbox") {
            logInfo("InputSettings: MUVIK_SWAP_AB=" + std::string(env) + "，使用标准 Xbox/Steam 键位");
            return false;
        }
    }

    // 2. 用户配置文件（~/.config/muvik/swap_ab）
    std::ifstream f(AppPaths::configFile("swap_ab"));
    if (f) {
        std::string line;
        if (std::getline(f, line)) {
            std::string s = trim(line);
            if (s == "1" || s == "true" || s == "switch" || s == "yes") {
                return true;
            }
            if (s == "0" || s == "false" || s == "xbox" || s == "no") {
                return false;
            }
        }
    }

    // 3. 硬件平台探测：TrimUI 系列掌机物理丝印为 Switch 布局（右键 A 确认 / 下键 B 返回）
    // 系统的 trimui_inputd 驱动模拟了 Xbox 360 手柄导致逻辑反转，故默认自动开启 Switch 键位反转
    if (AppPaths::isTrimUI()) {
        logInfo("InputSettings: 检测到 TrimUI 掌机硬件，默认启用 Switch 按键布局（右键 A 确认 / 下键 B 返回）");
        return true;
    }

    // 4. 默认不反转（Steam Deck / Xbox 标准布局）
    return false;
}

bool InputSettings::isSwapAB() {
#if !defined(TESTING_WITHOUT_BOREALIS)
    return brls::Application::isSwapInputKeys();
#else
    return g_mock_swap_ab;
#endif
}

void InputSettings::setSwapAB(bool swap, bool persist) {
#if !defined(TESTING_WITHOUT_BOREALIS)
    brls::Application::setSwapInputKeys(swap);
#else
    g_mock_swap_ab = swap;
#endif

    if (persist) {
        AppPaths::ensureDir(AppPaths::configDir());
        std::ofstream f(AppPaths::configFile("swap_ab"), std::ios::trunc);
        if (f) {
            f << (swap ? "1" : "0") << "\n";
            logInfo(std::string("InputSettings: 已保存手柄键位偏好: ") + (swap ? "Switch (1)" : "Xbox (0)"));
        }
    }
}

void InputSettings::toggleSwapAB(bool persist) {
    setSwapAB(!isSwapAB(), persist);
}

void InputSettings::applySettings() {
    bool swap = resolveSwapAB();
#if !defined(TESTING_WITHOUT_BOREALIS)
    brls::Application::setSwapInputKeys(swap);
    logInfo(std::string("InputSettings: 初始化手柄键位为: ") + (swap ? "Switch (B确认)" : "Xbox (A确认)"));
#else
    g_mock_swap_ab = swap;
#endif
}

} // namespace muvik
