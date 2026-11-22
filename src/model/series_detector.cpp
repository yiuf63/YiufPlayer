#include "model/series_detector.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <unordered_map>
#include <vector>
#include <string>
#include "utils/playback_history.hpp"

namespace fs = std::filesystem;

namespace muvik {

namespace {

// 清理标题中的多余技术标记与标点
std::string cleanSeriesTitle(const std::string& raw) {
    std::string s = raw;
    // 移除两端多余空格
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '-' || s.back() == '_')) s.pop_back();

    // 针对 "S04 数码宝贝无限地带（日国粤）" -> 提取核心剧名
    std::regex season_prefix(R"(^[Ss](\d{1,2})[\s\._\-]*)");
    s = std::regex_replace(s, season_prefix, "");

    // 移除末尾常见的括号标注如 "（日国粤）", "(1080p)" 等
    auto stripBracket = [&](const std::string& open_b, const std::string& close_b) {
        if (s.size() >= close_b.size() && s.compare(s.size() - close_b.size(), close_b.size(), close_b) == 0) {
            size_t p = s.rfind(open_b);
            if (p != std::string::npos && p > 0) {
                s = s.substr(0, p);
            }
        }
    };
    stripBracket("（", "）");
    stripBracket("(", ")");
    stripBracket("[", "]");

    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s.empty() ? raw : s;
}

} // namespace

bool SeriesDetector::extractEpisodeInfo(const std::string& filename,
                                       int& out_season,
                                       int& out_episode,
                                       std::string& out_clean_title) {
    out_season = 1;
    out_episode = -1;
    out_clean_title = filename;

    // 移除常见视频后缀
    std::string stem = filename;
    auto dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        std::string ext = stem.substr(dot + 1);
        if (ext == "mkv" || ext == "mp4" || ext == "avi" || ext == "ts" || ext == "flv" || ext == "mov") {
            stem = stem.substr(0, dot);
        }
    }

    // 1. 中文常见分集：第01话, 第1集, 第06回
    {
        std::regex re(R"(第\s*(\d{1,4})\s*[话集回期])");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_episode = std::stoi(m[1].str());
            return true;
        }
    }

    // 2. 欧美经典分集：S01E02, S4E6, s04.e06
    {
        std::regex re(R"([sS](\d{1,2})[\.\s_\-]*[eE](\d{1,4}))");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_season = std::stoi(m[1].str());
            out_episode = std::stoi(m[2].str());
            return true;
        }
    }

    // 3. 数字乘号分集：1x02, 4x06
    {
        std::regex re(R"((\d{1,2})[xX](\d{1,4}))");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_season = std::stoi(m[1].str());
            out_episode = std::stoi(m[2].str());
            return true;
        }
    }

    // 4. EP分集：EP06, E06, ep12
    {
        std::regex re(R"((?:^|[\s\._\-\[])[eE][pP]?\s*(\d{1,4})(?:$|[\s\._\-\]]))");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_episode = std::stoi(m[1].str());
            return true;
        }
    }

    // 5. 动漫压制分集：[06], [06v2]
    {
        std::regex re(R"(\[(\d{1,4})(?:v\d+)?\])");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_episode = std::stoi(m[1].str());
            return true;
        }
    }

    // 6. 前缀纯序号：01. 标题, 06 - 标题
    {
        std::regex re(R"(^(\d{1,3})[\s\.\-_])");
        std::smatch m;
        if (std::regex_search(stem, m, re)) {
            out_episode = std::stoi(m[1].str());
            return true;
        }
    }

    return false;
}

