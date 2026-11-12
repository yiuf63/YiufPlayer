#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <borealis.hpp>
#include "playback/audio_player.hpp"
#include "ui/icons.hpp"

namespace muvik {

class NowPlayingView;

class NowPlayingActivity : public brls::Activity {
public:
    NowPlayingActivity();
    ~NowPlayingActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    NowPlayingView* view_ = nullptr;
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace muvik
