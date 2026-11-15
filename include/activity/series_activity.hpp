#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <borealis.hpp>
#include "model/series_detector.hpp"

namespace muvik {

class ToolButton;

// 剧集专属详情与选集页面 (Kodi / Infuse 风格)
class SeriesActivity : public brls::Activity {
public:
    explicit SeriesActivity(SeriesGroup series);
    ~SeriesActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void refreshEpisodeList();
    void playEpisode(size_t index);

    SeriesGroup series_;
    ToolButton* play_btn_ = nullptr;
    brls::Box* episode_list_ = nullptr;
    brls::AppletFrame* frame_ = nullptr;
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace muvik
