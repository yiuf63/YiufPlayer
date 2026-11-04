#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "activity/file_browser_activity.hpp"
#include "utils/scanner.hpp"
#include "utils/media_name_parser.hpp"
#include "utils/playback_history.hpp"
#include "model/storage_provider.hpp"
#include "model/media_library.hpp"
#include "utils/app_paths.hpp"
#include "activity/file_picker_activity.hpp"
#include "playback/audio_player.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/plugin_config_store.hpp"
#include "activity/plugin_manager_activity.hpp"
#include "activity/plugin_browser_activity.hpp"
#include "model/metadata_store.hpp"
#include "view/now_playing_bar.hpp"
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include "activity/player_activity.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace deck_player {

// 自检用：模拟 processInput 保存的触摸起点视图
brls::View* g_touch_held_view = nullptr;

// 内容区右侧安全边距。
//
// 列表宽 = ScrollingFrame 宽（setContentView 里强制 setWidth），而内容区
// 本身是贴到屏幕右缘的：detailText 右对齐后最后一个字符正好落在像素列
// 1279 上，看起来像被裁切，掌机上还会被机身圆角吃掉。电视端 UI 一律留
// overscan 安全区，这里统一给 28px。
constexpr float CONTENT_SAFE_MARGIN = 28.0f;


// RAII 容器：TabFrame 切换标签时会 delete 旧 Tab 视图，
// 需要把 Activity 里保存的裸指针置空，避免后续访问野指针。
//
// 陷阱：早期实现是 DynamicBox([this]{ member_ = nullptr; }) ——
// lambda 捕获了 Activity 的 this。当 Activity 先于 DynamicBox 销毁时，
// 析构里执行这个 lambda 就是 use-after-free。
// ASan 实测：手势自检退出时 `FileBrowserActivity::createSourcesView()::$_0`
// 在 `DynamicBox::~DynamicBox()` 中写已释放对象，报 heap-use-after-free。
//
// 修正：回调只捕获「指向指针槽位的 shared_ptr」，不捕获 Activity。
// 槽位生命周期独立于 Activity，Activity 销毁后即使回调再执行也只是
// 给一个仍存活的槽位赋 nullptr，安全。
class DynamicBox : public brls::Box {
public:
    using Slot = std::shared_ptr<brls::Box*>;
    DynamicBox(Slot slot) : slot_(std::move(slot)) {}
    ~DynamicBox() override {
        if (slot_) *slot_ = nullptr;
    }
    static Slot makeSlot() { return std::make_shared<brls::Box*>(nullptr); }
private:
    Slot slot_;
};

