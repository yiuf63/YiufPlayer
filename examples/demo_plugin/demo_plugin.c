// ============================================================================
// DeckPlayer 示例插件：演示如何用纯 C ABI 同时提供「视频源」与「刮削器」。
//
// 这个插件不访问网络，返回假的演示数据，用于验证 ABI 链路是否打通。
// 真实插件可以在此基础上接入 Emby / Jellyfin / 哔哩哔哩 / TMDB 等。
//
// 编译（macOS）：
//   clang -shared -fPIC -o demo_plugin.dylib demo_plugin.c
// 编译（Linux）：
//   gcc -shared -fPIC -o demo_plugin.so demo_plugin.c
//
// 安装：
//   mkdir -p ~/.config/deck-player/plugins
//   cp demo_plugin.* ~/.config/deck-player/plugins/
// ============================================================================

#include "deck_plugin_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------

static char* dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static const deck_host_api* g_host = NULL;

// ---------------------------------------------------------------------------
// 视频源实现
// ---------------------------------------------------------------------------

static const char* vs_get_id(void* impl) { (void)impl; return "demo_source"; }

static const char* vs_get_display_name(void* impl) {
    (void)impl;
    return "演示视频源 (Sample)";
}

static int vs_needs_config(void* impl) { (void)impl; return 0; }

static deck_status vs_init(void* impl, const char* config_json,
                           const deck_host_api* host) {
    (void)impl; (void)config_json;
    g_host = host;
    if (g_host && g_host->log) g_host->log(0, "演示视频源已初始化");
    return DECK_OK;
}

static deck_status vs_list_items(void* impl, const char* path,
                                 deck_media_list** out_list) {
    (void)impl;
    (void)path;
    if (!out_list) return DECK_ERR_GENERIC;

    // 构造两条演示条目
    deck_media_list* list = (deck_media_list*)calloc(1, sizeof(deck_media_list));
    if (!list) return DECK_ERR_GENERIC;

    list->count = 2;
    list->items = (deck_media_item*)calloc(2, sizeof(deck_media_item));
    if (!list->items) {
        free(list);
        return DECK_ERR_GENERIC;
    }

    list->items[0].name = dup_str("演示影片一.mp4");
    list->items[0].path = dup_str("demo://movie/1");
    list->items[0].type = DECK_MEDIA_VIDEO;
    list->items[0].size = 1024LL * 1024 * 700;
    list->items[0].duration = 7200;

    list->items[1].name = dup_str("演示文件夹");
    list->items[1].path = dup_str("demo://folder/1");
    list->items[1].type = DECK_MEDIA_DIRECTORY;
    list->items[1].size = -1;
    list->items[1].duration = -1;

    *out_list = list;
    return DECK_OK;
}

static deck_status vs_resolve_play_url(void* impl, const char* path,
                                       char** out_url) {
    (void)impl;
    if (!path || !out_url) return DECK_ERR_GENERIC;
    // 真实插件在这里换取带鉴权的播放直链
    *out_url = dup_str(path);
    return *out_url ? DECK_OK : DECK_ERR_GENERIC;
}

static void vs_free_list(deck_media_list* list) {
    if (!list) return;
    if (list->items) {
        for (size_t i = 0; i < list->count; ++i) {
            free((void*)list->items[i].name);
            free((void*)list->items[i].path);
            free((void*)list->items[i].thumbnail);
        }
        free(list->items);
    }
    free(list);
}

static void vs_destroy(void* impl) { (void)impl; }

// ---------------------------------------------------------------------------
// 刮削器实现
// ---------------------------------------------------------------------------

static const char* sc_get_id(void* impl) { (void)impl; return "demo_scraper"; }
static const char* sc_get_display_name(void* impl) {
    (void)impl;
    return "演示刮削器 (Sample)";
}
static const char* sc_get_category(void* impl) { (void)impl; return "movie"; }

static deck_status sc_init(void* impl, const char* config_json,
                           const deck_host_api* host) {
    (void)impl; (void)config_json;
    g_host = host;
    return DECK_OK;
}

