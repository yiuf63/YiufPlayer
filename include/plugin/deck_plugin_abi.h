#pragma once

// ============================================================================
// DeckPlayer 插件 ABI  v1
// ============================================================================
//
// 设计约束（务必遵守，改动会破坏所有已发布的第三方插件）：
//
//  1. 只用 C 类型。禁止 std::string / std::vector / 类 / 模板 / 异常跨越 SO 边界。
//     原因：不同编译器版本、不同 STL 实现、不同 ABI 标签下，C++ 对象的内存布局
//     与符号修饰都可能不同。实测中，仅因结构体新增一个字段就会导致调用方
//     读到越界内存并触发 Bus error。
//
//  2. 所有字符串为 UTF-8、以 '\0' 结尾。
//
//  3. 所有权规则：
//     - 插件返回的字符串/数组由插件分配，主程序用本文件提供的 free_* 函数释放。
//     - 主程序传给插件的字符串仅在调用期间有效，插件必须自行复制。
//
//  4. 版本协商：插件必须导出 deck_player_plugin_abi_version()，
//     返回结构体大小或 ABI 版本号。主程序据此判断兼容性，拒绝加载不匹配的插件。
//
//  5. 所有函数指针在旧版本中必须保留（结构体只允许在尾部追加字段，
//     并用结构体大小做兼容判断）。
// ============================================================================

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 版本
// ---------------------------------------------------------------------------

#define DECK_PLAYER_ABI_VERSION 1u

// ---------------------------------------------------------------------------
// 基础类型
// ---------------------------------------------------------------------------

// 媒体类型
typedef enum deck_media_type {
    DECK_MEDIA_UNKNOWN = 0,
    DECK_MEDIA_DIRECTORY = 1,
    DECK_MEDIA_VIDEO = 2,
    DECK_MEDIA_AUDIO = 3,
} deck_media_type;

// 错误码
typedef enum deck_status {
    DECK_OK = 0,
    DECK_ERR_GENERIC = -1,
    DECK_ERR_NOT_AVAILABLE = -2,
    DECK_ERR_AUTH = -3,
    DECK_ERR_NETWORK = -4,
    DECK_ERR_CANCELLED = -5,
} deck_status;

// 媒体条目（纯 C 结构体，布局固定）
typedef struct deck_media_item {
    const char* name;       // 显示名，UTF-8
    const char* path;       // 资源定位符（本地路径或插件自定义 URI）
    const char* thumbnail;  // 缩略图 URL/路径，可为 NULL
    deck_media_type type;
    int64_t size;           // 字节数，未知填 -1
    int64_t duration;       // 秒，未知填 -1
} deck_media_item;

// 媒体条目数组（由插件分配，主程序释放）
typedef struct deck_media_list {
    deck_media_item* items;
    size_t count;
} deck_media_list;

// 刮削结果（元数据）
typedef struct deck_scrape_result {
    const char* title;
    const char* overview;
    const char* poster_url;
    const char* backdrop_url;
    const char* release_date;  // YYYY-MM-DD
    const char* genres;        // 逗号分隔
    double rating;             // 0-10，未知填 -1
    int32_t runtime_minutes;   // 未知填 -1
} deck_scrape_result;

// ---------------------------------------------------------------------------
// 插件宿主回调（由主程序提供，插件调用）
// ---------------------------------------------------------------------------

typedef struct deck_host_api {
    // 写入日志。level: 0=info 1=warn 2=error
    void (*log)(int level, const char* message);

    // 从插件配置中读取一个值，未找到返回 NULL。
    // 返回的字符串仅在下次调用前有效。
    const char* (*get_config)(const char* key);

    // HTTP 请求（由宿主统一实现，插件无需自带网络栈）。
    // 同步调用，返回 0 表示成功。响应体由宿主分配，用 free_buffer 释放。
    deck_status (*http_request)(const char* method,
                                const char* url,
                                const char* headers,      // "K: V\r\nK2: V2"，可 NULL
                                const char* body,         // 可 NULL
                                char** out_body,
                                size_t* out_len);

    // 释放宿主返回的缓冲区
    void (*free_buffer)(char* buffer);
} deck_host_api;

