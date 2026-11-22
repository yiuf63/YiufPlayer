#include <chrono>
#include <string>
#include <vector>
#include "plugin/js_runtime.hpp"
#include "plugin/plugin_config_store.hpp"
#include "utils/crypto_utils.hpp"
#include "utils/network_utils.hpp"
#include <borealis/core/logger.hpp>
#if !defined(ANDROID)
// Android 版本不链接 libcurl（NDK 不提供系统 curl，需要自行交叉编译），
// CMakeLists 在 ANDROID 分支里把 CURL_FOUND 置为 FALSE 并不做 find_package，
// 所以这里必须同样用 #if 包住，否则 Android 编译会直接
//   fatal error: 'curl/curl.h' file not found
// （实测：build.gradle 原样的参数在 NDK 28 下 34/149 个目标后中断）。
//
// 对照：plugin_manager.cpp 的 C ABI HTTP 实现早就有这个守卫，
// JS 侧与 webdav_client.cpp 都漏了 —— 是同一处疏漏的两个副本。
#include <curl/curl.h>
#endif
#include <sstream>
#include <cstring>
#include <cctype>
#include <algorithm>

extern "C" {
#include "quickjs.h"
}

namespace muvik {

namespace {

#if !defined(ANDROID)

struct CurlWriteContext {
    std::string body;
    std::vector<std::string> headers;
};

static size_t curlWriteBodyCallback(void* ptr, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* ctx = static_cast<CurlWriteContext*>(userp);
    ctx->body.append(static_cast<char*>(ptr), total);
    return total;
}

static size_t curlWriteHeaderCallback(void* ptr, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* ctx = static_cast<CurlWriteContext*>(userp);
    std::string line(static_cast<char*>(ptr), total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    if (!line.empty()) {
        ctx->headers.push_back(line);
    }
    return total;
}

// 供 JS 调用的 host.http
static JSValue jsHostHttp(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "host.http expects options object");
    }

    JSValue opt = argv[0];
    JSValue url_val = JS_GetPropertyStr(ctx, opt, "url");
    if (!JS_IsString(url_val)) {
        JS_FreeValue(ctx, url_val);
        return JS_ThrowTypeError(ctx, "options.url is required");
    }
    const char* url_cstr = JS_ToCString(ctx, url_val);
    std::string url = url_cstr ? url_cstr : "";
    JS_FreeCString(ctx, url_cstr);
    JS_FreeValue(ctx, url_val);

