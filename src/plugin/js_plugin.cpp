#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "plugin/js_plugin.hpp"
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace muvik {

JSPlugin::JSPlugin(std::string dir_path, std::string id, std::string name, std::string version)
    : dir_path_(std::move(dir_path)),
      id_(std::move(id)),
      name_(std::move(name)),
      version_(std::move(version)) {}

bool JSPlugin::hasCapability(const std::string& cap) const {
    for (const auto& c : capabilities_) {
        if (c == cap) return true;
    }
    return false;
}

std::shared_ptr<JSPlugin> JSPlugin::loadFromDirectory(const std::string& dir_path, std::string* out_err) {
    std::string manifest_path = dir_path + "/manifest.json";
    std::string main_js_path = dir_path + "/main.js";

    if (!fs::exists(manifest_path)) {
        if (out_err) *out_err = "缺少 manifest.json: " + manifest_path;
        return nullptr;
    }
    if (!fs::exists(main_js_path)) {
        if (out_err) *out_err = "缺少 main.js: " + main_js_path;
        return nullptr;
    }

    // 1. 读取并解析 manifest.json
    json meta;
    try {
        std::ifstream ifs(manifest_path);
        if (!ifs.is_open()) {
            if (out_err) *out_err = "无法打开 manifest.json: " + manifest_path;
            return nullptr;
        }
        meta = json::parse(ifs);
    } catch (const std::exception& e) {
        if (out_err) *out_err = std::string("解析 manifest.json 失败: ") + e.what();
        return nullptr;
    }

    std::string id = meta.value("id", "");
    std::string name = meta.value("name", "");
    std::string version = meta.value("version", "1.0.0");
    std::string author = meta.value("author", "");
    std::string description = meta.value("description", "");

    if (id.empty() || name.empty()) {
        if (out_err) *out_err = "manifest.json 缺少必需字段 id 或 name";
        return nullptr;
    }

    auto plugin = std::make_shared<JSPlugin>(dir_path, id, name, version);
    plugin->author_ = author;
    plugin->description_ = description;

    // 解析 capabilities
    if (meta.contains("capabilities") && meta["capabilities"].is_array()) {
        for (const auto& cap : meta["capabilities"]) {
            if (cap.is_string()) plugin->capabilities_.push_back(cap.get<std::string>());
        }
    }

    // 解析 settings
    if (meta.contains("settings") && meta["settings"].is_array()) {
        for (const auto& s : meta["settings"]) {
            if (s.is_object()) {
                PluginSettingField f;
                f.key = s.value("key", "");
                f.label = s.value("label", f.key);
                f.type = s.value("type", "string");
                f.default_value = s.value("default", "");
                if (!f.key.empty()) plugin->settings_.push_back(f);
            }
        }
    }

    // 2. 读取 main.js
    std::string script;
    {
        std::ifstream ifs(main_js_path);
        if (!ifs.is_open()) {
            if (out_err) *out_err = "无法打开 main.js: " + main_js_path;
            return nullptr;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        script = ss.str();
    }

    // 3. 创建 QuickJS 运行时沙箱并执行 main.js
    auto runtime = std::make_unique<JSRuntimeHost>(id);
    std::string js_err;
    if (!runtime->evaluate(script, main_js_path, &js_err)) {
        if (out_err) *out_err = "main.js 加载失败: " + js_err;
        brls::Logger::error("加载 JS 插件失败 {} ({}): {}", name, id, js_err);
        return nullptr;
    }

    // 自动探测能力（如果 manifest 中未明确指定）
    if (plugin->capabilities_.empty()) {
        if (runtime->hasFunction("search") || runtime->hasFunction("loadLinks")) {
            plugin->capabilities_.push_back("media_stream");
        }
        if (runtime->hasFunction("listDirectory") || runtime->hasFunction("resolvePlayUrl")) {
            plugin->capabilities_.push_back("storage");
        }
        if (runtime->hasFunction("scrapeSearch") || runtime->hasFunction("scrapeDetail")) {
            plugin->capabilities_.push_back("scraper");
        }
    }

    plugin->runtime_ = std::move(runtime);
    brls::Logger::info("已加载插件: {} v{} [{}] (JS)", name, version, id);
    return plugin;
}

// --- MediaStreamProvider 实现 ---

bool JSPlugin::search(const std::string& keyword, int page, std::vector<MediaItem>& out_items, bool& out_has_more) {
    out_items.clear();
    out_has_more = false;
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("search")) return false;

    std::string out_json, err;
    json arg_kw = keyword;
    json arg_pg = page;
    if (!runtime_->callFunction("search", { arg_kw.dump(), arg_pg.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] search 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        json items_arr = json::array();
        if (root.is_array()) {
            items_arr = root;
        } else if (root.is_object()) {
            out_has_more = root.value("hasMore", false);
            if (root.contains("items") && root["items"].is_array()) {
                items_arr = root["items"];
            }
        }

        for (const auto& item : items_arr) {
            if (!item.is_object()) continue;
            MediaItem mi;
            mi.name = item.value("title", item.value("name", ""));
            std::string item_id = item.value("id", "");
            mi.path = "plugin://" + id_ + "/stream/" + item_id;
            std::string type_str = item.value("type", "video");
            mi.type = (type_str == "audio") ? MediaType::Audio : MediaType::Video;
            mi.duration = item.value("duration", 0);
            out_items.push_back(mi);
        }
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] search 返回结果解析失败: {}", id_, e.what());
        return false;
    }
}

bool JSPlugin::loadDetail(const std::string& id, MediaDetail& out_detail) {
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("loadDetail")) return false;

    std::string out_json, err;
    json arg_id = id;
    if (!runtime_->callFunction("loadDetail", { arg_id.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] loadDetail 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        out_detail.id = root.value("id", id);
        out_detail.title = root.value("title", root.value("name", ""));
        out_detail.overview = root.value("overview", root.value("description", ""));
        out_detail.poster_url = root.value("poster", root.value("poster_url", ""));
        out_detail.backdrop_url = root.value("backdrop", root.value("backdrop_url", ""));
        out_detail.release_date = root.value("releaseDate", root.value("release_date", ""));

        out_detail.episodes.clear();
        if (root.contains("episodes") && root["episodes"].is_array()) {
            for (const auto& ep : root["episodes"]) {
                if (!ep.is_object()) continue;
                EpisodeItem epi;
                epi.id = ep.value("id", "");
                epi.title = ep.value("title", ep.value("name", ""));
                epi.episode_number = ep.value("episodeNumber", ep.value("number", 0));
                out_detail.episodes.push_back(epi);
            }
        }
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] loadDetail 返回解析失败: {}", id_, e.what());
        return false;
    }
}

