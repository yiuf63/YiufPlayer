#pragma once

#include <string>
#include <borealis.hpp>
#include "playback/audio_player.hpp"

namespace deck_player {

// 「正在播放」条：插入主界面的 AppletFrame 标题栏区域，
// 因此切换任何标签页、进入任何界面都能看到当前播放的音乐状态。
// 音乐本身由全局 AudioPlayer 持有，离开界面也不会中断，这才是后台播放。
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

    // 在指定 AppletFrame 上安装，返回实例（重复调用返回同一实例）
    static NowPlayingBar* install(brls::AppletFrame* frame);
    static NowPlayingBar* get() { return instance_; }

private:
    void buildUi();

    brls::Label* title_label_ = nullptr;
    brls::Label* time_label_ = nullptr;
    brls::Rectangle* progress_fill_ = nullptr;
    std::string last_title_;

    static inline NowPlayingBar* instance_ = nullptr;
};

} // namespace deck_player
