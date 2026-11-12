// ============================================================================
// WebDAV 视频源插件（Muvik 示例 / 参考实现）
//
// 这是一个**可实际工作**的插件，演示如何：
//   1. 用纯 C ABI 实现一个真实的视频源扩展点
//   2. 通过宿主提供的 http_request 访问网络（插件自身不链接任何网络库）
//   3. 解析 WebDAV 的 PROPFIND 响应（XML）并转换为媒体条目
//   4. 处理鉴权、错误码与内存所有权
//
// 适用服务：Alist、群晖 WebDAV、坚果云、Nextcloud、通用 WebDAV 服务器。
//
// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------
// 插件通过环境变量读取配置（真实产品中可由宿主传入 config_json）：
//
//   MUVIK_WEBDAV_URL      服务器地址，如 http://192.168.1.10:5244/dav
//   MUVIK_WEBDAV_USER     用户名（可空）
//   MUVIK_WEBDAV_PASS     密码（可空）
//
// 编译：
//   clang -shared -fPIC -I <muvik>/include/plugin \
//         -o webdav.dylib webdav_plugin.c
//
// 安装：
//   mkdir -p ~/.config/muvik/plugins
//   cp webdav.dylib ~/.config/muvik/plugins/
// ============================================================================

#include "muvik_plugin_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// 内存工具
// ---------------------------------------------------------------------------

static char* dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

// 释放媒体列表的全部内容
static void free_media_list(muvik_media_list* list) {
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

// ---------------------------------------------------------------------------
// 插件状态
// ---------------------------------------------------------------------------

typedef struct {
    char base_url[1024];   // 不含结尾斜杠
    char user[128];
    char pass[128];
    char auth_header[512]; // "Authorization: Basic xxx"
    const muvik_host_api* host;
} webdav_state;

// ---------------------------------------------------------------------------
// Base64（用于 Basic 鉴权）
// ---------------------------------------------------------------------------

static void base64_encode(const char* in, char* out, size_t out_size) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(in);
    size_t o = 0;
    for (size_t i = 0; i < len && o + 4 < out_size; i += 3) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < len) v |= (unsigned char)in[i + 1] << 8;
        if (i + 2 < len) v |= (unsigned char)in[i + 2];
        out[o++] = tbl[(v >> 18) & 0x3F];
        out[o++] = tbl[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? tbl[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? tbl[v & 0x3F] : '=';
    }
    out[o] = '\0';
}

// ---------------------------------------------------------------------------
// 极简 XML 扫描
//
// 不引入 XML 库：WebDAV 的 PROPFIND 响应结构固定，
// 只需提取 <D:response> 块内的 href 与 resourcetype 即可。
// 注意不同服务器前缀不同（D:、d:、lp1: 或无前缀），故统一做宽松匹配。
// ---------------------------------------------------------------------------

// 大小写不敏感的查找（返回首次出现位置）
static const char* find_ci(const char* hay, const char* needle) {
    if (!hay || !needle) return NULL;
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

// 在 [begin, end) 范围内查找。必须使用此版本判断条目属性，
// 否则会匹配到后续 response 块的内容，导致误判
// （实测症状：某个目录出现在后面时，前面的所有文件都被当成目录）。
static const char* find_ci_range(const char* begin, const char* end,
                                 const char* needle) {
    if (!begin || !end || !needle || begin >= end) return NULL;
    size_t nlen = strlen(needle);
    for (const char* p = begin; p + nlen <= end; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

// 从 href 中取出最后一段作为显示名，并做简单的百分号解码
static void decode_percent(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_size; ++i) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i+1]) &&
            isxdigit((unsigned char)in[i+2])) {
            char hex[3] = { in[i+1], in[i+2], 0 };
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

// 判断扩展名是否为媒体（与服务端无关的本地判断）
static int has_media_ext(const char* name) {
    static const char* exts[] = {
        ".mp4",".mkv",".webm",".mov",".m4v",".avi",".flv",".wmv",
        ".ts",".m2ts",".mpg",".mpeg",".rmvb",".rm",".3gp",".ogv",
        ".flac",".wav",".ape",".aiff",".mp3",".m4a",".aac",".ogg",
        ".opus",".wma",".mka",".ac3",".dts"
    };
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
        size_t e = strlen(exts[i]);
        if (n >= e && strcasecmp(name + n - e, exts[i]) == 0) return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 扩展点实现
// ---------------------------------------------------------------------------

static const char* vs_get_id(void* impl) { (void)impl; return "webdav"; }

static const char* vs_get_display_name(void* impl) {
    webdav_state* st = (webdav_state*)impl;
    if (!st || !st->base_url[0]) return "WebDAV (未配置)";
    return "WebDAV";
}

static int vs_needs_config(void* impl) {
    webdav_state* st = (webdav_state*)impl;
    return !(st && st->base_url[0]);
}

// 从形如 {"key":"value"} 的 JSON 中提取某个键的字符串值。
// 只处理本插件自己的扁平配置，不支持嵌套。
// 返回 1 表示找到。
static int json_get_string(const char* json, const char* key,
                           char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return 0;
    out[0] = '\0';

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);

    // 跳过空白与冒号
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) ++p;
    if (*p != '"') return 0;
    ++p;

    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_size) {
        if (*p == '\\' && p[1]) {   // 处理转义
            ++p;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 'r': out[o++] = '\r'; break;
                case 't': out[o++] = '\t'; break;
                default:  out[o++] = *p;   break;
            }
            ++p;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return 1;
}

