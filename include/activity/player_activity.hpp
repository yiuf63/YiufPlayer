#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <borealis.hpp>
#include "view/mpv_player.hpp"
#include "model/media_item.hpp"
#include <vector>
#include <memory>
#include <atomic>

namespace muvik {

class PlayerActivity : public brls::Activity {
public:
    PlayerActivity(const std::vector<MediaItem>& playlist, size_t initial_index);
    ~PlayerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void playCurrentIndex();
    void showPlaylistDialog();

    std::vector<MediaItem> playlist_;
    size_t current_index_ = 0;
    MPVPlayerView* player_view_ = nullptr;
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace muvik
