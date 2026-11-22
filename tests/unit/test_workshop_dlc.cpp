// 单元测试：Steam 创意工坊与 DLC 插件机制
#include "plugin/plugin_manager.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <filesystem>

using namespace muvik;
namespace fs = std::filesystem;

int main() {
    TEST_SUITE();
    auto& pm = PluginManager::getInstance();

    TEST_BEGIN("Steam / App ID 解析");
    unsetenv("STEAM_APP_ID");
    unsetenv("MUVIK_STEAM_APP_ID");
    CHECK_EQ(pm.getSteamAppId(), "3290940", "默认 Steam App ID 为 3290940");

    setenv("STEAM_APP_ID", "123456", 1);
    CHECK_EQ(pm.getSteamAppId(), "123456", "STEAM_APP_ID 环境变量覆盖");
    unsetenv("STEAM_APP_ID");

    setenv("MUVIK_STEAM_APP_ID", "987654", 1);
    CHECK_EQ(pm.getSteamAppId(), "987654", "MUVIK_STEAM_APP_ID 环境变量覆盖");
    unsetenv("MUVIK_STEAM_APP_ID");

    TEST_BEGIN("Steam / 创意工坊与 DLC 搜索路径覆盖");
    std::string fake_ws = "/tmp/test_deckplayer_ws";
    std::string fake_dlc = "/tmp/test_deckplayer_dlc";
    fs::create_directories(fake_ws);
    fs::create_directories(fake_dlc);

    setenv("STEAM_WORKSHOP_PATH", fake_ws.c_str(), 1);
    setenv("STEAM_DLC_PATH", fake_dlc.c_str(), 1);

    auto ws_paths = pm.getWorkshopSearchPaths();
    CHECK_FALSE(ws_paths.empty(), "能获取创意工坊路径");
    bool found_ws = false;
    for (const auto& p : ws_paths) {
        if (p == fake_ws) found_ws = true;
    }
    CHECK_TRUE(found_ws, "环境变量指定的创意工坊路径被包含");

    auto dlc_paths = pm.getDlcSearchPaths();
    CHECK_FALSE(dlc_paths.empty(), "能获取 DLC 路径");
    bool found_dlc = false;
    for (const auto& p : dlc_paths) {
        if (p == fake_dlc) found_dlc = true;
    }
    CHECK_TRUE(found_dlc, "环境变量指定的 DLC 路径被包含");

    unsetenv("STEAM_WORKSHOP_PATH");
    unsetenv("STEAM_DLC_PATH");
    fs::remove_all(fake_ws);
    fs::remove_all(fake_dlc);

    TEST_BEGIN("Steam / 插件来源类型与徽标属性");
    LoadedPlugin p_local;
    p_local.source_type = PluginSourceType::Local;
    p_local.source_badge = "[本地]";
    CHECK_EQ(p_local.source_badge, "[本地]", "本地插件徽标");

    LoadedPlugin p_ws;
    p_ws.source_type = PluginSourceType::Workshop;
    p_ws.workshop_item_id = "889900";
    p_ws.source_badge = "[创意工坊]";
    CHECK_EQ(p_ws.source_badge, "[创意工坊]", "创意工坊插件徽标");
    CHECK_EQ(p_ws.workshop_item_id, "889900", "创意工坊订阅 ID");

    LoadedPlugin p_dlc;
    p_dlc.source_type = PluginSourceType::DLC;
    p_dlc.source_badge = "[DLC]";
    CHECK_EQ(p_dlc.source_badge, "[DLC]", "DLC 插件徽标");

    return TEST_SUMMARY();
}
