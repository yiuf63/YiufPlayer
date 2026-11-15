#include <set>
#include <string>
#include <vector>
#include "utils/webdav_client.hpp"
#include "utils/crypto_utils.hpp"

#if !defined(TESTING_WITHOUT_BOREALIS)
#include <borealis/core/logger.hpp>
#else
namespace brls {
class Logger {
public:
    template<typename... Args> static void info(Args&&...) {}
    template<typename... Args> static void warning(Args&&...) {}
    template<typename... Args> static void error(Args&&...) {}
};
}
#endif
#include <curl/curl.h>

#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace muvik {

namespace {

size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

// 大小写不敏感的子串查找
[[maybe_unused]] const char* findCi(const char* haystack, const char* needle) {
    if (!haystack || !needle) return nullptr;
    size_t nlen = strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return p;
    }
    return nullptr;
}

const char* findCiRange(const char* begin, const char* end, const char* needle) {
    if (!begin || !end || !needle || begin >= end) return nullptr;
    size_t nlen = strlen(needle);
    for (const char* p = begin; p + nlen <= end; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return p;
    }
    return nullptr;
}

// 提取标签内的内容，例如 <d:href>...</d:href>
std::string extractTagContent(const char* start, const char* end, const std::string& tagName) {
    std::string openTag = "<" + tagName;
    const char* p = findCiRange(start, end, openTag.c_str());
    if (!p) {
        // 尝试无命名空间查找
        size_t colon = tagName.find(':');
        if (colon != std::string::npos) {
            std::string bareTag = "<" + tagName.substr(colon + 1);
            p = findCiRange(start, end, bareTag.c_str());
        }
    }
    if (!p) return "";

    // 寻找 '>'
    p = static_cast<const char*>(memchr(p, '>', end - p));
    if (!p) return "";
    p++; // 跳过 '>'

    // 寻找闭合标签
    std::string closeTag = "</" + tagName;
    const char* c = findCiRange(p, end, closeTag.c_str());
    if (!c) {
        size_t colon = tagName.find(':');
        if (colon != std::string::npos) {
            std::string bareClose = "</" + tagName.substr(colon + 1);
            c = findCiRange(p, end, bareClose.c_str());
        }
    }
    if (!c) return "";

    return std::string(p, c - p);
}

} // namespace

std::string WebDAVClient::urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() &&
            isxdigit(static_cast<unsigned char>(in[i+1])) &&
            isxdigit(static_cast<unsigned char>(in[i+2]))) {
            char hex[3] = { in[i+1], in[i+2], 0 };
            out += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string WebDAVClient::urlEncode(const std::string& in) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : in) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << (static_cast<int>(c) & 0xff);
        }
    }
    return escaped.str();
}

bool WebDAVClient::isMediaExtension(const std::string& filename) {
    static const std::vector<std::string> exts = {
        ".mp4", ".mkv", ".webm", ".mov", ".m4v", ".avi", ".flv", ".wmv",
        ".ts", ".m2ts", ".mpg", ".mpeg", ".rmvb", ".rm", ".3gp", ".ogv",
        ".flac", ".wav", ".ape", ".aiff", ".mp3", ".m4a", ".aac", ".ogg",
        ".opus", ".wma", ".mka", ".ac3", ".dts", ".iso"
    };
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& ext : exts) {
        if (lower.size() >= ext.size() &&
            lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
            return true;
        }
    }
    return false;
}

