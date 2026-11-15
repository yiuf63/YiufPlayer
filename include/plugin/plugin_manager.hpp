#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "model/media_item.hpp"

namespace muvik {

class JSPlugin;
struct PlayLinksResult;
struct StreamSource;
struct MediaDetail;

// 刮削详情的 C++ 表示（从 ABI 的 muvik_scrape_result 转换而来）
struct ScrapeDetail {
    std::string title;
    std::string overview;
    std::string poster_url;
    std::string backdrop_url;
    std::string release_date;
    std::string genres;
    double rating = -1.0;
    int32_t runtime_minutes = -1;
};

// 插件来源类型
enum class PluginSourceType {
    Local,      // 本地手动安装或 ZIP 解压（~/.config/muvik/plugins/）
    Workshop,   // Steam 创意工坊订阅（steamapps/workshop/content/<appid>/<itemid>/）
    DLC,        // Steam DLC 扩展内容（<appdir>/dlc/ 或 steamapps/common/.../dlc/）
};

// 一个已加载的插件实例
struct LoadedPlugin {
    std::string path;          // 动态库文件路径或 JS 插件目录
    std::string id;            // 唯一标识（如 "com.example.source"）
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    bool is_js_plugin = false; // 是否为现代 JS 插件
    bool has_video_source = false;
    bool has_scraper = false;
    bool has_storage = false;
    void* dl_handle = nullptr;  // 内部 C 句柄，不对外暴露

    PluginSourceType source_type = PluginSourceType::Local;
    std::string workshop_item_id; // 若来自创意工坊，为其订阅数字 ID
    std::string source_badge;     // 显示徽标，如 "[本地]", "[创意工坊]", "[DLC]"
};

// 插件管理器：负责扫描插件目录、按 ABI 规则加载、校验版本、卸载。
//
// 关键设计：插件通过纯 C ABI（见 muvik_plugin_abi.h）与本程序交互，
// 绝不跨动态库边界传递 C++ 对象，从而避免编译器/STL 版本差异导致的崩溃。
class PluginManager {
public:
    static PluginManager& getInstance();

    // 扫描插件目录并加载全部合法插件。返回成功加载的数量。
    // plugins_dir 为空时自动扫描本地插件、Steam 创意工坊订阅与 Steam DLC。
    size_t loadAll(const std::string& plugins_dir = "");

    // 加载单个插件文件（支持指定来源类型与创意工坊 ID）
    bool loadPlugin(const std::string& file_path,
                    PluginSourceType source = PluginSourceType::Local,
                    const std::string& workshop_id = "");

    // 获取检测到的 Steam 创意工坊搜索路径列表
    std::vector<std::string> getWorkshopSearchPaths() const;

    // 获取检测到的 Steam DLC 搜索路径列表
    std::vector<std::string> getDlcSearchPaths() const;

    // 获取 Steam App ID（优先读环境变量 STEAM_APP_ID，默认 "3290940"）
    std::string getSteamAppId() const;

    // 已加载插件列表
    std::vector<LoadedPlugin> getPlugins() const;

    // 卸载全部插件（程序退出前调用，必须早于静态对象析构）
    void unloadAll();

    // 通过插件提供的视频源列目录。成功返回 true。
    bool listVideoSourceItems(const std::string& plugin_path,
                              const std::string& dir_path,
                              std::vector<MediaItem>& out);

    // 检查插件视频源是否支持搜索
    bool canSearch(const std::string& plugin_path_or_id) const;

    // 通过插件视频源搜索媒体内容（支持流媒体番剧源与网盘文件检索）
    bool searchVideoSource(const std::string& plugin_path_or_id,
                           const std::string& keyword,
                           int page,
                           std::vector<MediaItem>& out_items,
                           bool& out_has_more,
                           const std::string& scope_path = "");

    // 通过插件提供的刮削器搜索
    bool scrapeSearch(const std::string& plugin_path,
                      const std::string& query, int64_t year,
                      std::string& out_json);

    // 拉取刮削详情。成功时填充 out（内部字符串已转换为 C++ 对象，插件侧内存已释放）
    bool scrapeFetchDetail(const std::string& plugin_path,
                           const std::string& candidate_id,
                           ScrapeDetail& out);

    // 便捷入口：对某个媒体文件执行「搜索 + 取第一条 + 拉详情」的完整流程。
    // 返回是否成功，成功后 out 填好元数据（path 字段需调用方填入）。
    bool scrapeForFile(const std::string& plugin_path,
                       const std::string& query,
                       int64_t year,
                       ScrapeDetail& out);

    // 获取所有刮削器插件的路径
    std::vector<std::string> getScraperPluginPaths() const;

    // 获取指定插件的 JS 实例（若该插件为 JS 驱动）
    std::shared_ptr<JSPlugin> getJSPlugin(const std::string& path_or_id) const;

    // 解析流媒体真实播放链接（由 JS 插件的 loadLinks 提供）
    bool loadStreamLinks(const std::string& plugin_path_or_id,
                         const std::string& episode_id,
                         PlayLinksResult& out_links);

    // 解析网盘/存储插件中的播放直链（由 JS 插件的 resolvePlayUrl 提供）
    bool resolvePlayUrl(const std::string& plugin_path_or_id,
                        const std::string& path,
                        StreamSource& out_source);

    // 解析番剧/影视详情（由 JS 插件的 loadDetail 提供）
    bool loadDetail(const std::string& plugin_path_or_id,
                    const std::string& id,
                    MediaDetail& out_detail);

private:
    // 插件表被多线程访问：UI 线程读取（getPlugins/渲染），
    // 后台线程可能同时 loadAll/listVideoSourceItems，
    // 而 unloadAll() 会 dlclose —— 若不互斥，
    // 正在遍历的线程会调用已卸载的代码（use-after-dlclose）。
    // 用 recursive_mutex：部分公开方法内部会互相调用。
    mutable std::recursive_mutex plugins_mutex_;
    PluginManager() = default;

    std::vector<LoadedPlugin> plugins_;
    std::map<std::string, std::shared_ptr<JSPlugin>> js_plugins_;
};

} // namespace muvik
