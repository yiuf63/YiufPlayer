#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "model/media_item.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <utility>
#include <mutex>
#include <atomic>

namespace deck_player {

// 媒体库中用户显式添加的一个文件夹（MX Player 式「添加媒体文件夹」）
struct MediaLibraryFolder {
    std::string path;        // 绝对路径
    std::string display_name; // 展示名
    bool recursive = true;    // 是否递归扫描子目录
    int64_t added_at = 0;     // 加入时间戳
};

// 媒体库：管理用户添加的文件夹，缓存扫描结果并持久化。
// 设计目标：
//  - 首次扫描后结果落盘，冷启动无需重新遍历整棵目录树
//  - 后续扫描按 (大小, 修改时间) 做增量判定，只重扫变化项
//  - 扫描可在后台线程进行，避免阻塞 UI
class MediaLibrary {
public:
    static MediaLibrary& getInstance();

    // ---- 文件夹管理 ----
    // 添加文件夹（自动去重、规范化路径、持久化），返回是否新增成功
    bool addFolder(const std::string& path, const std::string& display_name = "");
    // 移除文件夹
    bool removeFolder(const std::string& path);
    // 获取全部文件夹
    std::vector<MediaLibraryFolder> getFolders() const;
    // 某个路径是否已在库中
    bool hasFolder(const std::string& path) const;

    // ---- 媒体条目 ----
    // 获取已缓存的全部媒体条目（不触发扫描）
    std::vector<MediaItem> getItems() const;
    // 获取所有视频条目
    std::vector<MediaItem> getVideos() const;
    // 获取所有音频条目
    std::vector<MediaItem> getAudios() const;
    // 最近添加的条目（按文件修改时间倒序）。
    // 媒体库首屏需要一块「最近添加」：只按文件夹分组时，
    // 一屏几张目录卡，刚拷进去的片子反而找不到。
    std::vector<MediaItem> getRecent(size_t limit) const;

    // 按「用户添加的文件夹」归组后的结果。
    //
    // 为什么不复用 getVideos()：平铺列表把 10 个「01. 开头」排成一列，
    // 视频和音乐混在一起，「3:21」既可能是短片也可能是歌。
    // 文件夹是用户自己在文件管理器里分好的桶，让它露出来。
    struct Group {
        MediaLibraryFolder folder;
        std::vector<MediaItem> videos;
        std::vector<MediaItem> audios;
        size_t total() const { return videos.size() + audios.size(); }
    };
    std::vector<Group> getGroups() const;

    // ---- 扫描 ----
    // 是否正在扫描
    bool isScanning() const { return scanning_.load(); }
    // 扫描进度回调：已处理目录数 / 当前正在处理路径
    using ProgressCallback = std::function<void(int folders_done, int folders_total, const std::string& current)>;
    // 同步重建整个库（内部按增量规则判断是否需要重扫单文件）
    void rebuild(const ProgressCallback& on_progress = nullptr);

    // ---- 持久化 ----
    void load();
    void save() const;

    // 清空缓存（不删除文件夹配置）
    void clearCache();

private:
    MediaLibrary();

    std::string librariesFilePath() const;
    std::string cacheFilePath() const;

    // 扫描单个文件夹，将结果写入 items_
    void scanFolder(const MediaLibraryFolder& folder,
                     std::vector<MediaItem>& out,
                     const ProgressCallback& on_progress,
                     int done, int total);

    mutable std::mutex mutex_;
    std::vector<MediaLibraryFolder> folders_;
    std::vector<MediaItem> items_;
    // 缓存签名：path -> (size, mtime) 用于增量判定
    std::unordered_map<std::string, std::pair<int64_t, int64_t>> signatures_;
    std::atomic<bool> scanning_{false};
};

} // namespace deck_player