bool WebDAVClient::testConnection(const NetworkSource& source, std::string& err_msg) {
    if (source.url.empty()) {
        err_msg = "服务器地址不能为空";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_msg = "初始化网络客户端失败";
        return false;
    }

    std::string read_buffer;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Depth: 0");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");

    if (!source.username.empty()) {
        std::string auth_raw = source.username + ":" + source.password;
        std::string auth_header = "Authorization: Basic " + crypto::base64Encode(auth_raw);
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    // PROPFIND 请求
    static const char* kPropfindXml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:resourcetype/></D:prop></D:propfind>";

    curl_easy_setopt(curl, CURLOPT_URL, source.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, kPropfindXml);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Muvik/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err_msg = std::string("连接失败: ") + curl_easy_strerror(res);
        return false;
    }

    if (http_code == 401) {
        err_msg = "认证失败：用户名或密码错误 (HTTP 401)";
        return false;
    } else if (http_code == 403) {
        err_msg = "拒绝访问：无权限访问此目录 (HTTP 403)";
        return false;
    } else if (http_code == 404) {
        err_msg = "未找到服务：请检查 WebDAV 路径是否正确 (HTTP 404)";
        return false;
    } else if (http_code != 200 && http_code != 207) {
        err_msg = "服务器返回异常状态码: HTTP " + std::to_string(http_code);
        return false;
    }

    return true;
}

bool WebDAVClient::listDirectory(const NetworkSource& source,
                                const std::string& path,
                                std::vector<MediaItem>& out_items,
                                std::string& err_msg) {
    out_items.clear();

    if (source.url.empty()) {
        err_msg = "服务器地址为空";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_msg = "初始化网络客户端失败";
        return false;
    }

    // 拼接请求 URL
    std::string req_url = source.url;
    while (!req_url.empty() && req_url.back() == '/') {
        req_url.pop_back();
    }

    std::string sub = path;
    if (!sub.empty() && sub != "/") {
        if (sub.front() != '/') req_url += "/";
        req_url += sub;
    }
    // 目录以斜杠结尾
    if (req_url.back() != '/') {
        req_url += "/";
    }

    std::string read_buffer;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Depth: 1");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");

    if (!source.username.empty()) {
        std::string auth_raw = source.username + ":" + source.password;
        std::string auth_header = "Authorization: Basic " + crypto::base64Encode(auth_raw);
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    static const char* kPropfindXml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "<D:prop><D:displayname/><D:resourcetype/><D:getcontentlength/>"
        "<D:getcontenttype/></D:prop></D:propfind>";

    curl_easy_setopt(curl, CURLOPT_URL, req_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, kPropfindXml);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Muvik/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err_msg = std::string("网络请求错误: ") + curl_easy_strerror(res);
        return false;
    }

    if (http_code == 401) {
        err_msg = "认证失败，请检查账号密码 (HTTP 401)";
        return false;
    } else if (http_code != 200 && http_code != 207) {
        err_msg = "服务器返回错误状态码: HTTP " + std::to_string(http_code);
        return false;
    }

    // 解析 WebDAV XML 响应中的 <D:response>
    const char* cursor = read_buffer.c_str();
    const char* end = cursor + read_buffer.size();

    // 确定当前请求自身的规范路径，用于跳过自身项
    std::string self_norm = urlDecode(sub.empty() ? "/" : sub);
    while (self_norm.size() > 1 && self_norm.back() == '/') self_norm.pop_back();

    while (cursor < end) {
        const char* resp_start = findCiRange(cursor, end, "<d:response");
        if (!resp_start) {
            resp_start = findCiRange(cursor, end, "<response");
        }
        if (!resp_start) break;

        const char* resp_end = findCiRange(resp_start, end, "</d:response>");
        if (!resp_end) {
            resp_end = findCiRange(resp_start, end, "</response>");
        }
        if (!resp_end) break;
        resp_end += 13; // 跳过闭合标签

        // 提取 href
        std::string raw_href = extractTagContent(resp_start, resp_end, "d:href");
        if (raw_href.empty()) raw_href = extractTagContent(resp_start, resp_end, "href");

        std::string href = urlDecode(raw_href);
        while (href.size() > 1 && href.back() == '/') href.pop_back();

        // 判断是否为目录（<d:collection/> 或 <collection/>）
        bool is_dir = (findCiRange(resp_start, resp_end, "<d:collection") != nullptr) ||
                      (findCiRange(resp_start, resp_end, "<collection") != nullptr);

        // 提取显示名
        std::string name = extractTagContent(resp_start, resp_end, "d:displayname");
        if (name.empty()) name = extractTagContent(resp_start, resp_end, "displayname");
        if (name.empty()) {
            size_t last_slash = href.find_last_of('/');
            if (last_slash != std::string::npos && last_slash + 1 < href.size()) {
                name = href.substr(last_slash + 1);
            } else {
                name = href;
            }
        }
        name = urlDecode(name);

        // 跳过自身项
        if (href == self_norm || (href.size() >= self_norm.size() &&
            href.compare(href.size() - self_norm.size(), self_norm.size(), self_norm) == 0 &&
            name.empty())) {
            cursor = resp_end;
            continue;
        }
        // 如果名字是当前目录的最后一段，且与自身匹配，也跳过
        if (!self_norm.empty()) {
            size_t slash = self_norm.find_last_of('/');
            std::string self_last = (slash != std::string::npos) ? self_norm.substr(slash + 1) : self_norm;
            if (!self_last.empty() && name == self_last && is_dir) {
                cursor = resp_end;
                continue;
            }
        }

        // 提取文件大小
        std::string len_str = extractTagContent(resp_start, resp_end, "d:getcontentlength");
        if (len_str.empty()) len_str = extractTagContent(resp_start, resp_end, "getcontentlength");
        int64_t file_size = -1;
        if (!len_str.empty()) {
            try { file_size = std::stoll(len_str); } catch (...) {}
        }

        // 过滤非媒体文件
        if (!is_dir && !isMediaExtension(name)) {
            cursor = resp_end;
            continue;
        }

        MediaItem item;
        item.name = name;
        item.size = file_size;
        item.duration = -1;

        if (is_dir) {
            item.type = MediaType::Directory;
            item.path = raw_href; // 记录原始 href 或规范路径
        } else {
            // 根据后缀判断是视频还是音频
            static const std::vector<std::string> audio_exts = {
                ".flac", ".wav", ".ape", ".aiff", ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".wma"
            };
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            bool is_audio = false;
            for (const auto& ae : audio_exts) {
                if (lower.size() >= ae.size() &&
                    lower.compare(lower.size() - ae.size(), ae.size(), ae) == 0) {
                    is_audio = true;
                    break;
                }
            }
            item.type = is_audio ? MediaType::Audio : MediaType::Video;
            item.path = raw_href;
        }

        out_items.push_back(std::move(item));
        cursor = resp_end;
    }

    return true;
}