    // scheme 白名单：只允许 http / https。
    //
    // libcurl 默认支持 file://、scp://、dict:// 等一堆协议。不设限的话，
    // 插件一句 host.http({url:"file:///etc/passwd"}) 就能把宿主能读的
    // 任意文件读出来再外发 —— 沙箱等于不存在。
    // 实测：加上之前，file:///etc/hosts 会返回 status=0 与 213 字节内容。
    //
    // 注意 C ABI 路径（plugin_manager.cpp 的 host_http_request）本来就有
    // 这个检查，只有 JS 侧漏了。两处现在一致。
    {
        // 大小写不敏感地判断前缀，且要求 "%3a" 之类编码不能绕过：
        // 这里只接受字面 "http://" 或 "https://" 开头。
        auto startsWithCi = [](const std::string& s, const char* prefix) {
            const size_t n = std::strlen(prefix);
            if (s.size() < n) return false;
            for (size_t i = 0; i < n; ++i) {
                if (std::tolower(static_cast<unsigned char>(s[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) {
                    return false;
                }
            }
            return true;
        };
        // 去掉前导空白，避免 " file://" 之类的绕过
        size_t first = url.find_first_not_of(" \t\r\n");
        std::string trimmed = (first == std::string::npos) ? "" : url.substr(first);

        if (!startsWithCi(trimmed, "http://") && !startsWithCi(trimmed, "https://")) {
            brls::Logger::warning("插件 host.http 被拒绝（仅允许 http/https）: {}",
                                  url.substr(0, 64));
            return JS_ThrowTypeError(ctx, "host.http only supports http:// and https:// URLs");
        }
    }

    std::string method = "GET";
    JSValue method_val = JS_GetPropertyStr(ctx, opt, "method");
    if (JS_IsString(method_val)) {
        const char* m = JS_ToCString(ctx, method_val);
        if (m) {
            method = m;
            std::transform(method.begin(), method.end(), method.begin(), ::toupper);
            JS_FreeCString(ctx, m);
        }
    }
    JS_FreeValue(ctx, method_val);

    int timeout = 15;
    JSValue timeout_val = JS_GetPropertyStr(ctx, opt, "timeout");
    if (JS_IsNumber(timeout_val)) {
        int32_t t = 15;
        JS_ToInt32(ctx, &t, timeout_val);
        if (t > 0 && t <= 120) timeout = t;
    }
    JS_FreeValue(ctx, timeout_val);

    std::string post_body;
    JSValue body_val = JS_GetPropertyStr(ctx, opt, "body");
    if (JS_IsString(body_val)) {
        const char* b = JS_ToCString(ctx, body_val);
        if (b) {
            post_body = b;
            JS_FreeCString(ctx, b);
        }
    }
    JS_FreeValue(ctx, body_val);

    // 请求头解析（支持对象或数组）
    std::vector<std::string> request_headers;
    JSValue headers_val = JS_GetPropertyStr(ctx, opt, "headers");
    if (JS_IsObject(headers_val)) {
        JSPropertyEnum* tab = nullptr;
        uint32_t len = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, headers_val, JS_GPN_STRING_MASK) == 0) {
            for (uint32_t i = 0; i < len; ++i) {
                const char* key = JS_AtomToCString(ctx, tab[i].atom);
                JSValue val = JS_GetProperty(ctx, headers_val, tab[i].atom);
                const char* val_cstr = JS_ToCString(ctx, val);
                if (key && val_cstr) {
                    request_headers.push_back(std::string(key) + ": " + val_cstr);
                }
                if (key) JS_FreeCString(ctx, key);
                if (val_cstr) JS_FreeCString(ctx, val_cstr);
                JS_FreeValue(ctx, val);
            }
            js_free(ctx, tab);
        }
    }
    JS_FreeValue(ctx, headers_val);

    // 执行 libcurl
#if defined(ANDROID)
    // Android 未链接 libcurl（见文件头说明）。返回明确的错误，
    // 让插件 UI 显示「当前平台不支持网络请求」而不是静默超时。
    return JS_ThrowInternalError(ctx,
        "host.http is not available on this platform (libcurl not linked)");
#else
    CURL* curl = curl_easy_init();
    if (!curl) {
        return JS_ThrowInternalError(ctx, "curl_easy_init failed");
    }

    CurlWriteContext write_ctx;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWriteHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &write_ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Muvik/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

    struct curl_slist* header_list = nullptr;
    for (const auto& h : request_headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }
    if (header_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!post_body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_body.size());
        }
    } else if (method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!post_body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_body.size());
        }
    }

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK && http_code == 0) {
        std::string err_msg = "HTTP request failed: ";
        err_msg += curl_easy_strerror(rc);
        return JS_ThrowInternalError(ctx, "%s", err_msg.c_str());
    }

    // 构建 JS 返回结果对象
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, (int32_t)http_code));
    JS_SetPropertyStr(ctx, result, "statusText", JS_NewString(ctx, (http_code >= 200 && http_code < 300) ? "OK" : "Error"));
    JS_SetPropertyStr(ctx, result, "body", JS_NewStringLen(ctx, write_ctx.body.data(), write_ctx.body.size()));

    JSValue resp_headers = JS_NewObject(ctx);
    for (const auto& h : write_ctx.headers) {
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string k = h.substr(0, colon);
            std::string v = h.substr(colon + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
            JS_SetPropertyStr(ctx, resp_headers, k.c_str(), JS_NewString(ctx, v.c_str()));
        }
    }
    JS_SetPropertyStr(ctx, result, "headers", resp_headers);
    return result;
