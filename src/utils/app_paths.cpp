#include <cstdlib>
#include <string>
#include <algorithm>
#include "utils/app_paths.hpp"

#if defined(ANDROID)
#include <SDL2/SDL_system.h>
#endif

#include <filesystem>

namespace fs = std::filesystem;

namespace muvik {

std::string AppPaths::homeDir() {
    const char* home = std::getenv("HOME");
    if (home && *home) return home;
    return "";
}

std::string AppPaths::configDir() {
#if defined(ANDROID)
    if (const char* internal = SDL_AndroidGetInternalStoragePath();
        internal && *internal) {
        return std::string(internal) + "/muvik";
    }
    if (const char* tmp = std::getenv("TMPDIR"); tmp && *tmp) {
        return std::string(tmp) + "/muvik";
    }
    return "/data/local/tmp/muvik";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/muvik";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/muvik";
    }
    return "./muvik";
#endif
}

std::string AppPaths::pluginsDir() {
    return configDir() + "/plugins";
}

std::string AppPaths::themesDir() {
    std::string d = configDir() + "/themes";
    ensureDir(d);
    return d;
}

std::vector<std::string> AppPaths::workshopSearchPaths(const std::string& app_id) {
    std::vector<std::string> paths;
    const char* env_ws = std::getenv("STEAM_WORKSHOP_PATH");
    if (!env_ws || !*env_ws) env_ws = std::getenv("MUVIK_WORKSHOP_DIR");
    if (env_ws && *env_ws) {
        if (fs::exists(env_ws)) paths.push_back(env_ws);
    }

    std::string home = homeDir();
    std::vector<std::string> candidates = {
        home + "/.local/share/Steam/steamapps/workshop/content/" + app_id,
        home + "/.steam/steam/steamapps/workshop/content/" + app_id,
        home + "/.steam/root/steamapps/workshop/content/" + app_id,
        home + "/Library/Application Support/Steam/steamapps/workshop/content/" + app_id,
    };

    for (const auto& c : candidates) {
        if (fs::exists(c) && std::find(paths.begin(), paths.end(), c) == paths.end()) {
            paths.push_back(c);
        }
    }
    return paths;
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

} // namespace muvik
