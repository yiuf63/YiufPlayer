#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "utils/playback_history.hpp"
#include "utils/app_paths.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace muvik {

PlaybackHistory& PlaybackHistory::getInstance() {
    static PlaybackHistory instance;
    return instance;
}

PlaybackHistory::PlaybackHistory() {
    load();
}

std::string PlaybackHistory::getHistoryFilePath() const {
    return AppPaths::configFile("history.dat");
}

void PlaybackHistory::load() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    records_.clear();
    std::string filepath = getHistoryFilePath();
    if (!fs::exists(filepath)) return;

    std::ifstream infile(filepath);
    if (!infile.is_open()) return;

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string path;
        int64_t pos = 0, dur = 0, ts = 0;
        int sid = 1, aid = 1, comp = 0;

        // 格式: path|position|duration|timestamp|sid|aid|completed
        if (std::getline(iss, path, '|') &&
            (iss >> pos) && (iss.ignore(1, '|')) &&
            (iss >> dur) && (iss.ignore(1, '|')) &&
            (iss >> ts) && (iss.ignore(1, '|')) &&
            (iss >> sid) && (iss.ignore(1, '|')) &&
            (iss >> aid) && (iss.ignore(1, '|')) &&
            (iss >> comp)) {
            
            HistoryRecord rec;
            rec.path = path;
            rec.position = pos;
            rec.duration = dur;
            rec.timestamp = ts;
            rec.sid = sid;
            rec.aid = aid;
            rec.completed = (comp != 0);
            records_[path] = rec;
        }
    }
}

void PlaybackHistory::save() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string filepath = getHistoryFilePath();
    try {
        fs::create_directories(fs::path(filepath).parent_path());
    } catch (...) {}

    std::ofstream outfile(filepath, std::ios::trunc);
    if (!outfile.is_open()) return;

    outfile << "# Muvik Playback History\n";
    for (const auto& [path, rec] : records_) {
        outfile << rec.path << "|"
                << rec.position << "|"
                << rec.duration << "|"
                << rec.timestamp << "|"
                << rec.sid << "|"
                << rec.aid << "|"
                << (rec.completed ? 1 : 0) << "\n";
    }
}

void PlaybackHistory::updateProgress(const std::string& path, int64_t position, int64_t duration, int sid, int aid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (path.empty()) return;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // position <= 0 不代表「用户把进度拖回开头」，而几乎总是意味着
    // 「调用方还不知道当前位置」。
    //
    // 实测事故：MPVPlayerView::selectTrack() 在文件刚加载、time-pos 事件还没到时
    // 无条件落盘一次，把一条已经「看完（completed=1）」的记录覆盖成 0|0|...|0。
    // 表现是：一部标成「已看完」的片子，只要点开 1.5 秒再退出，
    // 徽标就变回「已看 0%」，断点续播点也一起消失，永远回不到上次位置。
    //
    // 这里做数据层兜底（调用点有 6 处，逐个加判断漏一个就复发）：
    // 收到 position<=0 时**不覆盖**已有的 position/duration/completed，
    // 只更新轨道选择（sid/aid）与时间戳 —— 因为 selectTrack() 真正想保存的
    // 就是轨道偏好。
    //
    // 没有旧记录时仍要建一条：否则「第一次打开就选字幕轨」的偏好无处可存，
    // 下次打开又回到第一条（这正是第 38 轮修过、很容易再次弄坏的「选过不记住」）。
    // 记录里 position/duration 保持 0，历史页不会显示进度条；
    // 真正的播放一开始就会把 time-pos 推上来，随后正常覆盖。
    if (position <= 0) {
        auto it = records_.find(path);
        if (it != records_.end()) {
            it->second.sid = sid;
            it->second.aid = aid;
            it->second.timestamp = now;
        } else {
            HistoryRecord fresh;
            fresh.path = path;
            fresh.position = 0;
            fresh.duration = 0;
            fresh.timestamp = now;
            fresh.sid = sid;
            fresh.aid = aid;
            fresh.completed = false;
            records_[path] = fresh;
        }
        save();
        return;
    }

    HistoryRecord rec;
    rec.path = path;
    rec.position = position;
    rec.duration = duration;
    rec.timestamp = now;
    rec.sid = sid;
    rec.aid = aid;
    // 如果播放到最后 95% 或者最后 30 秒，标记为已看完
    // 「已看完」判定：
    //   1) 播放到 95% 以上；或
    //   2) 距结尾不足 30 秒，且已播放超过一半。
    //
    // 第二个条件必须加「超过一半」的下限：否则 duration <= 30 秒的内容
    // （短视频、歌曲）在 position=1 时就会被判定为看完，
    // 导致它们永远不会断点续播（实测 30 秒音频播 1 秒即标记 completed）。
    const bool near_end = (position >= duration - 30) &&
                          (position >= duration / 2);
    rec.completed = (duration > 0 && (near_end || position >= duration * 0.95));

    records_[path] = rec;
    save();
}

bool PlaybackHistory::getRecord(const std::string& path, HistoryRecord& out_record) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = records_.find(path);
    if (it != records_.end()) {
        out_record = it->second;
        return true;
    }
    return false;
}

void PlaybackHistory::removeRecord(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (records_.erase(path) > 0) {
        save();
    }
}

std::vector<HistoryRecord> PlaybackHistory::getRecentList(size_t limit) const {
    // 必须加锁：本函数遍历 records_ 时，主循环（音频 tick / time-pos 回调）
    // 可能正在 updateProgress() 里写同一张 unordered_map。
    // 并发读一张正在 rehash 的哈希表会读到失效的桶指针，表现为偶发崩溃
    // 或历史页出现乱码条目。同类其它 5 个函数都有锁，唯独这里漏了 ——
    // 第 32 轮加锁时漏掉的这一处。
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<HistoryRecord> list;
    for (const auto& [_, rec] : records_) {
        list.push_back(rec);
    }
    std::sort(list.begin(), list.end(), [](const HistoryRecord& a, const HistoryRecord& b) {
        return a.timestamp > b.timestamp;
    });
    if (list.size() > limit) {
        list.resize(limit);
    }
    return list;
}

} // namespace muvik