#endif  // !defined(ANDROID)：curl 实现结束
}

// 供 JS 调用的 host.storage
static JSValue jsHostStorageGet(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_UNDEFINED;
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (!host) return JS_UNDEFINED;

    const char* k = JS_ToCString(ctx, argv[0]);
    std::string key = k ? k : "";
    JS_FreeCString(ctx, k);

    std::string def_val = "";
    if (argc >= 2 && JS_IsString(argv[1])) {
        const char* d = JS_ToCString(ctx, argv[1]);
        if (d) { def_val = d; JS_FreeCString(ctx, d); }
    }

    std::string val = PluginConfigStore::getInstance().get(host->getPluginId(), key);
    if (val.empty() && !def_val.empty()) val = def_val;
    return JS_NewString(ctx, val.c_str());
}

static JSValue jsHostStorageSet(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsString(argv[1])) return JS_UNDEFINED;
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (!host) return JS_UNDEFINED;

    const char* k = JS_ToCString(ctx, argv[0]);
    const char* v = JS_ToCString(ctx, argv[1]);
    std::string key = k ? k : "";
    std::string val = v ? v : "";
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, v);

    PluginConfigStore::getInstance().set(host->getPluginId(), key, val);
    PluginConfigStore::getInstance().save();
    return JS_UNDEFINED;
}

static JSValue jsHostStorageRemove(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_UNDEFINED;
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (!host) return JS_UNDEFINED;

    const char* k = JS_ToCString(ctx, argv[0]);
    std::string key = k ? k : "";
    JS_FreeCString(ctx, k);

    PluginConfigStore::getInstance().set(host->getPluginId(), key, "");
    PluginConfigStore::getInstance().save();
    return JS_UNDEFINED;
}

// 供 JS 调用的 host.getConfig
static JSValue jsHostGetConfig(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_UNDEFINED;
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (!host) return JS_UNDEFINED;

    const char* k = JS_ToCString(ctx, argv[0]);
    std::string key = k ? k : "";
    JS_FreeCString(ctx, k);

    std::string val = PluginConfigStore::getInstance().get(host->getPluginId(), key);
    return JS_NewString(ctx, val.c_str());
}

// 供 JS 调用的 host.crypto
static JSValue jsHostCryptoMd5(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(ctx, "");
    const char* str = JS_ToCString(ctx, argv[0]);
    std::string res = crypto::md5(str ? str : "");
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx, res.c_str());
}

static JSValue jsHostCryptoSha256(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(ctx, "");
    const char* str = JS_ToCString(ctx, argv[0]);
    std::string res = crypto::sha256(str ? str : "");
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx, res.c_str());
}

static JSValue jsHostCryptoHmacSha256(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsString(argv[1])) return JS_NewString(ctx, "");
    const char* key_cstr = JS_ToCString(ctx, argv[0]);
    const char* data_cstr = JS_ToCString(ctx, argv[1]);
    std::string res = crypto::hmacSha256(key_cstr ? key_cstr : "", data_cstr ? data_cstr : "");
    JS_FreeCString(ctx, key_cstr);
    JS_FreeCString(ctx, data_cstr);
    return JS_NewString(ctx, res.c_str());
}

static JSValue jsHostCryptoBase64Encode(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(ctx, "");
    const char* str = JS_ToCString(ctx, argv[0]);
    std::string res = crypto::base64Encode(str ? str : "");
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx, res.c_str());
}

static JSValue jsHostCryptoBase64Decode(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(ctx, "");
    const char* str = JS_ToCString(ctx, argv[0]);
    std::string res = crypto::base64Decode(str ? str : "");
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx, res.c_str());
}