bool JSPlugin::loadLinks(const std::string& episode_id, PlayLinksResult& out_links) {
    out_links.streams.clear();
    out_links.subtitles.clear();
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("loadLinks")) return false;

    std::string out_json, err;
    json arg_id = episode_id;
    if (!runtime_->callFunction("loadLinks", { arg_id.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] loadLinks 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        if (root.contains("streams") && root["streams"].is_array()) {
            for (const auto& s : root["streams"]) {
                if (!s.is_object()) continue;
                StreamSource src;
                src.name = s.value("name", "默认线路");
                src.url = s.value("url", "");
                src.format = s.value("format", "mp4");
                src.expires_at = s.value("expiresAt", 0);
                if (s.contains("headers") && s["headers"].is_object()) {
                    for (auto it = s["headers"].begin(); it != s["headers"].end(); ++it) {
                        if (it.value().is_string()) src.headers[it.key()] = it.value().get<std::string>();
                    }
                }
                if (!src.url.empty()) out_links.streams.push_back(src);
            }
        }
        if (root.contains("subtitles") && root["subtitles"].is_array()) {
            for (const auto& sub : root["subtitles"]) {
                if (!sub.is_object()) continue;
                SubtitleSource ss;
                ss.name = sub.value("name", "字幕");
                ss.url = sub.value("url", "");
                ss.format = sub.value("format", "srt");
                if (!ss.url.empty()) out_links.subtitles.push_back(ss);
            }
        }
        return !out_links.streams.empty();
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] loadLinks 解析失败: {}", id_, e.what());
        return false;
    }
}

// --- StorageProvider 实现 ---

