#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "activity/plugin_browser_activity.hpp"
#include "activity/player_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include <borealis/views/dialog.hpp>
#include <atomic>
#include <memory>
#include <thread>

namespace muvik {

PluginBrowserActivity::PluginBrowserActivity(std::string plugin_path,
                                             std::string plugin_name)
    : plugin_path_(std::move(plugin_path)),
      plugin_name_(std::move(plugin_name)) {}

PluginBrowserActivity::~PluginBrowserActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* PluginBrowserActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setMarginRight(28.0f);
    styleScrollingFrame(scroller_);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle(plugin_name_);
    return frame_;
}

void PluginBrowserActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 深链/测试：从指定路径开始浏览
    if (!initial_path_.empty()) {
        current_path_ = initial_path_;
        initial_path_.clear();
    }

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (!history_.empty()) {
            navigateUp();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });

    // 刷新当前目录
    this->registerAction("刷新", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        render();
        return true;
    });

    render();
}

std::string PluginBrowserActivity::currentTitle() const {
    std::string base = plugin_name_;
    if (current_path_.empty() || current_path_ == "/") return base;
    // 取路径最后一段
    std::string p = current_path_;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    std::string seg = (slash == std::string::npos) ? p : p.substr(slash + 1);
    return seg.empty() ? base : base + " / " + seg;
}

void PluginBrowserActivity::navigateTo(const std::string& path) {
    if (!current_path_.empty()) history_.push_back(current_path_);
    current_path_ = path;
    render();
}

void PluginBrowserActivity::navigateUp() {
    // 若当前在子目录，返回上一级
    if (current_path_.empty() || current_path_ == "/") {
        brls::Application::popActivity();
        return;
    }
    // 优先用历史记录（保持用户实际浏览路径）
    if (!history_.empty()) {
        current_path_ = history_.back();
        history_.pop_back();
        render();
        return;
    }
    // 无历史时按路径截断
    std::string p = current_path_;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        current_path_ = initial_path_.empty() ? "/" : initial_path_;
    } else {
        current_path_ = p.substr(0, slash);
    }
    render();
}

void PluginBrowserActivity::render() {
    if (!list_ || !frame_) return;

    frame_->setTitle(currentTitle());
    list_->clearViews();
    loading_ = true;

    // 加载提示
    list_->addView(new EmptyState(ui::Icon::Plugin, "正在从插件读取目录…",
                                  "网络较慢时可能需要几秒"));

    // 网络请求放到后台，避免阻塞 UI
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    std::string path = current_path_;
    std::string plugin = plugin_path_;

    std::thread([this, alive, path, plugin]() {
        std::vector<MediaItem> items;
        bool ok = PluginManager::getInstance().listVideoSourceItems(plugin, path, items);

        brls::sync([this, alive, ok, items, path]() {
            if (!alive->load()) return;   // 活动已销毁
            if (!list_ || !frame_) return;

            loading_ = false;
            list_->clearViews();

            if (!ok) {
                list_->addView(new EmptyState(
                    ui::Icon::Error, "无法读取该目录",
                    "首次使用请先在「插件管理」里配置服务器地址\n也可以按 Y 重试"));
                return;
            }

            // 返回上一级
            if (!path.empty() && path != "/") {
                auto* up = new MediaCell(ui::Icon::Back, "..");
                up->setChevron(true);
                up->registerClickAction([this](brls::View*) {
                    navigateUp();
                    return true;
                });
                list_->addView(up);
            }

            // 分目录与可播放条目
            std::vector<MediaItem> dirs, playable;
            for (const auto& it : items) {
                // 注意：插件可能返回 Unknown 类型，需显式判断
                if (it.is_directory()) dirs.push_back(it);
                else if (it.is_video() || it.is_audio()) playable.push_back(it);
            }

            if (dirs.empty() && playable.empty()) {
                list_->addView(new EmptyState(ui::Icon::Folder, "这个目录是空的",
                                              "没有找到可播放的媒体"));
                return;
            }

            if (!dirs.empty()) {
                list_->addView(new SectionHeader(
                    "文件夹 · " + std::to_string(dirs.size())));
                for (const auto& d : dirs) {
                    auto* cell = new MediaCell(ui::Icon::Folder, d.name, "文件夹");
                    cell->setChevron(true);
                    std::string target = d.path;
                    cell->registerClickAction([this, target](brls::View*) {
                        navigateTo(target);
                        return true;
                    });
                    list_->addView(cell);
                }
            }

            if (!playable.empty()) {
                list_->addView(new SectionHeader(
                    "媒体 · " + std::to_string(playable.size())));
                for (size_t i = 0; i < playable.size(); ++i) {
                    const bool is_video = playable[i].is_video();
                    auto* cell = new MediaCell(
                        is_video ? ui::Icon::Video : ui::Icon::Music,
                        playable[i].name,
                        is_video ? "视频" : "音频");
                    size_t idx = i;
                    std::vector<MediaItem> playlist = playable;
                    cell->registerClickAction([playlist, idx](brls::View*) {
                        brls::Application::pushActivity(
                            new PlayerActivity(playlist, idx));
                        return true;
                    });
                    list_->addView(cell);
                }
            }
            list_->setPaddingBottom(28.0f);
        });
    }).detach();
}

} // namespace muvik