// 供 JS 调用的 host.log / console.log
static void logMessage(JSContext* ctx, int level, int argc, JSValueConst* argv) {
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    std::string prefix = host ? ("[Plugin:" + host->getPluginId() + "] ") : "[Plugin] ";

    std::ostringstream ss;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) ss << " ";
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            ss << str;
            JS_FreeCString(ctx, str);
        }
    }
    std::string msg = prefix + ss.str();
    if (level == 2) brls::Logger::error("{}", msg);
    else if (level == 1) brls::Logger::warning("{}", msg);
    else brls::Logger::info("{}", msg);
}

static JSValue jsHostLog(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    logMessage(ctx, 0, argc, argv);
    return JS_UNDEFINED;
}

static JSValue jsHostLogWarn(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    logMessage(ctx, 1, argc, argv);
    return JS_UNDEFINED;
}

static JSValue jsHostLogError(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    logMessage(ctx, 2, argc, argv);
    return JS_UNDEFINED;
}

// 中断回调：防止 JS 死循环占用线程。
//
// QuickJS 会在解释器循环里周期性调用它，返回非 0 即中断执行并抛
// "interrupted" 异常。这里真正实现超时判断 —— 早先这个函数体只有
// `return 0`，注释写着「检查是否超时」却没做任何事，
// 于是 setTimeout 之类的机制全都不存在：
//   实测插件写 while(true){} 会 100% 占用调用线程且永不返回，
//   在 UI 线程上就是整个界面假死，只能强杀进程。
//
// 设计要点：
//   · is_running_ / op_start_time_ 由 callFunction 在每次调用前后维护；
//   · 只有「正在执行」时才计时，空闲时不误伤；
//   · 超时后返回 1，QuickJS 抛异常 → callFunction 走正常错误路径返回 false，
//     插件调用方拿到失败而不是进程挂死。
static int jsInterruptHandler(JSRuntime* /*rt*/, void* opaque) {
    auto* host = static_cast<JSRuntimeHost*>(opaque);
    if (!host) return 0;
    if (!host->isExecuting()) return 0;
    const int timeout = host->getExecutionTimeout();
    if (timeout <= 0) return 0;

    const auto elapsed = std::chrono::steady_clock::now() - host->getOpStartTime();
    if (elapsed > std::chrono::seconds(timeout)) {
        return 1;   // 中断执行
    }
    return 0; // 正常返回 0
}

#endif  // !defined(ANDROID)：curl 辅助函数与 host.http 实现结束

} // namespace

// ============================================================================
// JSRuntimeHost 实现
// ============================================================================

JSRuntimeHost::JSRuntimeHost(std::string plugin_id)
    : plugin_id_(std::move(plugin_id)) {
    initSandbox();
}

JSRuntimeHost::~JSRuntimeHost() {
    cleanup();
}

JSRuntimeHost::JSRuntimeHost(JSRuntimeHost&& other) noexcept
    : plugin_id_(std::move(other.plugin_id_)),
      timeout_seconds_(other.timeout_seconds_),
      rt_(other.rt_),
      ctx_(other.ctx_) {
    other.rt_ = nullptr;
    other.ctx_ = nullptr;
    if (ctx_) {
        JS_SetContextOpaque(ctx_, this);
    }
}

JSRuntimeHost& JSRuntimeHost::operator=(JSRuntimeHost&& other) noexcept {
    if (this != &other) {
        cleanup();
        plugin_id_ = std::move(other.plugin_id_);
        timeout_seconds_ = other.timeout_seconds_;
        rt_ = other.rt_;
        ctx_ = other.ctx_;
        other.rt_ = nullptr;
        other.ctx_ = nullptr;
        if (ctx_) {
            JS_SetContextOpaque(ctx_, this);
        }
    }
    return *this;
}

static JSValue jsHostGetDeviceIP(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* /*argv*/) {
    std::string ip = NetworkUtils::getLocalIPv4();
    return JS_NewString(ctx, ip.c_str());
}

static JSRuntimeHost::QRCodeHandler g_qrcode_handler = nullptr;
static JSRuntimeHost::NotifyHandler g_notify_handler = nullptr;