bool JSPlugin::listDirectory(const std::string& path, std::vector<MediaItem>& out_items) {
    out_items.clear();
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("listDirectory")) return false;

    std::string out_json, err;
    json arg_path = path;
    if (!runtime_->callFunction("listDirectory", { arg_path.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] listDirectory 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        // 支持 { directories: [...], files: [...] } 或统一数组
        if (root.is_object()) {
            if (root.contains("directories") && root["directories"].is_array()) {
                for (const auto& d : root["directories"]) {
                    if (!d.is_object()) continue;
                    MediaItem mi;
                    mi.name = d.value("name", "");
                    mi.path = d.value("path", "");
                    mi.type = MediaType::Directory;
                    out_items.push_back(mi);
                }
            }
            if (root.contains("files") && root["files"].is_array()) {
                for (const auto& f : root["files"]) {
                    if (!f.is_object()) continue;
                    MediaItem mi;
                    mi.name = f.value("name", "");
                    mi.path = f.value("path", "");
                    std::string t = f.value("type", "video");
                    mi.type = (t == "audio") ? MediaType::Audio : MediaType::Video;
                    mi.size = f.value("size", 0);
                    out_items.push_back(mi);
                }
            }
        } else if (root.is_array()) {
            for (const auto& item : root) {
                if (!item.is_object()) continue;
                MediaItem mi;
                mi.name = item.value("name", "");
                mi.path = item.value("path", "");
                bool is_dir = item.value("isDirectory", false);
                if (is_dir) {
                    mi.type = MediaType::Directory;
                } else {
                    std::string t = item.value("type", "video");
                    mi.type = (t == "audio") ? MediaType::Audio : MediaType::Video;
                    mi.size = item.value("size", 0);
                }
                out_items.push_back(mi);
            }
        }
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] listDirectory 解析失败: {}", id_, e.what());
        return false;
    }
}

bool JSPlugin::resolvePlayUrl(const std::string& path, StreamSource& out_source) {
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("resolvePlayUrl")) return false;

    std::string out_json, err;
    json arg_path = path;
    if (!runtime_->callFunction("resolvePlayUrl", { arg_path.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] resolvePlayUrl 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        out_source.url = root.value("url", "");
        out_source.name = root.value("name", "直链");
        out_source.format = root.value("format", "mp4");
        out_source.expires_at = root.value("expiresAt", 0);
        if (root.contains("headers") && root["headers"].is_object()) {
            for (auto it = root["headers"].begin(); it != root["headers"].end(); ++it) {
                if (it.value().is_string()) out_source.headers[it.key()] = it.value().get<std::string>();
            }
        }
        return !out_source.url.empty();
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] resolvePlayUrl 解析失败: {}", id_, e.what());
        return false;
    }
}

bool JSPlugin::onLinkExpired(const std::string& path, StreamSource& out_source) {
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_) return false;
    if (runtime_->hasFunction("onLinkExpired")) {
        std::string out_json, err;
        json arg_path = path;
        if (runtime_->callFunction("onLinkExpired", { arg_path.dump() }, out_json, &err)) {
            try {
                json root = json::parse(out_json);
                out_source.url = root.value("url", "");
                out_source.name = root.value("name", "刷新直链");
                out_source.format = root.value("format", "mp4");
                out_source.expires_at = root.value("expiresAt", 0);
                if (root.contains("headers") && root["headers"].is_object()) {
                    for (auto it = root["headers"].begin(); it != root["headers"].end(); ++it) {
                        if (it.value().is_string()) out_source.headers[it.key()] = it.value().get<std::string>();
                    }
                }
                return !out_source.url.empty();
            } catch (...) {}
        }
    }
    // 降级调用 resolvePlayUrl
    return resolvePlayUrl(path, out_source);
}

// --- ScraperProvider 实现 ---

bool JSPlugin::scrapeSearch(const std::string& query, int64_t year, std::string& out_json) {
    out_json.clear();
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("scrapeSearch")) return false;

    std::string res, err;
    json arg_q = query;
    json arg_y = year;
    if (!runtime_->callFunction("scrapeSearch", { arg_q.dump(), arg_y.dump() }, res, &err)) {
        brls::Logger::error("[JSPlugin:{}] scrapeSearch 执行出错: {}", id_, err);
        return false;
    }
    out_json = res;
    return true;
}

bool JSPlugin::scrapeDetail(const std::string& id, ScrapeDetail& out_detail) {
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("scrapeDetail")) return false;

    std::string out_json, err;
    json arg_id = id;
    if (!runtime_->callFunction("scrapeDetail", { arg_id.dump() }, out_json, &err)) {
        brls::Logger::error("[JSPlugin:{}] scrapeDetail 执行出错: {}", id_, err);
        return false;
    }

    try {
        json root = json::parse(out_json);
        out_detail.title = root.value("title", "");
        out_detail.overview = root.value("overview", "");
        out_detail.poster_url = root.value("poster_url", root.value("poster", ""));
        out_detail.backdrop_url = root.value("backdrop_url", root.value("backdrop", ""));
        out_detail.release_date = root.value("release_date", root.value("releaseDate", ""));
        out_detail.genres = root.value("genres", "");
        out_detail.rating = root.value("rating", -1.0);
        out_detail.runtime_minutes = root.value("runtime_minutes", root.value("runtime", -1));
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("[JSPlugin:{}] scrapeDetail 解析失败: {}", id_, e.what());
        return false;
    }
}

} // namespace muvik
