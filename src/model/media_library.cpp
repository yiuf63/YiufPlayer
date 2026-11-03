#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "model/media_library.hpp"
#include "utils/app_paths.hpp"
#include "utils/scanner.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

namespace deck_player {

namespace {

// 以 '|' 分隔字段写一行，规避路径中出现空格/中文的问题
void writePipeLine(std::ostream& os, const std::vector<std::string>& fields) {
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) os << '|';
        os << fields[i];
    }
    os << '\n';
}

std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream iss(line);
    while (std::getline(iss, cur, '|')) out.push_back(cur);
    return out;
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

MediaLibrary& MediaLibrary::getInstance() {
    static MediaLibrary instance;
    return instance;
}

MediaLibrary::MediaLibrary() {
    load();
}

std::string MediaLibrary::librariesFilePath() const {
    return AppPaths::configFile("libraries.dat");
}

std::string MediaLibrary::cacheFilePath() const {
    return AppPaths::configFile("library_cache.dat");
}

// ---------------- 文件夹管理 ----------------

bool MediaLibrary::addFolder(const std::string& path, const std::string& display_name) {
    if (path.empty()) return false;

    std::error_code ec;
    fs::path p(path);
    // 规范化：去除尾部斜杠、解析相对路径，但不解析符号链接（避免破坏用户意图）
    fs::path normalized = fs::absolute(p, ec);
    if (ec) normalized = p;
    std::string key = normalized.lexically_normal().string();
    // 去掉末尾多余的 '/'
    while (key.size() > 1 && key.back() == '/') key.pop_back();

    if (!fs::exists(key, ec) || !fs::is_directory(key, ec)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& f : folders_) {
        if (f.path == key) return false; // 已存在
    }

    MediaLibraryFolder folder;
    folder.path = key;
    folder.display_name = display_name.empty()
        ? normalized.lexically_normal().filename().string()
        : display_name;
    if (folder.display_name.empty()) folder.display_name = key;
    folder.recursive = true;
    folder.added_at = nowSeconds();
    folders_.push_back(folder);
    return true;
}

bool MediaLibrary::removeFolder(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(folders_.begin(), folders_.end(),
        [&](const MediaLibraryFolder& f) { return f.path == path; });
    if (it == folders_.end()) return false;
    folders_.erase(it, folders_.end());

    // 同时清掉来自该文件夹的缓存条目
    std::vector<MediaItem> kept;
    kept.reserve(items_.size());
    for (auto& item : items_) {
        if (item.path.rfind(path, 0) != 0) {
            kept.push_back(std::move(item));
        } else {
            signatures_.erase(item.path);
        }
    }
    items_ = std::move(kept);
    return true;
}

std::vector<MediaLibraryFolder> MediaLibrary::getFolders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return folders_;
}

bool MediaLibrary::hasFolder(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& f : folders_) {
        if (f.path == path) return true;
    }
    return false;
}

// ---------------- 媒体条目 ----------------

std::vector<MediaItem> MediaLibrary::getItems() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

std::vector<MediaItem> MediaLibrary::getVideos() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MediaItem> out;
    for (const auto& item : items_) {
        if (item.is_video()) out.push_back(item);
    }
    return out;
}

std::vector<MediaItem> MediaLibrary::getAudios() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MediaItem> out;
    for (const auto& item : items_) {
        if (item.is_audio()) out.push_back(item);
    }
    return out;
}

// ---------------- 扫描 ----------------

void MediaLibrary::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
    signatures_.clear();
}

void MediaLibrary::scanFolder(const MediaLibraryFolder& folder,
                              std::vector<MediaItem>& out,
                              const ProgressCallback& on_progress,
                              int done, int total) {
    std::error_code ec;
    if (!fs::exists(folder.path, ec) || !fs::is_directory(folder.path, ec)) {
        if (on_progress) on_progress(done + 1, total, folder.path);
        return;
    }

    std::vector<MediaItem> found;
    std::function<void(const fs::path&, int)> collect =
        [&](const fs::path& dir, int depth) {
        if (depth > 12) return; // 防御性深度上限，避免异常目录树
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) return;
        for (const auto& entry : it) {
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name[0] == '.') continue; // 跳过隐藏文件

            std::error_code entry_ec;
            if (entry.is_directory(entry_ec)) {
                if (folder.recursive) collect(entry.path(), depth + 1);
                continue;
            }
            if (!entry.is_regular_file(entry_ec)) continue;

            const std::string ext = entry.path().extension().string();
            const bool is_video = MediaScanner::isSupportedVideoFormat(ext);
            const bool is_audio = MediaScanner::isSupportedAudioFormat(ext);
            if (!is_video && !is_audio) continue;

            MediaItem item;
            item.name = name;
            item.path = entry.path().string();
            item.type = is_video ? MediaType::Video : MediaType::Audio;
            item.extension = ext;
            std::error_code size_ec;
            item.size = static_cast<int64_t>(entry.file_size(size_ec));
            if (size_ec) item.size = 0;
            if (is_video) {
                item.matched_subtitles = MediaScanner::findMatchingSubtitles(item.path);
            }
            found.push_back(std::move(item));
        }
    };

    collect(fs::path(folder.path), 0);

    {
        // 增量判定：只把新增或发生变化的条目并入结果
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : found) {
            int64_t mtime = 0;
            std::error_code tec;
            auto ft = fs::last_write_time(item.path, tec);
            if (!tec) {
                auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
                    ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                mtime = sctp.time_since_epoch().count();
            }
            signatures_[item.path] = {item.size, mtime};
            out.push_back(std::move(item));
        }
    }

    if (on_progress) on_progress(done + 1, total, folder.path);
}

