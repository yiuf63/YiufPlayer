#include <map>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
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
#include "activity/now_playing_activity.hpp"
#include "view/media_cell.hpp"
#include "view/media_folder_card.hpp"
#include "view/media_grid_card.hpp"
#include "view/media_thumb.hpp"
#include "view/tool_button.hpp"
#include "view/view_sheet.hpp"
#include "model/series_detector.hpp"
#include "activity/series_activity.hpp"
#include "model/library_view_options.hpp"
#include "model/network_source.hpp"
#include "activity/network_browser_activity.hpp"
#include "utils/webdav_client.hpp"
#include "utils/thumbnail_cache.hpp"
#include "utils/display_mode.hpp"
#include "utils/input_settings.hpp"
#include "utils/keymap.hpp"
#include "theme/muvik_theme.hpp"
#include "ui/icons.hpp"
#include "utils/i18n.hpp"
#include "activity/player_activity.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace muvik {

// 自检用：模拟 processInput 保存的触摸起点视图
brls::View* g_touch_held_view = nullptr;

// 内容区右侧安全边距。
//
// 列表宽 = ScrollingFrame 宽（setContentView 里强制 setWidth），而内容区
// 本身是贴到屏幕右缘的：detailText 右对齐后最后一个字符正好落在像素列
// 1279 上，看起来像被裁切，掌机上还会被机身圆角吃掉。电视端 UI 一律留
// overscan 安全区，这里统一给 28px。
constexpr float CONTENT_SAFE_MARGIN = 28.0f;

// 首次启动时自动探测本地常见媒体位置（Steam Deck 存储卡、系统视频/音乐目录）
static std::vector<std::pair<std::string, std::string>> detectLocalMediaSources() {
    std::vector<std::pair<std::string, std::string>> res;
    std::set<std::string> seen;

    auto checkAndAdd = [&](const std::string& path, const std::string& label) {
        if (path.empty() || seen.count(path)) return;
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            seen.insert(path);
            res.emplace_back(path, label);
        }
    };

    // 0. 开源掌机与嵌入式 Linux MicroSD 卡媒体目录（TrimUI / 掌机设备优先）
    checkAndAdd("/mnt/SDCARD/Videos", i18n::tr("files.sdcard_videos", "MicroSD 视频目录 (/mnt/SDCARD/Videos)"));
    checkAndAdd("/mnt/SDCARD/music", i18n::tr("files.sdcard_music", "MicroSD 音乐目录 (/mnt/SDCARD/music)"));
    checkAndAdd("/mnt/SDCARD/Music", i18n::tr("files.sdcard_music", "MicroSD 音乐目录 (/mnt/SDCARD/Music)"));
    if (seen.find("/mnt/SDCARD/Videos") == seen.end()) {
        checkAndAdd("/mnt/SDCARD", i18n::tr("files.sdcard_root", "MicroSD 存储卡 (/mnt/SDCARD)"));
    }

    // 1. Steam Deck MicroSD 存储卡
    checkAndAdd("/run/media/mmcblk0p1", i18n::tr("files.steam_deck_microsd", "Steam Deck 存储卡 (MicroSD)"));

    // 2. Steam Deck / Linux 挂载的外部存储
    std::error_code ec;
    if (fs::exists("/run/media/deck", ec) && !ec) {
        try {
            fs::directory_iterator it("/run/media/deck", fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator end;
            while (!ec && it != end) {
                std::error_code e2;
                if (it->is_directory(e2)) {
                    checkAndAdd(it->path().string(), i18n::tr("files.external_storage", "外部存储") + " (" + it->path().filename().string() + ")");
                }
                it.increment(ec);
            }
        } catch (...) {}
    }

    // 3. 用户主目录下的标准影音目录
    const char* home_env = std::getenv("HOME");
    std::string home = home_env ? home_env : "";
    if (!home.empty()) {
        checkAndAdd(home + "/Videos", i18n::tr("files.videos_dir", "视频目录") + " (" + home + "/Videos)");
        checkAndAdd(home + "/Movies", i18n::tr("files.movies_dir", "电影目录") + " (" + home + "/Movies)");
        checkAndAdd(home + "/Music", i18n::tr("files.music_dir", "音乐目录") + " (" + home + "/Music)");
    }

    // 4. Steam Deck 默认 deck 用户路径
    checkAndAdd("/home/deck/Videos", i18n::tr("files.deck_videos", "Steam Deck 视频目录"));
    checkAndAdd("/home/deck/Music", i18n::tr("files.deck_music", "Steam Deck 音乐目录"));

    // 5. macOS 外接卷
    ec.clear();
    if (fs::exists("/Volumes", ec) && !ec) {
        try {
            fs::directory_iterator it("/Volumes", fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator end;
            while (!ec && it != end) {
                std::error_code e2;
                if (it->is_directory(e2)) {
                    const std::string fn = it->path().filename().string();
                    if (fn != "Macintosh HD" && fn != "Recovery" && fn != "Preboot") {
                        checkAndAdd(it->path().string(), i18n::tr("files.external_volume", "外接卷") + " (" + fn + ")");
                    }
                }
                it.increment(ec);
            }
        } catch (...) {}
    }

    return res;
}


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

// 收集容器里的可聚焦控件（卡片装在行 Box 里，所以要下探两层）。
// 列表重建后必须有人把焦点接过来：焦点一旦落在被删掉的子项上，
// View 析构会把它置空，手柄按 A 就没有反应，ScrollingFrame 还会
// 对着空焦点取 frame（见 scrolling_frame.cpp 里的补丁）。
void collectFocusables(brls::Box* root, std::vector<brls::View*>& out, int depth = 0) {
    if (!root || depth > 6) return;
    if (root->isFocusable()) {
        out.push_back(root);
        return;
    }
    for (auto* child : root->getChildren()) {
        collectFocusables(dynamic_cast<brls::Box*>(child), out, depth + 1);
    }
}

// 媒体库整页：默认焦点给内容区，不给工具行。
// 工具行在 DOM 序里排在前面，框架的 getDefaultFocus 会把首焦点给搜索框
// —— 手柄用户切到媒体库按 A，弹出来的是输入法而不是进文件夹。
// 内容优先：搜索是低频操作，方向键向上一步就能到。
class LibraryRootBox : public brls::Box {
public:
    brls::View* getDefaultFocus() override {
        if (content_) {
            std::vector<brls::View*> items;
            collectFocusables(content_, items);
            if (!items.empty()) return items[0];
        }
        return brls::Box::getDefaultFocus();
    }
    brls::Box* content_ = nullptr;
};

// 一条视频在列表和网格里要显示的三行信息。
// 抽出来是因为两种卡片形态必须显示同一份数据 —— 各自算一遍的结果是
// 同一条片子在列表里标「已看 30%」、在网格里没有进度条。
struct CellMeta {
    std::string title;
    std::string sub;
    std::string badge;
    float progress = -1.0f;
};

CellMeta readVideoMeta(const MediaItem& item) {
    CellMeta m;
    auto meta = MetadataStore::getInstance().get(item.path);
    const bool has_meta = meta && !meta->title.empty();
    m.title = has_meta ? meta->title : item.name;

    if (meta && !meta->is_empty()) {
        if (!meta->release_date.empty()) m.sub = meta->release_date.substr(0, 4);
        if (meta->runtime_minutes > 0) {
            if (!m.sub.empty()) m.sub += " · ";
            m.sub += std::to_string(meta->runtime_minutes) + " 分钟";
        }
        if (!meta->genres.empty()) {
            if (!m.sub.empty()) m.sub += " · ";
            m.sub += meta->genres;
        }
        if (meta->rating > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "★ %.1f", meta->rating);
            m.badge = buf;
        }
    }

    if (m.sub.empty()) m.sub = MediaScanner::formatFileSize(item.size);
    if (!item.matched_subtitles.empty()) m.sub += " · 含字幕";

    HistoryRecord rec;
    if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
        if (rec.completed) {
            m.badge = "已看完";
            m.progress = 1.0f;
        } else if (rec.duration > 0) {
            m.progress = static_cast<float>(static_cast<double>(rec.position) /
                                            static_cast<double>(rec.duration));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "已看 %d%%",
                          static_cast<int>(m.progress * 100.0f));
            m.badge = buf;
        }
    }
    return m;
}

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
    //
    // 顺序与命名对齐原型（web/ui-review）：媒体库 / 文件 / 历史 / 设置。
    // 每一页回答一个问题 ——「我要看什么」「文件在哪」「上次看哪」「怎么改」，
    // 而不是「数据从哪来」。配置项全部撤出内容页，集中到设置。
    tab_frame_->addTab(i18n::tr("tabs.library", "媒体库"), BRLS_ASSET("icon/nav/media.png"),
                       [this]() -> brls::View* { return createLibraryView(); });
    tab_frame_->addTab(i18n::tr("tabs.files", "文件"), BRLS_ASSET("icon/nav/files.png"),
                       [this]() -> brls::View* { return createSourcesView(); });
    tab_frame_->addTab(i18n::tr("tabs.history", "历史"), BRLS_ASSET("icon/nav/history.png"),
                       [this]() -> brls::View* { return createRecentView(); });
    tab_frame_->addTab(i18n::tr("tabs.settings", "设置"), BRLS_ASSET("icon/nav/settings.png"),
                       [this]() -> brls::View* { return createSettingsView(); });

    frame_ = new brls::AppletFrame(tab_frame_);
    frame_->setTitle("Yiuf Player");
    frame_->setIcon(BRLS_ASSET("icon/icon.png"));

    // 手柄设置/菜单键 (BUTTON_START / ☰)：直接切换到「设置」标签页，而不是闪退
    frame_->registerAction("设置", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        if (tab_frame_) {
            tab_frame_->selectTab(3); // 3 为「设置」标签页
        }
        return true;
    });

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
    if (const char* target = std::getenv("MUVIK_AUTO_SCRAPE")) {
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
    if (std::getenv("MUVIK_CLICK_FIRST_VIDEO")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::getenv("MUVIK_CLICK_DELAY")
                    ? std::atoi(std::getenv("MUVIK_CLICK_DELAY")) : 120));
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
                //
                // 必须先 hitTest 找到真正被点的那个控件：
                // 直接拿 frame_ 派发，事件只到根节点自己的手势识别器，
                // 卡片上的 TapGestureRecognizer 根本收不到 ——
                // 这样的「点击自检」永远通过，也永远测不到东西。
                brls::View* root = frame_ ? frame_->getContentView() : nullptr;
                if (!root) { brls::Logger::warning("frame_ 为空"); return; }
                brls::View* target = root->hitTest(brls::Point(700, 200));
                if (!target) { brls::Logger::warning("hitTest 没有命中任何控件"); return; }
                brls::Logger::info("自检：命中 {}（{}x{}）", typeid(*target).name(),
                                   target->getWidth(), target->getHeight());
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

    // 自检：呼出「视图与排序」。等价于手柄按 BACK（registerAction 走的
    // 就是 openViewSheet），但不依赖按键映射与焦点位置。
    if (std::getenv("MUVIK_OPEN_SHEET")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                brls::Logger::info("自检：呼出视图与排序");
                openViewSheet();
            });
        }).detach();
    }

    // 自检：切到「设置」页（截图回归用：主题切换项、外观行的可读性）。
    if (std::getenv("MUVIK_OPEN_SETTINGS")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            brls::sync([this, alive]() {
                if (!alive->load() || !tab_frame_) return;
                brls::Logger::info("自检：切到设置页");
                tab_frame_->focusTab(3);   // 媒体库 / 文件 / 历史 / 设置
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
    if (std::getenv("MUVIK_TOUCH_REBUILD")) {
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
    if (const char* env = std::getenv("MUVIK_START_TAB")) {
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
    if (const char* which = std::getenv("MUVIK_OPEN_DIALOG")) {
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
                    brls::Logger::warning("未知的 MUVIK_OPEN_DIALOG: {}", kind);
                }
            });
        }).detach();
    }


    return frame_;
}

void FileBrowserActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 绑定返回键 B：在子目录/子文件夹时返回上一级；在最外层时弹出退出应用确认对话框
    this->registerAction(i18n::tr("common.exit", "退出"), brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        (void)view;
        // 如果视图与排序浮层正在显示，优先关闭浮层
        if (view_sheet_ && view_sheet_->isOpen()) {
            view_sheet_->hide();
            return true;
        }
        const int activeTab = tab_frame_ ? tab_frame_->getActiveIndex() : -1;
        if (activeTab == 0) { // 媒体库标签页
            if (!open_folder_.empty()) {
                navigateLibraryUp();
                return true;
            }
        } else if (activeTab == 1) { // 文件标签页
            if (!path_history_.empty() || !current_path_.empty()) {
                navigateUp();
                return true;
            }
        }
        showExitDialog();
        return true;
    });

    // 自动扫描外部 MOD 插件（本地、Steam 创意工坊与 DLC）
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

    // 侧栏聚焦检测：记录重绘前焦点是否位于侧边栏内部
    bool focus_in_sidebar = false;
    brls::View* cur = brls::Application::getCurrentFocus();
    if (cur && !brls::Application::isViewDead(cur)) {
        for (brls::View* v = cur; v != nullptr; v = v->getParent()) {
            if (dynamic_cast<brls::SidebarItem*>(v) || dynamic_cast<brls::Sidebar*>(v)) {
                focus_in_sidebar = true;
                break;
            }
        }
    }

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
        list->addView(new SectionHeader(i18n::tr("files.storage_devices", "本地存储设备")));

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

        // 2. 网络存储（NAS / WebDAV）
        list->addView(new SectionHeader(i18n::tr("files.network_storage", "网络存储 (NAS / WebDAV)")));

        auto net_sources = NetworkSourceStore::getInstance().getSources();
        for (const auto& nsrc : net_sources) {
            std::string sub = nsrc.url;
            if (!nsrc.username.empty()) sub += " (" + nsrc.username + ")";
            auto* cell = new MediaCell(ui::Icon::Drive, nsrc.name, sub);
            cell->setChevron(true);
            NetworkSource src_copy = nsrc;
            cell->registerClickAction([src_copy](brls::View*) {
                brls::Application::pushActivity(new NetworkBrowserActivity(src_copy));
                return true;
            });
            cell->registerAction("管理/删除", brls::ControllerButton::BUTTON_X, [this, src_copy](brls::View*) {
                showDeleteNetworkSourceDialog(src_copy);
                return true;
            });
            list->addView(cell);
        }

        // 添加网络存储入口
        auto* add_net = new MediaCell(
            ui::Icon::Plus, i18n::tr("files.add_network_source", "添加网络存储 (WebDAV / NAS)"),
            i18n::tr("files.add_network_source_desc", "连接群晖、威联通、AList 聚合盘或自建 WebDAV 服务器"));
        add_net->setChevron(true);
        add_net->registerClickAction([this](brls::View*) {
            showAddNetworkSourceDialog();
            return true;
        });
        list->addView(add_net);

        // 3. 网络存储与扩展 MOD 分区
        list->addView(new SectionHeader(i18n::tr("files.plugin_sources", "插件视频源")));

        auto plugins = PluginManager::getInstance().getPlugins();
        for (const auto& plug : plugins) {
            if (!plug.has_video_source) continue;

            std::string sub = i18n::tr("files.plugin_ver", "视频源插件 v") + plug.version +
                              (plug.has_scraper ? i18n::tr("files.plugin_with_scraper", " · 含刮削器") : "");
            if (!PluginConfigStore::getInstance().hasConfig(
                    fs::path(plug.path).stem().string())) {
                sub += i18n::tr("files.plugin_not_configured", " · 尚未配置");
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
            ui::Icon::Plus, i18n::tr("files.manage_plugins", "添加 / 管理插件"),
            i18n::tr("files.manage_plugins_desc", "从 ZIP 安装插件或启用第三方源"));
        manage->setChevron(true);
        manage->registerClickAction([](brls::View*) {
            brls::Application::pushActivity(new PluginManagerActivity());
            return true;
        });
        list->addView(manage);
    } else {
        // 返回上级
        auto* up = new MediaCell(ui::Icon::Back, "..");
        up->setChevron(true);
        up->registerClickAction([this](brls::View*) {
            navigateUp();
            return true;
        });
        list->addView(up);

        current_items_ = MediaScanner::getInstance().listDirectory(current_path_);

        if (current_items_.empty()) {
            list->addView(new EmptyState(ui::Icon::Folder, i18n::tr("files.empty_dir", "空目录"),
                                         i18n::tr("files.empty_dir_desc", "支持常见的视频 / 音频格式与同名字幕文件")));
        } else {
            std::vector<MediaItem> playable_playlist;
            for (const auto& item : current_items_) {
                if (item.is_video() || item.is_audio()) {
                    playable_playlist.push_back(item);
                }
            }

            for (const auto& item : current_items_) {
                if (item.is_directory()) {
                    auto* cell = new MediaCell(ui::Icon::Folder, item.name, i18n::tr("files.open_folder", "文件夹"));
                    cell->setChevron(true);
                    std::string target_dir = item.path;
                    cell->registerClickAction([this, target_dir](brls::View*) {
                        navigateTo(target_dir);
                        return true;
                    });
                    list->addView(cell);
                } else if (item.is_video()) {
                    std::string sub = MediaScanner::formatFileSize(item.size);
                    if (!item.matched_subtitles.empty()) sub += " · " + i18n::tr("common.has_subtitles", "含字幕");

                    auto* cell = new MediaCell(ui::Icon::Video, item.name, sub);

                    HistoryRecord rec;
                    if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
                        if (rec.completed) {
                            cell->setBadge(i18n::tr("common.played", "已看完"));
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
                        i18n::tr("common.audio", "音频") + " · " + MediaScanner::formatFileSize(item.size));
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

    // B 键在子目录内部是「返回上一级」，在最外层时交给外层（返回侧边栏或退出）。
    scroller->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            if (path_history_.empty() && current_path_.empty()) return false;
            navigateUp();
            return true;
        }, false);

    // 侧栏聚焦保护：若当前焦点在侧边栏内，严禁夺取焦点，避免在侧栏上下切换标签时强行抢走焦点。
    // 仅在用户真正处于文件浏览页面内部返回或跳转时才恢复内容焦点。
    bool in_sidebar = focus_in_sidebar;
    brls::View* end_cur = brls::Application::getCurrentFocus();
    if (end_cur && !brls::Application::isViewDead(end_cur)) {
        for (brls::View* v = end_cur; v != nullptr; v = v->getParent()) {
            if (dynamic_cast<brls::SidebarItem*>(v) || dynamic_cast<brls::Sidebar*>(v)) {
                in_sidebar = true;
                break;
            }
        }
    }
    const bool is_files_tab_active = !tab_frame_ || (tab_frame_->getActiveIndex() == 1);
    if (!in_sidebar && is_files_tab_active) {
        std::vector<brls::View*> items;
        collectFocusables(list, items);
        if (!items.empty()) {
            brls::Application::giveFocus(items[0]);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 媒体库：文件夹总览 ⇄ 单个文件夹
//
// 为什么要多出「文件夹」这一层：
//   把所有目录里的视频和音乐拉平成一条列表时，十个「01. 开头」的条目
//   排成一列，视频与音乐混在一起，「3:21」既可能是短片也可能是歌 ——
//   一屏扫过去读不出哪些是同一部剧。文件夹是用户自己在文件管理器里
//   分好的桶：先看桶，再决定进哪个桶。
//
// 首屏另配一段「最近添加」：只有目录卡时，刚拷进去的片子反而找不到。
// ─────────────────────────────────────────────────────────────────────────

// 视频条目卡片。媒体库、文件夹内、最近添加三处共用同一套外观与行为，
// 差别只在传入的播放列表（点开之后能连着往下放的那一组）。
MediaCell* FileBrowserActivity::makeVideoCell(const MediaItem& item,
                                              std::vector<MediaItem> playlist,
                                              size_t idx) {
    const CellMeta m = readVideoMeta(item);
    auto* cell = new MediaCell(ui::Icon::Video, m.title, m.sub);
    if (!m.badge.empty()) cell->setBadge(m.badge);
    if (m.progress >= 0.0f) cell->setProgress(m.progress);
    // 列表行也用 16:9 缩略图：一屏十几行「[视频] 01. 开头」的列表，
    // 靠文件名认片比靠画面慢得多
    cell->setThumbnail(sceneSpecFor(item));

    cell->registerClickAction([playlist = std::move(playlist), idx](brls::View*) {
        brls::Logger::info("[click] 视频条目被点击 idx={} 总数={}", idx, playlist.size());
        brls::Application::pushActivity(new PlayerActivity(playlist, idx));
        return true;
    });

    cell->registerAction(i18n::tr("media.get_info", "获取信息"), brls::ControllerButton::BUTTON_X, [this, item](brls::View*) {
        showMediaInfo(item.path, item.name);
        return true;
    });
    return cell;
}

brls::View* FileBrowserActivity::makeVideoCard(const MediaItem& item,
                                               std::vector<MediaItem> playlist,
                                               size_t idx) {
    const CellMeta m = readVideoMeta(item);
    auto* card = new MediaGridCard(item, m.title, m.sub, m.progress);
    if (!m.badge.empty()) card->setBadge(m.badge);

    card->registerClickAction([playlist = std::move(playlist), idx](brls::View*) {
        brls::Application::pushActivity(new PlayerActivity(playlist, idx));
        return true;
    });

    card->registerAction(i18n::tr("media.get_info", "获取信息"), brls::ControllerButton::BUTTON_X, [this, item](brls::View*) {
        showMediaInfo(item.path, item.name);
        return true;
    });
    return card;
}

MediaCell* FileBrowserActivity::makeAudioCell(const MediaItem& item,
                                              std::vector<MediaItem> playlist,
                                              size_t idx) {
    auto* cell = new MediaCell(ui::Icon::Music, item.name,
                               MediaScanner::formatFileSize(item.size));
    cell->registerClickAction([playlist = std::move(playlist), idx](brls::View*) {
        // 音乐走全局音频引擎，退出本界面后继续在后台播放
        AudioPlayer::getInstance().playPlaylist(playlist, idx);
        brls::Application::notify(i18n::tr("audio.now_playing_prefix", "正在播放：") + playlist[idx].name);
        return true;
    });
    return cell;
}

brls::View* FileBrowserActivity::makeAudioCard(const MediaItem& item,
                                               std::vector<MediaItem> playlist,
                                               size_t idx) {
    // 音频没有画面：这里不塞缩略图，让 drawScene 画占位 + 音符角标。
    // 「一张看似截图的图 + 音符标」比纯色块更容易被误读成视频。
    auto* card = new MediaGridCard(item, item.name,
                                   MediaScanner::formatFileSize(item.size));
    card->registerClickAction([playlist = std::move(playlist), idx](brls::View*) {
        AudioPlayer::getInstance().playPlaylist(playlist, idx);
        brls::Application::notify(i18n::tr("audio.now_playing_prefix", "正在播放：") + playlist[idx].name);
        return true;
    });
    return card;
}

// 媒体库视图 = 工具行 + 可滚动内容。
//
// 工具行只有一个入口：右边那枚显示当前摘要的按钮（「按文件夹 · 名称」），
// 点开是四组选项。为什么不做成一排胶囊：四组选项塞进 56px 高的工具行会
// 变成两行，而且「仅视频 / 最近添加 / 未看完」这种词占着位置却表达不了
// 分组关系 —— 菜单的价值是「入口唯一、里面可以分组」。
brls::Box* FileBrowserActivity::makeLibraryToolbar() {
    auto* bar = new brls::Box();
    bar->setAxis(brls::Axis::ROW);
    bar->setAlignItems(brls::AlignItems::CENTER);
    bar->setJustifyContent(brls::JustifyContent::FLEX_START);
    bar->setHeight(56.0f);
    bar->setWidthPercentage(100.0f);
    bar->setMarginRight(CONTENT_SAFE_MARGIN);
    bar->setMarginBottom(6.0f);

    auto* search = new ToolButton(i18n::tr("common.search", "搜索"), ui::Icon::Search, 14.0f);
    search->setWidth(260.0f);
    search->setMarginRight(12.0f);
    search->registerClickAction([this](brls::View*) {
        promptSearch();
        return true;
    });
    bar->addView(search);
    library_search_btn_ = search;

    auto* view_btn = new ToolButton(LibraryViewOptions::get().summary(),
                                    ui::Icon::Sliders, 14.0f);
    view_btn->setChevron(true);
    view_btn->registerClickAction([this](brls::View*) {
        openViewSheet();
        return true;
    });
    bar->addView(view_btn);
    library_view_btn_ = view_btn;

    return bar;
}

void FileBrowserActivity::refreshToolbar() {
    if (library_view_btn_) {
        library_view_btn_->setText(LibraryViewOptions::get().summary());
    }
    if (library_search_btn_) {
        library_search_btn_->setText(library_query_.empty()
                                         ? i18n::tr("common.search", "搜索")
                                         : i18n::tr("common.search", "搜索") + "：" + library_query_);
    }
}

void FileBrowserActivity::promptSearch() {
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            // 空串 = 清空搜索（IME 里删光再确认，是唯一不需要额外按钮的清除方式）
            library_query_ = text;
            refreshToolbar();
            renderLibraryContent();
        },
        i18n::tr("library.search_title", "搜索媒体库"),
        i18n::tr("library.search_hint", "输入片名的一部分，留空表示不筛选"), 64);
}

void FileBrowserActivity::openViewSheet() {
    // 浮层懒创建、复用：菜单是 media 库页的一部分，随 Activity 生死。
    // 选项变化后：工具行摘要与列表都要跟着变。回调发生在菜单关闭之前，
    // 所以这里只重建内容，不动菜单本身。
    if (!view_sheet_) {
        view_sheet_ = new ViewSheet(
            [this]() {
                // 切到平铺时，文件夹层级不再成立
                if (LibraryViewOptions::get().group == LibraryGroup::Flat)
                    open_folder_.clear();
                refreshToolbar();
                renderLibraryContent();
            },
            [this]() {
                // 关闭后优先把焦点交还给打开它的「视图与排序」工具按钮，若无则交还给内容区第一项
                if (library_view_btn_) {
                    brls::Application::giveFocus(library_view_btn_);
                } else if (library_list_) {
                    std::vector<brls::View*> items;
                    collectFocusables(library_list_, items);
                    if (!items.empty()) brls::Application::giveFocus(items[0]);
                }
            });
    }
    view_sheet_->show(library_root_);
}

brls::View* FileBrowserActivity::createLibraryView() {
    auto* root = new LibraryRootBox();
    library_root_ = root;
    library_root_->setAxis(brls::Axis::COLUMN);
    library_root_->setGrow(1.0f);
    library_root_->addView(makeLibraryToolbar());

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
    library_root_->addView(library_scroller_);
    root->content_ = library_list_;   // 默认焦点指向内容区第一个卡片

    // 首次进入时若无缓存则触发一次扫描。
    //
    // 只判「整个库为空」不够：新加一个文件夹之后库里已经有东西，
    // 那个新文件夹却会以「空文件夹」的样子挂在首屏，直到用户按 Y ——
    // 用户不知道要按 Y，只会觉得加进来的目录没生效。
    // 空目录本身不能每次进媒体库都重扫，所以按文件夹记一次「查过了」。
    {
        auto& lib = MediaLibrary::getInstance();
        if (!lib.getFolders().empty() && !lib.isScanning()) {
            bool need_scan = lib.getItems().empty();
            std::vector<std::string> newly_empty;
            for (const auto& g : lib.getGroups()) {
                if (g.total() > 0) continue;
                const bool checked = std::find(empty_checked_.begin(), empty_checked_.end(),
                                               g.folder.path) != empty_checked_.end();
                if (!checked) newly_empty.push_back(g.folder.path);
            }
            if (!newly_empty.empty()) {
                need_scan = true;
                for (const auto& p : newly_empty) empty_checked_.push_back(p);
            }
            if (need_scan) startLibraryScan();
        }
    }

    // 自检：预设搜索词（IME 是交互式的，无头验证只能这样进来）
    if (const char* q = std::getenv("MUVIK_SEARCH")) {
        library_query_ = q;
    }

    renderLibraryContent();

    library_scroller_->registerAction(i18n::tr("library.rescan_btn", "重新扫描"), brls::ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            startLibraryScan();
            return true;
        }, false);

    // B 在文件夹内部是「上一级」，在总览上才交给系统（退出界面）。
    // 不接管的话，从文件夹里退出去会直接关掉整个 Activity。
    library_scroller_->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            if (open_folder_.empty()) return false;
            navigateLibraryUp();
            return true;
        }, false);

    // BACK（Select）：呼出「视图与排序」。挂在整页上而不是列表上，
    // 因为在平铺网格里焦点落在卡片上，列表容器收不到按键。
    // 用 BACK 而不是某个字母键：手柄布局里没有 V，X/Y 已经给了刮削与重扫。
    library_root_->registerAction(i18n::tr("library.view_sort", "视图与排序"), brls::ControllerButton::BUTTON_BACK,
        [this](brls::View*) {
            openViewSheet();
            return true;
        }, false);

    return library_root_;
}