void JSRuntimeHost::setQRCodeHandler(QRCodeHandler handler) {
    g_qrcode_handler = std::move(handler);
}

void JSRuntimeHost::setNotifyHandler(NotifyHandler handler) {
    g_notify_handler = std::move(handler);
}

static JSValue jsHostShowQRCode(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "host.showQRCode expects an options object");
    }
    JSValue opt = argv[0];
    std::string url, title = "扫码", hint = "";

    JSValue val_url = JS_GetPropertyStr(ctx, opt, "url");
    if (!JS_IsString(val_url)) {
        JS_FreeValue(ctx, val_url);
        val_url = JS_GetPropertyStr(ctx, opt, "text");
    }
    if (JS_IsString(val_url)) {
        const char* s = JS_ToCString(ctx, val_url);
        if (s) { url = s; JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, val_url);

    JSValue val_title = JS_GetPropertyStr(ctx, opt, "title");
    if (JS_IsString(val_title)) {
        const char* s = JS_ToCString(ctx, val_title);
        if (s) { title = s; JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, val_title);

    JSValue val_hint = JS_GetPropertyStr(ctx, opt, "hint");
    if (JS_IsString(val_hint)) {
        const char* s = JS_ToCString(ctx, val_hint);
        if (s) { hint = s; JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, val_hint);

    if (url.empty()) {
        return JS_ThrowTypeError(ctx, "url or text is required");
    }

    if (g_qrcode_handler) {
        g_qrcode_handler(title, url, hint);
    }

    return JS_NewBool(ctx, 1);
}

static JSValue jsHostNotify(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "host.notify expects a message string");
    }
    const char* s = JS_ToCString(ctx, argv[0]);
    std::string msg = s ? s : "";
    JS_FreeCString(ctx, s);

    if (g_notify_handler) {
        g_notify_handler(msg);
    }
    return JS_NewBool(ctx, 1);
}

void JSRuntimeHost::cleanup() {
    if (ctx_) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
    }
}

