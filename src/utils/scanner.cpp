#include <algorithm>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "utils/scanner.hpp"
#include "utils/i18n.hpp"
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace muvik {

// 覆盖业界最普遍、主流及经典音视频格式
const std::set<std::string> MediaScanner::SUPPORTED_VIDEO_EXTS = {
    // 现代常见流媒体与封装
    ".mp4", ".mkv", ".webm", ".mov", ".m4v",
    // 蓝光、广播及高清传输流
    ".ts", ".m2ts", ".mts", ".tp", ".trp",
    // 传统与经典多媒体格式
    ".avi", ".flv", ".f4v", ".swf", ".wmv", ".asf",
    // DVD/VCD及MPEG历史格式
    ".vob", ".mpg", ".mpeg", ".m1v", ".m2v", ".mpv",
    // RealMedia及移动便携格式
    ".rmvb", ".rm", ".3gp", ".3g2",
    // 镜像与专业影视格式
    ".iso", ".ogv", ".divx", ".mxf"
};

const std::set<std::string> MediaScanner::SUPPORTED_AUDIO_EXTS = {
    // 无损高保真与母带
    ".flac", ".wav", ".alac", ".aiff", ".aif", ".ape", ".dsd", ".dff", ".dsf",
    // 流行压缩音频
    ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".wma",
    // 游戏/动漫与多声道
    ".mka", ".ac3", ".dts", ".mid"
};

const std::set<std::string> MediaScanner::SUPPORTED_SUBTITLE_EXTS = {
    // 文本与特效字幕
    ".srt", ".ass", ".ssa", ".vtt", ".lrc",
    // 图形与封装字幕
    ".sub", ".idx", ".sup", ".smi", ".rt"
};

MediaScanner& MediaScanner::getInstance() {
    static MediaScanner instance;
    return instance;
}

MediaScanner::MediaScanner() = default;

bool MediaScanner::isSupportedVideoFormat(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return SUPPORTED_VIDEO_EXTS.find(lower) != SUPPORTED_VIDEO_EXTS.end();
}

bool MediaScanner::isSupportedAudioFormat(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return SUPPORTED_AUDIO_EXTS.find(lower) != SUPPORTED_AUDIO_EXTS.end();
}

bool MediaScanner::isSupportedSubtitleFormat(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return SUPPORTED_SUBTITLE_EXTS.find(lower) != SUPPORTED_SUBTITLE_EXTS.end();
}

std::vector<StorageSource> MediaScanner::getAvailableSources() {
    std::vector<StorageSource> sources;
    std::set<std::string> seen;

    auto addSource = [&](const std::string& id, const std::string& name, const std::string& path,
                         const std::string& icon, bool removable, bool writable) {
        if (path.empty() || seen.count(path)) return;
        std::error_code ec;
        if (fs::exists(path, ec) && !ec && fs::is_directory(path, ec) && !ec) {
            seen.insert(path);
            sources.push_back({id, name, path, icon, removable, writable});
        }
    };

    // 0. Android 存储（必须放在最前，且优先于一切 POSIX 路径）
    //
    // 为什么原来在 Android 上「添加媒体库」会从根目录开始、而且是空的：
    //   这里整段候选路径全是 /mnt/SDCARD、/media、/run/media、/Volumes ——
    //   普通 Linux/macOS 的挂载点。Android 的共享存储既不在其中，
    //   也无法用 stat 探测（这些目录根本不存在），于是 getAvailableSources()
    //   返回空列表，文件选择器退化成「手动输入路径」的根目录视图。
    //
    // Android 的路径规则（相对稳定，可安全硬编码）：
    //   内置共享存储  /storage/emulated/<userId>   等价软链 /sdcard
    //   外置 SD 卡    /storage/<XXXX-XXXX>         物理卷 ID，需枚举
    //
    // 注意 /storage/emulated/0 与 /sdcard 是同一个位置，用 seen 集合去重。
#if defined(ANDROID)
    {
        // 内置存储：先按当前用户的 /sdcard（最通用，非 0 用户也能用）
        addSource("android_internal", "内部存储", "/sdcard", "sd_card", false, true);
        // 兜底：部分 ROM 没有 /sdcard 软链
        addSource("android_emulated_0", "内部存储", "/storage/emulated/0", "sd_card", false, true);

        // 常用媒体目录（存在才列出，让用户少点两层）
        for (const char* sub : {"Movies", "Music", "Download", "DCIM"}) {
            const std::string p = std::string("/sdcard/") + sub;
            addSource(std::string("android_") + sub, std::string("内部存储 / ") + sub,
                      p, "video_library", false, true);
        }

        // 外置 SD 卡：/storage 下除 emulated / self 之外的目录
        std::error_code sd_ec;
        if (fs::exists("/storage", sd_ec) && !sd_ec) {
            try {
                fs::directory_iterator it("/storage",
                                          fs::directory_options::skip_permission_denied, sd_ec);
                fs::directory_iterator end;
                while (!sd_ec && it != end) {
                    std::error_code e;
                    const std::string name = it->path().filename().string();
                    // emulated 是内置存储的挂载点，self 是当前用户的软链
                    if (name != "emulated" && name != "self" && it->is_directory(e)) {
                        addSource("android_sd_" + name,
                                  std::string("SD 卡 (") + name + ")",
                                  it->path().string(), "sd_card", true, true);
                    }
                    it.increment(sd_ec);
                }
            } catch (...) {}
        }
    }
#endif

    // 1. 开源掌机与嵌入式 Linux 存储卡及媒体目录 (TrimUI / Miyoo / Anbernic / Linux SBC)
    addSource("sdcard_videos", i18n::tr("files.sdcard_videos", "MicroSD 视频目录 (/mnt/SDCARD/Videos)"),
              "/mnt/SDCARD/Videos", "video_library", false, true);
    addSource("sdcard_music", i18n::tr("files.sdcard_music", "MicroSD 音乐目录 (/mnt/SDCARD/music)"),
              "/mnt/SDCARD/music", "music_note", false, true);
    addSource("sdcard_music_caps", i18n::tr("files.sdcard_music", "MicroSD 音乐目录 (/mnt/SDCARD/Music)"),
              "/mnt/SDCARD/Music", "music_note", false, true);
    addSource("sdcard_root", i18n::tr("files.sdcard_root", "MicroSD 存储卡 (/mnt/SDCARD)"),
              "/mnt/SDCARD", "sd_card", true, true);
    addSource("udisk_root", i18n::tr("files.udisk_root", "内置存储 (/mnt/UDISK)"),
              "/mnt/UDISK", "sd_card", false, true);
    addSource("mnt_sdcard", i18n::tr("files.external_storage", "扩展存储卡") + " (/mnt/sdcard)",
              "/mnt/sdcard", "sd_card", true, true);
    addSource("mnt_extsd", i18n::tr("files.external_storage", "扩展存储卡") + " (/mnt/extsd)",
              "/mnt/extsd", "sd_card", true, true);

    // 扫描 /media 挂载点
    std::error_code ec;
    if (fs::exists("/media", ec) && !ec) {
        try {
            fs::directory_iterator m_it("/media", fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator m_end;
            while (!ec && m_it != m_end) {
                std::error_code e_dir;
                if (m_it->is_directory(e_dir)) {
                    std::string m_name = m_it->path().filename().string();
                    // 检查是否为嵌套用户目录 /media/<user>/*
                    std::error_code sub_ec;
                    fs::directory_iterator sub_it(m_it->path(), fs::directory_options::skip_permission_denied, sub_ec);
                    fs::directory_iterator sub_end;
                    bool has_sub = false;
                    while (!sub_ec && sub_it != sub_end) {
                        std::error_code s_dir;
                        if (sub_it->is_directory(s_dir)) {
                            has_sub = true;
                            addSource("media_" + sub_it->path().filename().string(),
                                      i18n::tr("files.external_storage", "外部存储") + " (" + sub_it->path().filename().string() + ")",
                                      sub_it->path().string(), "sd_card", true, true);
                        }
                        sub_it.increment(sub_ec);
                    }
                    if (!has_sub) {
                        addSource("media_" + m_name,
                                  i18n::tr("files.external_storage", "外部存储") + " (" + m_name + ")",
                                  m_it->path().string(), "sd_card", true, true);
                    }
                }
                m_it.increment(ec);
            }
        } catch (...) {}
    }

    // 2. Linux / SteamOS 存储卡挂载点 (/run/media/*/*)
    const std::string steam_sd_base = "/run/media";
    ec.clear();
    if (fs::exists(steam_sd_base, ec) && !ec) {
        try {
            fs::directory_iterator u_it(steam_sd_base, fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator u_end;
            while (!ec && u_it != u_end) {
                std::error_code e_dir;
                if (u_it->is_directory(e_dir)) {
                    std::error_code dev_ec;
                    fs::directory_iterator dev_it(u_it->path(), fs::directory_options::skip_permission_denied, dev_ec);
                    fs::directory_iterator dev_end;
                    while (!dev_ec && dev_it != dev_end) {
                        std::error_code e_dev_dir;
                        if (dev_it->is_directory(e_dev_dir)) {
                            std::string dev_name = dev_it->path().filename().string();
                            std::string label = (dev_name == "mmcblk0p1")
                                ? i18n::tr("files.steam_deck_microsd", "Steam Deck MicroSD 卡")
                                : (i18n::tr("files.external_storage", "扩展存储卡") + " (" + dev_name + ")");
                            addSource("sdcard_" + dev_name, label, dev_it->path().string(), "sd_card", true, true);
                        }
                        dev_it.increment(dev_ec);
                    }
                }
                u_it.increment(ec);
            }
        } catch (...) {}
    }

    // 3. macOS 外部磁盘与 SD 卡挂载点 (/Volumes/*)
    const std::string mac_volumes_base = "/Volumes";
    ec.clear();
    if (fs::exists(mac_volumes_base, ec) && !ec) {
        try {
            fs::directory_iterator v_it(mac_volumes_base, fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator v_end;
            while (!ec && v_it != v_end) {
                std::error_code e_v;
                if (v_it->is_directory(e_v)) {
                    std::string vol_name = v_it->path().filename().string();
                    // 过滤系统内置主分区和只读镜像
                    if (vol_name != "Macintosh HD" && vol_name.find("com.apple") == std::string::npos && vol_name[0] != '.') {
                        addSource("mac_vol_" + vol_name,
                                  i18n::tr("files.external_volume", "外接卷") + " (" + vol_name + ")",
                                  v_it->path().string(), "sd_card", true, true);
                    }
                }
                v_it.increment(ec);
            }
        } catch (...) {}
    }

    // 4. 扫描用户 Home 常见目录 (Linux 与 macOS 深度兼容)
    const char* home_env = std::getenv("HOME");
    std::string home_dir = home_env ? home_env : "";
    if (!home_dir.empty()) {
        addSource("movies", i18n::tr("files.movies_dir", "电影目录") + " (Movies)",
                  home_dir + "/Movies", "video_library", false, true);
        addSource("videos", i18n::tr("files.videos_dir", "视频目录") + " (Videos)",
                  home_dir + "/Videos", "video_library", false, true);
        addSource("music", i18n::tr("files.music_dir", "音乐目录") + " (Music)",
                  home_dir + "/Music", "music_note", false, true);
        addSource("downloads", "下载目录 (Downloads)",
                  home_dir + "/Downloads", "file_download", false, true);
        addSource("home", "个人主目录 (Home)",
                  home_dir, "folder_home", false, true);
    }

    // 5. 系统根存储 (/)：确保在任何 Linux 机器上用户都能导航到非标准路径（如 /data, /storage, /mnt）
    addSource("system_root", i18n::tr("files.system_root", "系统根目录 (/)"),
              "/", "storage", false, true);

    return sources;
}

std::vector<MediaItem> MediaScanner::listDirectory(const std::string& directory_path) {
    std::vector<MediaItem> items;
    std::error_code ec;
    if (!fs::exists(directory_path, ec) || ec || !fs::is_directory(directory_path, ec) || ec) {
        return items;
    }

    try {
        std::vector<MediaItem> directories;
        std::vector<MediaItem> media_files;

        fs::directory_iterator it(directory_path, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        while (!ec && it != end) {
            try {
                const auto& path = it->path();
                std::string filename = path.filename().string();

                // 过滤隐藏文件与系统临时文件
                if (!filename.empty() && filename[0] == '.') {
                    it.increment(ec);
                    continue;
                }

                std::error_code entry_ec;
                if (it->is_directory(entry_ec)) {
                    MediaItem dir_item;
                    dir_item.name = filename;
                    dir_item.path = path.string();
                    dir_item.type = MediaType::Directory;
                    directories.push_back(std::move(dir_item));
                } else if (it->is_regular_file(entry_ec)) {
                    std::string ext = path.extension().string();
                    bool is_v = isSupportedVideoFormat(ext);
                    bool is_a = isSupportedAudioFormat(ext);

                    if (is_v || is_a) {
                        MediaItem m_item;
                        m_item.name = filename;
                        m_item.path = path.string();
                        m_item.type = is_v ? MediaType::Video : MediaType::Audio;
                        m_item.extension = ext;
                        std::error_code sz_ec;
                        m_item.size = static_cast<int64_t>(it->file_size(sz_ec));
                        if (sz_ec) m_item.size = 0;
                        if (is_v) {
                            m_item.matched_subtitles = findMatchingSubtitles(m_item.path);
                        }
                        media_files.push_back(std::move(m_item));
                    }
                }
            } catch (...) {}
            it.increment(ec);
        }

        auto sorter = [](const MediaItem& a, const MediaItem& b) {
            return a.name < b.name;
        };
        std::sort(directories.begin(), directories.end(), sorter);
        std::sort(media_files.begin(), media_files.end(), sorter);

        items.insert(items.end(), directories.begin(), directories.end());
        items.insert(items.end(), media_files.begin(), media_files.end());

    } catch (...) {}

    return items;
}

std::vector<MediaItem> MediaScanner::scanMediaRecursively(const std::string& root_path, bool video_only, int max_depth) {
    std::vector<MediaItem> results;
    std::error_code root_ec;
    if (!fs::exists(root_path, root_ec) || root_ec) return results;

    auto scan_helper = [&](auto& self, const fs::path& current_path, int current_depth) -> void {
        if (current_depth > max_depth) return;

        try {
            std::error_code it_ec;
            fs::directory_iterator it(current_path, fs::directory_options::skip_permission_denied, it_ec);
            fs::directory_iterator end;
            while (!it_ec && it != end) {
                try {
                    std::error_code e_type;
                    if (it->is_directory(e_type)) {
                        std::string dirname = it->path().filename().string();
                        if (!dirname.empty() && dirname[0] != '.') {
                            self(self, it->path(), current_depth + 1);
                        }
                    } else if (it->is_regular_file(e_type)) {
                        std::string ext = it->path().extension().string();
                        bool is_v = isSupportedVideoFormat(ext);
                        bool is_a = !video_only && isSupportedAudioFormat(ext);

                        if (is_v || is_a) {
                            MediaItem item;
                            item.name = it->path().filename().string();
                            item.path = it->path().string();
                            item.type = is_v ? MediaType::Video : MediaType::Audio;
                            item.extension = ext;
                            std::error_code sz_ec;
                            item.size = static_cast<int64_t>(it->file_size(sz_ec));
                            if (sz_ec) item.size = 0;
                            if (is_v) {
                                item.matched_subtitles = findMatchingSubtitles(item.path);
                            }
                            results.push_back(std::move(item));
                        }
                    }
                } catch (...) {}
                it.increment(it_ec);
            }
        } catch (...) {}
    };

    scan_helper(scan_helper, fs::path(root_path), 0);
    return results;
}

std::vector<std::string> MediaScanner::findMatchingSubtitles(const std::string& video_path) {
    std::vector<std::string> subtitles;
    try {
        fs::path v_path(video_path);
        fs::path parent_dir = v_path.parent_path();
        std::string stem_name = v_path.stem().string();

        std::error_code ec;
        if (fs::exists(parent_dir, ec) && !ec) {
            fs::directory_iterator it(parent_dir, fs::directory_options::skip_permission_denied, ec);
            fs::directory_iterator end;
            while (!ec && it != end) {
                try {
                    std::error_code f_ec;
                    if (it->is_regular_file(f_ec)) {
                        std::string ext = it->path().extension().string();
                        if (isSupportedSubtitleFormat(ext)) {
                            std::string sub_stem = it->path().stem().string();
                            if (sub_stem.rfind(stem_name, 0) == 0) {
                                subtitles.push_back(it->path().string());
                            }
                        }
                    }
                } catch (...) {}
                it.increment(ec);
            }
        }
    } catch (...) {}
    return subtitles;
}

std::string MediaScanner::formatDuration(int64_t seconds) {
    if (seconds <= 0) return "00:00";
    int64_t h = seconds / 3600;
    int64_t m = (seconds % 3600) / 60;
    int64_t s = seconds % 60;

    std::ostringstream oss;
    if (h > 0) {
        oss << std::setw(2) << std::setfill('0') << h << ":";
    }
    oss << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s;
    return oss.str();
}

std::string MediaScanner::formatFileSize(int64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << kb << " KB";
        return oss.str();
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << mb << " MB";
        return oss.str();
    }
    double gb = mb / 1024.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << gb << " GB";
    return oss.str();
}

} // namespace muvik