FileBrowserActivity::FileBrowserActivity() = default;
FileBrowserActivity::~FileBrowserActivity() {
    // 把所有在跑的后台任务标记为失效
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* FileBrowserActivity::createContentView() {
    tab_frame_ = new brls::TabFrame();

    // 采用工厂 Lambda，每个 Tab 被切换激活时按需分配新 View，被切换离开时由 Borealis 自动安全释放
    tab_frame_->addTab("存储位置", [this]() -> brls::View* { return createSourcesView(); });
    tab_frame_->addTab("视频库", [this]() -> brls::View* { return createLibraryView(); });
    tab_frame_->addTab("音乐库", [this]() -> brls::View* { return createAudioView(); });
    tab_frame_->addTab("播放历史", [this]() -> brls::View* { return createRecentView(); });

    frame_ = new brls::AppletFrame(tab_frame_);
    frame_->setTitle("DeckPlayer");
    frame_->setIcon(BRLS_ASSET("icon/icon.png"));

    // 安装「正在播放」条，并在状态变化时刷新
    NowPlayingBar::install(frame_);
    // 回调捕获存活标志：Activity 关闭后 AudioPlayer 仍会周期性触发，
    // 此时 bar 指针与 this 都可能失效。
    auto bar_alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(bar_alive);
    AudioPlayer::getInstance().setStateCallback([bar_alive](const AudioPlayer::State& st) {
        if (!bar_alive->load()) return;
        if (auto* bar = NowPlayingBar::get()) bar->refresh(st);
    });

    // 自检：自动对第一个视频触发刮削，验证「界面 → 插件 → 元数据 → 落盘」链路
    if (const char* target = std::getenv("DECKPLAYER_AUTO_SCRAPE")) {
        std::string path = target;
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        brls::sync([this, alive, path]() {
            if (!alive->load()) return;   // 必须最先，之后才能碰 this
            auto videos = MediaLibrary::getInstance().getVideos();
            for (const auto& v : videos) {
                if (v.path == path) {
                    brls::Logger::info("自动刮削: {}", v.name);
                    this->scrapeItem(v.path, v.name);
                    return;
                }
            }
            brls::Logger::warning("自动刮削: 库中未找到 {}", path);
        });
    }

    // 自检：模拟「在视频库里点击第一个视频」这一完整用户操作。
    // 第 40 轮用户实测报「点开视频立即闪退」，崩在
    // TapGestureRecognizer -> Application::giveFocus 的野指针访问。
    if (std::getenv("DECKPLAYER_CLICK_FIRST_VIDEO")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::getenv("DECKPLAYER_CLICK_DELAY")
                    ? std::atoi(std::getenv("DECKPLAYER_CLICK_DELAY")) : 120));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                auto videos = MediaLibrary::getInstance().getVideos();
                if (videos.empty()) {
                    brls::Logger::warning("click_first_video: 库里没有视频");
                    return;
                }
                brls::Logger::info("自检：注入触摸点击视频库第一项");
                // 关键：走真实的手势路径，而不是直接 pushActivity。
                // 用户报的闪退就发生在 TapGestureRecognizer -> giveFocus。
                brls::View* target = frame_;
                if (!target) { brls::Logger::warning("frame_ 为空"); return; }
                for (auto phase : {brls::TouchPhase::START, brls::TouchPhase::END}) {
                    brls::TouchState ts;
                    ts.fingerId = 0;
                    ts.phase = phase;
                    ts.position = brls::Point(700, 200);
                    ts.view = target;
                    brls::MouseState ms;
                    target->gestureRecognizerRequest(ts, ms, target);
                }
            });
        }).detach();
    }

    // 自检：精确复现「按下与抬起之间列表被重建」导致的野指针崩溃。
    //
    // 真实路径：processInput 在 TouchPhase::START 时 hitTest 得到 View 并
    // 存进 TouchState::view；抬起时不再重新 hitTest，直接复用该指针。
    // 若这两帧之间列表被 clearViews()（例如扫描完成触发 refresh），
    // 该 View 会进 deletionPool 并在下一帧被 delete，
    // 抬起时就是对着已释放内存调用 gestureRecognizerRequest。
    if (std::getenv("DECKPLAYER_TOUCH_REBUILD")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                // 目标必须是会被 clearViews 销毁的容器里的子控件。
                // 若容器为空则放弃，不要退回 frame_（它可能已失效，
                // 对其做 dynamic_cast 会直接崩在 __dynamic_cast 里）。
                brls::Box* container = sources_container_;
                if (!container) return;
                brls::View* target = container;
                brls::Logger::info("复现：按下（列表子控件）");
                brls::TouchState ts;
                ts.fingerId = 0;
                ts.phase = brls::TouchPhase::START;
                ts.position = brls::Point(700, 200);
                ts.view = target;
                brls::MouseState ms;
                target->gestureRecognizerRequest(ts, ms, target);
                // 自己保存这个指针，模拟 processInput 的 TouchState::view
                extern brls::View* g_touch_held_view;
                // 关键：抓一个真正的列表子控件（会被 clearViews 销毁），
                // 而不是 frame_ 这种长时间存活的根节点。
                brls::View* child = nullptr;
                auto kids = container->getChildren();
                if (!kids.empty()) child = kids.front();
                g_touch_held_view = child ? child : target;

            });
            // 按住期间重建列表（模拟扫描完成）
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                brls::Logger::info("复现：按住期间重建列表");
                refresh();
            });
            // 再等一帧让 deletionPool 真的 delete
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            brls::sync([]() {
                brls::Logger::info("复现：抬起（原 View 应已释放）");
                // 复用按下时保存的指针（模拟真实路径不再 hitTest）
                // 这里只能通过局部静态拿到它
                extern brls::View* g_touch_held_view;
                if (!g_touch_held_view) return;
                brls::TouchState ts;
                ts.fingerId = 0;
                ts.phase = brls::TouchPhase::END;
                ts.position = brls::Point(700, 200);
                ts.view = g_touch_held_view;
                // 真实路径由 processInput 守卫处理；这里复现时同样先检查
                if (brls::Application::isViewDead(g_touch_held_view))
                    return;
                g_touch_held_view->gestureRecognizerRequest(ts, brls::MouseState(), g_touch_held_view);
            });
        }).detach();
    }

    // 允许通过环境变量指定启动时打开的标签，便于自动化验证各页面渲染
    if (const char* env = std::getenv("DECKPLAYER_START_TAB")) {
        int idx = atoi(env);
        brls::Logger::info("启动标签: {}", idx);
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        brls::sync([this, alive, idx]() {
            if (!alive->load()) return;
            this->selectTabForTest(idx);
        });
    }

    // 自检：启动后自动打开某个对话框，验证它真的能渲染出来。
    // 这类「点了有没有反应」的问题单元测试发现不了：
    // 第 35 轮就出现过对话框 open() 成功但被视频整屏盖掉的情况。
    if (const char* which = std::getenv("DECKPLAYER_OPEN_DIALOG")) {
        const std::string kind = which;
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive, kind]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            brls::sync([this, alive, kind]() {
                if (!alive->load()) return;
                if (kind == "folder_options") {
                    this->showFolderOptions("/tmp/demofolder", "演示文件夹");
                } else if (kind == "media_info") {
                    auto videos = MediaLibrary::getInstance().getVideos();
                    if (!videos.empty()) {
                        this->showMediaInfo(videos.front().path, videos.front().name);
                    } else {
                        brls::Logger::warning("media_info: 库里没有视频");
                    }
                } else {
                    brls::Logger::warning("未知的 DECKPLAYER_OPEN_DIALOG: {}", kind);
                }
            });
        }).detach();
    }


    return frame_;
}

void FileBrowserActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 绑定返回键 B
    this->registerAction("返回上一级", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        (void)view;
        if (!path_history_.empty() || !current_path_.empty()) {
            navigateUp();
            return true;
        }
        return false;
    });

    // 自动扫描外部 MOD 插件
    // 加载新 ABI 插件（视频源 / 刮削源）。
    // 旧版 C++ ABI 插件已不再加载：跨库传递 std::string 等类型在
    // 编译器或标准库版本不一致时会崩溃，实测可复现。
    PluginManager::getInstance().loadAll();
}

brls::View* FileBrowserActivity::createSourcesView() {
    sources_slot_ = DynamicBox::makeSlot();
    sources_container_ = new DynamicBox(sources_slot_);
    *sources_slot_ = sources_container_;
    sources_container_->setAxis(brls::Axis::COLUMN);
    sources_container_->setGrow(1.0f);

    renderSourcesContent();
    return sources_container_;
}

void FileBrowserActivity::renderSourcesContent() {
    // 双保险：以 slot 为准同步一次。
    // DynamicBox 析构时会把 slot 置 nullptr，但 Activity 自己的
    // sources_container_ 是指向同一对象的另一份拷贝。两者若不同步，
    // refresh() 可能在容器已销毁后继续访问它（实测 SIGSEGV）。
    if (sources_slot_) sources_container_ = *sources_slot_;
    if (!sources_container_) return;
    sources_container_->clearViews();

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    // 底部留白：最后一个卡片滚到底时也要完整露出，
    // 否则会被底部提示栏硬切一条边（观感非常像裁切错误）。
    list->setPaddingBottom(28.0f);

    if (current_path_.empty()) {
        // 1. 本地存储挂载点
        list->addView(new SectionHeader("本地存储设备"));

        auto sources = MediaScanner::getInstance().getAvailableSources();
        for (const auto& src : sources) {
            auto* cell = new MediaCell(ui::Icon::Drive, src.name, src.path);
            cell->setChevron(true);
            std::string path_copy = src.path;
            std::string name_copy = src.name;
            cell->registerClickAction([this, path_copy, name_copy](brls::View*) {
                navigateTo(path_copy, name_copy);
                return true;
            });
            list->addView(cell);
        }

        // 2. 网络存储与扩展 MOD 分区
        list->addView(new SectionHeader("插件视频源"));

        auto plugins = PluginManager::getInstance().getPlugins();
        bool has_source_plugin = false;
        for (const auto& plug : plugins) {
            if (!plug.has_video_source) continue;
            has_source_plugin = true;

            std::string sub = "视频源插件 v" + plug.version +
                              (plug.has_scraper ? " · 含刮削器" : "");
            if (!PluginConfigStore::getInstance().hasConfig(
                    fs::path(plug.path).stem().string())) {
                sub += " · 尚未配置";
            }
            auto* cell = new MediaCell(ui::Icon::Plugin, plug.name, sub);
            cell->setChevron(true);
            std::string plugin_path = plug.path;
            std::string plugin_name = plug.name;
            cell->registerClickAction([plugin_path, plugin_name](brls::View*) {
                brls::Application::pushActivity(
                    new PluginBrowserActivity(plugin_path, plugin_name));
                return true;
            });
            list->addView(cell);
        }

        // 插件管理入口：始终显示，未装插件时也作为安装引导
        auto* manage = new MediaCell(
            ui::Icon::Plus, "添加 / 管理插件",
            has_source_plugin ? "查看已装插件、重新扫描、打开插件目录"
                              : "从 ZIP 或目录安装视频源 / 刮削源插件");
        manage->setChevron(true);
        manage->registerClickAction([](brls::View*) {
            brls::Application::pushActivity(new PluginManagerActivity());
            return true;
        });
        list->addView(manage);
    } else {
        // 返回上级
        auto* up = new MediaCell(ui::Icon::Back, "返回上一级", current_path_);
        up->setChevron(true);
        up->registerClickAction([this](brls::View*) {
            navigateUp();
            return true;
        });
        list->addView(up);

        current_items_ = MediaScanner::getInstance().listDirectory(current_path_);

        if (current_items_.empty()) {
            list->addView(new EmptyState(ui::Icon::Folder, "这个文件夹是空的",
                                         "支持常见的视频 / 音频格式与同名字幕文件"));
        } else {
            std::vector<MediaItem> playable_playlist;
            for (const auto& item : current_items_) {
                if (item.is_video() || item.is_audio()) {
                    playable_playlist.push_back(item);
                }
            }

            for (const auto& item : current_items_) {
                if (item.is_directory()) {
                    auto* cell = new MediaCell(ui::Icon::Folder, item.name, "文件夹");
                    cell->setChevron(true);
                    std::string target_dir = item.path;
                    cell->registerClickAction([this, target_dir](brls::View*) {
                        navigateTo(target_dir);
                        return true;
                    });
                    list->addView(cell);
                } else if (item.is_video()) {
                    std::string sub = MediaScanner::formatFileSize(item.size);
                    if (!item.matched_subtitles.empty()) sub += " · 含外挂字幕";

                    auto* cell = new MediaCell(ui::Icon::Video, item.name, sub);

                    HistoryRecord rec;
                    if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
                        if (rec.completed) {
                            cell->setBadge("已看完");
                            cell->setProgress(1.0f);
                        } else if (rec.duration > 0) {
                            const float ratio =
                                (float)((double)rec.position / rec.duration);
                            cell->setProgress(ratio);
                            cell->setBadge(std::to_string((int)(ratio * 100)) + "%");
                        }
                    }

                    std::string v_path = item.path;
                    cell->registerClickAction([playable_playlist, v_path](brls::View*) {
                        size_t target_idx = 0;
                        for (size_t i = 0; i < playable_playlist.size(); ++i) {
                            if (playable_playlist[i].path == v_path) {
                                target_idx = i;
                                break;
                            }
                        }
                        brls::Application::pushActivity(
                            new PlayerActivity(playable_playlist, target_idx));
                        return true;
                    });
                    list->addView(cell);
                } else if (item.is_audio()) {
                    auto* cell = new MediaCell(
                        ui::Icon::Music, item.name,
                        "音频 · " + MediaScanner::formatFileSize(item.size));
                    std::string a_path = item.path;
                    cell->registerClickAction([playable_playlist, a_path](brls::View*) {
                        size_t target_idx = 0;
                        for (size_t i = 0; i < playable_playlist.size(); ++i) {
                            if (playable_playlist[i].path == a_path) {
                                target_idx = i;
                                break;
                            }
                        }
                        brls::Application::pushActivity(
                            new PlayerActivity(playable_playlist, target_idx));
                        return true;
                    });
                    list->addView(cell);
                }
            }
        }
    }

    scroller->setContentView(list);
    sources_container_->addView(scroller);
}