void JSRuntimeHost::initSandbox() {
    rt_ = JS_NewRuntime();
    if (!rt_) return;

    // 内存安全沙箱：每个插件限制最大 16MB 内存
    JS_SetMemoryLimit(rt_, 16 * 1024 * 1024);

    // 栈上限：必须保留，不能设 0。
    //
    // 之前的注释说「0 表示无限制，避免后台线程误判栈溢出」，这个理由不成立：
    // QuickJS 的 js_check_stack_overflow 用的是
    //   rt->stack_limit = rt->stack_top - rt->stack_size
    // 而 stack_top 由 JS_UpdateStackTop() 在每次调用时按**当前线程**的
    // 栈指针刷新（callFunction / evaluate 开头都调了），
    // 所以换线程执行本来就不会误判。
    //
    // 设 0 的真实后果是彻底关掉保护：插件一句 deep(n+1) 就能把宿主原生栈
    // 打穿。实测：0 → Segmentation fault（退出码 139，无 JS 异常、无日志）；
    // 默认 256KB → 正常抛 InternalError: stack overflow，进程继续运行。
    //
    // 用默认值（256KB）而不是自己拍一个数：这是 QuickJS 为「解释器递归 +
    // 宿主回调」调好的保守值。插件的正常递归（解析 JSON、遍历目录树）
    // 远达不到这个深度。
    JS_SetMaxStackSize(rt_, JS_DEFAULT_STACK_SIZE);
    JS_SetInterruptHandler(rt_, jsInterruptHandler, this);

    ctx_ = JS_NewContext(rt_);
    if (!ctx_) return;
    JS_SetContextOpaque(ctx_, this);

    JSValue global = JS_GetGlobalObject(ctx_);

    // 1. 构建 host 全局对象
    JSValue host_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, host_obj, "http", JS_NewCFunction(ctx_, jsHostHttp, "http", 1));
    JS_SetPropertyStr(ctx_, host_obj, "getConfig", JS_NewCFunction(ctx_, jsHostGetConfig, "getConfig", 1));
    JS_SetPropertyStr(ctx_, host_obj, "getDeviceIP", JS_NewCFunction(ctx_, jsHostGetDeviceIP, "getDeviceIP", 0));
    JS_SetPropertyStr(ctx_, host_obj, "showQRCode", JS_NewCFunction(ctx_, jsHostShowQRCode, "showQRCode", 1));
    JS_SetPropertyStr(ctx_, host_obj, "notify", JS_NewCFunction(ctx_, jsHostNotify, "notify", 1));
    JS_SetPropertyStr(ctx_, host_obj, "toast", JS_NewCFunction(ctx_, jsHostNotify, "toast", 1));

    // host.storage
    JSValue storage_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, storage_obj, "get", JS_NewCFunction(ctx_, jsHostStorageGet, "get", 2));
    JS_SetPropertyStr(ctx_, storage_obj, "set", JS_NewCFunction(ctx_, jsHostStorageSet, "set", 2));
    JS_SetPropertyStr(ctx_, storage_obj, "remove", JS_NewCFunction(ctx_, jsHostStorageRemove, "remove", 1));
    JS_SetPropertyStr(ctx_, host_obj, "storage", storage_obj);

    // host.crypto
    JSValue crypto_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, crypto_obj, "md5", JS_NewCFunction(ctx_, jsHostCryptoMd5, "md5", 1));
    JS_SetPropertyStr(ctx_, crypto_obj, "sha256", JS_NewCFunction(ctx_, jsHostCryptoSha256, "sha256", 1));
    JS_SetPropertyStr(ctx_, crypto_obj, "hmacSha256", JS_NewCFunction(ctx_, jsHostCryptoHmacSha256, "hmacSha256", 2));
    JS_SetPropertyStr(ctx_, crypto_obj, "base64Encode", JS_NewCFunction(ctx_, jsHostCryptoBase64Encode, "base64Encode", 1));
    JS_SetPropertyStr(ctx_, crypto_obj, "base64Decode", JS_NewCFunction(ctx_, jsHostCryptoBase64Decode, "base64Decode", 1));
    JS_SetPropertyStr(ctx_, host_obj, "crypto", crypto_obj);

    // host.log
    JSValue log_obj = JS_NewCFunction(ctx_, jsHostLog, "log", 1);
    JS_SetPropertyStr(ctx_, log_obj, "info", JS_NewCFunction(ctx_, jsHostLog, "info", 1));
    JS_SetPropertyStr(ctx_, log_obj, "warn", JS_NewCFunction(ctx_, jsHostLogWarn, "warn", 1));
    JS_SetPropertyStr(ctx_, log_obj, "error", JS_NewCFunction(ctx_, jsHostLogError, "error", 1));
    JS_SetPropertyStr(ctx_, log_obj, "debug", JS_NewCFunction(ctx_, jsHostLog, "debug", 1));
    JS_SetPropertyStr(ctx_, host_obj, "log", log_obj);

    JS_SetPropertyStr(ctx_, global, "host", host_obj);

    // 2. 注入 console 对象
    JSValue console_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, console_obj, "log", JS_NewCFunction(ctx_, jsHostLog, "log", 1));
    JS_SetPropertyStr(ctx_, console_obj, "warn", JS_NewCFunction(ctx_, jsHostLogWarn, "warn", 1));
    JS_SetPropertyStr(ctx_, console_obj, "error", JS_NewCFunction(ctx_, jsHostLogError, "error", 1));
    JS_SetPropertyStr(ctx_, console_obj, "info", JS_NewCFunction(ctx_, jsHostLog, "info", 1));
    JS_SetPropertyStr(ctx_, global, "console", console_obj);

    JS_FreeValue(ctx_, global);
}

