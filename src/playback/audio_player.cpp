#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include "playback/audio_player.hpp"
#include "utils/playback_history.hpp"
#include "playback/background_playback.hpp"
#include <borealis/core/thread.hpp>
#include <borealis/core/logger.hpp>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace muvik {

AudioPlayer& AudioPlayer::getInstance() {
    static AudioPlayer instance;
    return instance;
}

AudioPlayer::AudioPlayer() {
    initMPV();
}

AudioPlayer::~AudioPlayer() {
    // 注意：不在析构里做任何依赖其它静态对象的工作。
    // PlaybackHistory 与 AudioPlayer 都是函数内静态对象，析构顺序不确定；
    // 若在此访问已析构的 PlaybackHistory 会导致崩溃
    // （实测 EXIT_BAD_ACCESS，位于 unordered_map 访问处）。
    // 进度保存由 main() 显式调用的 shutdown() 负责。
    destroyMPV();
}

void AudioPlayer::initMPV() {
    mpv_ = mpv_create();
    if (!mpv_) return;

    // 纯音频：不启用视频输出，也不创建 GL 渲染上下文，因此可以被安全地
    // 放在后台，且不会与视频播放器争夺 OpenGL 上下文。
    mpv_set_option_string(mpv_, "vid", "no");
    if (std::getenv("MUVIK_MUTE")) {
        mpv_set_option_string(mpv_, "mute", "yes");
        mpv_set_option_string(mpv_, "ao", "null");
        brls::Logger::info("AudioPlayer: 静音模式已开启（MUVIK_MUTE）");
    } else {
#if defined(__APPLE__)
        // macOS 上显式指定 coreaudio：mpv 的自动探测在部分机器上会先试
        // 别的后端再回退，导致启动时有短暂无声。
        mpv_set_option_string(mpv_, "ao", "coreaudio");
#elif defined(ANDROID)
        // Android 必须用 OpenSL ES（或 AAudio）。**不要**在这里写死具体名字，
        // 让 mpv 自己选：不同 Android 版本可用的后端不同（audiotrack /
        // opensles / aaudio），写死会在部分设备上直接没有声音。
        //
        // 曾经写死 "coreaudio"（macOS 专属），导致 Android 上音频初始化失败、
        // 界面里出现的是 mpv 探测到的**系统音频设备名**（如外接显示器），
        // 而不是任何有意义的内容。
        mpv_set_option_string(mpv_, "ao", "audiotrack,opensles,aaudio");
#else
        // Linux / SteamOS：pulse → pipewire → alsa 依次回退。
        // 不写死单一后端，掌机上 pulse 经常不可用（无桌面会话）。
        mpv_set_option_string(mpv_, "ao", "pulse,pipewire,alsa");
#endif
    }
    mpv_set_option_string(mpv_, "keep-open", "no");
    mpv_set_option_string(mpv_, "idle", "yes");        // 空闲时保持实例存活，便于连续播放
    mpv_set_option_string(mpv_, "gapless-audio", "yes");
    mpv_set_option_string(mpv_, "volume", "100");

    if (mpv_initialize(mpv_) < 0) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        brls::Logger::error("AudioPlayer: mpv 初始化失败");
        return;
    }

    // 订阅属性，供状态栏与后台播放 UI 使用
    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "media-title", MPV_FORMAT_STRING);
}

void AudioPlayer::destroyMPV() {
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

void AudioPlayer::shutdown() {
    if (shut_down_.exchange(true)) {
        return;  // 幂等：重复调用直接返回，避免重复访问静态对象
    }

    // 退出前保存进度
    State s = getState();
    if (!s.path.empty() && s.position > 0) {
        PlaybackHistory::getInstance().updateProgress(
            s.path, s.position, s.duration);
    }
    destroyMPV();
    if (active_.load()) BackgroundPlayback::end();
    active_.store(false);
}

bool AudioPlayer::playFile(const std::string& path, const std::string& title) {
    if (!mpv_ || path.empty()) return false;

    MediaItem item;
    item.path = path;
    item.name = title.empty() ? fs::path(path).filename().string() : title;
    item.type = MediaType::Audio;

    std::vector<MediaItem> single{item};
    playPlaylist(single, 0);
    return true;
}

std::string AudioPlayer::readTagLocked(const char* key) {
    if (!mpv_) return "";
    char* value = mpv_get_property_string(mpv_, key);
    std::string out;
    if (value) {
        out = value;
        mpv_free(value);
    }
    return out;
}

void AudioPlayer::playPlaylist(const std::vector<MediaItem>& items, size_t index) {
    if (items.empty() || !mpv_) return;
    if (index >= items.size()) index = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playlist_ = items;
        index_ = index;
    }
    active_.store(true);
    BackgroundPlayback::begin();  // 阻止系统在后台降频
    loadCurrent();
}

void AudioPlayer::loadCurrent() {
    // 防御：shutdown 后仍可能有排队的调用
    if (!mpv_) return;

    MediaItem item;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (playlist_.empty()) return;
        if (index_ >= playlist_.size()) index_ = 0; // 防越界
        item = playlist_[index_];
    }

    const char* cmd[] = {"loadfile", item.path.c_str(), "replace", nullptr};
    mpv_command(mpv_, cmd);

    // 恢复断点
    int64_t resume = 0;
    HistoryRecord rec;
    if (PlaybackHistory::getInstance().getRecord(item.path, rec)) {
        if (canResume(rec)) resume = rec.position;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.path = item.path;
        state_.title = item.name;
        // 去掉扩展名作为标题
        auto dot = state_.title.find_last_of('.');
        if (dot != std::string::npos && dot > 0) state_.title = state_.title.substr(0, dot);
        state_.position = resume;
        state_.duration = 0;
        state_.playing = true;
        state_.ended = false;
    }

    if (resume > 0) {
        std::string t = std::to_string(resume);
        const char* seek_cmd[] = {"seek", t.c_str(), "absolute", nullptr};
        mpv_command(mpv_, seek_cmd);
    }

    notifyState();
}