static muvik_status vs_init(void* impl, const char* config_json,
                           const muvik_host_api* host) {
    webdav_state* st = (webdav_state*)impl;
    if (!st) return MUVIK_ERR_GENERIC;
    st->host = host;

    // 配置优先级：宿主传入的 config_json > 宿主 get_config > 环境变量。
    // 后者用于无 UI 的调试场景。
    char cfg_url[1024] = {0}, cfg_user[128] = {0}, cfg_pass[128] = {0};
    int has_url = json_get_string(config_json, "url", cfg_url, sizeof(cfg_url));
    int has_user = json_get_string(config_json, "user", cfg_user, sizeof(cfg_user));
    int has_pass = json_get_string(config_json, "pass", cfg_pass, sizeof(cfg_pass));

    if (!has_url && host && host->get_config) {
        const char* v = host->get_config("url");
        if (v && *v) { snprintf(cfg_url, sizeof(cfg_url), "%s", v); has_url = 1; }
        v = host->get_config("user");
        if (v) { snprintf(cfg_user, sizeof(cfg_user), "%s", v); has_user = 1; }
        v = host->get_config("pass");
        if (v) { snprintf(cfg_pass, sizeof(cfg_pass), "%s", v); has_pass = 1; }
    }

    const char* url  = has_url ? cfg_url : getenv("MUVIK_WEBDAV_URL");
    const char* user = has_user ? cfg_user : getenv("MUVIK_WEBDAV_USER");
    const char* pass = has_pass ? cfg_pass : getenv("MUVIK_WEBDAV_PASS");

    if (!url || !*url) {
        if (host && host->log) host->log(1, "WebDAV: 未设置 MUVIK_WEBDAV_URL");
        return MUVIK_ERR_NOT_AVAILABLE;
    }

    // 规范化：去掉结尾斜杠
    snprintf(st->base_url, sizeof(st->base_url), "%s", url);
    size_t len = strlen(st->base_url);
    while (len > 0 && st->base_url[len-1] == '/') st->base_url[--len] = '\0';

    if (user && *user) {
        snprintf(st->user, sizeof(st->user), "%s", user);
        snprintf(st->pass, sizeof(st->pass), "%s", pass ? pass : "");
        char cred[300];
        snprintf(cred, sizeof(cred), "%s:%s", st->user, st->pass);
        char b64[420];
        base64_encode(cred, b64, sizeof(b64));
        snprintf(st->auth_header, sizeof(st->auth_header),
                 "Authorization: Basic %s", b64);
    }

    if (host && host->log) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "WebDAV 已初始化: %s%s",
                 st->base_url, st->user[0] ? " (带鉴权)" : " (无鉴权)");
        host->log(0, msg);
    }
    return MUVIK_OK;
}

// 从路径提取最后一段作为显示名
static void last_segment(const char* path, char* out, size_t out_size) {
    if (!path) { out[0] = '\0'; return; }
    size_t len = strlen(path);
    while (len > 0 && path[len-1] == '/') --len;      // 去尾斜杠
    size_t start = len;
    while (start > 0 && path[start-1] != '/') --start;
    size_t n = len - start;
    if (n >= out_size) n = out_size - 1;
    memcpy(out, path + start, n);
    out[n] = '\0';
}

