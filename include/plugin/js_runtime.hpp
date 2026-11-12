#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>

struct JSRuntime;
struct JSContext;

namespace muvik {

// HTTP 请求参数
struct JSHttpRequest {
    std::string url;
    std::string method = "GET";
    std::vector<std::string> headers;
    std::string body;
    int timeout_seconds = 15;
};

// HTTP 响应结果
struct JSHttpResponse {
    int status = 0;
    std::string status_text;
    std::string body;
    std::vector<std::string> headers;
};

// JS 脚本运行时与安全沙箱环境
class JSRuntimeHost {
public:
    explicit JSRuntimeHost(std::string plugin_id);
    ~JSRuntimeHost();

    // 禁用拷贝，允许移动
    JSRuntimeHost(const JSRuntimeHost&) = delete;
    JSRuntimeHost& operator=(const JSRuntimeHost&) = delete;
    JSRuntimeHost(JSRuntimeHost&& other) noexcept;
    JSRuntimeHost& operator=(JSRuntimeHost&& other) noexcept;

    // 执行脚本代码（如加载 main.js）
    bool evaluate(const std::string& script, const std::string& filename = "<main.js>", std::string* out_err = nullptr);

    // 检查是否存在指定的全局函数
    bool hasFunction(const std::string& func_name) const;

    // 同步调用 JS 全局函数并等待完成（即使是 async 函数也会在当前线程驱动 Promise 完成）
    // args_json: 每个参数为一个合法的 JSON 字符串
    // out_result_json: 函数成功返回的结果（以 JSON 字符串形式交付）
    // out_err: 若发生异常，填充错误描述
    bool callFunction(const std::string& func_name,
                      const std::vector<std::string>& args_json,
                      std::string& out_result_json,
                      std::string* out_err = nullptr);

    // 设置执行超时时间（秒，默认 30 秒，防止死循环）
    void setExecutionTimeout(int seconds);

    const std::string& getPluginId() const { return plugin_id_; }

private:
    void initSandbox();
    void cleanup();

    std::string plugin_id_;
    int timeout_seconds_ = 30;
    std::chrono::steady_clock::time_point op_start_time_;
    bool is_running_ = false;

    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;
};

} // namespace muvik
