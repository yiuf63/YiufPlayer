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
    std::string name;          // 文件名（原样，搜索与去重都用它）
    // 刮削得到的正式标题，空表示没有刮削过。
    //
    // 为什么单独一个字段而不是覆盖 name：
    //   · 搜索要同时命中「展示的片名」和「磁盘上的文件名」——
    //     实测 `cam_0042.mp4` 刮削成「蓝调时刻」后，搜片名得到 0 项，
    //     用户按看到的名字找不到片；
    //   · name 是身份的一部分（排序、去重、跳转都按它），
    //     覆盖掉会让「同一条媒体」在不同视图里有不同身份。
    // 显示时用 displayTitle() 取「有刮削用刮削，否则用文件名」。
    std::string display_title;
    std::string path;          // 完整绝对路径
    MediaType type;            // 文件或目录
    int64_t size = 0;          // 文件字节大小
    int64_t modified = 0;      // 文件修改时间（Unix 秒）
    // 首次被扫描进库的时间（Unix 秒）。0 表示未知（旧缓存）。
    //
    // 为什么必须和 modified 分开：
    //   「最近添加」按 modified 排是错的 —— 拷贝一部 2019 年的老片进库时，
    //   它的 mtime 可能被保留成 2019，于是「最近添加」里排到最后，
    //   用户刚拷进去的片反而找不到。
    //   modified 是「文件什么时候改的」，first_indexed 是「我什么时候知道它的」，
    //   前者属于文件系统，后者属于这个媒体库。
    int64_t first_indexed = 0;
    int64_t duration = 0;      // 媒体总时长（秒）
    int64_t last_position = 0; // 上次播放进度（秒）
    std::string extension;     // 后缀名（小写）
    std::string thumbnail_url; // 缩略图或海报 URL/路径
    std::vector<std::string> matched_subtitles; // 匹配到的同名外部字幕

    // 界面上该显示什么名字：优先刮削标题，没有就退回文件名。
    // 所有卡片、列表、对话框都该走这里，不要各自判断。
    const std::string& displayTitle() const {
        return display_title.empty() ? name : display_title;
    }

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
