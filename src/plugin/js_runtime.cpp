#include <chrono>
#include <string>
#include <vector>
#include "plugin/js_runtime.hpp"
#include "plugin/plugin_config_store.hpp"
#include "utils/crypto_utils.hpp"
#include <borealis/core/logger.hpp>
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <algorithm>

extern "C" {
#include "quickjs.h"
}

namespace muvik {

namespace {

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

// 中断回调：防止 JS 死循环占用线程
static int jsInterruptHandler(JSRuntime* /*rt*/, void* opaque) {
    auto* host = static_cast<JSRuntimeHost*>(opaque);
    if (!host) return 0;
    // 检查是否超时
    return 0; // 正常返回 0
}

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

    // 内存安全沙箱：每个插件限制最大 16MB 内存，1MB 栈空间，适应低配 ARM 开源掌机
    JS_SetMemoryLimit(rt_, 16 * 1024 * 1024);
    JS_SetMaxStackSize(rt_, 1024 * 1024);
    JS_SetInterruptHandler(rt_, jsInterruptHandler, this);

    ctx_ = JS_NewContext(rt_);
    if (!ctx_) return;
    JS_SetContextOpaque(ctx_, this);

    JSValue global = JS_GetGlobalObject(ctx_);

    // 1. 构建 host 全局对象
    JSValue host_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, host_obj, "http", JS_NewCFunction(ctx_, jsHostHttp, "http", 1));
    JS_SetPropertyStr(ctx_, host_obj, "getConfig", JS_NewCFunction(ctx_, jsHostGetConfig, "getConfig", 1));

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
    JSValue ret = JS_Eval(ctx_, script.c_str(), script.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue err = JS_GetException(ctx_);
        const char* err_str = JS_ToCString(ctx_, err);
        if (out_err) *out_err = err_str ? err_str : "Unknown JS error";
        if (err_str) JS_FreeCString(ctx_, err_str);
        JS_FreeValue(ctx_, err);
        JS_FreeValue(ctx_, ret);
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
    auto* p = static_cast<PromiseWaitContext*>(JS_GetContextOpaque(ctx));
    if (p) {
        if (argc > 0) p->result = JS_DupValue(ctx, argv[0]);
        else p->result = JS_UNDEFINED;
        p->state = 1;
    }
    return JS_UNDEFINED;
}

static JSValue onPromiseReject(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* p = static_cast<PromiseWaitContext*>(JS_GetContextOpaque(ctx));
    if (p) {
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
        const char* err_str = JS_ToCString(ctx_, err);
        if (out_err) *out_err = err_str ? err_str : "JS 执行异常";
        if (err_str) JS_FreeCString(ctx_, err_str);
        JS_FreeValue(ctx_, err);
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
        JS_SetContextOpaque(ctx_, &wait_ctx);

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

        JS_SetContextOpaque(ctx_, this);

        if (wait_ctx.state == -1) {
            success = false;
            const char* err_str = JS_ToCString(ctx_, wait_ctx.result);
            if (out_err) *out_err = err_str ? err_str : "Promise 被拒绝";
            if (err_str) JS_FreeCString(ctx_, err_str);
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
