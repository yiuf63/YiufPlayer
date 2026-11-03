#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <string>
#include <vector>   // getRecentList 的返回类型；
            // libstdc++ 不像 libc++ 那样间接包含，必须显式写
#include <unordered_map>
#include <chrono>

namespace deck_player {

struct HistoryRecord {
    std::string path;
    int64_t position = 0;   // 播放进度（秒）
    int64_t duration = 0;   // 总时长（秒）
    int64_t timestamp = 0;  // 上次播放时间戳
    int sid = 1;            // 上次选中的字幕轨
    int aid = 1;            // 上次选中的音轨
    bool completed = false; // 是否已看完
};

class PlaybackHistory {
public:
    static PlaybackHistory& getInstance();

    // 加载历史记录
    void load();

    // 保存历史记录到磁盘
    void save();

    // 更新某文件的播放进度
    void updateProgress(const std::string& path, int64_t position, int64_t duration, int sid = 1, int aid = 1);

    // 获取某文件的历史记录
    bool getRecord(const std::string& path, HistoryRecord& out_record) const;

    // 清除某文件的记录
    void removeRecord(const std::string& path);

    // 获取所有最近播放记录（按时间倒序）
    std::vector<HistoryRecord> getRecentList(size_t limit = 20) const;

private:
    PlaybackHistory();
    std::string getHistoryFilePath() const;

    // 并发保护：updateProgress 由主循环（音频 tick / 播放进度）调用，
    // getRecord / getRecentList 由 UI 线程调用，两者会同时访问这张表。
    // 早期没有加锁，属于数据竞争（unordered_map 并发写会破坏内部链表）。
    // 必须用 recursive_mutex：updateProgress() 持锁时会调用 save()，
    // 而 save() 也要加锁。普通 mutex 会自死锁（实测会直接卡住）。
    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::string, HistoryRecord> records_;
};

} // namespace deck_player
