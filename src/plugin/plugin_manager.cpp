#include <mutex>
#include <string>
#include <vector>
#include "plugin/plugin_manager.hpp"
#include "plugin/deck_plugin_abi.h"
#include "utils/app_paths.hpp"
#include "plugin/plugin_config_store.hpp"
#include <borealis/core/logger.hpp>
#include <dlfcn.h>
#if !defined(ANDROID)
// libcurl 只在桌面平台可用。Android 上首版不提供网络源插件，
// 见 host_http_request 的 stub 与 android/README.md 的“已知限制”。
#include <curl/curl.h>
#endif
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace deck_player {

// ---------------------------------------------------------------------------
// 宿主 API 实现
// ---------------------------------------------------------------------------

namespace {

void host_log(int level, const char* message) {
    if (!message) return;
    switch (level) {
        case 1:  brls::Logger::warning("plugin: {}", message); break;
        case 2:  brls::Logger::error("plugin: {}", message);   break;
        default: brls::Logger::info("plugin: {}", message);    break;
    }
}

// 当前正在初始化的插件 id。宿主回调需要它才能定位到该插件的配置。
// 插件是逐个加载并初始化的，因此这里用线程局部的简单状态即可。
static std::string g_current_plugin_id;

// 宿主配置回调：把请求转发到 PluginConfigStore。
// 注意：返回值只在下次调用前有效，与 ABI 约定一致
// （用 thread_local 缓冲避免跨调用生命周期问题）。
const char* host_get_config(const char* key) {
    if (!key) return nullptr;
    static thread_local std::string buffer;
    buffer = PluginConfigStore::getInstance().get(g_current_plugin_id, key);
    return buffer.empty() ? nullptr : buffer.c_str();
}

// ---------------------------------------------------------------------------
// HTTP：插件通过宿主发请求，无需自带网络栈。
// libcurl 采用 MIT/X 风格许可，允许闭源商业分发。
// ---------------------------------------------------------------------------

// libcurl 全局初始化只做一次（非线程安全，故用静态初始化包装）
#if !defined(ANDROID)
static void ensure_curl_global() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct CurlBuffer {
    char* data = nullptr;
    size_t size = 0;
};

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    CurlBuffer* buf = static_cast<CurlBuffer*>(userdata);
    char* grown = static_cast<char*>(realloc(buf->data, buf->size + total + 1));
    if (!grown) return 0; // 内存不足，让 curl 中止
    buf->data = grown;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

#endif  // !defined(ANDROID)：curl 辅助函数仅桌面平台需要

deck_status host_http_request(const char* method, const char* url,
                              const char* headers, const char* body,
                              char** out_body, size_t* out_len) {
    if (out_body) *out_body = nullptr;
    if (out_len) *out_len = 0;
    if (!url || !*url) return DECK_ERR_GENERIC;

#if defined(ANDROID)
    // Android 版本暂不链接 libcurl（NDK 不提供系统 curl，
    // 需要自行交叉编译）。网络源插件与在线刮削在此平台不可用，
    // 但本地媒体浏览/播放不受影响。
    // 返回明确的错误码，让插件 UI 显示「不支持」而不是静默超时。
    (void)method; (void)headers; (void)body;
    brls::Logger::warning("Android：网络请求不可用（未链接 libcurl）: {}", url);
    return DECK_ERR_NOT_AVAILABLE;
#else

    // 安全限制：只允许 http/https，避免插件读取本地文件（file://）等
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        brls::Logger::warning("插件 HTTP 请求被拒绝（仅允许 http/https）: {}", url);
        return DECK_ERR_GENERIC;
    }

    ensure_curl_global();

    CURL* curl = curl_easy_init();
    if (!curl) return DECK_ERR_GENERIC;

    CurlBuffer buf;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DeckPlayer/1.0");
    // 屏蔽进度输出
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

    struct curl_slist* header_list = nullptr;
    if (headers && *headers) {
        // 支持 "K: V\r\nK2: V2" 形式
        std::string hs(headers);
        size_t pos = 0;
        while (pos < hs.size()) {
            size_t nl = hs.find('\n', pos);
            std::string line = hs.substr(pos, (nl == std::string::npos) ? std::string::npos : nl - pos);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) header_list = curl_slist_append(header_list, line.c_str());
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        if (header_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    std::string m = method ? method : "GET";
    if (m == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    } else if (m != "GET") {
        // PUT / DELETE / HEAD 等
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, m.c_str());
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
    }

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        brls::Logger::warning("插件 HTTP 失败 ({}): {}", url, curl_easy_strerror(rc));
        free(buf.data);
        if (rc == CURLE_OPERATION_TIMEDOUT || rc == CURLE_COULDNT_CONNECT ||
            rc == CURLE_COULDNT_RESOLVE_HOST) {
            return DECK_ERR_NETWORK;
        }
        return DECK_ERR_GENERIC;
    }

    if (http_code == 401 || http_code == 403) {
        free(buf.data);
        return DECK_ERR_AUTH;
    }

    if (out_body) *out_body = buf.data;
    if (out_len) *out_len = buf.size;
    return DECK_OK;
#endif  // !defined(ANDROID)
}

void host_free_buffer(char* buffer) {
    free(buffer);
}

// 全局宿主 API 表，生命周期长于所有插件
static deck_host_api g_host_api = {
    &host_log,
    &host_get_config,
    &host_http_request,
    &host_free_buffer,
};

} // namespace

