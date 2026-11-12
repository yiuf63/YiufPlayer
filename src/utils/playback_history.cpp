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