// 媒体库视图：列出用户添加的文件夹及其中的视频
brls::View* FileBrowserActivity::createLibraryView() {
    library_scroller_ = new brls::ScrollingFrame();
    library_scroller_->setGrow(1.0f);
    library_scroller_->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(library_scroller_);
    // 注意：library_scroller_ 由 TabFrame 持有并在切 Tab 时 delete，
    // 因此仅在同步路径中使用，不用于异步回调（异步回调用 library_list_）。

    // 用 DynamicBox 包裹：TabFrame 在切换标签时会 delete 旧 Tab 视图，
    // 若不清空成员指针，后续异步回调会访问已释放内存。
    library_slot_ = DynamicBox::makeSlot();
    library_list_ = new DynamicBox(library_slot_);
    *library_slot_ = library_list_;
    library_list_->setAxis(brls::Axis::COLUMN);
    library_list_->setPaddingBottom(28.0f);
    library_scroller_->setContentView(library_list_);

    // 重建库列表
    auto rebuild = [this]() {
        // 与 renderSourcesContent() 保持一致：以 slot 为准同步一次。
        // DynamicBox 析构置空 slot 发生在 deletionPool drain，
        // 比 removeView 晚一帧；不同步就会在那一帧里对已删除的 Box 调 clearViews()。
        if (library_slot_) library_list_ = *library_slot_;
        if (!library_list_) return;
        library_list_->clearViews();
        auto& lib = MediaLibrary::getInstance();

        // --- 文件夹管理区 ---
        library_list_->addView(new SectionHeader("媒体文件夹"));

        auto* add_cell = new MediaCell(ui::Icon::Plus, "添加媒体文件夹",
                                       "选择包含视频 / 音乐的目录，自动扫描并记住");
        add_cell->registerClickAction([this](brls::View*) {
            promptAddFolder();
            return true;
        });
        library_list_->addView(add_cell);

        auto folders = lib.getFolders();
        if (folders.empty()) {
            library_list_->addView(new EmptyState(
                ui::Icon::Folder, "还没有添加任何文件夹",
                "先添加一个影视目录\n扫描完成后，影片会自动出现在下方"));
        } else {
            for (const auto& f : folders) {
                auto* cell = new MediaCell(ui::Icon::Folder, f.display_name, f.path);
                cell->setChevron(true);
                std::string path = f.path;
                std::string name = f.display_name;
                cell->registerClickAction([this, path, name](brls::View*) {
                    showFolderOptions(path, name);
                    return true;
                });
                library_list_->addView(cell);
            }

            auto* rescan = new MediaCell(
                ui::Icon::Search, "重新扫描媒体库",
                lib.isScanning() ? "扫描进行中…" : "更新新增或修改的文件");
            rescan->registerClickAction([this](brls::View*) {
                startLibraryScan();
                return true;
            });
            library_list_->addView(rescan);
        }

        // --- 视频列表区 ---
        auto videos = lib.getVideos();
        library_list_->addView(
            new SectionHeader("视频 · " + std::to_string(videos.size())));

        if (videos.empty()) {
            library_list_->addView(new EmptyState(
                ui::Icon::Video, "媒体库里还没有视频",
                "添加文件夹后会自动扫描\n也可以在「存储位置」里直接浏览目录"));
        } else {
            for (size_t i = 0; i < videos.size(); ++i) {
                const auto& item = videos[i];
                // 若已刮削过，优先显示正式标题
                auto meta = MetadataStore::getInstance().get(item.path);
                const bool has_meta = meta && !meta->title.empty();
                const std::string title = has_meta ? meta->title : item.name;

                std::string sub;
                std::string badge;
                float progress = -1.0f;

                if (meta && !meta->is_empty()) {
                    if (!meta->release_date.empty())
                        sub = meta->release_date.substr(0, 4);
                    if (meta->runtime_minutes > 0) {
                        if (!sub.empty()) sub += " · ";
                        sub += std::to_string(meta->runtime_minutes) + " 分钟";
                    }
                    if (!meta->genres.empty()) {
                        if (!sub.empty()) sub += " · ";
                        sub += meta->genres;
                    }
                    if (meta->rating > 0) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "★ %.1f", meta->rating);
                        badge = buf;
                    }
                }

                if (sub.empty()) sub = MediaScanner::formatFileSize(item.size);
                if (!item.matched_subtitles.empty()) sub += " · 含字幕";

                HistoryRecord rec;
                if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
                    if (rec.completed) {
                        badge = "已看完";
                        progress = 1.0f;
                    } else if (rec.duration > 0) {
                        progress = (float)((double)rec.position / rec.duration);
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "已看 %d%%", (int)(progress * 100));
                        badge = buf;
                    }
                }

                auto* cell = new MediaCell(ui::Icon::Video, title, sub);
                if (!badge.empty()) cell->setBadge(badge);
                if (progress >= 0.0f) cell->setProgress(progress);

                size_t idx = i;
                cell->registerClickAction([videos, idx](brls::View*) {
                    brls::Logger::info("[click] 视频条目被点击 idx={} 总数={}",
                                       idx, videos.size());
                    brls::Application::pushActivity(new PlayerActivity(videos, idx));
                    return true;
                });

                // X 键：对当前条目刮削元数据
                std::string vpath = item.path;
                std::string vname = item.name;
                cell->registerAction("获取信息", brls::ControllerButton::BUTTON_X,
                    [this, vpath, vname](brls::View*) {
                        this->scrapeItem(vpath, vname);
                        return true;
                    }, false);

                library_list_->addView(cell);
            }
        }
    };

    // 首次进入时若无缓存则触发一次扫描
    if (MediaLibrary::getInstance().getItems().empty() &&
        !MediaLibrary::getInstance().getFolders().empty() &&
        !MediaLibrary::getInstance().isScanning()) {
        startLibraryScan();
    }

    rebuild();

    // 把 rebuild 挂到刷新路径上：用一个子类化的 Box 在析构时清理指针
    library_scroller_->registerAction("重新扫描", brls::ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            startLibraryScan();
            return true;
        }, false);

    return library_scroller_;
}

