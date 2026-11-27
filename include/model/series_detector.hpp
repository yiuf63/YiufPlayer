#pragma once

#include <string>
#include <vector>
#include <optional>
#include "model/media_item.hpp"
#include "model/metadata_store.hpp"

namespace muvik {

// 剧集聚合实体：将多集连续剧/番剧聚合成一个作品主体
struct SeriesGroup {
    std::string id;              // 唯一标识（剧集名称或路径哈希）
    std::string title;           // 作品标题，如 "数码宝贝：无限地带"
    std::string folder_path;     // 所在文件夹路径
    std::string overview;        // 剧情简介
    std::string poster_url;      // 封面海报
    std::string backdrop_url;    // 背景横幅
    std::string release_date;    // 发行年份，如 "2002-04-07"
    std::string genres;          // 类型标签，如 "动画, 冒险, 奇幻"
    double rating = -1.0;        // 评分，如 8.4
    std::vector<MediaItem> episodes; // 排序后的所有单集视频

    // 每集在**磁盘文件名里**标称的季号与集号，与 episodes 同下标。
    //
    // 为什么必须单独存：
    //   episodes 只有下标（第几条），而界面要在文案里写「第 8 集」。
    //   缺集时（S01E05 + S01E08）下标 1 对应的真实集号是 8，
    //   用下标 +1 会写成「第 2 集」—— 用户以为文件放错了。
    //   extractEpisodeInfo() 早就解析出了真实编号，但此前只用于
    //   排序和分组，没有留下来。
    struct EpisodeNumber {
        int season = 0;   // 0 表示文件名里没写季号
        int episode = 0;  // 0 表示没写集号（如 OP/ED、无编号素材）
    };
    std::vector<EpisodeNumber> episode_numbers;

    // 某一集的真实集号；缺失（未编号素材 / 越界）时返回 0，
    // 调用方应退回用下标显示。
    //
    // 注意内部约定：SeriesDetector 用 **-1** 表示「文件名里没有集号」
    // （见 series_detector.cpp 的 EpInfo::episode）。这里是公开接口，
    // 转成 0 更好用 —— 调用方判 `> 0` 就是「有真实集号」，
    // 不用记住两个不同的哨兵值。
    int episodeOf(size_t idx) const {
        if (idx >= episode_numbers.size()) return 0;
        const int ep = episode_numbers[idx].episode;
        return ep > 0 ? ep : 0;
    }
    int seasonOf(size_t idx) const {
        if (idx >= episode_numbers.size()) return 0;
        const int s = episode_numbers[idx].season;
        return s > 0 ? s : 0;
    }

    // 续播状态
    int last_watched_idx = -1;   // 上次看到第几集（0-based 索引，-1 表示尚未播放）
    int64_t last_watched_pos = 0;// 播放停留在多少秒
    int64_t last_watched_dur = 0;// 停留在该集的总时长
    int completed_count = 0;     // 已看完集数

    // 获取代表性条目（优先用当前续播集，没有则用第 1 集）
    MediaItem representativeItem() const {
        if (last_watched_idx >= 0 && last_watched_idx < (int)episodes.size()) {
            return episodes[last_watched_idx];
        }
        return episodes.empty() ? MediaItem{} : episodes[0];
    }
};

struct DetectionResult {
    std::vector<SeriesGroup> series;
    std::vector<MediaItem> standalone_videos;
};

// 剧集启发式识别器
class SeriesDetector {
public:
    // 从一组视频文件中识别并聚合连续剧集，未成剧的作为独立视频保留
    static DetectionResult detect(const std::vector<MediaItem>& items);

    // 解析单集编号：
    // 支持 第01话/第1集, S01E02, 1x02, EP03, [04], 05. 等格式
    static bool extractEpisodeInfo(const std::string& filename,
                                  int& out_season,
                                  int& out_episode,
                                  std::string& out_clean_title);
};

} // namespace muvik