void FileBrowserActivity::openFolder(const std::string& path) {
    open_folder_ = path;
    renderLibraryContent();
    // 进 / 出文件夹是一次导航：焦点必须落在新一层的第一项上。
    // 不接管的话，原来那张卡片已经被删除，焦点是空的 ——
    // 手柄按 A 没反应，看起来像「界面卡住了」。
    //
    // 进文件夹时跳过第 0 项（那是「全部文件夹」返回行）：
    // 进了桶之后按 A 又退出来，是最容易让人以为「点不动」的行为。
    //
    // giveFocus 推迟到下一帧：刚 addView 的子项还没走完一次布局，
    // 冷缓存时（抽帧回调随时会再重建一次列表）立即 giveFocus 偶发失效，
    // 焦点留在返回行上 —— 下一按 A 就退出了文件夹。
    if (library_list_) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        const bool entering = !path.empty();
        brls::sync([this, alive, entering]() {
            if (!alive->load() || !library_list_) return;
            std::vector<brls::View*> items;
            collectFocusables(library_list_, items);
            if (items.empty()) return;
            const size_t idx = (entering && items.size() > 1) ? 1 : 0;
            brls::View* want = items[idx];
            brls::Application::giveFocus(want);
        });
    }
    // 换层之后要回到顶部：进文件夹还停在总览的滚动位置，
    // 首屏直接从第三张卡片开始，看着像内容丢了。
    if (library_scroller_) library_scroller_->setContentOffsetY(0.0f, false);
}

void FileBrowserActivity::navigateLibraryUp() {
    if (open_folder_.empty()) return;
    auto groups = MediaLibrary::getInstance().getGroups();
    const auto it = std::find_if(groups.begin(), groups.end(),
        [this](const MediaLibrary::Group& g) {
            if (g.folder.path == open_folder_) return true;
            std::string prefix = g.folder.path;
            if (!prefix.empty() && prefix.back() != '/') prefix += '/';
            return open_folder_.rfind(prefix, 0) == 0;
        });
    if (it == groups.end() || open_folder_ == it->folder.path) {
        // 已经在当前媒体库根目录，返回总览
        openFolder(std::string());
    } else {
        // 返回上一级子目录
        std::string parent = fs::path(open_folder_).parent_path().string();
        if (parent.size() < it->folder.path.size()) {
            parent = it->folder.path;
        }
        openFolder(parent);
    }
}

void FileBrowserActivity::renderLibraryContent() {
    // 与 renderSourcesContent() 保持一致：以 slot 为准同步一次。
    // DynamicBox 析构置空 slot 发生在 deletionPool drain，
    // 比 removeView 晚一帧；不同步就会在那一帧里对已删除的 Box 调 clearViews()。
    if (library_slot_) library_list_ = *library_slot_;
    if (!library_list_) return;

    // 重建会把列表项连同焦点一起销毁。先记下「焦点原本在第几项」，
    // 重建完按索引接回去 —— 记住指针没用，那个对象马上就没了。
    brls::View* prev = brls::Application::getCurrentFocus();
    int prev_idx = -1;
    if (prev) {
        std::vector<brls::View*> before;
        collectFocusables(library_list_, before);
        for (size_t i = 0; i < before.size(); ++i) {
            if (before[i] == prev) {
                prev_idx = static_cast<int>(i);
                break;
            }
        }
    }
    const bool focus_was_lost = (prev == nullptr) || brls::Application::isViewDead(prev);

    library_list_->clearViews();

    auto& lib = MediaLibrary::getInstance();
    if (lib.getFolders().empty()) {
        auto detected = detectLocalMediaSources();
        if (!detected.empty()) {
            library_list_->addView(new EmptyState(
                ui::Icon::Video, i18n::tr("library.welcome_title", "欢迎使用 Yiuf Player"),
                i18n::tr("library.welcome_desc", "已检测到本地存储设备与影音目录\n点击下方按钮一键建立您的专属掌机影院")));
            auto* one_click = new MediaCell(
                ui::Icon::Play, i18n::tr("library.one_click_setup", "一键建立影院"),
                i18n::tr("library.one_click_setup", "一键建立影院") + " (" + std::to_string(detected.size()) + " " + i18n::tr("library.folders_count", "个文件夹") + ")");
            one_click->setChevron(true);
            one_click->registerClickAction([this, detected](brls::View*) {
                auto& ml = MediaLibrary::getInstance();
                for (const auto& [p, name] : detected) {
                    ml.addFolder(p, name);
                }
                ml.save();
                brls::Application::notify(i18n::tr("library.folder_added_scanning", "已添加，正在扫描…"));
                startLibraryScan();
                refresh();
                return true;
            });
            library_list_->addView(one_click);

            auto* manual_btn = new MediaCell(
                ui::Icon::Folder, i18n::tr("library.manual_pick", "手动挑选媒体目录"),
                i18n::tr("library.hero_manual_desc", "选择含有音视频的本地存储或挂载路径"));
            manual_btn->setChevron(true);
            manual_btn->registerClickAction([this](brls::View*) {
                promptAddFolder();
                return true;
            });
            library_list_->addView(manual_btn);
        } else {
            library_list_->addView(new EmptyState(
                ui::Icon::Folder, i18n::tr("library.empty_title", "还没有添加任何媒体目录"),
                i18n::tr("library.empty_desc", "点击下方按钮或从「文件」标签页添加")));
            auto* add_folder = new MediaCell(
                ui::Icon::Plus, i18n::tr("library.add_folder", "添加媒体文件夹"),
                i18n::tr("library.hero_add_folder_desc", "选择含有音视频的本地存储或挂载目录"));
            add_folder->setChevron(true);
            add_folder->registerClickAction([this](brls::View*) {
                promptAddFolder();
                return true;
            });
            library_list_->addView(add_folder);
        }

        // 焦点接回：空库状态下同样确保首个操作按钮获得初始手柄焦点
        std::vector<brls::View*> empty_items;
        collectFocusables(library_list_, empty_items);
        if (!empty_items.empty() && focus_was_lost) {
            brls::Application::giveFocus(empty_items[0]);
        }
        return;
    }

    auto groups = lib.getGroups();
    auto& opt = LibraryViewOptions::get();

    // 「全部平铺」与搜索都让文件夹层级不成立：
    // 搜索结果跨文件夹，再套一层「先进文件夹」就没有意义了。
    const bool flat = !library_query_.empty() || opt.group == LibraryGroup::Flat;
    if (flat) open_folder_.clear();

    // 文件夹被移除 / 换库之后，open_folder_ 可能指向已经不存在的目录
    if (!open_folder_.empty()) {
        const auto it = std::find_if(groups.begin(), groups.end(),
            [this](const MediaLibrary::Group& g) {
                if (g.folder.path == open_folder_) return true;
                std::string prefix = g.folder.path;
                if (!prefix.empty() && prefix.back() != '/') prefix += '/';
                return open_folder_.rfind(prefix, 0) == 0;
            });
        if (it == groups.end()) open_folder_.clear();
    }

    // 内容 / 筛选先落到每个文件夹上：目录卡上的计数必须与进去之后
    // 看到的条目数一致，否则「3 视频」点进去只有 1 条，像是丢了数据。
    const bool narrowing = opt.content != LibraryContent::All ||
                           opt.filter != LibraryFilter::All;
    std::vector<MediaLibrary::Group> filtered;
    filtered.reserve(groups.size());
    for (auto g : groups) {
        g.videos = opt.apply(std::move(g.videos));
        g.audios = opt.apply(std::move(g.audios));
        // 筛选后为空的目录直接隐藏：这时的「0」不是真的空目录，
        // 而是「这一库没有符合筛选的条目」，摆一排空卡只会让人以为坏了
        if (narrowing && g.total() == 0) continue;
        filtered.push_back(std::move(g));
    }

    if (flat) {
        renderLibraryFlat(filtered);
    } else if (!open_folder_.empty()) {
        renderLibraryFolder(filtered);
    } else {
        renderLibraryOverview(filtered);
    }

    // 焦点接回：只有当当前 Tab 处于媒体库时才接回焦点，避免切到其他 Tab（如设置、文件）时将焦点错误抢回不可见的媒体库
    if (!tab_frame_ || tab_frame_->getActiveIndex() == 0) {
        // 模态安全守护：若视图与排序浮层正在显示，绝不能抢夺焦点（焦点必须留在浮层内）
        if (view_sheet_ && view_sheet_->isOpen()) {
            return;
        }
        std::vector<brls::View*> items;
        collectFocusables(library_list_, items);
        if (!items.empty()) {
            if (prev_idx >= 0) {
                // 原来在哪一项就还给哪一项（越界时退到最后一项）
                const int idx = std::min(prev_idx, static_cast<int>(items.size()) - 1);
                brls::Application::giveFocus(items[idx]);
            } else if (focus_was_lost || ((prev == library_search_btn_ || prev == library_view_btn_) && library_query_.empty())) {
                // 列表初次填充或失去焦点时，首个卡片获得焦点；若用户未主动搜索，默认焦点切回卡片
                brls::Application::giveFocus(items[0]);
            }
        }
    }
}

