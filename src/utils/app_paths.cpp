#include <cstdlib>
#include <string>
#include "utils/app_paths.hpp"

#if defined(ANDROID)
#include <SDL2/SDL_system.h>
#endif

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace deck_player {

std::string AppPaths::configDir() {
#if defined(ANDROID)
    // Android 上没有 XDG 规范，也没有 $HOME。
    // 应用私有目录（/data/data/<pkg>/files）可读写且无需任何权限，
    // 是存放配置与缓存的标准位置。
    // SDL 已封装好取该路径的逻辑，直接用它：
    //   SDL_AndroidGetInternalStoragePath() → /data/user/0/<pkg>/files
    // 若 SDL 尚未初始化，退回 TMPDIR。
    if (const char* internal = SDL_AndroidGetInternalStoragePath();
        internal && *internal) {
        return std::string(internal) + "/deck-player";
    }
    if (const char* tmp = std::getenv("TMPDIR"); tmp && *tmp) {
        return std::string(tmp) + "/deck-player";
    }
    return "/data/local/tmp/deck-player";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/deck-player";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/deck-player";
    }
    // 无 HOME 时退化为当前目录下的相对路径，保证程序仍可使用
    return "./deck-player";
#endif
}

std::string AppPaths::pluginsDir() {
    return configDir() + "/plugins";
}

std::string AppPaths::configFile(const std::string& filename) {
    return configDir() + "/" + filename;
}

bool AppPaths::ensureDir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (fs::exists(dir, ec)) return fs::is_directory(dir, ec);
    return fs::create_directories(dir, ec) && !ec;
}

} // namespace deck_player