brls::View* FileBrowserActivity::createAudioView() {
    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    // 底部留白：最后一个卡片滚到底时也要完整露出，
    // 否则会被底部提示栏硬切一条边（观感非常像裁切错误）。
    list->setPaddingBottom(28.0f);

    auto audios = MediaLibrary::getInstance().getAudios();
    list->addView(new SectionHeader("音乐 · " + std::to_string(audios.size())));

    if (audios.empty()) {
        list->addView(new EmptyState(
            ui::Icon::Music, "媒体库里还没有音乐",
            "到「视频库」标签页添加一个包含音频的文件夹\n也可以直接在「存储位置」里播放"));
    } else {
        for (size_t i = 0; i < audios.size(); ++i) {
            const auto& item = audios[i];
            auto* cell = new MediaCell(ui::Icon::Music, item.name,
                                       MediaScanner::formatFileSize(item.size));

            size_t idx = i;
            cell->registerClickAction([audios, idx](brls::View*) {
                // 音乐走全局音频引擎，退出本界面后继续在后台播放
                AudioPlayer::getInstance().playPlaylist(audios, idx);
                brls::Application::notify("正在播放：" + audios[idx].name);
                return true;
            });
            list->addView(cell);
        }
    }

    scroller->setContentView(list);
    return scroller;
}

