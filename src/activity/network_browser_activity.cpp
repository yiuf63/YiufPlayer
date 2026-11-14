#include "activity/network_browser_activity.hpp"
#include "activity/player_activity.hpp"
#include "utils/webdav_client.hpp"
#include "view/media_cell.hpp"
#include "ui/design.hpp"
#include "ui/icons.hpp"

#include <thread>

namespace muvik {

NetworkBrowserActivity::NetworkBrowserActivity(NetworkSource source, std::string initial_path)
    : source_(std::move(source)), current_path_(std::move(initial_path)) {}

NetworkBrowserActivity::~NetworkBrowserActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
}

brls::View* NetworkBrowserActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);
    list_->setPadding(ui::space::xl, ui::space::xl,
                      ui::space::xl, ui::space::xl);
    list_->setGrow(1.0f);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setMarginRight(28.0f);
    styleScrollingFrame(scroll);
    scroll->setContentView(list_);

    frame_ = new brls::AppletFrame(scroll);
    frame_->setTitle(currentTitle());
    return frame_;
}

void NetworkBrowserActivity::onContentAvailable() {
    // 注册返回动作
    registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (!path_stack_.empty()) {
            navigateUp();
        } else {
            brls::Application::popActivity();
        }
        return true;
    });

    // 注册刷新动作
    registerAction("刷新", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        if (!loading_) render();
        return true;
    });

    render();
}

std::string NetworkBrowserActivity::currentTitle() const {
    std::string prefix = "[" + source_.typeString() + "] " + source_.name;
    if (current_path_.empty() || current_path_ == "/") {
        return prefix;
    }
    std::string decoded = WebDAVClient::urlDecode(current_path_);
    return prefix + " " + decoded;
}

void NetworkBrowserActivity::navigateTo(const std::string& path) {
    path_stack_.push_back(current_path_);
    current_path_ = path;
    render();
}

void NetworkBrowserActivity::navigateUp() {
    if (!path_stack_.empty()) {
        current_path_ = path_stack_.back();
        path_stack_.pop_back();
    } else {
        current_path_.clear();
    }
    render();
}

void NetworkBrowserActivity::render() {
    if (!list_ || !frame_) return;

    frame_->setTitle(currentTitle());
    list_->clearViews();
    loading_ = true;

    // 加载提示
    list_->addView(new EmptyState(ui::Icon::Drive, "正在连接 NAS 并读取目录…",
                                  source_.url + "\n按 Y 键可随时重新刷新"));

    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    NetworkSource src_copy = source_;
    std::string path_copy = current_path_;

    std::thread([this, alive, src_copy, path_copy]() {
        std::vector<MediaItem> items;
        std::string err_msg;
        bool ok = WebDAVClient::listDirectory(src_copy, path_copy, items, err_msg);

        brls::sync([this, alive, ok, items, err_msg, path_copy]() {
            if (!alive->load()) return;
            if (!list_ || !frame_) return;

            loading_ = false;
            list_->clearViews();

            if (!ok) {
                list_->addView(new EmptyState(
                    ui::Icon::Error, "无法访问此目录",
                    err_msg.empty() ? "请检查 NAS 网络是否通畅或密码是否正确" : err_msg));
                return;
            }

            // 返回上一级
            if (!path_stack_.empty() || (!path_copy.empty() && path_copy != "/")) {
                auto* up = new MediaCell(ui::Icon::Back, "..");
                up->setChevron(true);
                up->registerClickAction([this](brls::View*) {
                    navigateUp();
                    return true;
                });
                list_->addView(up);
            }

            // 分流文件夹与媒体条目
            std::vector<MediaItem> dirs;
            std::vector<MediaItem> playable;
            for (const auto& it : items) {
                if (it.is_directory()) dirs.push_back(it);
                else if (it.is_video() || it.is_audio()) playable.push_back(it);
            }

            if (dirs.empty() && playable.empty()) {
                list_->addView(new EmptyState(ui::Icon::Folder, "该目录为空",
                                              "未在此文件夹下找到视频或音频文件"));
                return;
            }

            // 文件夹部分
            if (!dirs.empty()) {
                list_->addView(new SectionHeader("文件夹 · " + std::to_string(dirs.size())));
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

            // 媒体文件部分
            if (!playable.empty()) {
                list_->addView(new SectionHeader("媒体 · " + std::to_string(playable.size())));

                // 为播放列表准备串流 URL
                std::vector<MediaItem> playlist = playable;
                for (auto& p_item : playlist) {
                    p_item.path = WebDAVClient::buildPlayUrl(source_, p_item.path);
                }

                for (size_t i = 0; i < playable.size(); ++i) {
                    const bool is_video = playable[i].is_video();
                    std::string subtitle = is_video ? "视频" : "音频";
                    if (playable[i].size > 0) {
                        double mb = playable[i].size / (1024.0 * 1024.0);
                        if (mb >= 1024.0) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), " · %.2f GB", mb / 1024.0);
                            subtitle += buf;
                        } else {
                            char buf[32];
                            snprintf(buf, sizeof(buf), " · %.1f MB", mb);
                            subtitle += buf;
                        }
                    }

                    auto* cell = new MediaCell(
                        is_video ? ui::Icon::Video : ui::Icon::Music,
                        playable[i].name, subtitle);
                    size_t idx = i;
                    std::vector<MediaItem> pl = playlist;
                    cell->registerClickAction([pl, idx](brls::View*) {
                        brls::Application::pushActivity(new PlayerActivity(pl, idx));
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
