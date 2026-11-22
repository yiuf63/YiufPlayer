#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace muvik {

// 局域网手机 Web 配置门户与远程控制后台
class WebPortal {
public:
    static WebPortal& getInstance();

    // 启动嵌入式 HTTP 服务（默认端口 8888）
    bool start(int port = 8888);

    // 停止服务
    void stop();

    bool isRunning() const { return running_.load(); }
    int getPort() const { return port_; }

    // 获取手机访问地址，如 http://192.168.31.194:8888
    std::string getPortalUrl() const;

    // 带访问令牌的完整地址（扫码/分享用）。
    //
    // 门户面向 LAN 提供插件配置读写、按键注入与插件安装，早先完全无鉴权。
    // 现在所有 /api/* 都要求令牌，这个地址把令牌一并带上，
    // 用户扫码即可直接进入，不需要手输。
    std::string getPortalUrlWithToken() const;

    // 当前访问令牌（只读，供二维码与日志展示）
    const std::string& getAccessToken() const { return access_token_; }

private:
    WebPortal() = default;
    ~WebPortal();
    WebPortal(const WebPortal&) = delete;
    WebPortal& operator=(const WebPortal&) = delete;

    std::atomic<bool> running_{false};
    int port_ = 8888;
    int server_fd_ = -1;
    std::unique_ptr<std::thread> thread_;
    std::string access_token_;

    void serverLoop();
    void handleClient(int client_fd);

    // 常量时间比较，避免按字节提前返回泄露令牌长度/前缀信息
    bool tokenMatches(const std::string& candidate) const;

    // 从请求头 / 查询串 / Cookie 里取令牌并校验
    bool checkAuth(const std::string& method, const std::string& path,
                   const std::string& raw_request) const;

    static std::string generateToken();

    // 一次 send 可能只写出去一部分（大响应 / 慢客户端），
    // 必须循环直到写完。同时用 MSG_NOSIGNAL 避免对端 RST 时收到 SIGPIPE
    // （实测未防护时进程直接以 141 退出）。
    static bool sendAll(int fd, const std::string& data);

    std::string buildIndexHtml() const;
};

} // namespace muvik
