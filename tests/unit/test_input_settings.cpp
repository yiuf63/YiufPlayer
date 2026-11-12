// 单元测试：InputSettings 手柄键位设置与持久化
#include "utils/input_settings.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>

using namespace muvik;

int main() {
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_input", 1);
    std::system("rm -rf /tmp/muvik_test_input");

    TEST_BEGIN("InputSettings / 默认 Xbox 布局");
    unsetenv("MUVIK_SWAP_AB");
    CHECK_FALSE(InputSettings::resolveSwapAB(), "无环境变量和配置时默认不反转（Xbox 布局）");

    TEST_BEGIN("InputSettings / MUVIK_SWAP_AB 环境变量");
    setenv("MUVIK_SWAP_AB", "1", 1);
    CHECK_TRUE(InputSettings::resolveSwapAB(), "MUVIK_SWAP_AB=1 时反转为 Switch 布局");
    setenv("MUVIK_SWAP_AB", "switch", 1);
    CHECK_TRUE(InputSettings::resolveSwapAB(), "MUVIK_SWAP_AB=switch 时反转为 Switch 布局");
    setenv("MUVIK_SWAP_AB", "0", 1);
    CHECK_FALSE(InputSettings::resolveSwapAB(), "MUVIK_SWAP_AB=0 时恢复 Xbox 布局");
    setenv("MUVIK_SWAP_AB", "xbox", 1);
    CHECK_FALSE(InputSettings::resolveSwapAB(), "MUVIK_SWAP_AB=xbox 时恢复 Xbox 布局");
    unsetenv("MUVIK_SWAP_AB");

    TEST_BEGIN("InputSettings / 配置文件持久化解析");
    AppPaths::ensureDir(AppPaths::configDir());
    {
        std::ofstream f(AppPaths::configFile("swap_ab"), std::ios::trunc);
        f << "1\n";
    }
    CHECK_TRUE(InputSettings::resolveSwapAB(), "配置文件为 1 时解析为 Switch 布局");

    {
        std::ofstream f(AppPaths::configFile("swap_ab"), std::ios::trunc);
        f << "0\n";
    }
    CHECK_FALSE(InputSettings::resolveSwapAB(), "配置文件为 0 时解析为 Xbox 布局");

    TEST_BEGIN("InputSettings / setSwapAB 持久化");
    InputSettings::setSwapAB(true, true);
    CHECK_TRUE(InputSettings::resolveSwapAB(), "setSwapAB(true) 成功写入并解析为 Switch 布局");
    InputSettings::setSwapAB(false, true);
    CHECK_FALSE(InputSettings::resolveSwapAB(), "setSwapAB(false) 成功写入并解析为 Xbox 布局");

    TEST_BEGIN("InputSettings / 环境变量覆盖配置文件");
    InputSettings::setSwapAB(false, true);
    setenv("MUVIK_SWAP_AB", "1", 1);
    CHECK_TRUE(InputSettings::resolveSwapAB(), "环境变量 MUVIK_SWAP_AB=1 优先于配置文件");
    unsetenv("MUVIK_SWAP_AB");

    TEST_END();
}
