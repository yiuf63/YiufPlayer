#pragma once

#include "model/media_item.hpp"
#include "plugin/js_runtime.hpp"
#include "plugin/plugin_manager.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

namespace muvik {

// 播放流信息
struct StreamSource {
    std::string name;                          // 如 "1080P 超清", "原画"
    std::string url;                           // 真实播放链接（http/m3u8/mp4等）
    std::string format;                        // "hls", "mp4", "dash"
    std::map<std::string, std::string> headers;// 防盗链 Headers (Referer, User-Agent, Cookie)
    int64_t expires_at = 0;                    // 预签名链接过期时间戳（秒）
};

// 伴随字幕信息
struct SubtitleSource {
    std::string name;                          // 如 "中文 (ASS)", "English"
    std::string url;                           // 字幕链接或文件路径
    std::string format;                        // "ass", "srt", "vtt"
};

// 分集/剧集信息
struct EpisodeItem {
    std::string id;
    std::string title;
    int32_t episode_number = 0;
};

// 流媒体播放信息解析结果
struct PlayLinksResult {
    std::vector<StreamSource> streams;
    std::vector<SubtitleSource> subtitles;
};

// 媒体详情
struct MediaDetail {
    std::string id;
    std::string title;
    std::string overview;
    std::string poster_url;
    std::string backdrop_url;
    std::string release_date;
    std::vector<EpisodeItem> episodes;
};

// 插件设置项声明
struct PluginSettingField {
    std::string key;
    std::string label;
    std::string type; // "string", "password", "number", "boolean"
    std::string default_value;
};

// JavaScript 驱动的现代插件
class JSPlugin {
public:
    static std::shared_ptr<JSPlugin> loadFromDirectory(const std::string& dir_path, std::string* out_err = nullptr);

    JSPlugin(std::string dir_path, std::string id, std::string name, std::string version);
    ~JSPlugin() = default;

    const std::string& getDirectory() const { return dir_path_; }
    const std::string& getId() const { return id_; }
    const std::string& getName() const { return name_; }
    const std::string& getVersion() const { return version_; }
    const std::string& getAuthor() const { return author_; }
    const std::string& getDescription() const { return description_; }

    bool hasCapability(const std::string& cap) const;
    bool hasMediaStream() const { return hasCapability("media_stream"); }
    bool hasStorage() const { return hasCapability("storage"); }
    bool hasScraper() const { return hasCapability("scraper"); }
    bool hasSearch() const;

    const std::vector<PluginSettingField>& getSettings() const { return settings_; }

    // --- MediaStreamProvider / 搜索接口 ---
    bool search(const std::string& keyword, int page, std::vector<MediaItem>& out_items, bool& out_has_more, const std::string& scope_path = "");
    bool loadDetail(const std::string& id, MediaDetail& out_detail);
    bool loadLinks(const std::string& episode_id, PlayLinksResult& out_links);

    // --- StorageProvider 接口 ---
    bool listDirectory(const std::string& path, std::vector<MediaItem>& out_items);
    bool resolvePlayUrl(const std::string& path, StreamSource& out_source);
    bool onLinkExpired(const std::string& path, StreamSource& out_source);

    // --- ScraperProvider 接口 ---
    bool scrapeSearch(const std::string& query, int64_t year, std::string& out_json);
    bool scrapeDetail(const std::string& id, ScrapeDetail& out_detail);

private:
    std::string dir_path_;
    std::string id_;
    std::string name_;
    std::string version_;
    std::string author_;
    std::string description_;
    std::vector<std::string> capabilities_;
    std::vector<PluginSettingField> settings_;

    std::unique_ptr<JSRuntimeHost> runtime_;
    mutable std::mutex runtime_mutex_;
};

} // namespace muvik
