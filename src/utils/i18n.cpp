#include "utils/i18n.hpp"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "utils/app_paths.hpp"
#include "utils/resource_locator.hpp"

namespace muvik::i18n {

namespace {

std::mutex g_mutex;
std::string g_configured_lang = "auto";
std::string g_resolved_lang = "zh-Hans";

nlohmann::json g_current_json;
nlohmann::json g_zh_json;
nlohmann::json g_en_json;
bool g_initialized = false;

const std::vector<LanguageOption> kAvailableLanguages = {
    {"auto", "跟随系统 / Auto"},
    {"zh-Hans", "简体中文"},
    {"en-US", "English"},
};

std::string getLocaleFilePath() {
    return AppPaths::configDir() + "/locale";
}

nlohmann::json loadJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return nlohmann::json::object();
    try {
        nlohmann::json j;
        file >> j;
        return j;
    } catch (...) {
        return nlohmann::json::object();
    }
}

// 尝试从多个备选路径加载翻译文件
nlohmann::json loadLocaleCatalog(const std::string& locale) {
    std::vector<std::string> candidates;

    // 1. 通过 ResourceLocator 定位到的资源目录
    const std::string& res_dir = ResourceLocator::resourceDir();
    if (!res_dir.empty()) {
        candidates.push_back(res_dir + "/i18n/" + locale + "/muvik.json");
        candidates.push_back(res_dir + "/i18n/" + locale + "/deck_player.json");
    }

    // 2. 相对可执行文件目录
    //
    // 安装目录名必须同时覆盖 "yiufplayer"（当前 CMakeLists 的安装目标）与
    // "muvik"（旧版布局 / Flatpak）。之前只查 muvik，配合
    // install(DESTINATION share/yiufplayer)，装出来就是英文界面变中文 key、
    // 或者整块文字消失 —— 与 resource_locator 的同一处缺陷同源。
    const std::string exe_dir = ResourceLocator::executableDir();
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir + "/resources/i18n/" + locale + "/muvik.json");
        candidates.push_back(exe_dir + "/../resources/i18n/" + locale + "/muvik.json");
        for (const char* app : {"yiufplayer", "muvik"}) {
            candidates.push_back(exe_dir + "/share/" + app + "/resources/i18n/" + locale + "/muvik.json");
            candidates.push_back(exe_dir + "/../share/" + app + "/resources/i18n/" + locale + "/muvik.json");
        }
    }

    // 3. 环境变量 MUVIK_RESOURCES
    if (const char* env = std::getenv("MUVIK_RESOURCES")) {
        candidates.push_back(std::string(env) + "/i18n/" + locale + "/muvik.json");
    }

    // 4. 当前工作目录及相对路径
    candidates.push_back("resources/i18n/" + locale + "/muvik.json");
    candidates.push_back("./resources/i18n/" + locale + "/muvik.json");
    candidates.push_back("../resources/i18n/" + locale + "/muvik.json");
    candidates.push_back("resources/i18n/" + locale + "/deck_player.json");
    candidates.push_back("./resources/i18n/" + locale + "/deck_player.json");
    candidates.push_back("../resources/i18n/" + locale + "/deck_player.json");

    // 5. 常见系统安装路径（同样两个目录名都要覆盖）
    for (const char* app : {"yiufplayer", "muvik"}) {
        candidates.push_back(std::string("/app/share/") + app + "/resources/i18n/" + locale + "/muvik.json");
        candidates.push_back(std::string("/usr/share/") + app + "/resources/i18n/" + locale + "/muvik.json");
        candidates.push_back(std::string("/usr/local/share/") + app + "/resources/i18n/" + locale + "/muvik.json");
    }

    for (const auto& path : candidates) {
        nlohmann::json j = loadJsonFile(path);
        if (!j.empty()) return j;
    }
    return nlohmann::json::object();
}

std::string detectSystemLanguage() {
    const char* env_lang = std::getenv("MUVIK_LANG");
    if (!env_lang || !*env_lang) env_lang = std::getenv("LC_ALL");
    if (!env_lang || !*env_lang) env_lang = std::getenv("LANG");
    if (!env_lang || !*env_lang) env_lang = std::getenv("LANGUAGE");

    if (env_lang && *env_lang) {
        std::string s(env_lang);
        if (s.find("zh") != std::string::npos || s.find("CN") != std::string::npos || s.find("Hans") != std::string::npos) {
            return "zh-Hans";
        }
        if (s.find("en") != std::string::npos) {
            return "en-US";
        }
    }
    // 默认中文
    return "zh-Hans";
}

void reloadCatalogsLocked() {
    g_zh_json = loadLocaleCatalog("zh-Hans");
    g_en_json = loadLocaleCatalog("en-US");

    if (g_configured_lang == "auto") {
        g_resolved_lang = detectSystemLanguage();
    } else {
        g_resolved_lang = g_configured_lang;
    }

    if (g_resolved_lang == "en-US") {
        g_current_json = g_en_json;
    } else {
        g_current_json = g_zh_json;
    }
}

} // namespace

void init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) return;

    // 从配置文件读取
    std::ifstream file(getLocaleFilePath());
    if (file.is_open()) {
        std::string line;
        if (std::getline(file, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line == "zh-Hans" || line == "en-US" || line == "auto") {
                g_configured_lang = line;
            }
        }
    }

    // 允许通过环境变量临时覆盖
    if (const char* env = std::getenv("MUVIK_LANG")) {
        std::string s(env);
        if (s == "zh-Hans" || s == "en-US" || s == "auto") {
            g_configured_lang = s;
        }
    }

    reloadCatalogsLocked();
    g_initialized = true;
}

std::string getLanguage() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        // 懒初始化
        reloadCatalogsLocked();
        g_initialized = true;
    }
    return g_configured_lang;
}

std::string getResolvedLanguage() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        reloadCatalogsLocked();
        g_initialized = true;
    }
    return g_resolved_lang;
}

void setLanguage(const std::string& lang) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (lang != "zh-Hans" && lang != "en-US" && lang != "auto") {
        return;
    }
    g_configured_lang = lang;
    reloadCatalogsLocked();
    g_initialized = true;

    // 持久化保存
    std::ofstream file(getLocaleFilePath());
    if (file.is_open()) {
        file << g_configured_lang << "\n";
    }
}

const std::vector<LanguageOption>& getAvailableLanguages() {
    return kAvailableLanguages;
}

std::string tr(const std::string& key, const std::string& fallback) {
    if (key.empty()) return fallback;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        reloadCatalogsLocked();
        g_initialized = true;
    }

    // 将 key 按 '.' 或 '/' 拆解成 JSON 指针路径
    std::vector<std::string> parts;
    std::string token;
    for (char c : key) {
        if (c == '.' || c == '/') {
            if (!token.empty()) {
                parts.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        parts.push_back(token);
    }

    auto lookup = [&parts](const nlohmann::json& root) -> std::string {
        if (root.empty() || parts.empty()) return "";
        const nlohmann::json* cur = &root;
        for (const auto& part : parts) {
            if (!cur->is_object() || !cur->contains(part)) {
                return "";
            }
            cur = &(*cur)[part];
        }
        if (cur->is_string()) {
            return cur->get<std::string>();
        }
        return "";
    };

    // 1. 优先查当前语言
    std::string val = lookup(g_current_json);
    if (!val.empty()) return val;

    // 2. 回退查默认中文语言
    val = lookup(g_zh_json);
    if (!val.empty()) return val;

    // 3. 最终返回 fallback 或 key
    return fallback.empty() ? key : fallback;
}

} // namespace muvik::i18n