void FileBrowserActivity::renderLibraryOverview(const std::vector<MediaLibrary::Group>& groups) {
    auto& lib = MediaLibrary::getInstance();

    size_t total = 0;
    for (const auto& g : groups) total += g.total();

    if (total == 0) {
        // 区分「库是空的」和「筛掉了」：前者要让用户去设置里加目录，
        // 后者只要提示换个筛选条件
        const auto& opt = LibraryViewOptions::get();
        const bool narrowing = opt.content != LibraryContent::All ||
                               opt.filter != LibraryFilter::All;
        library_list_->addView(narrowing
            ? new EmptyState(ui::Icon::Filter, "当前筛选下没有条目",
                             "换一个内容类型或筛选条件试试\n按 BACK 打开视图与排序")
            : new EmptyState(ui::Icon::Video, "这些文件夹里还没有媒体文件",
                             "支持的视频与音频会自动进库\n按 Y 可以重新扫描"));
        return;
    }

    library_list_->addView(
        new SectionHeader(i18n::tr("common.folders", "文件夹") + " · " + std::to_string(groups.size())));

    // 目录卡的四宫格也要真实帧：收集每张卡的前四个条目，统一排队抽帧
    std::vector<MediaItem> want_thumbs;

    // 一行 4 张。卡片自己左右各让 8，"缝"就有了，容器不需要管间距。
    brls::Box* row = nullptr;
    int col = 0;
    for (const auto& g : groups) {
        if (col == 0) {
            row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setWidthPercentage(100.0f);
            row->setHeight(MediaFolderCard::HEIGHT);
            library_list_->addView(row);
        }

        // 四宫格取前四个：视频优先，不足再用音乐补。
        // 不复制已有条目来凑满四格 —— 同一张画面出现两次，用户会以为
        // 文件夹里有两部片。
        std::vector<MediaItem> covers;
        covers.reserve(4);
        for (const auto& v : g.videos) {
            if (covers.size() >= 4) break;
            covers.push_back(v);
        }
        for (const auto& a : g.audios) {
            if (covers.size() >= 4) break;
            covers.push_back(a);
        }
        want_thumbs.insert(want_thumbs.end(), covers.begin(), covers.end());

        auto* card = new MediaFolderCard(g.folder.display_name, g.folder.path,
                                         std::move(covers),
                                         static_cast<int>(g.videos.size()),
                                         static_cast<int>(g.audios.size()));
        card->setWidthPercentage(25.0f);
        const std::string fpath = g.folder.path;
        const std::string fname = g.folder.display_name;
        card->registerClickAction([this, fpath](brls::View*) {
            openFolder(fpath);
            return true;
        });
        card->registerAction(i18n::tr("library.folder_options", "文件夹选项"), brls::ControllerButton::BUTTON_X,
            [this, fpath, fname](brls::View*) {
                showFolderOptions(fpath, fname);
                return true;
            });
        row->addView(card);
        col = (col + 1) % 4;
    }

    // 最近添加：按文件修改时间倒序，跨文件夹取 8 条。
    // 只有目录卡时，刚拷进去的片子反而找不到。
    //
    // 它只在「按文件夹 + 不筛内容 + 不筛进度」时出现：平铺视图里再插一段
    // 最近添加就是同一批条目出现两次；筛「仅视频」时还在这一段里混音乐，
    // 用户会以为筛选没生效。
    requestThumbnails(want_thumbs);
    if (!LibraryViewOptions::get().showsRecent()) return;

    auto recent = LibraryViewOptions::get().apply(lib.getRecent(8));
    if (recent.empty()) return;

    requestThumbnails(recent);

    std::vector<MediaItem> recent_videos;
    std::vector<MediaItem> recent_audios;
    for (const auto& it : recent) {
        if (it.is_video()) recent_videos.push_back(it);
        else if (it.is_audio()) recent_audios.push_back(it);
    }

    library_list_->addView(
        new SectionHeader(i18n::tr("library.recent_count", "最近添加") + " · " + std::to_string(recent.size())));
    for (const auto& item : recent) {
        if (item.is_video()) {
            size_t idx = 0;
            for (size_t i = 0; i < recent_videos.size(); ++i) {
                if (recent_videos[i].path == item.path) { idx = i; break; }
            }
            library_list_->addView(makeVideoCell(item, recent_videos, idx));
        } else if (item.is_audio()) {
            size_t idx = 0;
            for (size_t i = 0; i < recent_audios.size(); ++i) {
                if (recent_audios[i].path == item.path) { idx = i; break; }
            }
            library_list_->addView(makeAudioCell(item, recent_audios, idx));
        }
    }
}

void FileBrowserActivity::renderLibraryFolder(const std::vector<MediaLibrary::Group>& groups) {
    const auto it = std::find_if(groups.begin(), groups.end(),
        [this](const MediaLibrary::Group& g) {
            if (g.folder.path == open_folder_) return true;
            std::string prefix = g.folder.path;
            if (!prefix.empty() && prefix.back() != '/') prefix += '/';
            return open_folder_.rfind(prefix, 0) == 0;
        });
    if (it == groups.end()) return;
    const MediaLibrary::Group& g = *it;

    // 规范化当前路径前缀（确保以 / 结尾且不含重复斜杠）
    std::string cur_prefix = open_folder_;
    while (cur_prefix.size() > 1 && cur_prefix.back() == '/') {
        cur_prefix.pop_back();
    }
    cur_prefix += '/';

    const bool is_root = (open_folder_ == g.folder.path);

    // 统计与收集：子文件夹 vs 当前目录直属文件
    struct SubfolderEntry {
        std::string name;
        std::string path;
        int video_count = 0;
        int audio_count = 0;
    };
    std::map<std::string, SubfolderEntry> subfolder_map;
    std::vector<MediaItem> direct_videos;
    std::vector<MediaItem> direct_audios;

    auto partition_item = [&](const MediaItem& item) {
        if (item.path.size() <= cur_prefix.size()) return;
        if (item.path.compare(0, cur_prefix.size(), cur_prefix) != 0) return;
        std::string rel = item.path.substr(cur_prefix.size());
        size_t slash = rel.find_first_of("/\\");
        if (slash == std::string::npos) {
            // 直接位于当前目录下
            if (item.is_video()) direct_videos.push_back(item);
            else if (item.is_audio()) direct_audios.push_back(item);
        } else {
            // 位于子目录下（聚合到下一级目录名）
            std::string sub_name = rel.substr(0, slash);
            std::string sub_path = cur_prefix + sub_name;
            auto& entry = subfolder_map[sub_name];
            entry.name = sub_name;
            entry.path = sub_path;
            if (item.is_video()) entry.video_count++;
            else if (item.is_audio()) entry.audio_count++;
        }
    };

    for (const auto& v : g.videos) partition_item(v);
    for (const auto& a : g.audios) partition_item(a);

    // 1. 返回行：和条目一样的卡片规格
    if (is_root) {
        // 在媒体库添加的顶层目录：返回总览
        std::string back_sub = g.folder.display_name;
        if (!g.videos.empty()) back_sub += " · " + std::to_string(g.videos.size()) + " " + i18n::tr("common.videos", "视频");
        if (!g.audios.empty()) back_sub += " · " + std::to_string(g.audios.size()) + " " + i18n::tr("common.audios", "音乐");
        auto* back = new MediaCell(ui::Icon::Back, i18n::tr("library.all_folders", "全部文件夹"), back_sub);
        back->registerClickAction([this](brls::View*) {
            openFolder(std::string());
            return true;
        });
        const std::string cur_fpath = g.folder.path;
        const std::string cur_fname = g.folder.display_name;
        back->registerAction(i18n::tr("library.folder_options", "文件夹选项"), brls::ControllerButton::BUTTON_X,
            [this, cur_fpath, cur_fname](brls::View*) {
                showFolderOptions(cur_fpath, cur_fname);
                return true;
            });
        library_list_->addView(back);
    } else {
        // 在子目录中：返回上级目录
        std::string cur_folder_name = fs::path(open_folder_).filename().string();
        std::string parent_folder_path = fs::path(open_folder_).parent_path().string();
        std::string parent_folder_name;
        if (parent_folder_path == g.folder.path) {
            parent_folder_name = g.folder.display_name;
        } else {
            parent_folder_name = fs::path(parent_folder_path).filename().string();
        }
        std::string back_title = ".. " + i18n::tr("common.parent_dir", "返回上级");
        std::string back_sub = cur_folder_name;
        size_t total_in_folder = direct_videos.size() + direct_audios.size();
        for (const auto& [_, sub] : subfolder_map) {
            total_in_folder += sub.video_count + sub.audio_count;
        }
        back_sub += " · " + std::to_string(total_in_folder) + " " + i18n::tr("common.items", "项");

        auto* back = new MediaCell(ui::Icon::Back, back_title, back_sub);
        back->registerClickAction([this, parent_folder_path](brls::View*) {
            openFolder(parent_folder_path);
            return true;
        });
        library_list_->addView(back);
    }

    // 2. 子文件夹（Subfolders）
    if (!subfolder_map.empty()) {
        library_list_->addView(new SectionHeader(
            i18n::tr("common.folders", "子文件夹") + " · " + std::to_string(subfolder_map.size())));
        for (const auto& [name, entry] : subfolder_map) {
            std::string sub_desc;
            if (entry.video_count > 0) {
                sub_desc += std::to_string(entry.video_count) + " " + i18n::tr("common.videos", "视频");
            }
            if (entry.audio_count > 0) {
                if (!sub_desc.empty()) sub_desc += " · ";
                sub_desc += std::to_string(entry.audio_count) + " " + i18n::tr("common.audios", "音乐");
            }
            auto* cell = new MediaCell(ui::Icon::Folder, entry.name, sub_desc);
            cell->setChevron(true);
            const std::string target_path = entry.path;
            cell->registerClickAction([this, target_path](brls::View*) {
                openFolder(target_path);
                return true;
            });
            library_list_->addView(cell);
        }
    }

    // 3. 直属视频（Direct Videos，按 Kodi 规则自动聚合剧集并保留独立单片）
    if (!direct_videos.empty()) {
        auto det = SeriesDetector::detect(direct_videos);

        // 剧集聚合条目：将多集动画/电视剧收敛为单个条目，点击进入沉浸式选集详情页
        if (!det.series.empty()) {
            library_list_->addView(new SectionHeader(
                i18n::tr("common.series", "剧集") + " · " + std::to_string(det.series.size())));
            for (const auto& s : det.series) {
                std::string sub = "共 " + std::to_string(s.episodes.size()) + " 集";
                if (s.last_watched_idx >= 0) {
                    sub += " · 看到第 " + std::to_string(s.last_watched_idx + 1) + " 集";
                    if (s.last_watched_pos > 0) {
                        sub += " (" + MediaScanner::formatDuration(s.last_watched_pos) + ")";
                    }
                } else if (!s.release_date.empty()) {
                    sub = s.release_date.substr(0, 4) + " · " + sub;
                }

                auto* cell = new MediaCell(ui::Icon::Video, s.title, sub);
                cell->setChevron(true);
                if (s.rating > 0.0) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "★ %.1f", s.rating);
                    cell->setBadge(buf);
                } else {
                    cell->setBadge(std::to_string(s.episodes.size()) + " 集");
                }
                if (s.last_watched_dur > 0 && s.last_watched_pos > 0) {
                    cell->setProgress(static_cast<float>((double)s.last_watched_pos / s.last_watched_dur));
                }

                cell->registerClickAction([s](brls::View*) {
                    brls::Application::pushActivity(new SeriesActivity(s));
                    return true;
                });
                cell->registerAction(i18n::tr("media.get_info", "获取信息"), brls::ControllerButton::BUTTON_X, [this, s](brls::View*) {
                    showMediaInfo(s.representativeItem().path, s.title);
                    return true;
                });
                library_list_->addView(cell);
            }
        }

        // 独立单片/电影
        if (!det.standalone_videos.empty()) {
            library_list_->addView(new SectionHeader(
                i18n::tr("common.videos", "单片视频") + " · " + std::to_string(det.standalone_videos.size())));
            requestThumbnails(det.standalone_videos);
            for (size_t i = 0; i < det.standalone_videos.size(); ++i) {
                library_list_->addView(makeVideoCell(det.standalone_videos[i], det.standalone_videos, i));
            }
        }
    }

    // 4. 直属音频（Direct Audios）
    if (!direct_audios.empty()) {
        library_list_->addView(
            new SectionHeader(i18n::tr("common.audios", "音乐") + " · " + std::to_string(direct_audios.size())));
        for (size_t i = 0; i < direct_audios.size(); ++i) {
            library_list_->addView(makeAudioCell(direct_audios[i], direct_audios, i));
        }
    }

    // 5. 空状态
    if (subfolder_map.empty() && direct_videos.empty() && direct_audios.empty()) {
        library_list_->addView(new EmptyState(
            ui::Icon::Folder, "这个文件夹里没有媒体文件",
            "支持的视频与音频会自动进库\n按 Y 可以重新扫描"));
    }
}