bool JSRuntimeHost::evaluate(const std::string& script, const std::string& filename, std::string* out_err) {
    if (!ctx_) return false;
    JS_UpdateStackTop(rt_);

    // 脚本加载阶段同样要受超时保护。
    //
    // 插件在顶层写 while(true){} 时，死循环发生在 evaluate() 里，
    // 早先只有 callFunction 会维护计时，所以这一路径永远不受控
    // （实测：带死循环的 init 让启动卡死）。这里把 is_running_ 一并打开。
    op_start_time_ = std::chrono::steady_clock::now();
    is_running_ = true;
    JSValue ret = JS_Eval(ctx_, script.c_str(), script.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    is_running_ = false;
    if (JS_IsException(ret)) {
        JSValue err = JS_GetException(ctx_);
        std::string full_err;
        const char* err_str = JS_ToCString(ctx_, err);
        if (err_str) {
            full_err = err_str;
            JS_FreeCString(ctx_, err_str);
        }
        JSValue stack = JS_GetPropertyStr(ctx_, err, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* stack_str = JS_ToCString(ctx_, stack);
            if (stack_str) {
                full_err += "\nStack:\n" + std::string(stack_str);
                JS_FreeCString(ctx_, stack_str);
            }
        }
        JS_FreeValue(ctx_, stack);
        JS_FreeValue(ctx_, err);
        JS_FreeValue(ctx_, ret);
        if (out_err) *out_err = full_err.empty() ? "Unknown JS error" : full_err;
        return false;
    }
    JS_FreeValue(ctx_, ret);
    return true;
}

bool JSRuntimeHost::hasFunction(const std::string& func_name) const {
    if (!ctx_) return false;
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, func_name.c_str());
    bool ok = JS_IsFunction(ctx_, fn);
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    return ok;
}

namespace {

struct PromiseWaitContext {
    JSValue result = JS_UNDEFINED;
    int state = 0; // 0: running, 1: resolved, -1: rejected
};

static JSValue onPromiseResolve(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (host && host->current_wait_ctx_) {
        auto* p = static_cast<PromiseWaitContext*>(host->current_wait_ctx_);
        if (argc > 0) p->result = JS_DupValue(ctx, argv[0]);
        else p->result = JS_UNDEFINED;
        p->state = 1;
    }
    return JS_UNDEFINED;
}

static JSValue onPromiseReject(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* host = static_cast<JSRuntimeHost*>(JS_GetContextOpaque(ctx));
    if (host && host->current_wait_ctx_) {
        auto* p = static_cast<PromiseWaitContext*>(host->current_wait_ctx_);
        if (argc > 0) p->result = JS_DupValue(ctx, argv[0]);
        else p->result = JS_UNDEFINED;
        p->state = -1;
    }
    return JS_UNDEFINED;
}

} // namespace

