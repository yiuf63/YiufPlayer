#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "activity/player_activity.hpp"
#include "utils/playback_history.hpp"
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include <borealis/views/dialog.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <filesystem>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdio>

namespace deck_player {

PlayerActivity::PlayerActivity(const std::vector<MediaItem>& playlist, size_t initial_index)
    : playlist_(playlist), current_index_(initial_index) {}

PlayerActivity::~PlayerActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* PlayerActivity::createContentView() {
    player_view_ = new MPVPlayerView();
    player_view_->setGrow(1.0f);
    return player_view_;
}

void PlayerActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 播完自动切下一集
    player_view_->setOnFinishCallback([this]() {
        brls::Logger::info("播放结束: 当前第 {}/{} 集",
                           current_index_ + 1, playlist_.size());
        if (current_index_ + 1 < playlist_.size()) {
            current_index_++;
            brls::Logger::info("自动切换下一集 -> 第 {} 集", current_index_ + 1);
            playCurrentIndex();
        } else {
            // 已经是最后一集，退出播放器（回到文件浏览器）
            brls::Application::popActivity();
        }
    });

    // 手柄上一集/下一集切换
    player_view_->setOnPrevNextCallback([this](bool is_next) {
        if (is_next) {
            if (current_index_ + 1 < playlist_.size()) {
                current_index_++;
                playCurrentIndex();
            } else {
                brls::Application::notify("已经是最后一集了");
            }
        } else {
            if (current_index_ > 0) {
                current_index_--;
                playCurrentIndex();
            } else {
                brls::Application::notify("已经是第一集了");
            }
        }
    });

    // 播放列表（按 ↑ 打开）
    player_view_->setOnShowPlaylistCallback([this]() { showPlaylistDialog(); });

    brls::Logger::info("播放列表按键: ↑ (BUTTON_UP)");

    // 测试用：启动后自动打开播放列表，便于自动化验证对话框内容
    if (std::getenv("DECKPLAYER_OPEN_PLAYLIST")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                showPlaylistDialog();
            });
        }).detach();
    }

    playCurrentIndex();
}

void PlayerActivity::showPlaylistDialog() {
    brls::Logger::info("showPlaylistDialog: 列表 {} 个", playlist_.size());
    if (playlist_.empty()) {
        brls::Application::notify("播放列表为空");
        return;
    }

    brls::Box* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setWidth(680);

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setHeight(420);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPaddingBottom(20.0f);

    brls::Dialog* dialog = new brls::Dialog(container);
    list->addView(new SectionHeader("播放列表 · " +
                                    std::to_string(playlist_.size()) + " 项"));

    for (size_t i = 0; i < playlist_.size(); ++i) {
        const auto& item = playlist_[i];
        const bool is_current = (i == current_index_);

        // 详情：当前正在播放 / 是否有断点
        std::string detail;
        if (is_current) {
            detail = "正在播放";
        }
        HistoryRecord rec;
        if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
            if (rec.completed) {
                detail += detail.empty() ? "已看完" : " · 已看完";
            } else if (rec.position > 15) {
                const int mins = static_cast<int>(rec.position) / 60;
                const int secs = static_cast<int>(rec.position) % 60;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "上次看到 %d:%02d", mins, secs);
                detail += detail.empty() ? buf : std::string(" · ") + buf;
            }
        }

        // 不再拼接「序号. 」，因为文件名本身常带 01./02. 前缀，
        // 拼出来会变成「2. 02. Blade Runner 2049.mp4」。顺序由列表位置表达，
        // 当前项另有「播放中」badge。
        auto* cell = new MediaCell(ui::Icon::Video, item.name, detail);
        cell->setHeight(64.0f);
        cell->setBadge(is_current ? "播放中" : "");

        // 当前项：选中即关闭，不做重新加载（避免打断正在播放的内容）
        if (is_current) {
            cell->registerClickAction([dialog](brls::View*) {
                dialog->close();
                return true;
            });
        } else {
            cell->registerClickAction([this, dialog, i](brls::View*) {
                dialog->close();
                current_index_ = i;
                playCurrentIndex();
                return true;
            });
        }
        list->addView(cell);
    }

    scroller->setContentView(list);

    // 高度贴合内容：固定 420 时 3 集的列表会拖出一个大黑框
    float content_h = 0.0f;
    for (auto* child : list->getChildren()) content_h += child->getHeight();
    scroller->setHeight(std::min(420.0f, content_h + 24.0f));

    container->addView(scroller);

    dialog->addButton("关闭", []() {});
    dialog->open();
    brls::Logger::info("播放列表对话框已打开");
}

void PlayerActivity::playCurrentIndex() {
    if (current_index_ >= playlist_.size()) return;

    const auto& item = playlist_[current_index_];
    
    // 检查历史断点记录
    int64_t resume_pos = 0;
    HistoryRecord rec;
    if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
        if (!rec.completed && rec.position > 15) {
            resume_pos = rec.position;
        }
    }

    player_view_->openFile(item.path, item.name, resume_pos);
}

} // namespace deck_player