// 全部平铺：一屏四列的缩略图墙。
//
// 为什么不平铺成列表：列表一屏 8 条、每条只有一段文字，扫一屏读不到
// 「这批片长什么样」；墙一屏 12 张，靠画面就能认。代价是每条占的面板
// 更大，所以「按文件夹」仍然是默认结构。
void FileBrowserActivity::renderLibraryFlat(
    const std::vector<MediaLibrary::Group>& groups) {
    auto& opt = LibraryViewOptions::get();

    std::vector<MediaItem> all;
    for (const auto& g : groups) {
        all.insert(all.end(), g.videos.begin(), g.videos.end());
        all.insert(all.end(), g.audios.begin(), g.audios.end());
    }
    all = opt.apply(std::move(all), library_query_);

    if (all.empty()) {
        if (!library_query_.empty()) {
            library_list_->addView(new EmptyState(
                ui::Icon::Search, "没有找到「" + library_query_ + "」",
                "换个关键词，或再点一次搜索框清空"));
        } else {
            library_list_->addView(new EmptyState(
                ui::Icon::Filter, "当前筛选下没有条目",
                "换一个内容类型或筛选条件试试\n按 BACK 打开视图与排序"));
        }
        return;
    }

    std::vector<MediaItem> videos;
    std::vector<MediaItem> audios;
    for (auto& it : all) {
        if (it.is_video()) videos.push_back(it);
        else if (it.is_audio()) audios.push_back(it);
    }

    requestThumbnails(videos);

    auto renderGrid = [this](std::vector<MediaItem>& seg, bool is_video) {
        brls::Box* row = nullptr;
        int col = 0;
        for (size_t i = 0; i < seg.size(); ++i) {
            if (col == 0) {
                row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setWidthPercentage(100.0f);
                row->setHeight(MediaGridCard::HEIGHT);
                library_list_->addView(row);
            }
            brls::View* card = is_video ? makeVideoCard(seg[i], seg, i)
                                        : makeAudioCard(seg[i], seg, i);
            // 一行四张：卡片自己让左右边形成缝，容器不需要管间距
            card->setWidthPercentage(25.0f);
            row->addView(card);
            col = (col + 1) % 4;
        }
    };

    if (!videos.empty()) {
        auto det = SeriesDetector::detect(videos);

        // 1. 剧集海报墙（Series Poster Wall）
        if (!det.series.empty()) {
            library_list_->addView(
                new SectionHeader(i18n::tr("common.series", "剧集与番剧") + " · " + std::to_string(det.series.size())));
            brls::Box* row = nullptr;
            int col = 0;
            for (size_t i = 0; i < det.series.size(); ++i) {
                const auto& s = det.series[i];
                if (col == 0) {
                    row = new brls::Box();
                    row->setAxis(brls::Axis::ROW);
                    row->setWidthPercentage(100.0f);
                    row->setHeight(MediaGridCard::HEIGHT);
                    library_list_->addView(row);
                }
                std::string sub = "共 " + std::to_string(s.episodes.size()) + " 集";
                if (s.last_watched_idx >= 0) {
                    sub += " · 看到第 " + std::to_string(s.last_watched_idx + 1) + " 集";
                } else if (!s.release_date.empty()) {
                    sub = s.release_date.substr(0, 4) + " · " + sub;
                }
                float prog = (s.last_watched_dur > 0 && s.last_watched_pos > 0)
                                 ? static_cast<float>((double)s.last_watched_pos / s.last_watched_dur)
                                 : -1.0f;
                auto* card = new MediaGridCard(s.representativeItem(), s.title, sub, prog);
                if (s.rating > 0.0) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "★ %.1f", s.rating);
                    card->setBadge(buf);
                } else {
                    card->setBadge(std::to_string(s.episodes.size()) + " 集");
                }
                card->setWidthPercentage(25.0f);
                card->registerClickAction([s](brls::View*) {
                    brls::Application::pushActivity(new SeriesActivity(s));
                    return true;
                });
                card->registerAction(i18n::tr("media.get_info", "获取信息"), brls::ControllerButton::BUTTON_X, [this, s](brls::View*) {
                    showMediaInfo(s.representativeItem().path, s.title);
                    return true;
                });
                row->addView(card);
                col = (col + 1) % 4;
            }
        }

        // 2. 电影与独立单片
        if (!det.standalone_videos.empty()) {
            library_list_->addView(
                new SectionHeader(i18n::tr("common.movies", "电影与单片") + " · " + std::to_string(det.standalone_videos.size())));
            renderGrid(det.standalone_videos, true);
        }
    }
    if (!audios.empty()) {
        library_list_->addView(
            new SectionHeader(i18n::tr("common.audios", "音乐") + " · " + std::to_string(audios.size())));
        renderGrid(audios, false);
    }
}

void FileBrowserActivity::requestThumbnails(const std::vector<MediaItem>& items) {
    if (!ThumbnailCache::getInstance().enabled()) return;

    std::vector<std::string> paths;
    paths.reserve(items.size());
    for (const auto& it : items) {
        if (it.is_video()) paths.push_back(it.path);
    }
    if (paths.empty()) return;

    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);
    ThumbnailCache::getInstance().requestBatch(paths, [this, alive]() {
        if (!alive->load()) return;   // Activity 已销毁
        // 重建一次列表让新生成的帧显示出来。用重建而不是只标脏：
        // 缩略图路径是在构造卡片时算好的，不重建就还是旧值。
        renderLibraryContent();
    });
}

// 设置页：原来散落在「存储位置」「视频库」里的配置项全部收在这里。
// 内容页只留内容 —— 这是信息架构重排的核心，其余都是配套。
brls::View* FileBrowserActivity::createSettingsView() {
    auto* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(scroller);

    settings_slot_ = DynamicBox::makeSlot();
    settings_list_ = new DynamicBox(settings_slot_);
    *settings_slot_ = settings_list_;
    settings_list_->setAxis(brls::Axis::COLUMN);
    settings_list_->setPaddingBottom(28.0f);
    scroller->setContentView(settings_list_);

    renderSettingsContent();
    return scroller;
}

