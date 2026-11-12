#pragma once

// ============================================================================
// Muvik 插件 ABI  v1
// ============================================================================
//
// 设计约束（务必遵守，改动会破坏所有已发布的第三方插件）：
//
//  1. 只用 C 类型。禁止 std::string / std::vector / 类 / 模板 / 异常跨越 SO 边界。
//     原因：不同编译器版本、不同 STL 实现、不同 ABI 标签下，C++ 对象的内存布局
//     与符号修饰都可能不同。
//
//  2. 所有字符串为 UTF-8、以 '\0' 结尾。
//
//  3. 所有权规则：
//     - 插件返回的字符串/数组由插件分配，主程序用本文件提供的 free_* 函数释放。
//     - 主程序传给插件的字符串仅在调用期间有效，插件必须自行复制。
//
//  4. 版本协商：插件必须导出 muvik_plugin_entry_v1()，
//     返回结构体包含 abi_version。主程序据此判断兼容性，拒绝加载不匹配的插件。
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

#define MUVIK_ABI_VERSION 1u

// ---------------------------------------------------------------------------
// 基础类型
// ---------------------------------------------------------------------------

// 媒体类型
typedef enum muvik_media_type {
    MUVIK_MEDIA_UNKNOWN = 0,
    MUVIK_MEDIA_DIRECTORY = 1,
    MUVIK_MEDIA_VIDEO = 2,
    MUVIK_MEDIA_AUDIO = 3,
} muvik_media_type;

// 错误码
typedef enum muvik_status {
    MUVIK_OK = 0,
    MUVIK_ERR_GENERIC = -1,
    MUVIK_ERR_NOT_AVAILABLE = -2,
    MUVIK_ERR_AUTH = -3,
    MUVIK_ERR_NETWORK = -4,
    MUVIK_ERR_CANCELLED = -5,
} muvik_status;

// 媒体条目（纯 C 结构体，布局固定）
typedef struct muvik_media_item {
    const char* name;       // 显示名，UTF-8
    const char* path;       // 资源定位符（本地路径或插件自定义 URI）
    const char* thumbnail;  // 缩略图 URL/路径，可为 NULL
    muvik_media_type type;
    int64_t size;           // 字节数，未知填 -1
    int64_t duration;       // 秒，未知填 -1
} muvik_media_item;

// 媒体条目数组（由插件分配，主程序释放）
typedef struct muvik_media_list {
    muvik_media_item* items;
    size_t count;
} muvik_media_list;

// 刮削结果（元数据）
typedef struct muvik_scrape_result {
    const char* title;
    const char* overview;
    const char* poster_url;
    const char* backdrop_url;
    const char* release_date;  // YYYY-MM-DD
    const char* genres;        // 逗号分隔
    double rating;             // 0-10，未知填 -1
    int32_t runtime_minutes;   // 未知填 -1
} muvik_scrape_result;

// ---------------------------------------------------------------------------
// 插件宿主回调（由主程序提供，插件调用）
// ---------------------------------------------------------------------------

typedef struct muvik_host_api {
    // 写入日志。level: 0=info 1=warn 2=error
    void (*log)(int level, const char* message);

    // 从插件配置中读取一个值，未找到返回 NULL。
    // 返回的字符串仅在下次调用前有效。
    const char* (*get_config)(const char* key);

    // HTTP 请求（由宿主统一实现，插件无需自带网络栈）。
    // 同步调用，返回 0 表示成功。响应体由宿主分配，用 free_buffer 释放。
    muvik_status (*http_request)(const char* method,
                                 const char* url,
                                 const char* headers,      // "K: V\r\nK2: V2"，可 NULL
                                 const char* body,         // 可 NULL
                                 char** out_body,
                                 size_t* out_len);

    // 释放宿主返回的缓冲区
    void (*free_buffer)(char* buffer);
} muvik_host_api;

// ---------------------------------------------------------------------------
// 扩展点 1：视频源 (Video Source)
// ---------------------------------------------------------------------------

typedef struct muvik_video_source {
    void* impl;  // 插件的私有实例数据

    // 唯一 id，如 "jellyfin"。必须稳定。
    const char* (*get_id)(void* impl);

    // 显示名
    const char* (*get_display_name)(void* impl);

    // 是否需要用户配置（如服务器地址、账号）
    int (*needs_config)(void* impl);

    // 初始化。config_json 为插件自己的配置（可为 NULL）。
    muvik_status (*init)(void* impl, const char* config_json, const muvik_host_api* host);

    // 列出某个目录下的条目。path 为空或 "/" 表示根。
    // 返回值由插件分配，主程序用本 ABI 的释放函数处理。
    muvik_status (*list_items)(void* impl, const char* path, muvik_media_list** out_list);

    // 把逻辑路径解析为可交给播放器的实际 URL
    muvik_status (*resolve_play_url)(void* impl, const char* path,
                                     char** out_url);

    // 释放本插件分配的媒体列表
    void (*free_list)(muvik_media_list* list);

    // 销毁实例
    void (*destroy)(void* impl);
} muvik_video_source;

// ---------------------------------------------------------------------------
// 扩展点 2：刮削器 (Scraper)
// ---------------------------------------------------------------------------

typedef struct muvik_scraper {
    void* impl;

    const char* (*get_id)(void* impl);
    const char* (*get_display_name)(void* impl);

    const char* (*get_media_category)(void* impl);

    muvik_status (*init)(void* impl, const char* config_json, const muvik_host_api* host);

    // 依据查询串（通常是清洗后的文件名）搜索候选。
    muvik_status (*search)(void* impl, const char* query, int64_t year,
                           char** out_json);

    // 依据候选 id 拉取完整元数据
    muvik_status (*fetch_detail)(void* impl, const char* candidate_id,
                                 muvik_scrape_result** out_result);

    // 释放刮削结果内部字符串
    void (*free_result)(muvik_scrape_result* result);

    void (*destroy)(void* impl);
} muvik_scraper;

// ---------------------------------------------------------------------------
// 插件导出符号（每个插件必须实现）
// ---------------------------------------------------------------------------

typedef struct muvik_plugin_descriptor {
    // 必须等于 MUVIK_ABI_VERSION，否则主程序拒绝加载
    uint32_t abi_version;

    // 插件自身元信息
    const char* plugin_name;
    const char* plugin_version;
    const char* plugin_author;

    // 扩展点。可为 NULL 表示不提供。
    muvik_video_source* (*create_video_source)(void);
    muvik_scraper* (*create_scraper)(void);
} muvik_plugin_descriptor;

// 插件唯一导出入口。主程序通过 dlsym 获取。
// 名称固定为 "muvik_plugin_entry_v1"。
typedef const muvik_plugin_descriptor* (*muvik_plugin_entry_fn)(void);

#define MUVIK_PLUGIN_ENTRY_SYMBOL "muvik_plugin_entry_v1"

#ifdef __cplusplus
} // extern "C"
#endif
