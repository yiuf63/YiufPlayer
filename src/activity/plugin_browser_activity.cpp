#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "activity/plugin_browser_activity.hpp"
#include "activity/player_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/dialog.hpp>
#include <atomic>
#include <memory>
#include <thread>

namespace deck_player {

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
    brls::Label* loading_label = new brls::Label();
    loading_label->setText("正在加载…");
    loading_label->setMarginTop(20);
    loading_label->setMarginLeft(20);
    list_->addView(loading_label);

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
                brls::Label* err = new brls::Label();
                err->setText("无法读取该目录。\n\n"
                             "如果这是首次使用，请先在【插件管理】里配置服务器地址。\n"
                             "也可以按 Y 重试。");
                err->setMarginTop(20);
                err->setMarginLeft(20);
                list_->addView(err);
                return;
            }

            // 返回上一级
            if (!path.empty() && path != "/") {
                brls::DetailCell* up = new brls::DetailCell();
                up->setText("⬆️ .. [返回上一级]");
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
                brls::Label* empty = new brls::Label();
                empty->setText("该目录下没有可播放的媒体。");
                empty->setMarginTop(20);
                empty->setMarginLeft(20);
                list_->addView(empty);
                return;
            }

            if (!dirs.empty()) {
                brls::Header* h = new brls::Header();
                h->setTitle("文件夹 (" + std::to_string(dirs.size()) + ")");
                list_->addView(h);
                for (const auto& d : dirs) {
                    brls::DetailCell* cell = new brls::DetailCell();
                    cell->setText("[文件夹] " + d.name);
                    std::string target = d.path;
                    cell->registerClickAction([this, target](brls::View*) {
                        navigateTo(target);
                        return true;
                    });
                    list_->addView(cell);
                }
            }

            if (!playable.empty()) {
                brls::Header* h = new brls::Header();
                h->setTitle("媒体 (" + std::to_string(playable.size()) + ")");
                list_->addView(h);
                for (size_t i = 0; i < playable.size(); ++i) {
                    brls::DetailCell* cell = new brls::DetailCell();
                    cell->setText((playable[i].is_video() ? "[视频] " : "[音乐] ") +
                                  playable[i].name);
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
        });
    }).detach();
}

} // namespace deck_player