void MediaLibrary::rebuild(const ProgressCallback& on_progress) {
    bool expected = false;
    if (!scanning_.compare_exchange_strong(expected, true)) {
        return; // 已有扫描在进行
    }

    std::vector<MediaLibraryFolder> folders_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        folders_snapshot = folders_;
    }

    std::vector<MediaItem> collected;
    const int total = static_cast<int>(folders_snapshot.size());
    int done = 0;
    for (const auto& folder : folders_snapshot) {
        scanFolder(folder, collected, on_progress, done, total);
        ++done;
    }

    // 按名称排序，媒体库展示更稳定
    std::sort(collected.begin(), collected.end(),
        [](const MediaItem& a, const MediaItem& b) { return a.name < b.name; });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_ = std::move(collected);
    }

    scanning_.store(false);
    save();
}

// ---------------- 持久化 ----------------

void MediaLibrary::load() {
    // 1) 文件夹配置
    {
        std::ifstream infile(librariesFilePath());
        if (infile.is_open()) {
            std::string line;
            std::lock_guard<std::mutex> lock(mutex_);
            while (std::getline(infile, line)) {
                if (line.empty() || line[0] == '#') continue;
                auto f = splitPipe(line);
                if (f.size() < 4) continue;
                MediaLibraryFolder folder;
                folder.path = f[0];
                folder.display_name = f[1];
                folder.recursive = (f[2] != "0");
                try { folder.added_at = std::stoll(f[3]); } catch (...) { folder.added_at = 0; }
                folders_.push_back(folder);
            }
        }
    }

#if defined(ANDROID)
    // Android 首次启动没有任何媒体文件夹，界面上「视频库」会是空的。
    // 用户不知道该去哪里「添加文件夹」（Android 没有桌面那种文件管理器习惯），
    // 所以自动把系统标准媒体目录加进来。
    //
    // 注意：这些路径在 Android 10+ 上受分区存储限制，
    // 能否真正读到取决于是否授予了 READ_MEDIA_* 权限
    // （见 DeckPlayerActivity.requestMediaPermissions）。
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (folders_.empty()) {
            struct { const char* path; const char* name; } defaults[] = {
                {"/storage/emulated/0/Movies",    "视频"},
                {"/storage/emulated/0/Music",     "音乐"},
                {"/storage/emulated/0/DCIM",      "相机"},
                {"/storage/emulated/0/Download",  "下载"},
            };
            std::error_code ec;
            for (auto& d : defaults) {
                if (fs::is_directory(d.path, ec)) {
                    MediaLibraryFolder folder;
                    folder.path = d.path;
                    folder.display_name = d.name;
                    folder.recursive = true;
                    folder.added_at = (long long)std::time(nullptr);
                    folders_.push_back(folder);
                    // 注意：本文件被单元测试直接编译，不链接 borealis。
                    // 因此这里不能用 brls::Logger，只能走 stderr。
                    std::fprintf(stderr, "[media] Android 自动添加媒体目录: %s\n", d.path);
                }
            }
        }
    }
#endif

    // 2) 媒体缓存
    {
        std::ifstream infile(cacheFilePath());
        if (infile.is_open()) {
            std::string line;
            std::lock_guard<std::mutex> lock(mutex_);
            while (std::getline(infile, line)) {
                if (line.empty() || line[0] == '#') continue;
                auto f = splitPipe(line);
                // path|name|type|size|ext
                if (f.size() < 5) continue;
                MediaItem item;
                item.path = f[0];
                item.name = f[1];
                item.type = (f[2] == "v") ? MediaType::Video
                          : (f[2] == "a") ? MediaType::Audio
                                          : MediaType::Unknown;
                try { item.size = std::stoll(f[3]); } catch (...) { item.size = 0; }
                item.extension = f[4];
                items_.push_back(std::move(item));
            }
        }
    }
}

void MediaLibrary::save() const {
    AppPaths::ensureDir(AppPaths::configDir());

    // 1) 文件夹配置
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream out(librariesFilePath(), std::ios::trunc);
        if (out.is_open()) {
            out << "# DeckPlayer media libraries v1\n";
            out << "# path|display_name|recursive|added_at\n";
            for (const auto& f : folders_) {
                writePipeLine(out, {f.path, f.display_name,
                                    f.recursive ? "1" : "0",
                                    std::to_string(f.added_at)});
            }
        }

        // 2) 媒体缓存
        std::ofstream cache(cacheFilePath(), std::ios::trunc);
        if (cache.is_open()) {
            cache << "# DeckPlayer library cache v1\n";
            cache << "# path|name|type|size|ext\n";
            for (const auto& item : items_) {
                const char* t = item.is_video() ? "v" : (item.is_audio() ? "a" : "u");
                writePipeLine(cache, {item.path, item.name, t,
                                      std::to_string(item.size), item.extension});
            }
        }
    }
}

} // namespace deck_player
