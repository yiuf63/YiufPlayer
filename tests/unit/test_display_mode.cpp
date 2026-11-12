// 单元测试：DisplayMode 全屏解析与配置持久化
#include "utils/display_mode.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>

using namespace muvik;

int main() {
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_display", 1);
    std::system("rm -rf /tmp/muvik_test_display");

    TEST_BEGIN("DisplayMode / 默认全屏模式");
    unsetenv("MUVIK_WINDOW");
    unsetenv("MUVIK_FULLSCREEN");
    CHECK_TRUE(DisplayMode::resolveFullscreen(), "无环境变量和配置时默认开启全屏");

    TEST_BEGIN("DisplayMode / MUVIK_WINDOW 强制窗口");
    setenv("MUVIK_WINDOW", "1280x720", 1);
    CHECK_FALSE(DisplayMode::resolveFullscreen(), "MUVIK_WINDOW 存在时强制窗口模式");
    unsetenv("MUVIK_WINDOW");

    TEST_BEGIN("DisplayMode / MUVIK_FULLSCREEN 环境变量");
    setenv("MUVIK_FULLSCREEN", "0", 1);
    CHECK_FALSE(DisplayMode::resolveFullscreen(), "MUVIK_FULLSCREEN=0 时为窗口模式");
    setenv("MUVIK_FULLSCREEN", "window", 1);
    CHECK_FALSE(DisplayMode::resolveFullscreen(), "MUVIK_FULLSCREEN=window 时为窗口模式");
    setenv("MUVIK_FULLSCREEN", "1", 1);
    CHECK_TRUE(DisplayMode::resolveFullscreen(), "MUVIK_FULLSCREEN=1 时为全屏模式");
    setenv("MUVIK_FULLSCREEN", "fullscreen", 1);
    CHECK_TRUE(DisplayMode::resolveFullscreen(), "MUVIK_FULLSCREEN=fullscreen 时为全屏模式");
    unsetenv("MUVIK_FULLSCREEN");

    TEST_BEGIN("DisplayMode / 配置文件持久化解析");
    AppPaths::ensureDir(AppPaths::configDir());
    {
        std::ofstream f(AppPaths::configFile("fullscreen"), std::ios::trunc);
        f << "window\n";
    }
    CHECK_FALSE(DisplayMode::resolveFullscreen(), "配置文件为 window 时解析为窗口模式");

    {
        std::ofstream f(AppPaths::configFile("fullscreen"), std::ios::trunc);
        f << "fullscreen\n";
    }
    CHECK_TRUE(DisplayMode::resolveFullscreen(), "配置文件为 fullscreen 时解析为全屏模式");

    TEST_BEGIN("DisplayMode / 环境变量覆盖配置文件");
    setenv("MUVIK_FULLSCREEN", "0", 1);
    CHECK_FALSE(DisplayMode::resolveFullscreen(), "环境变量 MUVIK_FULLSCREEN=0 优先于配置文件");
    unsetenv("MUVIK_FULLSCREEN");

    TEST_BEGIN("DisplayMode / 帧率上限配置与环境变量解析");
    unsetenv("MUVIK_FPS");
    DisplayMode::setTargetFps(0, true);
    CHECK_EQ(DisplayMode::getTargetFps(), 0, "默认或设为 0 时为自动跟随屏幕");
    DisplayMode::setTargetFps(90, true);
    CHECK_EQ(DisplayMode::getTargetFps(), 90, "支持持久化设置为 90 FPS");
    setenv("MUVIK_FPS", "144", 1);
    CHECK_EQ(DisplayMode::getTargetFps(), 144, "MUVIK_FPS 环境变量优先覆盖");
    unsetenv("MUVIK_FPS");

    TEST_END();
}
