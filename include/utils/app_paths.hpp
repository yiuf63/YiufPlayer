#pragma once

#include <string>
#include <vector>

namespace muvik {

// 应用数据目录的统一解析。遵循 XDG 规范，兼容 Linux / SteamOS、macOS 与 Android。
class AppPaths {
public:
    // 用户主目录，如 /home/deck 或 /Users/xxx
    static std::string homeDir();

    // 配置根目录，如 ~/.config/muvik（必要时创建）
    static std::string configDir();

    // 插件目录 ~/.config/muvik/plugins（必要时创建）
    static std::string pluginsDir();

    // 主题目录 ~/.config/muvik/themes（必要时创建）
    static std::string themesDir();

    // Steam 创意工坊搜索路径列表
    static std::vector<std::string> workshopSearchPaths(const std::string& app_id = "3290940");

    // 配置文件完整路径，如 ~/.config/muvik/<name>
    static std::string configFile(const std::string& filename);

    // 确保某个目录存在，返回是否成功
    static bool ensureDir(const std::string& dir);

    // 检查当前设备是否为 TrimUI 系列掌机 (TrimUI Smart Pro / Brick / Smart)
    static bool isTrimUI();
};

} // namespace muvik