// ---------------------------------------------------------------------------
// PluginManager
// ---------------------------------------------------------------------------

PluginManager& PluginManager::getInstance() {
    static PluginManager instance;
    return instance;
}

bool PluginManager::loadPlugin(const std::string& file_path) {
    if (file_path.empty()) return false;

    void* handle = dlopen(file_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        brls::Logger::warning("插件加载失败 {}: {}", file_path, err ? err : "unknown");
        return false;
    }

    // 只认固定入口符号，避免误加载普通动态库
    auto entry = reinterpret_cast<deck_plugin_entry_fn>(
        dlsym(handle, DECK_PLUGIN_ENTRY_SYMBOL));
    if (!entry) {
        brls::Logger::warning("跳过 {}：未导出 {}（非 DeckPlayer 插件或版本过旧）",
                              file_path, DECK_PLUGIN_ENTRY_SYMBOL);
        dlclose(handle);
        return false;
    }

    const deck_plugin_descriptor* desc = entry();
    if (!desc) {
        brls::Logger::warning("跳过 {}：插件入口返回空", file_path);
        dlclose(handle);
        return false;
    }

    // ABI 版本校验：不匹配直接拒绝，避免内存布局错位引发崩溃
    if (desc->abi_version != DECK_PLAYER_ABI_VERSION) {
        brls::Logger::error(
            "跳过 {}：ABI 版本不匹配（插件 {}，宿主 {}）",
            file_path, desc->abi_version, DECK_PLAYER_ABI_VERSION);
        dlclose(handle);
        return false;
    }

    LoadedPlugin info;
    info.path = file_path;
    info.name = desc->plugin_name ? desc->plugin_name : fs::path(file_path).stem().string();
    info.version = desc->plugin_version ? desc->plugin_version : "0.0.0";
    info.author = desc->plugin_author ? desc->plugin_author : "";
    info.has_video_source = desc->create_video_source != nullptr;
    info.has_scraper = desc->create_scraper != nullptr;
    info.dl_handle = handle;

    // 让插件完成初始化（把宿主 API 与它自己的配置交给它）
    auto init_source = [&desc, &info](const std::string& plugin_id) {
        if (!desc->create_video_source) return;
        deck_video_source* vs = desc->create_video_source();
        if (!vs) return;
        if (vs->init) {
            std::string cfg = PluginConfigStore::getInstance().toJson(plugin_id);
            g_current_plugin_id = plugin_id;
            deck_status st = vs->init(vs->impl, cfg.c_str(), &g_host_api);
            g_current_plugin_id.clear();
            if (st != DECK_OK) {
                brls::Logger::warning("{} 的视频源初始化未完成（返回 {}）",
                                      info.name, (int)st);
            }
        }
        if (vs->destroy) vs->destroy(vs->impl);
    };

    // 插件 id 取自插件的 get_id，但 create 之前拿不到；
    // 先用库文件名作为初始 id，get_id 之后再校准一次。
    std::string plugin_id = fs::path(file_path).stem().string();
    init_source(plugin_id);

    plugins_.push_back(std::move(info));
    brls::Logger::info("已加载插件: {} v{} ({}{})",
                       plugins_.back().name, plugins_.back().version,
                       plugins_.back().has_video_source ? "视频源" : "",
                       plugins_.back().has_scraper ? (plugins_.back().has_video_source ? "+刮削" : "刮削") : "");
    return true;
}