void FileBrowserActivity::renderSettingsContent() {
    if (settings_slot_) settings_list_ = *settings_slot_;
    if (!settings_list_) return;

    int prev_idx = -1;
    brls::View* prev = brls::Application::getCurrentFocus();
    bool focus_was_lost = false;
    if (prev) {
        std::vector<brls::View*> old_items;
        collectFocusables(settings_list_, old_items);
        for (size_t i = 0; i < old_items.size(); ++i) {
            if (old_items[i] == prev) { prev_idx = (int)i; break; }
        }
        if (prev_idx < 0) focus_was_lost = true;
    } else {
        focus_was_lost = true;
    }

    settings_list_->clearViews();

    auto* list = settings_list_;
    auto& lib = MediaLibrary::getInstance();

    // ── 媒体库 ──
    list->addView(new SectionHeader(i18n::tr("library.title", "媒体库")));

    auto* add_folder = new MediaCell(
        ui::Icon::Plus, i18n::tr("library.add_folder", "添加媒体文件夹"),
        i18n::tr("settings.add_folder_desc", "选择包含视频 / 音乐的目录，自动扫描并记住"));
    add_folder->registerClickAction([this](brls::View*) {
        promptAddFolder();
        return true;
    });
    list->addView(add_folder);

    for (const auto& f : lib.getFolders()) {
        auto* cell = new MediaCell(ui::Icon::Folder, f.display_name, f.path);
        cell->setChevron(true);
        std::string path = f.path;
        std::string name = f.display_name;
        cell->registerClickAction([this, path, name](brls::View*) {
            showFolderOptions(path, name);
            return true;
        });
        list->addView(cell);
    }

    auto* rescan = new MediaCell(
        ui::Icon::Search, i18n::tr("settings.rescan", "重新扫描媒体库"),
        lib.isScanning() ? i18n::tr("settings.scanning", "扫描进行中…")
                         : i18n::tr("settings.rescan_desc", "更新新增或修改的文件"));
    rescan->registerClickAction([this](brls::View*) {
        startLibraryScan();
        return true;
    });
    list->addView(rescan);

    auto* cache = new MediaCell(
        ui::Icon::Info, i18n::tr("settings.clear_cache", "清空扫描缓存"),
        i18n::tr("settings.clear_cache_desc", "不清除文件夹，只丢弃已缓存的条目与文件签名"));
    cache->registerClickAction([](brls::View*) {
        MediaLibrary::getInstance().clearCache();
        brls::Application::notify(i18n::tr("settings.cache_cleared", "已清空扫描缓存，下次扫描将重新读取"));
        return true;
    });
    list->addView(cache);

    // ── 外观 ──
    list->addView(new SectionHeader(i18n::tr("settings.appearance", "外观")));

    {
        ThemeItem cur_theme = getCurrentTheme();
        auto* row = new MediaCell(
            ui::Icon::Info,
            i18n::tr("settings.theme", "主题风格") + "：" + cur_theme.name,
            i18n::tr("settings.theme_desc", "深色 / OLED 真黑 / 浅色 / 创意工坊自定义主题"));
        row->registerClickAction([this, row](brls::View*) {
            showThemeDialog(row);
            return true;
        });
        list->addView(row);
    }

    {
        const bool fs = DisplayMode::isFullscreen();
        auto* fs_row = new MediaCell(
            ui::Icon::Aspect,
            i18n::tr("settings.display", "显示与视口") + "：" + (fs ? i18n::tr("settings.fullscreen", "全屏") : i18n::tr("settings.windowed", "窗口")),
            i18n::tr("settings.display_desc", "内置 16:10 / 16:9 / 宽屏与沉浸黑边适配"));
        fs_row->registerClickAction([fs_row](brls::View*) {
            DisplayMode::toggleFullscreen(true);
            const bool now_fs = DisplayMode::isFullscreen();
            fs_row->setTitle(i18n::tr("settings.display", "显示与视口") + "：" + (now_fs ? i18n::tr("settings.fullscreen", "全屏") : i18n::tr("settings.windowed", "窗口")));
            return true;
        });
        list->addView(fs_row);
    }

    {
        const int cur_fps = DisplayMode::getTargetFps();
        auto* fps_row = new MediaCell(
            ui::Icon::Sliders,
            i18n::tr("settings.fps_limit", "帧率上限") + "：" + DisplayMode::getFpsLimitLabel(cur_fps),
            i18n::tr("settings.fps_limit_desc", "自动跟随屏幕刷新率（OLED 90Hz / 外接高刷 120Hz+）或自定义限帧"));
        fps_row->registerClickAction([this, fps_row](brls::View*) {
            showFpsLimitDialog(fps_row);
            return true;
        });
        list->addView(fps_row);
    }

    {
        std::string cur_lang = i18n::getLanguage();
        std::string display_lang = (cur_lang == "zh-Hans") ? "简体中文" : (cur_lang == "en-US" ? "English" : "跟随系统 / Auto");
        auto* lang_row = new MediaCell(
            ui::Icon::Subtitles,
            i18n::tr("settings.language", "界面语言 / Language") + "：" + display_lang,
            i18n::tr("settings.language_desc", "选择应用程序显示语言"));
        lang_row->registerClickAction([this, lang_row](brls::View*) {
            showLanguageDialog(lang_row);
            return true;
        });
        list->addView(lang_row);
    }

    // ── 控制 ──
    list->addView(new SectionHeader(i18n::tr("settings.controls", "控制")));

    {
        const bool swap = InputSettings::isSwapAB();
        auto* swap_row = new MediaCell(
            ui::Icon::Sliders,
            i18n::tr("settings.swap_ab", "手柄确认键") + "：" + (swap ? i18n::tr("settings.layout_switch", "Switch 布局（右键确认 / 下键返回）") : i18n::tr("settings.layout_xbox", "Xbox 布局（下键确认 / 右键返回）")),
            i18n::tr("settings.swap_ab_desc", "Nintendo Switch 风格或 Xbox/Steam 风格按键布局，即时生效"));
        swap_row->registerClickAction([swap_row](brls::View*) {
            InputSettings::toggleSwapAB();
            const bool now_swap = InputSettings::isSwapAB();
            swap_row->setTitle(i18n::tr("settings.swap_ab", "手柄确认键") + "：" +
                               (now_swap ? i18n::tr("settings.layout_switch", "Switch 布局（右键确认 / 下键返回）") : i18n::tr("settings.layout_xbox", "Xbox 布局（下键确认 / 右键返回）")));
            brls::Application::notify(i18n::tr("settings.swap_ab", "手柄确认键") + "：" +
                                      (now_swap ? "Switch (右键确认)" : "Xbox (下键确认)"));
            return true;
        });
        list->addView(swap_row);
    }

    {
        auto* keymap_row = new MediaCell(
            ui::Icon::Sliders,
            i18n::tr("settings.keymap_icon", "手柄按键提示图标") + "：" + Keymap::styleToString(Keymap::getStyle()),
            i18n::tr("settings.keymap_icon_desc", "底栏手柄按键提示图标风格（支持 Xbox、PS 与 Switch）"));
        keymap_row->registerClickAction([this, keymap_row](brls::View*) {
            showKeymapDialog(keymap_row);
            return true;
        });
        list->addView(keymap_row);
    }

    // ── 插件 ──
    list->addView(new SectionHeader(i18n::tr("settings.plugins", "插件")));

    auto* plugins = new MediaCell(
        ui::Icon::Plugin, i18n::tr("settings.plugins_manage", "插件管理"),
        i18n::tr("settings.plugins_desc", "安装 / 移除视频源与刮削源，来自 ZIP 或目录"));
    plugins->setChevron(true);
    plugins->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new PluginManagerActivity());
        return true;
    });
    list->addView(plugins);

    // ── 关于 ──
    list->addView(new SectionHeader(i18n::tr("settings.about", "关于")));

    auto* about = new MediaCell(
        ui::Icon::Info, i18n::tr("settings.about_app", "关于 Yiuf Player"),
        i18n::tr("settings.about_desc", "点击查看版本信息与核心引擎详情"));
    about->setChevron(true);
    about->registerClickAction([this](brls::View*) {
        showAboutDialog();
        return true;
    });
    list->addView(about);

    auto* license_cell = new MediaCell(
        ui::Icon::Info, i18n::tr("settings.license", "开源许可证：GNU GPL v3.0"),
        i18n::tr("settings.license_desc", "自由开源软件，保障用户使用、修改与分发的全部权利"));
    license_cell->registerClickAction([](brls::View*) {
        brls::Application::notify("GPL-3.0 · LICENSE");
        return true;
    });
    list->addView(license_cell);

    auto* engines_cell = new MediaCell(
        ui::Icon::Play, i18n::tr("settings.engines", "解码核心与界面引擎"),
        i18n::tr("settings.engines_desc", "libmpv 硬件加速 + FFmpeg 全能编解码 + Borealis 原生手柄 UI"));
    engines_cell->registerClickAction([this](brls::View*) {
        showAboutDialog();
        return true;
    });
    list->addView(engines_cell);

    // ── 系统 ──
    list->addView(new SectionHeader(i18n::tr("settings.system", "系统")));

    auto* exit_cell = new MediaCell(
        ui::Icon::Close, i18n::tr("settings.exit", "退出 Yiuf Player"),
        i18n::tr("settings.exit_desc", "保存所有状态并正常退出程序（手柄按 A 确认）"));
    exit_cell->registerClickAction([this](brls::View*) {
        showExitDialog();
        return true;
    });
    list->addView(exit_cell);

    if (!tab_frame_ || tab_frame_->getActiveIndex() == 3) {
        std::vector<brls::View*> items;
        collectFocusables(settings_list_, items);
        if (!items.empty()) {
            if (prev_idx >= 0) {
                const int idx = std::min(prev_idx, static_cast<int>(items.size()) - 1);
                brls::Application::giveFocus(items[idx]);
            } else if (focus_was_lost) {
                brls::Application::giveFocus(items[0]);
            }
        }
    }
}

void FileBrowserActivity::showExitDialog() {
    auto* dialog = new brls::Dialog(i18n::tr("dialog.exit_confirm", "确定要退出 Yiuf Player 吗？"));
    dialog->addButton(i18n::tr("common.cancel", "取消"), []() {});
    dialog->addButton(i18n::tr("common.exit", "退出"), []() {
        brls::Application::quit();
    });
    dialog->open();
}

void FileBrowserActivity::showAboutDialog() {
    brls::Box* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setWidth(680);
    box->setPaddingTop(16.0f);
    box->setPaddingBottom(16.0f);
    box->setPaddingLeft(24.0f);
    box->setPaddingRight(24.0f);

    auto* dialog = new brls::Dialog(box);

    auto* title_lbl = new brls::Label();
    title_lbl->setText(i18n::tr("about.title", "Yiuf Player"));
    title_lbl->setFontSize(26.0f);
    title_lbl->setTextColor(brls::Application::getTheme()["brls/sidebar/active_item"]);
    box->addView(title_lbl);

    auto* subtitle_lbl = new brls::Label();
    subtitle_lbl->setText(i18n::tr("about.subtitle", "专为大屏与掌机手柄打造的跨平台纯净影音中心"));
    subtitle_lbl->setFontSize(15.0f);
    subtitle_lbl->setMarginTop(4.0f);
    subtitle_lbl->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    box->addView(subtitle_lbl);

    auto* sep1 = new brls::Rectangle();
    sep1->setHeight(1.0f);
    sep1->setMarginTop(12.0f);
    sep1->setMarginBottom(12.0f);
    sep1->setColor(brls::Application::getTheme()["brls/sidebar/separator"]);
    box->addView(sep1);

    auto add_item = [box](const std::string& label, const std::string& desc) {
        auto* item_box = new brls::Box(brls::Axis::COLUMN);
        item_box->setMarginBottom(10.0f);
        auto* l1 = new brls::Label();
        l1->setText(label);
        l1->setFontSize(16.0f);
        l1->setTextColor(brls::Application::getTheme()["brls/text"]);
        auto* l2 = new brls::Label();
        l2->setText(desc);
        l2->setFontSize(13.0f);
        l2->setMarginTop(2.0f);
        l2->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
        item_box->addView(l1);
        item_box->addView(l2);
        box->addView(item_box);
    };

    add_item(i18n::tr("about.version_title", "版本信息"), i18n::tr("about.version_detail", "v1.0.0 (Release Build · C++17 · 60FPS Controller UI)"));
    add_item(i18n::tr("about.license_title", "开源协议"), i18n::tr("about.license_detail", "GNU General Public License v3.0 (GPL-3.0)"));
    add_item(i18n::tr("about.decoder_title", "解码核心"), i18n::tr("about.decoder_detail", "libmpv (硬件加速、ASS/SSA 特效字幕) + FFmpeg"));
    add_item(i18n::tr("about.ui_engine_title", "界面引擎"), i18n::tr("about.ui_engine_detail", "Borealis UI + Yoga Flexbox (原生手柄 60FPS 渲染)"));
    add_item(i18n::tr("about.handheld_title", "交互调优"), i18n::tr("about.handheld_detail", "大屏手柄原生支持、掌机与电视深度适配、视听双引擎独立驱动"));
    add_item(i18n::tr("about.plugin_ext_title", "扩展支持"), i18n::tr("about.plugin_ext_detail", "QuickJS 极轻量沙盒插件引擎，支持自建媒体源、网盘与刮削器"));
    add_item(i18n::tr("about.acknowledgments_title", "致谢与参考"), i18n::tr("about.acknowledgments_detail", "特别致谢 wiliwili、Kodi、mpv 等开源项目在手柄交互与音视频架构设计上的启发与贡献"));

    dialog->addButton(i18n::tr("common.close", "关闭"), []() {});
    dialog->open();
}