// ---------------------------------------------------------------------------
// 扩展点 1：视频源 (Video Source)
// ---------------------------------------------------------------------------
//
// 让用户自行接入视频平台（Emby / Jellyfin / 哔哩哔哩 / 自建站等）。
// 由插件实现，主程序调用。
// ---------------------------------------------------------------------------

typedef struct deck_video_source {
    void* impl;  // 插件的私有实例数据

    // 唯一 id，如 "jellyfin"。必须稳定。
    const char* (*get_id)(void* impl);

    // 显示名
    const char* (*get_display_name)(void* impl);

    // 是否需要用户配置（如服务器地址、账号）
    int (*needs_config)(void* impl);

    // 初始化。config_json 为插件自己的配置（可为 NULL）。
    deck_status (*init)(void* impl, const char* config_json, const deck_host_api* host);

    // 列出某个目录下的条目。path 为空或 "/" 表示根。
    // 返回值由插件分配，主程序用本 ABI 的释放函数处理。
    deck_status (*list_items)(void* impl, const char* path, deck_media_list** out_list);

    // 把逻辑路径解析为可交给播放器的实际 URL
    // （例如为 mpv 生成带鉴权 token 的直链）
    deck_status (*resolve_play_url)(void* impl, const char* path,
                                    char** out_url);

    // 释放本插件分配的媒体列表
    void (*free_list)(deck_media_list* list);

    // 销毁实例
    void (*destroy)(void* impl);
} deck_video_source;

// ---------------------------------------------------------------------------
// 扩展点 2：刮削器 (Scraper)
// ---------------------------------------------------------------------------
//
// 让用户自行提供元数据来源（TMDB / 豆瓣 / Bangumi 等），
// 为本地媒体补齐封面、简介、评分。
// ---------------------------------------------------------------------------

typedef struct deck_scraper {
    void* impl;

    const char* (*get_id)(void* impl);
    const char* (*get_display_name)(void* impl);

    // 该刮削器能处理的文件名模式（可简单理解为支持的媒体类型标签，
    // 例如 "movie" / "tv" / "anime"），主程序用这个做分流。
    const char* (*get_media_category)(void* impl);

    deck_status (*init)(void* impl, const char* config_json, const deck_host_api* host);

    // 依据查询串（通常是清洗后的文件名）搜索候选。
    // 结果以 JSON 数组字符串返回，由插件分配。
    deck_status (*search)(void* impl, const char* query, int64_t year,
                          char** out_json);

    // 依据候选 id 拉取完整元数据
    deck_status (*fetch_detail)(void* impl, const char* candidate_id,
                                deck_scrape_result** out_result);

    // 释放刮削结果内部字符串
    void (*free_result)(deck_scrape_result* result);

    void (*destroy)(void* impl);
} deck_scraper;

// ---------------------------------------------------------------------------
// 插件导出符号（每个插件必须实现）
// ---------------------------------------------------------------------------
//
// 插件一个 .so/.dylib 可以同时提供视频源与刮削器，也可以只提供其中一个。
// 未提供的扩展点返回 NULL。

typedef struct deck_plugin_descriptor {
    // 必须等于 DECK_PLAYER_ABI_VERSION，否则主程序拒绝加载
    uint32_t abi_version;

    // 插件自身元信息
    const char* plugin_name;
    const char* plugin_version;
    const char* plugin_author;

    // 扩展点。可为 NULL 表示不提供。
    deck_video_source* (*create_video_source)(void);
    deck_scraper* (*create_scraper)(void);
} deck_plugin_descriptor;

// 插件唯一导出入口。主程序通过 dlsym 获取。
// 名称固定为 "deck_player_plugin_entry_v1"。
typedef const deck_plugin_descriptor* (*deck_plugin_entry_fn)(void);

#define DECK_PLUGIN_ENTRY_SYMBOL "deck_player_plugin_entry_v1"

#ifdef __cplusplus
} // extern "C"
#endif