void FileBrowserActivity::promptAddFolder() {
    auto picker = FilePickerActivity::folders([this](const std::string& path) {
        auto& lib = MediaLibrary::getInstance();
        if (lib.hasFolder(path)) {
            brls::Application::notify("该文件夹已在媒体库中");
            return;
        }
        if (lib.addFolder(path)) {
            lib.save();
            brls::Application::notify("已添加，正在扫描…");
            startLibraryScan();
        } else {
            brls::Application::notify("添加失败：路径不可用或已存在");
        }
    });
    brls::Application::pushActivity(picker);
}

void FileBrowserActivity::startLibraryScan() {
    auto& lib = MediaLibrary::getInstance();
    if (lib.isScanning()) {
        brls::Application::notify("扫描已在进行中");
        return;
    }

    // 后台线程持有活标志，避免活动销毁后回调访问悬垂 this
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    std::thread([this, alive]() {
        MediaLibrary::getInstance().rebuild(
            [](int done, int total, const std::string& cur) {
                brls::Logger::info("扫描中 {}/{}: {}", done, total, cur);
            });
        brls::sync([this, alive]() {
            // 必须最先检查：活动销毁后 this 已悬垂，任何成员访问都是未定义行为
            if (!alive->load()) return;
            brls::Application::notify("媒体库扫描完成");
            refresh();
        });
    }).detach();
}

