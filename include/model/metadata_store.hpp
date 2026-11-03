#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace deck_player {

// 刮削得到的媒体元数据。
// 独立于 MediaItem 存储：媒体库缓存只记录文件事实（路径/大小），
// 元数据是「补充信息」，可以随时重新刮削，不应与扫描结果耦合。
struct MediaMetadata {
    std::string path;          // 关联的媒体文件路径（主键）
    std::string title;         // 正式标题
    std::string overview;      // 剧情简介
    std::string poster_url;    // 封面
    std::string backdrop_url;  // 背景图
    std::string release_date;  // YYYY-MM-DD
    std::string genres;        // 逗号分隔
    double rating = -1.0;      // 0-10
    int32_t runtime_minutes = -1;
    std::string scraper_id;    // 由哪个刮削器提供
    int64_t updated_at = 0;    // 刮削时间戳

    bool is_empty() const {
        return title.empty() && overview.empty() && poster_url.empty();
    }

    // 展示用：有主标题用主标题，否则回落到文件名
    std::string displayTitle(const std::string& fallback) const {
        return title.empty() ? fallback : title;
    }
};

// 元数据仓库：内存缓存 + 磁盘持久化。
// 存储在独立的 metadata.dat 中，与媒体库缓存分离。
class MetadataStore {
public:
    static MetadataStore& getInstance();

    // 读取某个文件的元数据
    std::optional<MediaMetadata> get(const std::string& path) const;

    // 保存（覆盖）某个文件的元数据
    void put(const MediaMetadata& meta);

    // 删除
    void remove(const std::string& path);

    // 是否已有该文件的元数据
    bool has(const std::string& path) const;

    // 全部元数据
    std::vector<MediaMetadata> getAll() const;

    // 统计
    size_t count() const;

    // 持久化
    void load();
    void save() const;

private:
    MetadataStore();

    std::string filePath() const;

    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::string, MediaMetadata> records_;
};

} // namespace deck_player