std::string WebDAVClient::buildPlayUrl(const NetworkSource& source, const std::string& path) {
    if (path.empty()) return "";

    std::string base = source.url;
    while (!base.empty() && base.back() == '/') base.pop_back();

    std::string play_url;
    if (path.find("://") != std::string::npos) {
        play_url = path;
    } else {
        // 如果 path 已经包含完整的服务器路径（例如 /dav/movies/1.mkv）
        // 检查 base 的 host 根
        size_t scheme_pos = base.find("://");
        std::string host_root = base;
        if (scheme_pos != std::string::npos) {
            size_t path_pos = base.find('/', scheme_pos + 3);
            if (path_pos != std::string::npos) {
                host_root = base.substr(0, path_pos);
            }
        }

        if (!path.empty() && path.front() == '/') {
            play_url = host_root + path;
        } else {
            play_url = base + "/" + path;
        }
    }

    // 嵌入 Basic 认证凭证: http://user:pass@host:port/...
    if (!source.username.empty()) {
        size_t scheme_end = play_url.find("://");
        if (scheme_end != std::string::npos) {
            // 确保还没有 @
            size_t next_slash = play_url.find('/', scheme_end + 3);
            std::string auth_part = play_url.substr(scheme_end + 3,
                (next_slash == std::string::npos ? play_url.size() : next_slash) - (scheme_end + 3));

            if (auth_part.find('@') == std::string::npos) {
                std::string scheme = play_url.substr(0, scheme_end + 3);
                std::string rest = play_url.substr(scheme_end + 3);
                play_url = scheme + source.username + ":" + source.password + "@" + rest;
            }
        }
    }

    return play_url;
}

} // namespace muvik
