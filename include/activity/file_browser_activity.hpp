#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <borealis.hpp>
#include "model/media_item.hpp"
#include "model/media_library.hpp"
#include <vector>
#include <string>
#include <atomic>
#include <memory>

namespace deck_player {

class MediaCell;
class ToolButton;
class ViewSheet;

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

    // 供自动化测试：媒体库是否停在某个文件夹里（false = 文件夹总览）
    bool libraryInFolderForTest() const { return !open_folder_.empty(); }

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

    // 媒体库渲染：总览（文件夹卡片 + 最近添加）⇄ 单个文件夹 ⇄ 全部平铺
    void renderLibraryContent();
    void renderLibraryOverview(const std::vector<MediaLibrary::Group>& groups);
    void renderLibraryFolder(const std::vector<MediaLibrary::Group>& groups);
    void renderLibraryFlat(const std::vector<MediaLibrary::Group>& groups);
    void openFolder(const std::string& path);

    // 工具行：唯一的视图入口 + 搜索
    brls::Box* makeLibraryToolbar();
    void refreshToolbar();
    void promptSearch();
    void openViewSheet();

    // 条目卡片。三处共用同一套外观，差别只在传入的播放列表。
    MediaCell* makeVideoCell(const MediaItem& item, std::vector<MediaItem> playlist,
                             size_t idx);
    MediaCell* makeAudioCell(const MediaItem& item, std::vector<MediaItem> playlist,
                             size_t idx);
    // 平铺网格的卡片：同一份数据，换成「墙」的形态
    brls::View* makeVideoCard(const MediaItem& item, std::vector<MediaItem> playlist,
                              size_t idx);
    brls::View* makeAudioCard(const MediaItem& item, std::vector<MediaItem> playlist,
                              size_t idx);

    // 后台抽帧：列表里出现的视频排进队列，完成后刷新一次界面
    void requestThumbnails(const std::vector<MediaItem>& items);

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

    // 媒体库整页（工具行 + 列表），createLibraryView 返回的就是它
    brls::Box* library_root_ = nullptr;
    brls::Box* library_toolbar_ = nullptr;
    brls::Label* library_summary_ = nullptr;
    ToolButton* library_view_btn_ = nullptr;
    ToolButton* library_search_btn_ = nullptr;
    // 「视图与排序」浮层：懒创建，开着就挂在 library_root_ 上
    ViewSheet* view_sheet_ = nullptr;
    // 搜索串：非空时强制平铺（搜索结果不分文件夹层级）
    std::string library_query_;

    // 当前进入的文件夹（空 = 文件夹总览）
    std::string open_folder_;
    // 本次运行中已经为哪些「空文件夹」触发过扫描。
    // 没有它，一个真实的空目录会让人每次进媒体库都全量重扫一遍。
    std::vector<std::string> empty_checked_;

    // 后台扫描完成回调的存活标志，销毁时置 false
    // 每个后台任务各有一个存活标志。
    // 早期是单个 shared_ptr，每次启动新任务就覆盖 ——
    // 导致先前任务的标志永远为 true，其回调在 Activity 销毁后仍会执行，
    // 访问已释放对象（ASan 实测 heap-use-after-free）。
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace deck_player
