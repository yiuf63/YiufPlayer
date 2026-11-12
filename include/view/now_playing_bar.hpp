#pragma once

#include <string>
#include <borealis.hpp>
#include "playback/audio_player.hpp"
#include "ui/icons.hpp"

namespace muvik {

// 「正在播放」条：贴在主界面底部，切换任何标签页都能看到音乐状态。
// 音乐本身由全局 AudioPlayer 持有，离开界面也不会中断，这才是后台播放。
//
// 视觉：整条自绘（不再拼 Label + Rectangle）。
//   [♪ 图标块]  标题 / 播放状态       01:23 / 04:00
//   ──────────────────────────────────────────  2px 进度线贴底
// 这样它与列表卡片、播放器 OSD 是同一套语言：图标块 + 两级文字 + 细进度线。
class NowPlayingBar : public brls::Box {
public:
    explicit NowPlayingBar(brls::AppletFrame* frame);
    // 必须清掉单例指针：AudioPlayer 的状态回调是全局的，
    // Activity 销毁后仍会调用 NowPlayingBar::get()->refresh()，
    // 若这里不置空就是对已释放内存的写入。
    ~NowPlayingBar() override {
        if (instance_ == this) instance_ = nullptr;
    }

    // 依据引擎状态刷新显示
    void refresh(const AudioPlayer::State& state);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    // 在指定 AppletFrame 上安装，返回实例（重复调用返回同一实例）
    static NowPlayingBar* install(brls::AppletFrame* frame);
    static NowPlayingBar* get() { return instance_; }

private:
    std::string title_;
    std::string status_;
    std::string time_text_;
    float progress_ = 0.0f;
    bool playing_ = false;
    ui::Icon icon_ = ui::Icon::Music;

    static inline NowPlayingBar* instance_ = nullptr;
};

} // namespace muvik
