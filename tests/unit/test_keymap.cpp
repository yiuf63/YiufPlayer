// 单元测试：Keymap 手柄提示图标风格与字体路径解析
#include "utils/keymap.hpp"
#include "utils/input_settings.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>

using namespace muvik;

int main() {
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_keymap", 1);
    std::system("rm -rf /tmp/muvik_test_keymap");

    TEST_BEGIN("Keymap / 默认 Auto 风格");
    unsetenv("MUVIK_KEYMAP");
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Auto, "无环境变量和配置时默认 Auto");

    TEST_BEGIN("Keymap / MUVIK_KEYMAP 环境变量");
    setenv("MUVIK_KEYMAP", "xbox", 1);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Xbox, "MUVIK_KEYMAP=xbox 解析为 Xbox");
    setenv("MUVIK_KEYMAP", "ps", 1);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::PlayStation, "MUVIK_KEYMAP=ps 解析为 PlayStation");
    setenv("MUVIK_KEYMAP", "switch", 1);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Switch, "MUVIK_KEYMAP=switch 解析为 Switch");
    setenv("MUVIK_KEYMAP", "auto", 1);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Auto, "MUVIK_KEYMAP=auto 解析为 Auto");
    unsetenv("MUVIK_KEYMAP");

    TEST_BEGIN("Keymap / 配置文件持久化解析");
    AppPaths::ensureDir(AppPaths::configDir());
    {
        std::ofstream f(AppPaths::configFile("keymap"), std::ios::trunc);
        f << "ps\n";
    }
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::PlayStation, "配置文件为 ps 时解析为 PlayStation");

    {
        std::ofstream f(AppPaths::configFile("keymap"), std::ios::trunc);
        f << "switch\n";
    }
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Switch, "配置文件为 switch 时解析为 Switch");

    TEST_BEGIN("Keymap / setStyle 持久化写入");
    Keymap::setStyle(KeymapStyle::Xbox, true);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Xbox, "setStyle(Xbox) 成功写入配置文件");
    Keymap::setStyle(KeymapStyle::Auto, true);
    CHECK_EQ((int)Keymap::getStyle(), (int)KeymapStyle::Auto, "setStyle(Auto) 成功写入配置文件");

    TEST_BEGIN("Keymap / 字体路径解析");
    std::string xbox_path = Keymap::resolveFontAssetPath(KeymapStyle::Xbox);
    CHECK_TRUE(xbox_path.find("keymap_xbox.ttf") != std::string::npos, "Xbox 风格对应 keymap_xbox.ttf");

    std::string ps_path = Keymap::resolveFontAssetPath(KeymapStyle::PlayStation);
    CHECK_TRUE(ps_path.find("keymap_ps.ttf") != std::string::npos, "PS 风格对应 keymap_ps.ttf");

    std::string switch_path = Keymap::resolveFontAssetPath(KeymapStyle::Switch);
    CHECK_TRUE(switch_path.find("switch_icons.ttf") != std::string::npos, "Switch 风格对应 switch_icons.ttf");

    TEST_BEGIN("Keymap / 字符串辅助函数");
    CHECK_FALSE(Keymap::styleToString(KeymapStyle::Xbox).empty(), "styleToString 非空");
    CHECK_FALSE(Keymap::styleToShortString(KeymapStyle::PlayStation).empty(), "styleToShortString 非空");

    TEST_SUMMARY();
}
