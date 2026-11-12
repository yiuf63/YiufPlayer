#pragma once

#include <string>
#include <vector>
#include <string>
#include <vector>
#include <chrono>

namespace muvik {

enum class MediaType {
    Directory,
    Video,
    Audio,
    Unknown
};

enum class AspectRatioMode {
    Auto,       // 原始比例自适应
    Stretch_16_10, // Steam Deck 屏幕满屏 (16:10)
    Fit_16_9,   // 标准 16:9
    Fit_4_3,    // 经典 4:3 (旧动画/复古剧)
    ZoomCrop    // 裁剪填充
};

struct MediaItem {
    std::string name;          // 显示名称
    std::string path;          // 完整绝对路径
    MediaType type;            // 文件或目录
    int64_t size = 0;          // 文件字节大小
    int64_t modified = 0;      // 文件修改时间（Unix 秒）
    int64_t duration = 0;      // 媒体总时长（秒）
    int64_t last_position = 0; // 上次播放进度（秒）
    std::string extension;     // 后缀名（小写）
    std::vector<std::string> matched_subtitles; // 匹配到的同名外部字幕

    bool is_directory() const { return type == MediaType::Directory; }
    bool is_video() const { return type == MediaType::Video; }
    bool is_audio() const { return type == MediaType::Audio; }
};

struct StorageSource {
    std::string id;       // 标识符，如 "sdcard", "videos", "home"
    std::string name;     // 显示名称，如 "MicroSD 卡"
    std::string path;     // 挂载点绝对路径
    std::string icon;     // 图标名称
    bool is_removable = false; // 是否为可拔插存储
    bool is_available = false; // 当前路径是否存在
};

} // namespace muvik