static deck_status sc_search(void* impl, const char* query, int64_t year,
                             char** out_json) {
    (void)impl; (void)year;
    if (!out_json) return DECK_ERR_GENERIC;

    // 演示如何通过宿主发 HTTP 请求（插件自身不链接任何网络库）。
    // 把 DECK_DEMO_HTTP_TARGET 编译进插件即可指向真实 API；
    // 未配置时退化为本地演示数据，保证离线可用。
    const char* target = getenv("DECK_DEMO_HTTP_TARGET");
    if (g_host && g_host->http_request && target && *target) {
        char* body = NULL;
        size_t len = 0;
        deck_status st = g_host->http_request("GET", target, NULL, NULL, &body, &len);
        if (st == DECK_OK && body) {
            if (g_host->log) g_host->log(0, "刮削器已通过宿主成功获取远程数据");
            *out_json = dup_str(body);
            g_host->free_buffer(body);
            if (*out_json) return DECK_OK;
        }
        if (body) g_host->free_buffer(body);
        if (g_host->log) g_host->log(1, "宿主 HTTP 不可用，回退到本地演示数据");
    }

    // 回退：本地演示候选
    char buf[512];
    snprintf(buf, sizeof(buf),
             "[{\"id\":\"demo-1\",\"title\":\"%s (演示结果)\",\"year\":2024,"
             "\"overview\":\"这是示例刮削结果\"}]",
             query ? query : "");
    *out_json = dup_str(buf);
    return *out_json ? DECK_OK : DECK_ERR_GENERIC;
}

static deck_status sc_fetch_detail(void* impl, const char* candidate_id,
                                   deck_scrape_result** out_result) {
    (void)impl;
    if (!candidate_id || !out_result) return DECK_ERR_GENERIC;

    deck_scrape_result* r = (deck_scrape_result*)calloc(1, sizeof(deck_scrape_result));
    if (!r) return DECK_ERR_GENERIC;
    r->title = dup_str("演示影片");
    r->overview = dup_str("这是演示用的剧情简介。");
    r->poster_url = dup_str("https://example.invalid/poster.jpg");
    r->backdrop_url = NULL;
    r->release_date = dup_str("2024-01-01");
    r->genres = dup_str("剧情,科幻");
    r->rating = 8.5;
    r->runtime_minutes = 120;
    *out_result = r;
    return DECK_OK;
}

static void sc_free_result(deck_scrape_result* r) {
    if (!r) return;
    free((void*)r->title);
    free((void*)r->overview);
    free((void*)r->poster_url);
    free((void*)r->backdrop_url);
    free((void*)r->release_date);
    free((void*)r->genres);
    free(r);
}

static void sc_destroy(void* impl) { (void)impl; }

// ---------------------------------------------------------------------------
// 扩展点实例
// ---------------------------------------------------------------------------

static deck_video_source g_video_source = {
    .impl = NULL,
    .get_id = vs_get_id,
    .get_display_name = vs_get_display_name,
    .needs_config = vs_needs_config,
    .init = vs_init,
    .list_items = vs_list_items,
    .resolve_play_url = vs_resolve_play_url,
    .free_list = vs_free_list,
    .destroy = vs_destroy,
};

static deck_scraper g_scraper = {
    .impl = NULL,
    .get_id = sc_get_id,
    .get_display_name = sc_get_display_name,
    .get_media_category = sc_get_category,
    .init = sc_init,
    .search = sc_search,
    .fetch_detail = sc_fetch_detail,
    .free_result = sc_free_result,
    .destroy = sc_destroy,
};

static deck_video_source* create_video_source(void) { return &g_video_source; }
static deck_scraper* create_scraper(void) { return &g_scraper; }

// ---------------------------------------------------------------------------
// 插件入口
// ---------------------------------------------------------------------------

static const deck_plugin_descriptor g_descriptor = {
    .abi_version = DECK_PLAYER_ABI_VERSION,
    .plugin_name = "Demo Plugin",
    .plugin_version = "1.0.0",
    .plugin_author = "DeckPlayer",
    .create_video_source = create_video_source,
    .create_scraper = create_scraper,
};

const deck_plugin_descriptor* deck_player_plugin_entry_v1(void) {
    return &g_descriptor;
}