DetectionResult SeriesDetector::detect(const std::vector<MediaItem>& items) {
    DetectionResult result;
    if (items.empty()) return result;

    // 按父目录归类：绝大多数连续剧存放在专门的子目录中
    std::unordered_map<std::string, std::vector<MediaItem>> dir_groups;
    for (const auto& it : items) {
        if (!it.is_video()) continue;
        fs::path p(it.path);
        std::string parent = p.parent_path().string();
        dir_groups[parent].push_back(it);
    }

    for (auto& [folder, group_items] : dir_groups) {
        struct EpInfo {
            MediaItem item;
            int season = 1;
            int episode = -1;
            bool is_ep = false;
        };

        std::vector<EpInfo> classified;
        classified.reserve(group_items.size());
        int ep_count = 0;

        for (const auto& item : group_items) {
            EpInfo info;
            info.item = item;
            std::string clean_title;
            if (extractEpisodeInfo(item.name, info.season, info.episode, clean_title)) {
                info.is_ep = true;
                ep_count++;
            }
            classified.push_back(std::move(info));
        }

        // 判定阈值：如果一个目录下有 2 集及以上识别为分集，则该目录归并为一部剧集
        if (ep_count >= 2) {
            SeriesGroup sg;
            sg.folder_path = folder;

            fs::path fpath(folder);
            std::string folder_name = fpath.filename().string();
            sg.title = cleanSeriesTitle(folder_name);

            // 从元数据中心提取剧集级元信息
            auto& meta_store = MetadataStore::getInstance();
            for (const auto& ci : classified) {
                auto meta = meta_store.get(ci.item.path);
                if (meta && !meta->is_empty()) {
                    if (sg.overview.empty()) sg.overview = meta->overview;
                    if (sg.poster_url.empty()) sg.poster_url = meta->poster_url;
                    if (sg.backdrop_url.empty()) sg.backdrop_url = meta->backdrop_url;
                    if (sg.release_date.empty()) sg.release_date = meta->release_date;
                    if (sg.genres.empty()) sg.genres = meta->genres;
                    if (sg.rating <= 0.0 && meta->rating > 0.0) sg.rating = meta->rating;

                    // 若元数据标题带有公用前缀（如 "数码宝贝：无限地带 第01话..."）
                    size_t ep_delim = meta->title.find("第");
                    if (ep_delim == std::string::npos) ep_delim = meta->title.find(" S");
                    if (ep_delim == std::string::npos) ep_delim = meta->title.find(" - ");
                    if (ep_delim != std::string::npos && ep_delim > 2) {
                        std::string meta_show = meta->title.substr(0, ep_delim);
                        while (!meta_show.empty() && (meta_show.back() == ' ' || meta_show.back() == ':')) {
                            meta_show.pop_back();
                        }
                        const std::string cn_colon = "：";
                        if (meta_show.size() >= cn_colon.size() &&
                            meta_show.compare(meta_show.size() - cn_colon.size(), cn_colon.size(), cn_colon) == 0) {
                            meta_show.erase(meta_show.size() - cn_colon.size());
                        }
                        while (!meta_show.empty() && meta_show.back() == ' ') meta_show.pop_back();
                        if (!meta_show.empty()) sg.title = meta_show;
                    }
                }
            }

            // 排序：单集按集数自然升序排在前面，未编号的（如 OP/ED 花絮）排在末尾
            std::sort(classified.begin(), classified.end(), [](const EpInfo& a, const EpInfo& b) {
                if (a.is_ep && b.is_ep) {
                    if (a.season != b.season) return a.season < b.season;
                    return a.episode < b.episode;
                }
                if (a.is_ep != b.is_ep) return a.is_ep; // 正片在前
                return a.item.name < b.item.name;
            });

            sg.episodes.reserve(classified.size());
            for (auto& ci : classified) {
                sg.episodes.push_back(std::move(ci.item));
            }

            // 结合播放历史计算在看集数与进度
            auto& hist = PlaybackHistory::getInstance();
            int64_t latest_played_time = 0;
            for (size_t i = 0; i < sg.episodes.size(); ++i) {
                HistoryRecord rec;
                if (hist.getRecord(sg.episodes[i].path, rec)) {
                    if (rec.completed) {
                        sg.completed_count++;
                    }
                    if (rec.timestamp > latest_played_time && rec.position > 10) {
                        latest_played_time = rec.timestamp;
                        sg.last_watched_idx = static_cast<int>(i);
                        sg.last_watched_pos = rec.position;
                        sg.last_watched_dur = rec.duration;
                    }
                }
            }

            sg.id = folder;
            result.series.push_back(std::move(sg));
        } else {
            // 不满足剧集条件的，保持为独立视频
            for (auto& ci : classified) {
                result.standalone_videos.push_back(std::move(ci.item));
            }
        }
    }

    return result;
}

} // namespace muvik