namespace {

struct SelectionOption {
    std::string id;
    std::string title;
    std::string subtitle;
    ui::Icon icon = ui::Icon::Sliders;
    bool is_selected = false;
    std::string badge;
};

void showSelectionDialog(
    const std::string& dialog_title,
    const std::vector<SelectionOption>& options,
    std::function<void(const std::string& id)> on_select
) {
    auto* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setWidth(600);

    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPaddingBottom(16.0f);

    list->addView(new SectionHeader(dialog_title));

    auto* scroller = new brls::ScrollingFrame();
    styleScrollingFrame(scroller);

    auto* dialog = new brls::Dialog(container);
    dialog->setCancelable(true);

    brls::View* focused_view = nullptr;

    for (const auto& opt : options) {
        auto* cell = new MediaCell(opt.icon, opt.title, opt.subtitle);
        cell->setHeight(64.0f);
        if (!opt.badge.empty()) {
            cell->setBadge(opt.badge);
        } else if (opt.is_selected) {
            cell->setBadge(i18n::tr("common.current", "当前"));
        }

        std::string opt_id = opt.id;
        cell->registerClickAction([dialog, on_select, opt_id](brls::View*) {
            if (on_select) on_select(opt_id);
            dialog->close();
            return true;
        });

        if (opt.is_selected && !focused_view) {
            focused_view = cell;
        }

        list->addView(cell);
    }

    scroller->setContentView(list);

    // 计算弹窗高度（自适应选项数量，避免空虚多余留白）
    const float content_h = std::min(440.0f, options.size() * 64.0f + 64.0f);
    scroller->setHeight(content_h);
    container->addView(scroller);

    // 底部添加单项返回按钮，按 B 键或点击均可正常退出
    dialog->addButton(i18n::tr("common.cancel", "取消"), []() {});

    // 默认高亮选中当前正在生效的项目
    if (focused_view) {
        dialog->setLastFocusedView(focused_view);
    }

    dialog->open();
}

} // namespace

void FileBrowserActivity::showLanguageDialog([[maybe_unused]] MediaCell* lang_row) {
    std::string cur_lang = i18n::getLanguage();
    std::vector<SelectionOption> options;

    for (const auto& opt : i18n::getAvailableLanguages()) {
        SelectionOption item;
        item.id = opt.code;
        item.title = opt.name;
        item.subtitle = (opt.code == "zh-Hans" ? "简体中文 (Simplified Chinese)" : "English (United States)");
        item.icon = ui::Icon::Subtitles;
        item.is_selected = (opt.code == cur_lang);
        options.push_back(item);
    }

    showSelectionDialog(
        i18n::tr("dialog.select_language", "选择界面语言"),
        options,
        [this](const std::string& code) {
            i18n::setLanguage(code);
            if (tab_frame_) {
                tab_frame_->setTabLabel(0, i18n::tr("tabs.library", "媒体库"));
                tab_frame_->setTabLabel(1, i18n::tr("tabs.files", "文件"));
                tab_frame_->setTabLabel(2, i18n::tr("tabs.history", "历史"));
                tab_frame_->setTabLabel(3, i18n::tr("tabs.settings", "设置"));
                tab_frame_->selectTab(3);
            }
            refresh();
            brls::Application::notify(i18n::tr("dialog.language_changed", "已切换语言：") + code);
        }
    );
}

void FileBrowserActivity::showThemeDialog(MediaCell* theme_row) {
    ThemeItem cur = getCurrentTheme();
    auto themes = getAvailableThemes();

    std::vector<SelectionOption> options;
    for (const auto& t : themes) {
        SelectionOption opt;
        opt.id = t.id;
        opt.title = t.name;
        if (!t.author.empty() && !t.is_builtin) {
            opt.subtitle = "作者: " + t.author;
        } else if (t.id == "dark") {
            opt.subtitle = i18n::tr("theme.dark_desc", "标准深灰蓝影音底色");
        } else if (t.id == "oled") {
            opt.subtitle = i18n::tr("theme.oled_desc", "纯黑像素级关屏省电背景");
        } else if (t.id == "light") {
            opt.subtitle = i18n::tr("theme.light_desc", "暖白纸感明亮主题");
        }
        opt.icon = ui::Icon::Info;
        if (t.is_workshop) opt.badge = "[创意工坊]";
        else if (!t.is_builtin) opt.badge = "[自定义]";
        opt.is_selected = (t.id == cur.id);
        options.push_back(opt);
    }

    showSelectionDialog(
        i18n::tr("dialog.select_theme", "选择主题风格"),
        options,
        [this, theme_row, themes](const std::string& tid) {
            applyThemeById(tid);
            std::string tname = tid;
            for (const auto& t : themes) {
                if (t.id == tid) { tname = t.name; break; }
            }
            if (theme_row) {
                theme_row->setTitle(i18n::tr("settings.theme", "主题风格") + "：" + tname);
            }
            refresh();
            brls::Application::notify(i18n::tr("settings.theme_changed", "已切换为：") + tname);
        }
    );
}

void FileBrowserActivity::showFpsLimitDialog(MediaCell* fps_row) {
    const int cur_fps = DisplayMode::getTargetFps();
    const std::vector<std::pair<int, std::string>> presets = {
        {0, i18n::tr("fps.auto", "自动（跟随屏幕 / 垂直同步）")},
        {30, i18n::tr("fps.30", "30 FPS（极致省电）")},
        {40, i18n::tr("fps.40", "40 FPS（掌机平衡）")},
        {48, i18n::tr("fps.48", "48 FPS（24fps 影视匹配）")},
        {60, i18n::tr("fps.60", "60 FPS（标准 LCD）")},
        {90, i18n::tr("fps.90", "90 FPS（Steam Deck OLED）")},
        {120, i18n::tr("fps.120", "120 FPS（外接高刷）")},
        {144, i18n::tr("fps.144", "144 FPS（电竞高刷）")},
    };

    std::vector<SelectionOption> options;
    for (const auto& [fps, desc] : presets) {
        SelectionOption opt;
        opt.id = std::to_string(fps);
        opt.title = (fps == 0 ? "自动 (VSync)" : std::to_string(fps) + " FPS");
        opt.subtitle = desc;
        opt.icon = ui::Icon::Sliders;
        opt.is_selected = (fps == cur_fps);
        options.push_back(opt);
    }

    showSelectionDialog(
        i18n::tr("settings.fps_limit", "帧率上限"),
        options,
        [fps_row](const std::string& id) {
            int fps = std::stoi(id);
            DisplayMode::setTargetFps(fps, true);
            std::string label = DisplayMode::getFpsLimitLabel(fps);
            if (fps_row) {
                fps_row->setTitle(i18n::tr("settings.fps_limit", "帧率上限") + "：" + label);
            }
            brls::Application::notify(i18n::tr("settings.fps_limit", "帧率上限") + "：" + label);
        }
    );
}

void FileBrowserActivity::showKeymapDialog(MediaCell* keymap_row) {
    const KeymapStyle cur_style = Keymap::getStyle();
    const std::vector<std::pair<KeymapStyle, std::string>> styles = {
        {KeymapStyle::Auto, i18n::tr("keymap.auto", "自动识别（跟随手柄）")},
        {KeymapStyle::Xbox, i18n::tr("keymap.xbox", "Xbox / Steam Deck（A/B/X/Y）")},
        {KeymapStyle::PlayStation, i18n::tr("keymap.ps", "PlayStation（✕/◯/▢/△）")},
        {KeymapStyle::Switch, i18n::tr("keymap.switch", "Nintendo Switch（Switch 风格）")},
    };

    std::vector<SelectionOption> options;
    for (const auto& [s, desc] : styles) {
        SelectionOption opt;
        opt.id = std::to_string(static_cast<int>(s));
        opt.title = Keymap::styleToString(s);
        opt.subtitle = desc;
        opt.icon = ui::Icon::Sliders;
        opt.is_selected = (s == cur_style);
        options.push_back(opt);
    }

    showSelectionDialog(
        i18n::tr("dialog.select_keymap", "选择手柄按键提示风格"),
        options,
        [keymap_row](const std::string& id) {
            KeymapStyle s = static_cast<KeymapStyle>(std::stoi(id));
            Keymap::setStyle(s);
            Keymap::reloadKeymapFont();
            if (keymap_row) {
                keymap_row->setTitle(i18n::tr("settings.keymap_icon", "手柄按键提示图标") + "：" + Keymap::styleToString(s));
            }
            brls::Application::notify(Keymap::styleToShortString(s));
        }
    );
}

void FileBrowserActivity::showAddNetworkSourceDialog() {
    std::vector<SelectionOption> presets = {
        {"alist", "AList 聚合网盘", "http://192.168.1.xxx:5244/dav (聚合挂载 115/夸克/本地存储)", ui::Icon::Drive, false, "推荐"},
        {"synology", "群晖 Synology WebDAV", "http://192.168.1.xxx:5005 (DSM WebDAV Server 套件)", ui::Icon::Drive, false, ""},
        {"qnap", "威联通 QNAP WebDAV", "http://192.168.1.xxx:5000/WebDAV (QTS WebDAV 共享)", ui::Icon::Drive, false, ""},
        {"custom", "自定义 WebDAV 服务器", "输入任意标准的 HTTP / HTTPS WebDAV 地址", ui::Icon::Sliders, false, ""}
    };

    showSelectionDialog(
        "选择网络存储类型 / 预设",
        presets,
        [this](const std::string& preset_id) {
            auto source = std::make_shared<NetworkSource>();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            source->id = "net_" + std::to_string(now_ms);
            source->type = NetworkSourceType::WebDAV;

            if (preset_id == "alist") {
                source->name = "AList 聚合网盘";
                source->url = "http://192.168.1.100:5244/dav";
            } else if (preset_id == "synology") {
                source->name = "群晖 NAS";
                source->url = "http://192.168.1.100:5005";
            } else if (preset_id == "qnap") {
                source->name = "威联通 NAS";
                source->url = "http://192.168.1.100:5000/WebDAV";
            } else {
                source->name = "我的 WebDAV";
                source->url = "http://192.168.1.100:8080";
            }

            auto* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setWidth(640);
            box->setPaddingTop(16.0f);
            box->setPaddingBottom(16.0f);
            box->setPaddingLeft(24.0f);
            box->setPaddingRight(24.0f);

            box->addView(new SectionHeader("配置网络存储 (" + source->name + ")"));

            auto* name_row = new MediaCell(ui::Icon::Sliders, "显示名称", source->name);
            name_row->setChevron(true);
            name_row->registerClickAction([source, name_row](brls::View*) {
                brls::Application::getImeManager()->openForText([source, name_row](std::string text) {
                    if (!text.empty()) {
                        source->name = text;
                        name_row->setSubtitle(text);
                    }
                }, "请输入网络存储显示名称", "例如：客厅 NAS / 追番网盘", 64, source->name);
                return true;
            });
            box->addView(name_row);

            auto* url_row = new MediaCell(ui::Icon::Sliders, "服务器地址 (URL)", source->url);
            url_row->setChevron(true);
            url_row->registerClickAction([source, url_row](brls::View*) {
                brls::Application::getImeManager()->openForText([source, url_row](std::string text) {
                    if (!text.empty()) {
                        source->url = text;
                        url_row->setSubtitle(text);
                    }
                }, "请输入 WebDAV 完整地址", "支持 http:// 或 https://，含端口与路径", 256, source->url);
                return true;
            });
            box->addView(url_row);

            auto* user_row = new MediaCell(ui::Icon::Sliders, "账号 (可留空)", source->username.empty() ? "(未填写)" : source->username);
            user_row->setChevron(true);
            user_row->registerClickAction([source, user_row](brls::View*) {
                brls::Application::getImeManager()->openForText([source, user_row](std::string text) {
                    source->username = text;
                    user_row->setSubtitle(text.empty() ? "(未填写)" : text);
                }, "请输入用户名", "若无需账号认证可留空", 64, source->username);
                return true;
            });
            box->addView(user_row);

            auto* pass_row = new MediaCell(ui::Icon::Lock, "密码 (可留空)", source->password.empty() ? "(未填写)" : "••••••••");
            pass_row->setChevron(true);
            pass_row->registerClickAction([source, pass_row](brls::View*) {
                brls::Application::getImeManager()->openForText([source, pass_row](std::string text) {
                    source->password = text;
                    pass_row->setSubtitle(text.empty() ? "(未填写)" : "••••••••");
                }, "请输入密码", "若无需密码可留空", 64, source->password);
                return true;
            });
            box->addView(pass_row);

            auto* dialog = new brls::Dialog(box);
            dialog->setCancelable(true);

            dialog->addButton("取消", []() {});
            dialog->addButton("测试并保存", [this, source, dialog]() {
                brls::Application::notify("正在测试连接…");
                std::thread([this, source, dialog]() {
                    std::string err;
                    bool ok = WebDAVClient::testConnection(*source, err);
                    brls::sync([this, source, dialog, ok, err]() {
                        if (ok) {
                            NetworkSourceStore::getInstance().addSource(*source);
                            brls::Application::notify("网络存储添加成功：" + source->name);
                            dialog->close();
                            refresh();
                        } else {
                            auto* err_dlg = new brls::Dialog("连接测试失败\n\n" + err + "\n\n是否仍然强制保存？");
                            err_dlg->addButton("取消", []() {});
                            err_dlg->addButton("仍然保存", [this, source, dialog]() {
                                NetworkSourceStore::getInstance().addSource(*source);
                                brls::Application::notify("已保存网络存储：" + source->name);
                                dialog->close();
                                refresh();
                            });
                            err_dlg->open();
                        }
                    });
                }).detach();
            });

            dialog->open();
        }
    );
}

