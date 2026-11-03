#pragma once

#include <functional>
#include <vector>
#include <borealis.hpp>
#include <string>
#include <vector>
#include <functional>
#include "view/mpv_player.hpp"

namespace deck_player {

class TrackSelectionDialog {
public:
    // 字幕选择面板：区分内嵌 / 外挂 / 图形字幕，支持延时微调与加载外部字幕文件
    static void showSubtitleDialog(
        const std::vector<TrackInfo>& sub_tracks,
        float current_delay,
        std::function<void(int track_id)> on_select_track,
        std::function<void(float delay)> on_adjust_delay,
        std::function<void()> on_load_external = nullptr
    );

    // 音轨选择面板（支持中/日/英等多语言音频切换）
    static void showAudioDialog(
        const std::vector<TrackInfo>& audio_tracks,
        std::function<void(int track_id)> on_select_track
    );
};

} // namespace deck_player