void AudioPlayer::togglePlayPause() {
    if (!mpv_ || !active_.load()) return;
    int paused = 0;
    mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &paused);
    int new_val = paused ? 0 : 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &new_val);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.playing = (new_val == 0);
    }
    notifyState();
}

void AudioPlayer::pause() {
    if (!mpv_) return;
    int v = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &v);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.playing = false;
    }
    notifyState();
}

void AudioPlayer::resume() {
    if (!mpv_) return;
    int v = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &v);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.playing = true;
    }
    notifyState();
}

void AudioPlayer::stop() {
    State s = getState();
    if (!s.path.empty() && s.position > 0) {
        PlaybackHistory::getInstance().updateProgress(s.path, s.position, s.duration);
    }
    if (mpv_) {
        const char* cmd[] = {"stop", nullptr};
        mpv_command(mpv_, cmd);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State();
        playlist_.clear();
        index_ = 0;
    }
    active_.store(false);
    BackgroundPlayback::end();
    notifyState();
}

void AudioPlayer::next() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (playlist_.empty()) return;
        if (index_ + 1 >= playlist_.size()) return;
        index_++;
    }
    loadCurrent();
}

void AudioPlayer::previous() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (playlist_.empty()) return;
        if (index_ == 0) return;
        index_--;
    }
    loadCurrent();
}

void AudioPlayer::seek(int64_t seconds_offset) {
    if (!mpv_) return;
    std::string s = (seconds_offset > 0 ? "+" : "") + std::to_string(seconds_offset);
    const char* cmd[] = {"seek", s.c_str(), "relative", nullptr};
    mpv_command(mpv_, cmd);
}

void AudioPlayer::seekTo(int64_t target) {
    if (!mpv_) return;
    if (target < 0) target = 0;
    std::string s = std::to_string(target);
    const char* cmd[] = {"seek", s.c_str(), "absolute", nullptr};
    mpv_command(mpv_, cmd);
}

void AudioPlayer::setVolume(int volume) {
    int v = std::clamp(volume, 0, 100);
    if (mpv_) {
        double dv = v;
        mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &dv);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.volume = v;
    }
    notifyState();
}

void AudioPlayer::setSpeed(double speed) {
    if (mpv_) {
        double s = speed;
        mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &s);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.speed = speed;
    }
    notifyState();
}

AudioPlayer::State AudioPlayer::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool AudioPlayer::isActive() const {
    return active_.load();
}

bool AudioPlayer::isPlaying() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_.load() && state_.playing;
}

void AudioPlayer::setStateCallback(StateCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    callback_ = std::move(cb);
}

void AudioPlayer::notifyState() {
    // 节流：避免每帧都触发回调
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_notify_).count() < 200) {
        return;
    }
    last_notify_ = now;

    StateCallback cb;
    {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cb = callback_;
    }
    if (cb) {
        State s = getState();
        brls::sync([cb, s]() { cb(s); });
    }
}

void AudioPlayer::processEvents() {
    if (!mpv_) return;

    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) break;

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property* prop = (mpv_event_property*)event->data;
                if (!prop->name || !prop->data) break;
                std::lock_guard<std::mutex> lock(mutex_);
                if (!std::strcmp(prop->name, "time-pos")) {
                    state_.position = (int64_t)*(double*)prop->data;
                } else if (!std::strcmp(prop->name, "duration")) {
                    state_.duration = (int64_t)*(double*)prop->data;
                } else if (!std::strcmp(prop->name, "pause")) {
                    state_.playing = (*(int*)prop->data == 0);
                } else if (!std::strcmp(prop->name, "media-title")) {
                    const char* t = *(char**)prop->data;
                    if (t && *t) state_.title = t;
                }
                break;
            }
            case MPV_EVENT_END_FILE: {
                // 自动连播下一首
                bool advanced = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (index_ + 1 < playlist_.size()) {
                        index_++;
                        advanced = true;
                    } else {
                        state_.playing = false;
                        state_.ended = true;
                    }
                }
                if (advanced) loadCurrent();
                break;
            }
            default:
                break;
        }
    }
}

void AudioPlayer::tick() {
    if (!mpv_) return;
    processEvents();

    // 周期性输出播放状态，便于在无音频设备的环境下验证解码确实在推进
    {
        static int frames = 0;
        if (++frames % 120 == 0 && active_.load()) {
            State s = getState();
            brls::Logger::info("音频状态: pos={}s dur={}s playing={} title={}",
                               s.position, s.duration,
                               s.playing ? "是" : "否", s.title);
        }
    }

    // 周期性保存进度，防止异常退出丢失
    static auto last_save = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (active_.load() &&
        std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count() >= 10) {
        last_save = now;
        State s = getState();
        if (!s.path.empty() && s.position > 0) {
            PlaybackHistory::getInstance().updateProgress(s.path, s.position, s.duration);
        }
    }
}

} // namespace muvik