void FileBrowserActivity::showDeleteNetworkSourceDialog(const NetworkSource& source) {
    auto* dlg = new brls::Dialog("网络存储操作\n\n名称：" + source.name + "\n地址：" + source.url);
    dlg->addButton("取消", []() {});
    dlg->addButton("删除该源", [this, source]() {
        NetworkSourceStore::getInstance().removeSource(source.id);
        brls::Application::notify("已移除网络存储：" + source.name);
        refresh();
    });
    dlg->open();
}

void FileBrowserActivity::promptAddFolder() {
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);
    auto picker = FilePickerActivity::folders([this, alive](const std::string& path) {
        if (!alive->load()) return;
        auto& lib = MediaLibrary::getInstance();
        if (lib.hasFolder(path)) {
            brls::Application::notify(i18n::tr("library.folder_exists", "该文件夹已在媒体库中"));
            return;
        }
        if (lib.addFolder(path)) {
            lib.save();
            brls::Application::notify(i18n::tr("library.folder_added_scanning", "已添加，正在扫描…"));
            startLibraryScan();
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                refresh();
            });
        } else {
            brls::Application::notify(i18n::tr("library.folder_add_failed", "添加失败：路径不可用或已存在"));
        }
    });
    brls::Application::pushActivity(picker);
}

void FileBrowserActivity::startLibraryScan() {
    auto& lib = MediaLibrary::getInstance();
    if (lib.isScanning()) {
        brls::Application::notify(i18n::tr("library.scan_running", "扫描已在进行中"));
        return;
    }

    // 后台线程持有活标志，避免活动销毁后回调访问悬垂 this
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    std::thread([this, alive]() {
        try {
            MediaLibrary::getInstance().rebuild(
                [](int done, int total, const std::string& cur) {
                    brls::Logger::info("扫描中 {}/{}: {}", done, total, cur);
                });
            brls::sync([this, alive]() {
                // 必须最先检查：活动销毁后 this 已悬垂，任何成员访问都是未定义行为
                if (!alive->load()) return;
                brls::Application::notify(i18n::tr("library.scan_complete", "媒体库扫描完成"));
                refresh();
            });
        } catch (const std::exception& e) {
            brls::Logger::error("startLibraryScan thread error: {}", e.what());
        } catch (...) {
            brls::Logger::error("startLibraryScan thread unknown error");
        }
    }).detach();
}

void FileBrowserActivity::showFolderOptions(const std::string& path, const std::string& name) {
    brls::Dialog* dialog = new brls::Dialog(i18n::tr("library.folder_options_title", "媒体文件夹：") + name + "\n" + path);
    dialog->addButton(i18n::tr("common.cancel", "取消"), []() {});
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);
    dialog->addButton(i18n::tr("library.remove_folder", "移除该文件夹"), [this, alive, path]() {
        if (MediaLibrary::getInstance().removeFolder(path)) {
            MediaLibrary::getInstance().save();
            brls::Application::notify(i18n::tr("library.folder_removed", "已移除该文件夹"));
            if (alive->load()) {
                std::string prefix = path;
                if (!prefix.empty() && prefix.back() != '/') prefix += '/';
                if (open_folder_ == path || open_folder_.rfind(prefix, 0) == 0) {
                    open_folder_.clear();
                }
                this->refresh();
            }
        }
    });
    dialog->open();
}


brls::View* FileBrowserActivity::createRecentView() {
    auto* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setMarginRight(CONTENT_SAFE_MARGIN);
    styleScrollingFrame(scroller);

    recent_slot_ = DynamicBox::makeSlot();
    recent_list_ = new DynamicBox(recent_slot_);
    *recent_slot_ = recent_list_;
    recent_list_->setAxis(brls::Axis::COLUMN);
    recent_list_->setPaddingBottom(28.0f);

    renderRecentContent();

    scroller->setContentView(recent_list_);
    return scroller;
}

void FileBrowserActivity::renderRecentContent() {
    if (recent_slot_) recent_list_ = *recent_slot_;
    if (!recent_list_) return;
    recent_list_->clearViews();

    recent_list_->addView(new SectionHeader(i18n::tr("history.title", "历史")));

    auto recents = PlaybackHistory::getInstance().getRecentList(30);
    if (recents.empty()) {
        recent_list_->addView(new EmptyState(
            ui::Icon::Clock, i18n::tr("history.empty_title", "暂无播放记录"),
            i18n::tr("history.empty_desc", "播放过的视频和音频会显示在这里")));
        return;
    }

    std::vector<std::string> thumb_paths_to_request;
    for (const auto& rec : recents) {
        fs::path p(rec.path);
        MediaItem item;
        item.path = rec.path;
        item.name = p.filename().string();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (MediaScanner::isSupportedAudioFormat(ext)) {
            item.type = MediaType::Audio;
        } else {
            item.type = MediaType::Video;
        }

        std::string title = item.name;
        std::string subtitle = p.parent_path().filename().string();
        if (auto meta = MetadataStore::getInstance().get(item.path); meta && !meta->is_empty()) {
            if (!meta->title.empty()) title = meta->title;
            if (!meta->release_date.empty()) subtitle = meta->release_date.substr(0, 4) + " · " + subtitle;
        }

        auto* cell = new MediaCell(
            item.is_audio() ? ui::Icon::Music : ui::Icon::Video,
            title,
            subtitle);

        if (item.is_video()) {
            cell->setThumbnail(sceneSpecFor(item));
            if (!ThumbnailCache::getInstance().has(item.path)) {
                thumb_paths_to_request.push_back(item.path);
            }
        }

        std::string badge;
        if (rec.completed) {
            badge = i18n::tr("common.played", "已看完");
            cell->setProgress(1.0f);
        } else if (rec.duration > 0) {
            const float ratio = (float)((double)rec.position / rec.duration);
            cell->setProgress(ratio);
            badge = MediaScanner::formatDuration(rec.position) + " / " +
                    MediaScanner::formatDuration(rec.duration);
        }
        if (!badge.empty()) cell->setBadge(badge);

        cell->registerClickAction([item](brls::View*) {
            if (item.is_audio()) {
                AudioPlayer::getInstance().playFile(item.path, item.name);
                brls::Application::pushActivity(new NowPlayingActivity());
            } else {
                std::vector<MediaItem> playlist = { item };
                brls::Application::pushActivity(new PlayerActivity(playlist, 0));
            }
            return true;
        });

        recent_list_->addView(cell);
    }

    if (!thumb_paths_to_request.empty() && ThumbnailCache::getInstance().enabled()) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        ThumbnailCache::getInstance().requestBatch(thumb_paths_to_request, [this, alive]() {
            if (!alive->load()) return;
            renderRecentContent();
        });
    }
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
        frame_->setTitle("Yiuf Player");
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
            i18n::tr("scraper.not_installed",
                "尚未安装刮削器插件。\n\n"
                "刮削器可为本地影片自动补齐封面、简介与评分。\n"
                "请到【设置 → 插件管理】安装 .so / .dylib / .js 刮削插件。"));
        d->addButton(i18n::tr("common.understood", "知道了"), []() {});
        d->open();
        return;
    }

    std::string query;
    int64_t year = 0;
    MediaNameParser::parse(filename, query, year);

    brls::Application::notify(i18n::tr("scraper.searching", "正在搜索：") + query);

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
                    i18n::tr("scraper.not_found", "未找到匹配的影片信息。\n\n搜索词：") + query +
                    i18n::tr("scraper.not_found_hint", "\n\n可尝试重命名文件使其更接近正式片名。"));
                d->addButton(i18n::tr("common.understood", "知道了"), []() {});
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

            std::string body = i18n::tr("media.meta_title", "标题：") + meta.title + "\n";
            if (!meta.release_date.empty()) body += i18n::tr("media.meta_release", "上映：") + meta.release_date + "\n";
            if (meta.runtime_minutes > 0)
                body += i18n::tr("media.meta_runtime", "片长：") + std::to_string(meta.runtime_minutes) + " " + i18n::tr("common.minutes", "分钟") + "\n";
            if (!meta.genres.empty()) body += i18n::tr("media.meta_genres", "类型：") + meta.genres + "\n";
            if (meta.rating > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f / 10\n", meta.rating);
                body += i18n::tr("media.meta_rating", "评分：") + buf;
            }
            if (!meta.overview.empty()) body += "\n" + meta.overview;

            brls::Dialog* d = new brls::Dialog(body);
            d->addButton(i18n::tr("common.done", "完成"), [this]() {
                brls::Application::notify(i18n::tr("scraper.saved", "已保存影片信息"));
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
        body = i18n::tr("media.meta_title", "标题：") + meta->title + "\n";
        if (!meta->release_date.empty()) body += i18n::tr("media.meta_release", "上映：") + meta->release_date + "\n";
        if (meta->runtime_minutes > 0)
            body += i18n::tr("media.meta_runtime", "片长：") + std::to_string(meta->runtime_minutes) + " " + i18n::tr("common.minutes", "分钟") + "\n";
        if (!meta->genres.empty()) body += i18n::tr("media.meta_genres", "类型：") + meta->genres + "\n";
        if (meta->rating > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f / 10\n", meta->rating);
            body += i18n::tr("media.meta_rating", "评分：") + buf;
        }
        if (!meta->overview.empty()) body += "\n" + meta->overview;
    } else {
        body = filename + "\n\n" + i18n::tr("media.no_info", "尚未获取影片信息。\n可点击下方按钮发起刮削。");
    }
    brls::Dialog* d = new brls::Dialog(body);
    if (!meta || meta->is_empty()) {
        d->addButton(i18n::tr("scraper.scrape_now", "立即刮削"), [this, path, filename]() {
            this->scrapeItem(path, filename);
        });
    } else {
        d->addButton(i18n::tr("scraper.rescrape", "重新刮削"), [this, path, filename]() {
            this->scrapeItem(path, filename);
        });
    }
    d->addButton(i18n::tr("common.close", "关闭"), []() {});
    d->open();
}

void FileBrowserActivity::refresh() {
    renderSourcesContent();
    renderLibraryContent();
    renderSettingsContent();
    renderRecentContent();
}

} // namespace muvik
