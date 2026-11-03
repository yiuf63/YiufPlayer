#pragma once

#include <string>
#include <string>

namespace deck_player {

// 应用数据目录的统一解析。遵循 XDG 规范，兼容 Linux / SteamOS 与 macOS。
class AppPaths {
public:
    // 配置根目录，如 ~/.config/deck-player（必要时创建）
    static std::string configDir();

    // 插件目录 ~/.config/deck-player/plugins（必要时创建）
    static std::string pluginsDir();

    // 配置文件完整路径，如 ~/.config/deck-player/<name>
    static std::string configFile(const std::string& filename);

    // 确保某个目录存在，返回是否成功
    static bool ensureDir(const std::string& dir);
};

} // namespace deck_player
