#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <borealis.hpp>
#include "model/media_item.hpp"
#include <vector>
#include <string>
#include <atomic>
#include <memory>

namespace deck_player {

class FileBrowserActivity : public brls::Activity {
public:
    FileBrowserActivity();
    ~FileBrowserActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

    // 目录导航
    // 切换到指定标签页（0=媒体库 1=文件 2=历史 3=设置）。
    // 供自动化测试使用：设置环境变量后逐个标签抓帧。
    void selectTabForTest(int index);

    void navigateTo(const std::string& path, const std::string& title = "");
    void navigateUp();
    void refresh();

private:
    brls::View* createSourcesView();
    brls::View* createLibraryView();
    brls::View* createRecentView();
    brls::View* createSettingsView();
    void renderSourcesContent();

    // 媒体库相关
    void promptAddFolder();
    void startLibraryScan();
    void showFolderOptions(const std::string& path, const std::string& name);

    // 对单个媒体文件执行刮削，并展示结果供用户确认
    void scrapeItem(const std::string& path, const std::string& filename);

    // 展示某条目的元数据详情
    void showMediaInfo(const std::string& path, const std::string& filename);

    std::string current_path_;
    std::vector<std::string> path_history_;

    brls::AppletFrame* frame_ = nullptr;
    brls::TabFrame* tab_frame_ = nullptr;
    brls::Box* sources_container_ = nullptr;
    std::shared_ptr<brls::Box*> sources_slot_;
    std::vector<MediaItem> current_items_;

    // 媒体库视图的重建入口，扫描完成后用于刷新界面
    brls::ScrollingFrame* library_scroller_ = nullptr;
    brls::Box* library_list_ = nullptr;
    std::shared_ptr<brls::Box*> library_slot_;

    // 后台扫描完成回调的存活标志，销毁时置 false
    // 每个后台任务各有一个存活标志。
    // 早期是单个 shared_ptr，每次启动新任务就覆盖 ——
    // 导致先前任务的标志永远为 true，其回调在 Activity 销毁后仍会执行，
    // 访问已释放对象（ASan 实测 heap-use-after-free）。
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace deck_player
