#pragma once

#include <set>
#include <string>
#include <vector>
#include "model/media_item.hpp"
#include <string>
#include <vector>
#include <set>

namespace muvik {

class MediaScanner {
public:
    static MediaScanner& getInstance();

    // 获取 Steam Deck 与系统可用的存储源列表
    std::vector<StorageSource> getAvailableSources();

    // 扫描指定目录下的文件与文件夹
    std::vector<MediaItem> listDirectory(const std::string& directory_path);

    // 递归查找指定目录下的所有视频或音频
    std::vector<MediaItem> scanMediaRecursively(const std::string& root_path, bool video_only = false, int max_depth = 5);

    // 格式判断
    static bool isSupportedVideoFormat(const std::string& ext);
    static bool isSupportedAudioFormat(const std::string& ext);
    static bool isSupportedSubtitleFormat(const std::string& ext);

    // 查找同级目录下相近或同名的字幕文件
    static std::vector<std::string> findMatchingSubtitles(const std::string& video_path);

    // 格式化输出
    static std::string formatDuration(int64_t seconds);
    static std::string formatFileSize(int64_t bytes);

private:
    MediaScanner();
    static const std::set<std::string> SUPPORTED_VIDEO_EXTS;
    static const std::set<std::string> SUPPORTED_AUDIO_EXTS;
    static const std::set<std::string> SUPPORTED_SUBTITLE_EXTS;
};

} // namespace muvik