size_t PluginManager::loadAll(const std::string& plugins_dir) {
    // 与 getPlugins/unloadAll/listVideoSourceItems 用同一把锁：
    // loadAll 会 dlopen 并往 plugins_ 追加，与遍历方并发即数据竞争。
    std::lock_guard<std::recursive_mutex> lock(plugins_mutex_);
    std::string dir = plugins_dir.empty() ? AppPaths::pluginsDir() : plugins_dir;
    if (!fs::exists(dir)) return 0;

    // 两处来源都要扫描：
    //   1) plugins/ 下的散装 .so/.dylib（手动放置的插件）
    //   2) plugins/<包名>/ 子目录（ZIP 安装的插件）
    // 早期只扫顶层文件，导致「从 ZIP 安装的插件永远不会被加载」——
    // 安装器把文件解压进子目录，而扫描器跳过了目录。
    size_t count = 0;
    auto tryLoad = [this, &count](const fs::path& p) {
        std::string ext = p.extension().string();
        if (ext != ".so" && ext != ".dylib" && ext != ".dll") return;
        if (loadPlugin(p.string())) count++;
    };

    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                tryLoad(entry.path());
            } else if (entry.is_directory()) {
                // 子目录：扫描其中的动态库
                std::error_code ec;
                for (const auto& sub : fs::directory_iterator(entry.path(), ec)) {
                    if (ec) break;
                    if (sub.is_regular_file()) tryLoad(sub.path());
                }
            }
        }
    } catch (const std::exception& e) {
        brls::Logger::error("扫描插件目录出错: {}", e.what());
    }
    return count;
}

std::vector<LoadedPlugin> PluginManager::getPlugins() const {
    std::lock_guard<std::recursive_mutex> lock(plugins_mutex_);
    return plugins_;   // 返回副本，调用方不会持有内部引用
}

void PluginManager::unloadAll() {
    std::lock_guard<std::recursive_mutex> lock(plugins_mutex_);
    for (auto& p : plugins_) {
        if (p.dl_handle) {
            dlclose(p.dl_handle);
            p.dl_handle = nullptr;
        }
    }
    plugins_.clear();
}

bool PluginManager::listVideoSourceItems(const std::string& plugin_path,
                                        const std::string& dir_path,
                                        std::vector<MediaItem>& out) {
    out.clear();

    // 找到对应插件并重新取描述符
    std::lock_guard<std::recursive_mutex> lock(plugins_mutex_);
    for (const auto& p : plugins_) {
        if (p.path != plugin_path || !p.has_video_source || !p.dl_handle) continue;

        auto entry = reinterpret_cast<deck_plugin_entry_fn>(
            dlsym(p.dl_handle, DECK_PLUGIN_ENTRY_SYMBOL));
        if (!entry) return false;
        const deck_plugin_descriptor* desc = entry();
        if (!desc || !desc->create_video_source) return false;

        deck_video_source* vs = desc->create_video_source();
        if (!vs) return false;

        // 必须对每个新建实例调用 init，把宿主 API 与该插件自己的配置交给它。
        // 漏掉这一步时插件拿不到 base_url，list_items 会直接返回
        // DECK_ERR_NOT_AVAILABLE，表现为「插件浏览页永远读不到任何内容」。
        if (vs->init) {
            const std::string plugin_id = std::filesystem::path(p.path).stem().string();
            const std::string cfg = PluginConfigStore::getInstance().toJson(plugin_id);
            g_current_plugin_id = plugin_id;
            deck_status st = vs->init(vs->impl, cfg.c_str(), &g_host_api);
            g_current_plugin_id.clear();
            if (st != DECK_OK) {
                brls::Logger::warning("插件 {} 初始化失败（返回 {}），无法列出目录",
                                      p.name, (int)st);
                if (vs->destroy) vs->destroy(vs->impl);
                return false;
            }
        }

        bool ok = false;
        if (vs->list_items) {
            deck_media_list* list = nullptr;
            deck_status lst = vs->list_items(vs->impl, dir_path.c_str(), &list);
            if (lst == DECK_OK && list) {
                for (size_t i = 0; i < list->count; ++i) {
                    const deck_media_item& it = list->items[i];
                    MediaItem mi;
                    mi.name = it.name ? it.name : "";
                    mi.path = it.path ? it.path : "";
                    mi.size = it.size;
                    mi.duration = it.duration;
                    switch (it.type) {
                        case DECK_MEDIA_DIRECTORY: mi.type = MediaType::Directory; break;
                        case DECK_MEDIA_VIDEO:     mi.type = MediaType::Video; break;
                        case DECK_MEDIA_AUDIO:     mi.type = MediaType::Audio; break;
                        default:                   mi.type = MediaType::Unknown; break;
                    }
                    out.push_back(std::move(mi));
                }
                ok = true;
            }
            // 释放：优先用插件自己的 free_list，否则用宿主的释放约定
            if (vs->free_list) {
                vs->free_list(list);
            } else {
                free(list);
            }
        }
        if (vs->destroy) vs->destroy(vs->impl);
        return ok;
    }
    return false;
}