static muvik_status vs_list_items(void* impl, const char* path,
                                 muvik_media_list** out_list) {
    webdav_state* st = (webdav_state*)impl;
    if (!st || !out_list || !st->host || !st->host->http_request) {
        return MUVIK_ERR_NOT_AVAILABLE;
    }
    if (!st->base_url[0]) return MUVIK_ERR_NOT_AVAILABLE;
    *out_list = NULL;

    // 组装完整 URL。path 形如 "/" 或 "/sub/dir"
    char url[2048];
    if (!path || !*path || strcmp(path, "/") == 0) {
        snprintf(url, sizeof(url), "%s/", st->base_url);
    } else {
        const char* p = path;
        while (*p == '/') ++p;               // 避免出现双斜杠
        snprintf(url, sizeof(url), "%s/%s", st->base_url, p);
    }
    // 目录请求需要结尾斜杠
    size_t ul = strlen(url);
    if (ul + 1 < sizeof(url) && url[ul-1] != '/') {
        url[ul] = '/';
        url[ul+1] = '\0';
    }

    // PROPFIND 请求体：只要 name 与 resourcetype
    static const char* kPropfindBody =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "<D:prop><D:displayname/><D:resourcetype/><D:getcontentlength/>"
        "<D:getcontenttype/></D:prop></D:propfind>";

    // 拼接请求头
    char headers[1024];
    snprintf(headers, sizeof(headers),
             "Depth: 1\r\nContent-Type: application/xml; charset=utf-8\r\n%s",
             st->auth_header[0] ? st->auth_header : "");

    char* body = NULL;
    size_t body_len = 0;
    muvik_status rc = st->host->http_request(
        "PROPFIND", url, headers, kPropfindBody, &body, &body_len);

    if (rc == MUVIK_ERR_AUTH) {
        if (st->host->log) st->host->log(2, "WebDAV: 鉴权失败，请检查用户名与密码");
        if (body) st->host->free_buffer(body);
        return rc;
    }
    if (rc != MUVIK_OK || !body) {
        if (st->host->log) st->host->log(1, "WebDAV: 请求失败");
        if (body) st->host->free_buffer(body);
        return rc == MUVIK_OK ? MUVIK_ERR_NETWORK : rc;
    }

    // 统计 response 块数量，便于一次性分配
    size_t count = 0;
    {
        const char* p = body;
        while ((p = find_ci(p, "<d:response")) != NULL) { ++count; p += 12; }
        if (count == 0) {
            // 有些服务器不带命名空间前缀
            p = body;
            while ((p = find_ci(p, "<response")) != NULL) { ++count; p += 9; }
        }
    }

    if (count == 0) {
        if (st->host->log) st->host->log(1, "WebDAV: 响应中未找到条目");
        st->host->free_buffer(body);
        return MUVIK_ERR_GENERIC;
    }

    muvik_media_list* list = (muvik_media_list*)calloc(1, sizeof(muvik_media_list));
    if (!list) { st->host->free_buffer(body); return MUVIK_ERR_GENERIC; }
    list->items = (muvik_media_item*)calloc(count, sizeof(muvik_media_item));
    if (!list->items) { free(list); st->host->free_buffer(body); return MUVIK_ERR_GENERIC; }

    size_t emitted = 0;
    const char* cursor = body;

    while (emitted < count) {
        // 定位下一个 response 块
        const char* blk = find_ci(cursor, "<d:response");
        size_t skip = 12;
        if (!blk) { blk = find_ci(cursor, "<response"); skip = 9; }
        if (!blk) break;
        blk += skip;
        cursor = blk;

        const char* blk_end = find_ci(blk, "</d:response");
        if (!blk_end) blk_end = find_ci(blk, "</response");
        if (!blk_end) break;

        // 提取 href
        const char* h = find_ci_range(blk, blk_end, "<d:href");
        size_t hskip = 7;
        if (!h) { h = find_ci_range(blk, blk_end, "<href"); hskip = 5; }
        if (!h || h > blk_end) { cursor = blk_end; continue; }
        const char* gt = strchr(h + hskip, '>');
        const char* lt = gt ? strchr(gt + 1, '<') : NULL;
        if (!gt || !lt || lt > blk_end) { cursor = blk_end; continue; }

        char raw_href[1024];
        size_t href_len = (size_t)(lt - gt - 1);
        if (href_len >= sizeof(raw_href)) href_len = sizeof(raw_href) - 1;
        memcpy(raw_href, gt + 1, href_len);
        raw_href[href_len] = '\0';

        // 判断是目录还是文件。
        // 必须在【当前 response 块内】查找，不能搜到 body 其他位置。
        int is_dir = (find_ci_range(blk, blk_end, "<d:collection") != NULL) ||
                     (find_ci_range(blk, blk_end, "<collection") != NULL);

        // 跳过父目录自身（href 即为请求路径本身）
        char decoded[1024];
        decode_percent(raw_href, decoded, sizeof(decoded));
        char path_norm[1024];
        snprintf(path_norm, sizeof(path_norm), "%s", decoded);
        size_t pn = strlen(path_norm);
        while (pn > 1 && path_norm[pn-1] == '/') path_norm[--pn] = '\0';

        char own_norm[1024];
        const char* pp = (path && *path) ? path : "/";
        snprintf(own_norm, sizeof(own_norm), "%s", pp);
        size_t on = strlen(own_norm);
        while (on > 1 && own_norm[on-1] == '/') own_norm[--on] = '\0';

        // 若 href 解析后与自身路径相同，说明是父项，跳过
        const char* self_tail = strstr(path_norm, own_norm);
        if (self_tail && strlen(self_tail) == strlen(own_norm) && pn == on) {
            cursor = blk_end;
            continue;
        }

        char name[512];
        last_segment(path_norm, name, sizeof(name));
        if (name[0] == '\0') { cursor = blk_end; continue; }

        // 根目录下的自身项，名称通常等于最后一段，同样跳过
        if (strcmp(path_norm, own_norm) == 0) { cursor = blk_end; continue; }

        // 文件必须是我们支持的媒体格式
        if (!is_dir && !has_media_ext(name)) {
            cursor = blk_end;
            continue;
        }

        muvik_media_item* it = &list->items[list->count];
        it->name = dup_str(name);
        it->path = dup_str(path_norm);
        it->thumbnail = NULL;
        it->type = is_dir ? MUVIK_MEDIA_DIRECTORY : MUVIK_MEDIA_VIDEO;
        it->size = -1;
        it->duration = -1;
        list->count++;

        if (list->count >= count) break;
        cursor = blk_end;
    }

    if (body) st->host->free_buffer(body);

    if (list->count == 0) {
        free_media_list(list);
        if (st->host->log) st->host->log(1, "WebDAV: 该目录下没有可用媒体");
        return MUVIK_ERR_NOT_AVAILABLE;
    }

    *out_list = list;
    return MUVIK_OK;
}

