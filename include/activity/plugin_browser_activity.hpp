#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <borealis.hpp>
#include <string>
#include <vector>
#include "model/media_item.hpp"

namespace muvik {

// 插件视频源的目录浏览界面。
//
// 解决的问题：之前点击插件只能列出根目录的直接视频并立即播放，
// 无法进入子目录，也没有「返回上一级」。WebDAV / 媒体服务器这类
// 层级结构必须能逐层浏览。
class PluginBrowserActivity : public brls::Activity {
public:
    PluginBrowserActivity(std::string plugin_path, std::string plugin_name);
    ~PluginBrowserActivity() override;

    // 指定初始浏览路径（用于测试或深链）
    void setInitialPath(const std::string& p) { initial_path_ = p; }

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void render();
    void navigateTo(const std::string& path);
    void navigateUp();
    std::string currentTitle() const;

    void promptSearch();
    void performSearch(const std::string& query);
    void clearSearch();
    void renderSearchResults(const std::string& query, bool ok, const std::vector<MediaItem>& results);

    std::string plugin_path_;
    std::string plugin_name_;
    std::string current_path_;
    std::string initial_path_;
    std::vector<std::string> history_;

    bool is_searching_ = false;
    std::string search_query_;

    brls::AppletFrame* frame_ = nullptr;
    brls::ScrollingFrame* scroller_ = nullptr;
    brls::Box* list_ = nullptr;

    // 后台加载目录时的存活标志
    // 每个后台任务各有一个存活标志（同 FileBrowserActivity 的教训：
    // 单标志会被后续任务覆盖，导致先前的回调在 Activity 销毁后仍执行）。
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
    bool loading_ = false;
};

} // namespace muvik