bool PluginManager::scrapeSearch(const std::string& plugin_path,
                                 const std::string& query, int64_t year,
                                 std::string& out_json) {
    out_json.clear();
    for (const auto& p : plugins_) {
        if (p.path != plugin_path || !p.has_scraper || !p.dl_handle) continue;

        auto entry = reinterpret_cast<deck_plugin_entry_fn>(
            dlsym(p.dl_handle, DECK_PLUGIN_ENTRY_SYMBOL));
        if (!entry) return false;
        const deck_plugin_descriptor* desc = entry();
        if (!desc || !desc->create_scraper) return false;

        deck_scraper* sc = desc->create_scraper();
        if (!sc) return false;

        bool ok = false;
        if (sc->search) {
            char* json = nullptr;
            if (sc->search(sc->impl, query.c_str(), year, &json) == DECK_OK && json) {
                out_json = json;
                ok = true;
                free(json);
            }
        }
        if (sc->destroy) sc->destroy(sc->impl);
        return ok;
    }
    return false;
}

bool PluginManager::scrapeFetchDetail(const std::string& plugin_path,
                                      const std::string& candidate_id,
                                      ScrapeDetail& out) {
    for (const auto& p : plugins_) {
        if (p.path != plugin_path || !p.has_scraper || !p.dl_handle) continue;

        auto entry = reinterpret_cast<deck_plugin_entry_fn>(
            dlsym(p.dl_handle, DECK_PLUGIN_ENTRY_SYMBOL));
        if (!entry) return false;
        const deck_plugin_descriptor* desc = entry();
        if (!desc || !desc->create_scraper) return false;

        deck_scraper* sc = desc->create_scraper();
        if (!sc) return false;

        bool ok = false;
        if (sc->fetch_detail) {
            deck_scrape_result* r = nullptr;
            if (sc->fetch_detail(sc->impl, candidate_id.c_str(), &r) == DECK_OK && r) {
                // 复制到 C++ 对象，随后立即释放插件侧内存
                out.title = r->title ? r->title : "";
                out.overview = r->overview ? r->overview : "";
                out.poster_url = r->poster_url ? r->poster_url : "";
                out.backdrop_url = r->backdrop_url ? r->backdrop_url : "";
                out.release_date = r->release_date ? r->release_date : "";
                out.genres = r->genres ? r->genres : "";
                out.rating = r->rating;
                out.runtime_minutes = r->runtime_minutes;
                ok = true;
                if (sc->free_result) sc->free_result(r);
                else free(r);
            }
        }
        if (sc->destroy) sc->destroy(sc->impl);
        return ok;
    }
    return false;
}

bool PluginManager::scrapeForFile(const std::string& plugin_path,
                                  const std::string& query,
                                  int64_t year,
                                  ScrapeDetail& out) {
    std::string json;
    if (!scrapeSearch(plugin_path, query, year, json)) return false;

    // search 返回的是 JSON 数组。这里不引入 JSON 库，只提取第一个 "id" 字段，
    // 足以驱动「取最佳候选」的流程；后续若需要多候选选择可再扩展。
    std::string cid;
    size_t id_pos = json.find("\"id\"");
    if (id_pos != std::string::npos) {
        size_t colon = json.find(':', id_pos);
        if (colon != std::string::npos) {
            size_t q1 = json.find('"', colon + 1);
            if (q1 != std::string::npos) {
                size_t q2 = json.find('"', q1 + 1);
                if (q2 != std::string::npos) {
                    cid = json.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
    }
    if (cid.empty()) return false;

    return scrapeFetchDetail(plugin_path, cid, out);
}

std::vector<std::string> PluginManager::getScraperPluginPaths() const {
    std::vector<std::string> out;
    for (const auto& p : plugins_) {
        if (p.has_scraper) out.push_back(p.path);
    }
    return out;
}

} // namespace deck_player
