#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <mutex>
#include "model/media_item.hpp"
#include <string>
#include <vector>
#include <memory>

namespace deck_player {

// 刮削详情的 C++ 表示（从 ABI 的 deck_scrape_result 转换而来）
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

// 一个已加载的插件实例
struct LoadedPlugin {
    std::string path;          // 动态库文件路径
    std::string name;
    std::string version;
    std::string author;
    bool has_video_source = false;
    bool has_scraper = false;
    void* dl_handle = nullptr;  // 内部句柄，不对外暴露
};

// 插件管理器：负责扫描插件目录、按 ABI 规则加载、校验版本、卸载。
//
// 关键设计：插件通过纯 C ABI（见 deck_plugin_abi.h）与本程序交互，
// 绝不跨动态库边界传递 C++ 对象，从而避免编译器/STL 版本差异导致的崩溃。
class PluginManager {
public:
    static PluginManager& getInstance();

    // 扫描插件目录并加载全部合法插件。返回成功加载的数量。
    size_t loadAll(const std::string& plugins_dir = "");

    // 加载单个插件文件
    bool loadPlugin(const std::string& file_path);

    // 已加载插件列表
    std::vector<LoadedPlugin> getPlugins() const;

    // 卸载全部插件（程序退出前调用，必须早于静态对象析构）
    void unloadAll();

    // 通过插件提供的视频源列目录。成功返回 true。
    bool listVideoSourceItems(const std::string& plugin_path,
                              const std::string& dir_path,
                              std::vector<MediaItem>& out);

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

private:
    // 插件表被多线程访问：UI 线程读取（getPlugins/渲染），
    // 后台线程可能同时 loadAll/listVideoSourceItems，
    // 而 unloadAll() 会 dlclose —— 若不互斥，
    // 正在遍历的线程会调用已卸载的代码（use-after-dlclose）。
    // 用 recursive_mutex：部分公开方法内部会互相调用。
    mutable std::recursive_mutex plugins_mutex_;
    PluginManager() = default;

    std::vector<LoadedPlugin> plugins_;
};

} // namespace deck_player