bool JSRuntimeHost::callFunction(const std::string& func_name,
                                 const std::vector<std::string>& args_json,
                                 std::string& out_result_json,
                                 std::string* out_err) {
    if (!ctx_) return false;
    JS_UpdateStackTop(rt_);
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, func_name.c_str());
    if (!JS_IsFunction(ctx_, fn)) {
        JS_FreeValue(ctx_, fn);
        JS_FreeValue(ctx_, global);
        if (out_err) *out_err = "函数不存在: " + func_name;
        return false;
    }

    std::vector<JSValue> argv;
    for (const auto& a_json : args_json) {
        JSValue v = JS_ParseJSON(ctx_, a_json.c_str(), a_json.size(), "<arg>");
        if (JS_IsException(v)) {
            v = JS_NewString(ctx_, a_json.c_str());
        }
        argv.push_back(v);
    }

    op_start_time_ = std::chrono::steady_clock::now();
    is_running_ = true;

    JSValue ret = JS_Call(ctx_, fn, global, (int)argv.size(), argv.data());
    for (auto& v : argv) JS_FreeValue(ctx_, v);
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);

    if (JS_IsException(ret)) {
        JSValue err = JS_GetException(ctx_);
        std::string full_err;
        const char* err_str = JS_ToCString(ctx_, err);
        if (err_str) {
            full_err = err_str;
            JS_FreeCString(ctx_, err_str);
        }
        JSValue stack = JS_GetPropertyStr(ctx_, err, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* stack_str = JS_ToCString(ctx_, stack);
            if (stack_str) {
                full_err += "\nStack:\n" + std::string(stack_str);
                JS_FreeCString(ctx_, stack_str);
            }
        }
        JS_FreeValue(ctx_, stack);
        JS_FreeValue(ctx_, err);
        if (out_err) *out_err = full_err.empty() ? "JS 执行异常" : full_err;
        is_running_ = false;
        return false;
    }

    // 检查返回值是否是 Promise（拥有 .then 方法）
    JSValue then_fn = JS_GetPropertyStr(ctx_, ret, "then");
    JSValue final_val = JS_UNDEFINED;
    bool success = true;

    if (JS_IsFunction(ctx_, then_fn)) {
        // Promise 驱动：挂接 resolve / reject 回调
        PromiseWaitContext wait_ctx;
        auto* prev_wait = current_wait_ctx_;
        current_wait_ctx_ = &wait_ctx;

        JSValue cb_resolve = JS_NewCFunction(ctx_, onPromiseResolve, "onPromiseResolve", 1);
        JSValue cb_reject = JS_NewCFunction(ctx_, onPromiseReject, "onPromiseReject", 1);
        JSValue cb_args[2] = { cb_resolve, cb_reject };

        JSValue then_call = JS_Call(ctx_, then_fn, ret, 2, cb_args);
        JS_FreeValue(ctx_, then_call);
        JS_FreeValue(ctx_, cb_resolve);
        JS_FreeValue(ctx_, cb_reject);

        // 驱动 microtasks 事件循环
        JSContext* pctx = nullptr;
        while (wait_ctx.state == 0 && JS_ExecutePendingJob(rt_, &pctx) > 0) {}

        current_wait_ctx_ = prev_wait;

        if (wait_ctx.state == -1) {
            success = false;
            std::string full_err;
            const char* err_str = JS_ToCString(ctx_, wait_ctx.result);
            if (err_str) {
                full_err = err_str;
                JS_FreeCString(ctx_, err_str);
            }
            JSValue stack = JS_GetPropertyStr(ctx_, wait_ctx.result, "stack");
            if (!JS_IsUndefined(stack)) {
                const char* stack_str = JS_ToCString(ctx_, stack);
                if (stack_str) {
                    full_err += "\nStack:\n" + std::string(stack_str);
                    JS_FreeCString(ctx_, stack_str);
                }
            }
            JS_FreeValue(ctx_, stack);
            if (out_err) *out_err = full_err.empty() ? "Promise 被拒绝" : full_err;
            JS_FreeValue(ctx_, wait_ctx.result);
        } else {
            final_val = wait_ctx.result;
        }
    } else {
        final_val = JS_DupValue(ctx_, ret);
    }

    JS_FreeValue(ctx_, then_fn);
    JS_FreeValue(ctx_, ret);
    is_running_ = false;

    if (!success) {
        return false;
    }

    // 格式化返回值输出为 JSON
    if (JS_IsUndefined(final_val)) {
        out_result_json = "null";
    } else {
        JSValue json_str_val = JS_JSONStringify(ctx_, final_val, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json_str_val)) {
            const char* s = JS_ToCString(ctx_, final_val);
            out_result_json = s ? s : "";
            if (s) JS_FreeCString(ctx_, s);
        } else {
            const char* s = JS_ToCString(ctx_, json_str_val);
            out_result_json = s ? s : "";
            if (s) JS_FreeCString(ctx_, s);
        }
        JS_FreeValue(ctx_, json_str_val);
    }
    JS_FreeValue(ctx_, final_val);
    return true;
}

void JSRuntimeHost::setExecutionTimeout(int seconds) {
    if (seconds > 0) timeout_seconds_ = seconds;
}

} // namespace muvik
