#pragma once

#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <borealis.hpp>
#include "model/media_item.hpp"
#include "model/media_library.hpp"
#include "view/resume_hero.hpp"
#include <vector>
#include <string>
#include <atomic>
#include <memory>

namespace muvik {

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

    // 供自动化测试：当前获焦视图代表的**条目身份**（媒体路径 / 目录路径）。
    // 空的表示焦点不在任何带身份的卡片上（工具行、返回行等）。
    //
    // 为什么需要它：焦点恢复是本项目最容易「看起来改了、实际没生效」的地方 ——
    // 测试若只断言「有焦点」，按序号恢复和按身份恢复都能通过。
    // 必须能读出「是哪一项」才能区分两者。
    std::string focusedIdentityForTest() const;

    // 供自动化测试：当前层记录的焦点身份（不含 UI 实时状态）。
    std::string rememberedIdentityForTest() const {
        auto it = focus_identity_.find(open_folder_);
        return it == focus_identity_.end() ? std::string() : it->second;
    }

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
    void navigateLibraryUp();

    // 工具行：唯一的视图入口 + 搜索
    brls::Box* makeLibraryToolbar();
    void refreshToolbar();
    void promptSearch();
    void openViewSheet();

    // 交互对话框
    void showExitDialog();
    void showAboutDialog();
    void showLanguageDialog(MediaCell* lang_row);
    void showThemeDialog(MediaCell* theme_row);
    void showFpsLimitDialog(MediaCell* fps_row);
    void showKeymapDialog(MediaCell* keymap_row);
    void showAddNetworkSourceDialog();
    void showDeleteNetworkSourceDialog(const struct NetworkSource& source);

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

    // 历史视图重建入口
    void renderRecentContent();
    brls::Box* recent_list_ = nullptr;
    std::shared_ptr<brls::Box*> recent_slot_;

    // 设置视图重建入口
    void renderSettingsContent();
    brls::Box* settings_list_ = nullptr;
    std::shared_ptr<brls::Box*> settings_slot_;

    // 媒体库整页（工具行 + 列表），createLibraryView 返回的就是它
    brls::Box* library_root_ = nullptr;
    ToolButton* library_view_btn_ = nullptr;
    ToolButton* library_search_btn_ = nullptr;
    // 「视图与排序」浮层：懒创建，开着就挂在 library_root_ 上
    ViewSheet* view_sheet_ = nullptr;
    // 搜索串：非空时强制平铺（搜索结果不分文件夹层级）
    std::string library_query_;

    // 当前进入的文件夹（空 = 文件夹总览）
    std::string open_folder_;

    // 每一层「焦点停在哪一项」，按**路径身份**记录。
    //
    // 键：层标识 —— 总览是空串，文件夹是它的绝对路径。
    // 值：该层最后获得焦点的条目路径（文件路径或子目录路径）。
    //
    // 为什么不能记序号：列表会因为缩略图到达、筛选变化、重扫而重建，
    // 同一序号在重建后可能是另一个条目。实测缺陷（fixture:
    // restore-focus-by-item）：从第三个目录返回总览后焦点回到第 1 项，
    // 用户得重新找一遍刚才看的那一行。
    std::map<std::string, std::string> focus_identity_;

    // 组装首页「继续观看」主视觉的数据。没有可显示的记录时返回 false。
    bool buildResumeHero(struct ResumeHeroSpec& out) const;

    // 首次建库时，哪些探测到的目录**不**导入。
    //
    // 存「被排除的」而不是「被选中的」：默认全选是现有行为，
    // 这样新增一个探测位置时它自动被选中，不会因为以前存过一份
    // 「选中列表」而被漏掉。
    // 用路径而非下标：重建列表会重排，下标对不上。
    std::set<std::string> setup_excluded_;

public:
    // 供自动化测试：首次建库界面里，被用户取消勾选的目录数。
    //
    // 没有它就只能测「界面上有个方块」—— 勾选状态存在哪个字段、
    // 按 A 会不会真的切换，全都没被覆盖。
    size_t setupExcludedCountForTest() const { return setup_excluded_.size(); }

private:

    // 换层后要恢复到的目标身份（由 openFolder 设置，render 消费后清空）。
    //
    // 为什么不能沿用「当前焦点」的身份：换层那一瞬间焦点还停在旧层，
    // 而**同一个文件在两层的身份字符串是相同的** ——
    // 文件夹内的那一行是 `x/ep.mp4`，总览「最近添加」里的同一部片
    // 也是 `x/ep.mp4`。按身份找回时会匹配到后者，
    // 于是「从文件夹返回」落在某个最近添加的行上，而不是原来那张目录卡。
    // 实测就是这个现象。
    std::string pending_focus_identity_;
    // 本次渲染是不是「换层」引发的（与上面配对，标记一次性的换层渲染）。
    bool focus_layer_changed_ = false;
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

} // namespace muvik