void FileBrowserActivity::showFolderOptions(const std::string& path, const std::string& name) {
    brls::Dialog* dialog = new brls::Dialog("媒体文件夹：" + name + "\n" + path);
    dialog->addButton("取消", []() {});
    dialog->addButton("移除该文件夹", [path]() {
        if (MediaLibrary::getInstance().removeFolder(path)) {
            MediaLibrary::getInstance().save();
            brls::Application::notify("已移除");
        }
    });
    dialog->open();
}


brls::View* FileBrowserActivity::createRecentView() {
    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    // 底部留白：最后一个卡片滚到底时也要完整露出，
    // 否则会被底部提示栏硬切一条边（观感非常像裁切错误）。
    list->setPaddingBottom(28.0f);

    list->addView(new SectionHeader("继续观看"));

    auto recents = PlaybackHistory::getInstance().getRecentList(30);
    if (recents.empty()) {
        list->addView(new EmptyState(
            ui::Icon::Clock, "还没有播放记录",
            "看过一半的影片会出现在这里\n下次打开直接从断点继续"));
    } else {
        for (const auto& rec : recents) {
            fs::path p(rec.path);
            auto* cell = new MediaCell(ui::Icon::Video, p.filename().string(),
                                       fs::path(rec.path).parent_path().filename().string());

            std::string badge;
            if (rec.completed) {
                badge = "已看完";
                cell->setProgress(1.0f);
            } else if (rec.duration > 0) {
                const float ratio = (float)((double)rec.position / rec.duration);
                cell->setProgress(ratio);
                badge = MediaScanner::formatDuration(rec.position) + " / " +
                        MediaScanner::formatDuration(rec.duration);
            }
            if (!badge.empty()) cell->setBadge(badge);

            std::string file_path = rec.path;
            cell->registerClickAction([file_path](brls::View*) {
                MediaItem single_item;
                single_item.name = fs::path(file_path).filename().string();
                single_item.path = file_path;
                single_item.type = MediaType::Video;

                std::vector<MediaItem> playlist = { single_item };
                brls::Application::pushActivity(new PlayerActivity(playlist, 0));
                return true;
            });

            list->addView(cell);
        }
    }

    scroller->setContentView(list);
    return scroller;
}

void FileBrowserActivity::selectTabForTest(int index) {
    if (!tab_frame_) return;
    // TabFrame::selectTab 内部走 Sidebar::getItem(position)，
    // 那里对 contentBox->getChildren()[position] 不做边界检查，
    // 越界会拿到野指针并在 dynamic_cast 里 SEGV（实测 START_TAB=1000 必崩）。
    // 本界面固定 4 个 Tab，越界一律忽略。
    if (index < 0 || index >= 4) {
        brls::Logger::warning("selectTabForTest: 索引 {} 越界（0..3）", index);
        return;
    }
    tab_frame_->selectTab(index);
}

void FileBrowserActivity::navigateTo(const std::string& path, const std::string& title) {
    if (!current_path_.empty()) {
        path_history_.push_back(current_path_);
    }
    current_path_ = path;
    if (!title.empty()) {
        frame_->setTitle(title);
    } else {
        frame_->setTitle(fs::path(path).filename().string());
    }
    renderSourcesContent();
}

void FileBrowserActivity::navigateUp() {
    if (path_history_.empty()) {
        current_path_ = "";
        frame_->setTitle("DeckPlayer");
        renderSourcesContent();
        return;
    }

    std::string prev_path = path_history_.back();
    path_history_.pop_back();
    current_path_ = prev_path;
    frame_->setTitle(fs::path(prev_path).filename().string());
    renderSourcesContent();
}

