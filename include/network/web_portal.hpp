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

private:
    WebPortal() = default;
    ~WebPortal();
    WebPortal(const WebPortal&) = delete;
    WebPortal& operator=(const WebPortal&) = delete;

    std::atomic<bool> running_{false};
    int port_ = 8888;
    int server_fd_ = -1;
    std::unique_ptr<std::thread> thread_;

    void serverLoop();
    void handleClient(int client_fd);
    std::string buildIndexHtml() const;
};

} // namespace muvik
