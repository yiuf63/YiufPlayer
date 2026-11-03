#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "model/media_item.hpp"
#include <mpv/client.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

namespace deck_player {

// 全局音乐播放引擎。
//
// 与 MPVPlayerView 的关键区别：本引擎的生命周期与界面无关。用户离开播放器界面、
// 甚至回到媒体库浏览时，音乐仍在后台继续解码播放。这是「后台播放」的实现基础。
//
// 采用独立的 mpv_handle 且不创建 render context（vid=no），因此不占用 GL 资源，
// 也不会与视频播放器的渲染上下文冲突。
class AudioPlayer {
public:
    struct State {
        std::string path;
        std::string title;      // 展示用标题（默认取文件名去扩展名）
        std::string artist;     // 从内嵌元数据读取（若存在）
        int64_t position = 0;   // 秒
        int64_t duration = 0;   // 秒
        bool playing = false;
        bool ended = false;
        double speed = 1.0;
        int volume = 100;
    };

    using StateCallback = std::function<void(const State&)>;

    static AudioPlayer& getInstance();

    // 播放单个文件（会替换当前曲目）
    bool playFile(const std::string& path, const std::string& title = "");

    // 以播放列表方式播放，index 为起始曲目
    void playPlaylist(const std::vector<MediaItem>& items, size_t index);

    // 播放 / 暂停 / 切换
    void togglePlayPause();
    void pause();
    void resume();
    void stop();
    void next();      // 列表中下一首
    void previous();  // 列表中上一首
    void seek(int64_t seconds_offset);
    void seekTo(int64_t target);
    void setVolume(int volume_0_100);
    void setSpeed(double speed);

    // 当前状态快照（线程安全）
    State getState() const;
    bool isActive() const;         // 是否正在播放或暂停中
    bool isPlaying() const;

    // 状态变化回调（在 UI 线程被调用，由 brls::sync 派发）
    void setStateCallback(StateCallback cb);

    // 每帧驱动：必须在主循环里调用，用于拉取 mpv 事件与派发回调
    void tick();

    // 关闭引擎（程序退出时调用）
    void shutdown();

private:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    void initMPV();
    void destroyMPV();
    void processEvents();
    void notifyState();
    void loadCurrent();
    std::string readTagLocked(const char* key);

    mpv_handle* mpv_ = nullptr;

    mutable std::mutex mutex_;
    State state_;
    std::vector<MediaItem> playlist_;
    size_t index_ = 0;

    StateCallback callback_;
    mutable std::mutex cb_mutex_;

    std::atomic<bool> active_{false};
    std::atomic<bool> shut_down_{false};  // shutdown 幂等标志
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point last_notify_;
};

} // namespace deck_player
