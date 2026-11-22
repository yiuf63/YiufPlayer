#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "plugin/js_plugin.hpp"
#include "plugin/plugin_config_store.hpp"
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
    //
    // 这里把文档里的能力名翻译成内部名。为什么必须做这层映射：
    // docs/PLUGIN-DEV.md 与 examples/ 里的示例插件都写 "video_source"，
    // 但 host 侧判断视频源用的是 hasMediaStream()/hasStorage()，
    // 于是「完全照文档写」的插件反而不出现在「文件 → 插件视频源」里，
    // 而「不写 capabilities」的插件靠自动探测却能出现。
    // 实测：同一份 main.js，写 ["video_source"] 时主界面看不到该插件，
    // 删掉 capabilities 后正常显示 —— 文档与实现直接冲突。
    //
    // 现在两种写法等价：
    //   "video_source" → media_stream（能出列表/搜索）+ storage（能进目录）
    //   "storage"      → 保留，兼容旧写法
    //   "media_stream" → 保留
    //   "scraper"      → 保留
    if (meta.contains("capabilities") && meta["capabilities"].is_array()) {
        for (const auto& cap : meta["capabilities"]) {
            if (!cap.is_string()) continue;
            const std::string name = cap.get<std::string>();
            if (name == "video_source") {
                // 视频源插件按定义就同时要「列目录」与「出条目」两种能力
                plugin->capabilities_.push_back("media_stream");
                plugin->capabilities_.push_back("storage");
            } else {
                plugin->capabilities_.push_back(name);
            }
        }
    }

    // 解析设置项。
    //
    // 同时接受 "settings" 与 "configSchema" 两种键名：
    // 文档和 examples/tmdb_scraper_plugin 用的是 configSchema（字段名 title/
    // description），而早期代码只认 settings（字段名 label）。结果是照文档装的
    // 插件一条配置项都不显示，插件管理页只有「该插件无需额外配置。」，
    // API Key 永远填不进去。官方示例自己就是错的示范。
    //
    // 两种写法都支持后，谁也不用改自己的既有插件。
    const nlohmann::json* settings_arr = nullptr;
    if (meta.contains("settings") && meta["settings"].is_array()) {
        settings_arr = &meta["settings"];
    } else if (meta.contains("configSchema") && meta["configSchema"].is_array()) {
        settings_arr = &meta["configSchema"];
    }
    if (settings_arr) {
        bool config_changed = false;
        for (const auto& s : *settings_arr) {
            if (s.is_object()) {
                PluginSettingField f;
                f.key = s.value("key", "");
                // title（文档写法）与 label（旧写法）都认，最后回退到 key
                if (s.contains("label") && s["label"].is_string()) {
                    f.label = s.value("label", f.key);
                } else if (s.contains("title") && s["title"].is_string()) {
                    f.label = s.value("title", f.key);
                } else {
                    f.label = f.key;
                }
                f.type = s.value("type", "string");
                f.default_value = s.contains("default") ? s.value("default", "") : s.value("default_value", "");
                if (!f.key.empty()) {
                    plugin->settings_.push_back(f);
                    // 如果持久化配置中尚未有此项且该字段具有默认值，写入默认配置
                    if (!f.default_value.empty() && PluginConfigStore::getInstance().get(id, f.key).empty()) {
                        PluginConfigStore::getInstance().set(id, f.key, f.default_value);
                        config_changed = true;
                    }
                }
            }
        }
        if (config_changed) {
            PluginConfigStore::getInstance().save();
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
    if (runtime->hasFunction("search") && !plugin->hasCapability("search")) {
        plugin->capabilities_.push_back("search");
    }

    // 如果插件定义了 init(config)，自动传入当前持久化配置。
    //
    // 启动路径上的 init 必须短超时：它是在 UI 线程同步跑的，
    // 一个写错（或恶意）的插件在这里死循环就会让播放器卡在启动画面。
    // 默认 30 秒对「启动」来说仍然太长，这里收紧到 5 秒 ——
    // 正常的 init 只是解析配置、拼几个 URL，连网络请求都不该发。
    if (runtime->hasFunction("init")) {
        runtime->setExecutionTimeout(5);
        std::string cfg_json = PluginConfigStore::getInstance().toJson(id);
        std::string init_res, init_err;
        if (!runtime->callFunction("init", { cfg_json }, init_res, &init_err)) {
            brls::Logger::warning("插件 {} 的 init() 执行失败: {}", id, init_err);
        }
        // 之后的常规调用（搜索 / 解析直链）可能要走网络，恢复默认超时。
        runtime->setExecutionTimeout(30);
    }

    plugin->runtime_ = std::move(runtime);
    brls::Logger::info("已加载插件: {} v{} [{}] (JS)", name, version, id);
    return plugin;
}

bool JSPlugin::hasSearch() const {
    if (hasCapability("search")) return true;
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    return runtime_ && runtime_->hasFunction("search");
}

void JSPlugin::reloadConfig() {
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (runtime_ && runtime_->hasFunction("init")) {
        std::string cfg_json = PluginConfigStore::getInstance().toJson(id_);
        std::string init_res, init_err;
        runtime_->callFunction("init", { cfg_json }, init_res, &init_err);
    }
}

// --- MediaStreamProvider / 搜索实现 ---

bool JSPlugin::search(const std::string& keyword, int page, std::vector<MediaItem>& out_items, bool& out_has_more, const std::string& scope_path) {
    out_items.clear();
    out_has_more = false;
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (!runtime_ || !runtime_->hasFunction("search")) return false;

    std::string out_json, err;
    json arg_kw = keyword;
    json arg_pg = page;
    json arg_sc = scope_path;
    if (!runtime_->callFunction("search", { arg_kw.dump(), arg_pg.dump(), arg_sc.dump() }, out_json, &err)) {
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
            if (item.contains("path") && item["path"].is_string()) {
                mi.path = item["path"].get<std::string>();
            } else if (!item_id.empty()) {
                mi.path = item_id;
            } else {
                mi.path = "plugin://" + id_ + "/stream/" + item_id;
            }

            std::string type_str = item.value("type", "video");
            if (type_str == "directory" || type_str == "folder" || item.value("isDirectory", false)) {
                mi.type = MediaType::Directory;
            } else if (type_str == "audio") {
                mi.type = MediaType::Audio;
            } else {
                mi.type = MediaType::Video;
            }

            mi.duration = item.value("duration", 0);
            mi.size = item.value("size", 0);
            if (item.contains("thumbnail") && item["thumbnail"].is_string()) {
                mi.thumbnail_url = item["thumbnail"].get<std::string>();
            } else if (item.contains("cover") && item["cover"].is_string()) {
                mi.thumbnail_url = item["cover"].get<std::string>();
            }
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
    // 加锁后转调无锁实现：保证「谁持有锁」只有一个地方决定，
    // 也避免 onLinkExpired 的降级路径重入死锁（见那里的说明）。
    return resolvePlayUrlLocked(path, out_source);
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
    // 降级调用 resolvePlayUrl。
    //
    // **必须走无锁内部版**：本函数已持有 runtime_mutex_，而 runtime_mutex_
    // 是普通的 std::mutex（非递归）。直接调公开的 resolvePlayUrl()
    // 会让同一线程对同一把锁第二次加锁 → 自死锁，且无法从 UI 恢复。
    // 这正是 AGENTS.md 陷阱 21（ThumbnailCache）的同一类写法，换了个文件而已。
    // 所有「持锁状态下调用可能再加锁的函数」都必须走 *Locked 内部版。
    return resolvePlayUrlLocked(path, out_source);
}

// 无锁内部实现：调用方必须已持有 runtime_mutex_。
bool JSPlugin::resolvePlayUrlLocked(const std::string& path, StreamSource& out_source) {
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