static muvik_status vs_resolve_play_url(void* impl, const char* path,
                                       char** out_url) {
    webdav_state* st = (webdav_state*)impl;
    if (!st || !out_url || !path) return MUVIK_ERR_GENERIC;
    *out_url = NULL;

    if (!st->base_url[0]) return MUVIK_ERR_NOT_AVAILABLE;

    // 构造直接播放地址；若有鉴权，把凭证内嵌到 URL 中，
    // 因为播放器（libmpv）只接受 URL，无法传递自定义请求头。
    char url[3072];
    const char* p = path;
    while (*p == '/') ++p;

    if (st->user[0]) {
        // 在 scheme 之后插入 user:pass@
        const char* scheme_end = strstr(st->base_url, "://");
        if (scheme_end) {
            size_t scheme_len = (size_t)(scheme_end - st->base_url) + 3;
            char scheme[64];
            snprintf(scheme, sizeof(scheme), "%.*s", (int)scheme_len, st->base_url);
            const char* rest = st->base_url + scheme_len;
            snprintf(url, sizeof(url), "%s%s:%s@%s/%s",
                     scheme, st->user, st->pass, rest, p);
        } else {
            snprintf(url, sizeof(url), "%s/%s", st->base_url, p);
        }
    } else {
        snprintf(url, sizeof(url), "%s/%s", st->base_url, p);
    }

    *out_url = dup_str(url);
    return *out_url ? MUVIK_OK : MUVIK_ERR_GENERIC;
}

static void vs_free_list(muvik_media_list* list) { free_media_list(list); }

static void vs_destroy(void* impl) { free(impl); }

static void* vs_create_impl(void) {
    return calloc(1, sizeof(webdav_state));
}

// ---------------------------------------------------------------------------
// 扩展点实例
// ---------------------------------------------------------------------------

static muvik_video_source g_video_source = {
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

static muvik_video_source* create_video_source(void) {
    void* impl = vs_create_impl();
    if (!impl) return NULL;
    muvik_video_source* src = (muvik_video_source*)malloc(sizeof(muvik_video_source));
    if (!src) { free(impl); return NULL; }
    *src = g_video_source;
    src->impl = impl;
    return src;
}

// ---------------------------------------------------------------------------
// 插件入口
// ---------------------------------------------------------------------------

static const muvik_plugin_descriptor g_descriptor = {
    .abi_version = MUVIK_ABI_VERSION,
    .plugin_name = "WebDAV Source",
    .plugin_version = "1.0.0",
    .plugin_author = "Muvik",
    .create_video_source = create_video_source,
    .create_scraper = NULL,
};

const muvik_plugin_descriptor* muvik_plugin_entry_v1(void) {
    return &g_descriptor;
}