// ---------------------------------------------------------------------------
// 刮削
// ---------------------------------------------------------------------------

void FileBrowserActivity::scrapeItem(const std::string& path, const std::string& filename) {
    auto scraper_paths = PluginManager::getInstance().getScraperPluginPaths();
    if (scraper_paths.empty()) {
        brls::Dialog* d = new brls::Dialog(
            "尚未安装刮削器插件。\n\n"
            "刮削器可为本地影片自动补齐封面、简介与评分。\n"
            "请到【存储位置 → 插件管理】安装 .so / .dylib 刮削插件。");
        d->addButton("知道了", []() {});
        d->open();
        return;
    }

    std::string query;
    int64_t year = 0;
    MediaNameParser::parse(filename, query, year);

    brls::Application::notify("正在搜索：" + query);

    // 网络请求可能较慢，放到后台线程，完成后回到 UI 线程。
    // 每次都新建独立标志：若复用 alive_flag_，多个并发刮削会互相覆盖，
    // 且首次刮削前 alive_flag_ 可能为空（从未触发过扫描）。
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);
    std::string first_scraper = scraper_paths.front();
    std::string file_path = path;
    std::string display_name = filename;

    std::thread([this, alive, first_scraper, query, year, file_path, display_name]() {
        ScrapeDetail detail;
        bool ok = PluginManager::getInstance().scrapeForFile(
            first_scraper, query, year, detail);

        brls::sync([this, alive, ok, detail, file_path, display_name, query, first_scraper]() {
            // 首要检查：活动销毁后 this 悬垂，不得访问任何成员
            if (!alive->load()) return;

            if (!ok) {
                brls::Dialog* d = new brls::Dialog(
                    "未找到匹配的影片信息。\n\n"
                    "搜索词：" + query + "\n\n"
                    "可尝试重命名文件使其更接近正式片名。");
                d->addButton("知道了", []() {});
                d->open();
                return;
            }

            MediaMetadata meta;
            meta.path = file_path;
            meta.title = detail.title;
            meta.overview = detail.overview;
            meta.poster_url = detail.poster_url;
            meta.backdrop_url = detail.backdrop_url;
            meta.release_date = detail.release_date;
            meta.genres = detail.genres;
            meta.rating = detail.rating;
            meta.runtime_minutes = detail.runtime_minutes;
            meta.scraper_id = first_scraper;
            MetadataStore::getInstance().put(meta);
            MetadataStore::getInstance().save();

            std::string body = "标题：" + meta.title + "\n";
            if (!meta.release_date.empty()) body += "上映：" + meta.release_date + "\n";
            if (meta.runtime_minutes > 0)
                body += "片长：" + std::to_string(meta.runtime_minutes) + " 分钟\n";
            if (!meta.genres.empty()) body += "类型：" + meta.genres + "\n";
            if (meta.rating > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "评分：%.1f / 10\n", meta.rating);
                body += buf;
            }
            if (!meta.overview.empty()) body += "\n" + meta.overview;

            brls::Dialog* d = new brls::Dialog(body);
            d->addButton("完成", [this]() {
                brls::Application::notify("已保存影片信息");
                this->refresh();
            });
            d->open();
        });
    }).detach();
}

void FileBrowserActivity::showMediaInfo(const std::string& path, const std::string& filename) {
    auto meta = MetadataStore::getInstance().get(path);
    std::string body;
    if (meta && !meta->is_empty()) {
        body = "标题：" + meta->title + "\n";
        if (!meta->release_date.empty()) body += "上映：" + meta->release_date + "\n";
        if (meta->runtime_minutes > 0)
            body += "片长：" + std::to_string(meta->runtime_minutes) + " 分钟\n";
        if (!meta->genres.empty()) body += "类型：" + meta->genres + "\n";
        if (meta->rating > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "评分：%.1f / 10\n", meta->rating);
            body += buf;
        }
        if (!meta->overview.empty()) body += "\n" + meta->overview;
    } else {
        body = filename + "\n\n尚未获取影片信息。\n选中该条目后按 X 键可自动刮削。";
    }
    brls::Dialog* d = new brls::Dialog(body);
    d->addButton("关闭", []() {});
    d->open();
}

void FileBrowserActivity::refresh() {
    renderSourcesContent();
}

} // namespace deck_player
