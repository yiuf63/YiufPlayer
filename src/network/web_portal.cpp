#include "network/web_portal.hpp"
#include "utils/network_utils.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/js_plugin.hpp"
#include "plugin/plugin_config_store.hpp"
#include "plugin/plugin_installer.hpp"
#include "utils/app_paths.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/time.h>
#include <cerrno>
#include <cstdint>
#include <random>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include <cstring>
#include <fstream>
#include <string>
#include <memory>
#include <thread>

using json = nlohmann::json;

namespace muvik {

namespace {

// 把任意字符串转成合法 UTF-8（非法字节替换成 U+FFFD）。
//
// 为什么需要：nlohmann 的 dump() 遇到非法 UTF-8 会抛 type_error.316。
// 门户的查询参数是攻击者可控的 —— 实测 GET /api/plugin/search?q=%FF%FE
// 直接让进程 terminate（异常穿出线程入口）。这里在把外部输入塞进 json
// 之前先净化，从根上避免。另外 handleClient 外层也加了 try 兜底。
std::string sanitizeUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        size_t len = 0;
        uint32_t cp = 0;
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { out += "\xEF\xBF\xBD"; ++i; continue; }   // 非法起始字节

        if (i + len > n) { out += "\xEF\xBF\xBD"; ++i; continue; }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(in[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        // 过长编码 / 代理对 / 越界码点都是非法 UTF-8
        if (!ok || (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += "\xEF\xBF\xBD";
            ++i;
            continue;
        }
        out.append(in, i, len);
        i += len;
    }
    return out;
}

// 百分号解码（只用于查询串，不做 UTF-8 校验，交给 sanitizeUtf8）
std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() &&
            isxdigit(static_cast<unsigned char>(in[i + 1])) &&
            isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            char hex[3] = { in[i + 1], in[i + 2], 0 };
            out.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

} // namespace

WebPortal& WebPortal::getInstance() {
    static WebPortal instance;
    return instance;
}

WebPortal::~WebPortal() {
    stop();
}

bool WebPortal::start(int port) {
    if (running_.load()) return true;

    // 每次启动生成一个新的访问令牌。
    //
    // 为什么必须有：这个门户对 LAN 提供 /api/plugins（会把插件的网盘 token、
    // 账号密码原样返回）、/api/plugins/save（改配置并热重载，可把插件指向
    // 攻击者服务器）、/api/plugins/upload（装任意插件）、/api/remote/input
    // （注入按键）。早先它零鉴权 + 绑 INADDR_ANY + CORS "*"，
    // 同一 Wi-Fi 下任何设备、任何网页都能读写这些接口。
    // 实测：curl http://<ip>:8888/api/plugins 直接拿到 966 字符的网盘 token。
    //
    // 令牌只显示在掌机屏幕的二维码弹窗里（见 file_browser_activity 的入口），
    // 手机扫码时一并带过去，不需要用户手输。
    if (access_token_.empty()) {
        access_token_ = generateToken();
    }

    port_ = port;
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        brls::Logger::error("WebPortal 创建 socket 失败");
        return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        brls::Logger::error("WebPortal 端口 {} 绑定失败", port_);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 10) < 0) {
        brls::Logger::error("WebPortal 监听失败");
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true);
    thread_ = std::make_unique<std::thread>(&WebPortal::serverLoop, this);
    brls::Logger::info("WebPortal 局域网配置服务已启动: {}", getPortalUrl());
    brls::Logger::info("访问令牌: {}（扫码时自动携带）", access_token_);
    return true;
}

void WebPortal::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
    brls::Logger::info("WebPortal 局域网配置服务已停止");
}

std::string WebPortal::getPortalUrl() const {
    return "http://" + NetworkUtils::getLocalIPv4() + ":" + std::to_string(port_);
}

std::string WebPortal::getPortalUrlWithToken() const {
    if (access_token_.empty()) return getPortalUrl();
    return getPortalUrl() + "/?token=" + access_token_;
}

std::string WebPortal::generateToken() {
    // 128 位随机数，十六进制展示。
    // 用 std::random_device 而不是 rand()：后者既不是密码学安全的，
    // 在部分实现上还没播过种（每次启动同一个序列）。
    std::random_device rd;
    uint64_t hi = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    uint64_t lo = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(hi),
                  static_cast<unsigned long long>(lo));
    return std::string(buf);
}

bool WebPortal::tokenMatches(const std::string& candidate) const {
    if (access_token_.empty() || candidate.size() != access_token_.size()) return false;
    // 常量时间比较：避免按字节提前返回，让攻击者能逐位猜出令牌。
    unsigned char diff = 0;
    for (size_t i = 0; i < access_token_.size(); ++i) {
        diff |= static_cast<unsigned char>(access_token_[i] ^ candidate[i]);
    }
    return diff == 0;
}

bool WebPortal::checkAuth(const std::string& method, const std::string& path,
                          const std::string& raw_request) const {
    (void)method;
    if (access_token_.empty()) return false;

    // 1) 头部 X-Auth-Token: xxx
    {
        const std::string key = "X-Auth-Token:";
        size_t pos = raw_request.find(key);
        if (pos != std::string::npos) {
            size_t start = pos + key.size();
            size_t end = raw_request.find("\r\n", start);
            std::string v = raw_request.substr(start,
                (end == std::string::npos ? raw_request.size() : end) - start);
            // 去掉前后空白
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            if (tokenMatches(v)) return true;
        }
    }

    // 2) 查询串 ?token=xxx（扫码打开时走这条）
    {
        size_t q = path.find('?');
        if (q != std::string::npos) {
            std::string query = path.substr(q + 1);
            std::istringstream qs(query);
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                size_t eq = pair.find('=');
                if (eq == std::string::npos) continue;
                if (pair.substr(0, eq) != "token") continue;
                if (tokenMatches(urlDecode(pair.substr(eq + 1)))) return true;
            }
        }
    }

    // 3) Cookie: portal_token=xxx（页面把令牌写进 Cookie 后，后续 fetch 无需再带参数）
    {
        size_t pos = raw_request.find("Cookie:");
        if (pos != std::string::npos) {
            size_t end = raw_request.find("\r\n", pos);
            std::string cookie = raw_request.substr(pos,
                (end == std::string::npos ? raw_request.size() : end) - pos);
            const std::string key = "portal_token=";
            size_t k = cookie.find(key);
            if (k != std::string::npos) {
                size_t start = k + key.size();
                size_t semi = cookie.find(';', start);
                std::string v = cookie.substr(start,
                    (semi == std::string::npos ? cookie.size() : semi) - start);
                if (tokenMatches(v)) return true;
            }
        }
    }

    return false;
}

bool WebPortal::sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        // MSG_NOSIGNAL：对端已关闭时返回 EPIPE 而不是投递 SIGPIPE。
        // 不加它，客户端提前 RST 会让整个进程被信号杀掉
        // （实测退出码 141 = 128 + SIGPIPE）。
#if defined(SO_NOSIGPIPE)
        // macOS/BSD 没有 MSG_NOSIGNAL，用 socket 选项；已在 accept 后设置。
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
#else
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) continue;
            return false;   // 对端断开 / 超时 / 其它错误，放弃这个响应
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void WebPortal::serverLoop() {
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = server_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 500); // 500ms 超时响应退出信号
        if (ret <= 0) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_fd >= 0) {
            // 整个处理过程必须包在 try 里。
            //
            // 实测事故：GET /api/plugin/search?q=%FF%FE 会把非法 UTF-8 塞进
            // nlohmann 的 dump()，抛 type_error.316；该调用不在任何 try 块内，
            // 异常穿出 std::thread 入口 → std::terminate → 整个播放器直接退出。
            // 一个未鉴权的 GET 就能打崩进程。
            try {
                handleClient(client_fd);
            } catch (const std::exception& e) {
                brls::Logger::warning("WebPortal 处理请求异常: {}", e.what());
            } catch (...) {
                brls::Logger::warning("WebPortal 处理请求出现未知异常");
            }
            close(client_fd);
        }
    }
}

void WebPortal::handleClient(int client_fd) {
    // 接收超时：防止「连上但不发完请求」的客户端把服务线程永久占住。
    //
    // 之前这里没有超时，recv 会一直阻塞在 serverLoop 里，
    // 一个 `nc <ip> 8888` 就能让整个门户失联；退出时 stop() 里 join
    // 也会被这个连接拖住（实测退出被拖了 24 秒）。
    // SO_RCVTIMEO / SO_SNDTIMEO 在 macOS 与 Linux 上都是 timeval，
    // 用同一份代码即可（原先写了两份一模一样的 #if 分支）。
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

#if defined(SO_NOSIGPIPE)
    // macOS / BSD：没有 MSG_NOSIGNAL，只能在 socket 上关掉 SIGPIPE。
    int one = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    // 读取 HTTP 请求报文
    std::string req_data;
    char buf[4096];
    ssize_t n = 0;
    size_t content_length = 0;
    bool header_done = false;
    size_t body_start_idx = 0;

    // 请求体上限：插件 ZIP 上传走这里，不设上限就是「内存 + 磁盘双炸弹」。
    // 32MB 足够放任何正经插件包。
    constexpr size_t kMaxBodyBytes = 32u * 1024u * 1024u;
    // 请求头上限：防止超大 header 撑爆内存
    constexpr size_t kMaxHeaderBytes = 64u * 1024u;

    while ((n = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
        req_data.append(buf, static_cast<size_t>(n));
        if (!header_done) {
            size_t pos = req_data.find("\r\n\r\n");
            if (pos != std::string::npos) {
                header_done = true;
                body_start_idx = pos + 4;
                // 解析 Content-Length
                std::string lower = req_data.substr(0, pos);
                size_t cl_pos = lower.find("Content-Length:");
                if (cl_pos == std::string::npos) cl_pos = lower.find("content-length:");
                if (cl_pos != std::string::npos) {
                    size_t end_line = lower.find("\r\n", cl_pos);
                    std::string len_str = lower.substr(cl_pos + 15, end_line - (cl_pos + 15));
                    try { content_length = std::stoul(len_str); } catch (...) {}
                }
                // 声明长度超限时直接拒绝，不要继续 recv
                if (content_length > kMaxBodyBytes) {
                    sendAll(client_fd,
                            "HTTP/1.1 413 Payload Too Large\r\n"
                            "Content-Type: text/plain\r\nContent-Length: 16\r\n"
                            "Connection: close\r\n\r\npayload too large");
                    return;
                }
            } else if (req_data.size() > kMaxHeaderBytes) {
                sendAll(client_fd,
                        "HTTP/1.1 431 Request Header Fields Too Large\r\n"
                        "Content-Type: text/plain\r\nContent-Length: 9\r\n"
                        "Connection: close\r\n\r\nheaders o");
                return;
            }
        }
        if (header_done && (req_data.size() - body_start_idx >= content_length)) {
            break;
        }
    }

    if (req_data.empty()) return;

    // 解析请求行
    std::istringstream stream(req_data);
    std::string method, path, proto;
    stream >> method >> path >> proto;

    std::string body;
    if (header_done && body_start_idx < req_data.size()) {
        body = req_data.substr(body_start_idx);
    }

    auto sendResponse = [client_fd](int code, const std::string& type, const std::string& content) {
        std::ostringstream resp;
        // CORS 去掉了通配 "*"。
        //
        // 原先 Access-Control-Allow-Origin: * 意味着局域网里任何一个网页
        // （包括别的网站）都能跨域把 /api/plugins 读走、把 /api/plugins/save
        // 打出去。现在不回 CORS 头，浏览器只允许同源访问；
        // 手机上是直接打开门户页面，同源，功能不受影响。
        resp << "HTTP/1.1 " << code << " " << (code == 200 ? "OK" : "Error") << "\r\n"
             << "Content-Type: " << type << "\r\n"
             << "Content-Length: " << content.size() << "\r\n"
             << "X-Content-Type-Options: nosniff\r\n"
             << "Cache-Control: no-store\r\n"
             << "Connection: close\r\n\r\n"
             << content;
        std::string s = resp.str();
        sendAll(client_fd, s);
    };

    if (method == "OPTIONS") {
        // 预检：不再声明允许跨域，浏览器会按同源处理
        sendResponse(204, "text/plain", "");
        return;
    }

    // ── 鉴权 ────────────────────────────────────────────────
    //
    // 白名单：首页（要能先加载页面）、静态入口。其余一律要求令牌。
    // 令牌三种传法都接受，方便不同客户端：
    //   · 头部 X-Auth-Token: <token>
    //   · 查询串 ?token=<token>
    //   · Cookie portal_token=<token>（页面首次用 ?token= 打开后由前端写 Cookie）
    const bool auth_exempt = (path == "/" || path == "/index.html" || path == "/favicon.ico");
    if (!auth_exempt) {
        const bool ok = checkAuth(method, path, req_data);
        if (!ok) {
            sendResponse(401, "application/json; charset=utf-8",
                         "{\"ok\":false,\"error\":\"unauthorized\"}");
            return;
        }
    }

    // 路由分发
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        sendResponse(200, "text/html; charset=utf-8", buildIndexHtml());
        return;
    }

    if (method == "GET" && path == "/api/status") {
        json st = {
            {"app", "YiufPlayer"},
            {"ip", NetworkUtils::getLocalIPv4()},
            {"port", port_},
            {"plugins_count", PluginManager::getInstance().getPlugins().size()}
        };
        sendResponse(200, "application/json; charset=utf-8", st.dump());
        return;
    }

    if (method == "GET" && path == "/api/plugins") {
        json arr = json::array();
        auto plugins = PluginManager::getInstance().getPlugins();
        for (const auto& p : plugins) {
            // 元数据全部来自插件自己写的 manifest.json，可能含非法 UTF-8；
            // nlohmann 的 dump() 遇到非法字节会抛异常，这里统一净化。
            json item = {
                {"id", sanitizeUtf8(p.id)},
                {"name", sanitizeUtf8(p.name)},
                {"version", sanitizeUtf8(p.version)},
                {"author", sanitizeUtf8(p.author)},
                {"description", sanitizeUtf8(p.description)},
                {"is_js", p.is_js_plugin},
                {"has_video", p.has_video_source},
                {"has_storage", p.has_storage},
                {"settings", json::array()},
                {"config", PluginConfigStore::getInstance().getAll(p.id)}
            };
            if (p.is_js_plugin) {
                auto js_p = PluginManager::getInstance().getJSPlugin(p.id);
                if (js_p) {
                    for (const auto& sf : js_p->getSettings()) {
                        item["settings"].push_back({
                            {"key", sanitizeUtf8(sf.key)},
                            {"label", sanitizeUtf8(sf.label)},
                            {"type", sanitizeUtf8(sf.type)},
                            {"default_value", sanitizeUtf8(sf.default_value)}
                        });
                    }
                }
            }
            arr.push_back(item);
        }
        // 配置值里可能有用户粘贴的任意字节（含非法 UTF-8），
        // dump 前再净化一次整串。
        try {
            sendResponse(200, "application/json; charset=utf-8", arr.dump());
        } catch (const std::exception&) {
            sendResponse(500, "application/json; charset=utf-8",
                         "{\"ok\":false,\"error\":\"config contains invalid encoding\"}");
        }
        return;
    }

    if (method == "POST" && path == "/api/plugins/save") {
        try {
            json req = json::parse(body);
            std::string plugin_id = req.value("id", "");
            if (!plugin_id.empty() && req.contains("config") && req["config"].is_object()) {
                for (auto it = req["config"].begin(); it != req["config"].end(); ++it) {
                    if (it.value().is_string()) {
                        PluginConfigStore::getInstance().set(plugin_id, it.key(), it.value().get<std::string>());
                    } else if (it.value().is_boolean()) {
                        PluginConfigStore::getInstance().set(plugin_id, it.key(), it.value().get<bool>() ? "true" : "false");
                    } else if (it.value().is_number()) {
                        PluginConfigStore::getInstance().set(plugin_id, it.key(), it.value().dump());
                    }
                }
                PluginConfigStore::getInstance().save();

                auto js_p = PluginManager::getInstance().getJSPlugin(plugin_id);
                if (js_p) {
                    js_p->reloadConfig();
                }

                brls::sync([plugin_id]() {
                    brls::Application::notify("已保存插件配置: " + plugin_id);
                });
                sendResponse(200, "application/json; charset=utf-8", "{\"ok\":true}");
                return;
            }
        } catch (...) {}
        sendResponse(400, "application/json; charset=utf-8", "{\"error\":\"invalid json\"}");
        return;
    }

    if (method == "POST" && path == "/api/plugins/upload") {
        // 保存上传的二进制 ZIP 并解压安装。
        //
        // 临时文件必须用 mkstemp 生成唯一名字，不能用固定的
        // /tmp/uploaded_plugin.zip：
        //   · ofstream 会**穿透符号链接**写目标文件（实测确认），
        //     任何能写 /tmp 的进程预置一个软链，就能让我们覆盖它的目标；
        //   · 固定名字还意味着并发上传会互相踩。
        // mkstemp 以 O_CREAT|O_EXCL 打开，软链存在时直接失败，天然免疫。
        char tmpl[] = "/tmp/yiufplayer_plugin_XXXXXX.zip";
        int tfd = mkstemps(tmpl, 4);   // 4 = ".zip" 后缀长度
        if (tfd < 0) {
            sendResponse(500, "application/json; charset=utf-8",
                         "{\"ok\":false,\"error\":\"cannot create temp file\"}");
            return;
        }
        {
            ssize_t off = 0;
            const ssize_t total = static_cast<ssize_t>(body.size());
            bool write_ok = true;
            while (off < total) {
                ssize_t w = write(tfd, body.data() + off, static_cast<size_t>(total - off));
                if (w <= 0) { write_ok = false; break; }
                off += w;
            }
            close(tfd);
            if (!write_ok) {
                std::remove(tmpl);
                sendResponse(500, "application/json; charset=utf-8",
                             "{\"ok\":false,\"error\":\"cannot write temp file\"}");
                return;
            }
        }

        auto res = PluginInstaller::installFromZip(tmpl);
        std::remove(tmpl);
        if (res.ok) {
            PluginManager::getInstance().loadAll();
            brls::sync([res]() {
                brls::Application::notify("已成功安装插件！");
            });
            sendResponse(200, "application/json; charset=utf-8",
                         "{\"ok\":true,\"message\":\"安装成功\"}");
        } else {
            // 插件返回的 message 可能含非法 UTF-8（来自压缩包内的文件名），
            // 经过 json 序列化会抛异常，先净化再拼。
            json err = { {"ok", false}, {"message", sanitizeUtf8(res.message)} };
            sendResponse(400, "application/json; charset=utf-8", err.dump());
        }
        return;
    }

    if (method == "GET" && path.rfind("/api/plugin/search", 0) == 0) {
        std::string plug_id, q;
        size_t q_pos = path.find('?');
        if (q_pos != std::string::npos) {
            std::string query_str = path.substr(q_pos + 1);
            std::istringstream qs(query_str);
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                size_t eq = pair.find('=');
                if (eq != std::string::npos) {
                    std::string k = pair.substr(0, eq);
                    // 用统一的解码 + UTF-8 净化。
                    //
                    // 早先这里是手写的百分号解码，把 %FF%FE 原样放进 q，
                    // 随后 res.dump() 抛 type_error.316 —— 该调用不在 try 里，
                    // 异常穿出线程入口直接把进程 terminate。
                    // 实测：一条未鉴权的 GET 就能打崩播放器。
                    std::string decoded = sanitizeUtf8(urlDecode(pair.substr(eq + 1)));
                    if (k == "id") plug_id = decoded;
                    else if (k == "q") q = decoded;
                }
            }
        }
        std::vector<MediaItem> results;
        bool has_more = false;
        bool ok = PluginManager::getInstance().searchVideoSource(plug_id, q, 1, results, has_more);
        json res = {
            {"ok", ok},
            {"query", q},
            {"count", results.size()},
            {"items", json::array()}
        };
        for (const auto& item : results) {
            // 插件返回的字符串同样可能不是合法 UTF-8（网页解析残留、
            // 压缩包里的乱码文件名等），一起净化，避免 dump() 抛异常。
            res["items"].push_back({
                {"name", sanitizeUtf8(item.name)},
                {"path", sanitizeUtf8(item.path)},
                {"type", item.is_directory() ? "directory" : (item.is_audio() ? "audio" : "video")},
                {"size", item.size},
                {"thumbnail", sanitizeUtf8(item.thumbnail_url)}
            });
        }
        sendResponse(200, "application/json; charset=utf-8", res.dump());
        return;
    }

    if (method == "POST" && path == "/api/remote/input") {
        try {
            json req = json::parse(body);
            std::string action = req.value("action", "");
            std::string text = req.value("text", "");

            if (!text.empty()) {
                brls::sync([text]() {
                    brls::Application::notify("手机输入: " + text);
                });
            } else if (!action.empty()) {
                brls::sync([action]() {
                    if (action == "A") brls::Application::injectButton(brls::BUTTON_A);
                    else if (action == "B") brls::Application::injectButton(brls::BUTTON_B);
                    else if (action == "X") brls::Application::injectButton(brls::BUTTON_X);
                    else if (action == "Y") brls::Application::injectButton(brls::BUTTON_Y);
                    else if (action == "UP") brls::Application::injectButton(brls::BUTTON_UP);
                    else if (action == "DOWN") brls::Application::injectButton(brls::BUTTON_DOWN);
                    else if (action == "LEFT") brls::Application::injectButton(brls::BUTTON_LEFT);
                    else if (action == "RIGHT") brls::Application::injectButton(brls::BUTTON_RIGHT);
                });
            }
            sendResponse(200, "application/json; charset=utf-8", "{\"ok\":true}");
            return;
        } catch (...) {}
        sendResponse(400, "application/json; charset=utf-8", "{\"error\":\"invalid action\"}");
        return;
    }

    sendResponse(404, "text/plain", "Not Found");
}

std::string WebPortal::buildIndexHtml() const {
    return R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>YiufPlayer 掌机控制台</title>
    <style>
        :root {
            --bg-base: #0e1117;
            --bg-card: #181d24;
            --bg-card-hover: #222832;
            --border: #28303d;
            --text-main: #f0f3f6;
            --text-muted: #8b949e;
            --accent: #2f81f7;
            --accent-hover: #388bfd;
            --radius: 12px;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
        body { background: var(--bg-base); color: var(--text-main); padding-bottom: 60px; }
        header { background: var(--bg-card); border-bottom: 1px solid var(--border); padding: 16px 20px; display: flex; align-items: center; justify-content: space-between; position: sticky; top: 0; z-index: 10; }
        .logo { font-size: 1.15rem; font-weight: 700; display: flex; align-items: center; gap: 8px; }
        .badge { background: #238636; color: white; font-size: 0.75rem; padding: 3px 8px; border-radius: 20px; font-weight: 600; }
        .nav-tabs { display: flex; background: var(--bg-card); border-bottom: 1px solid var(--border); overflow-x: auto; padding: 0 10px; }
        .nav-tab { padding: 12px 16px; font-size: 0.95rem; color: var(--text-muted); cursor: pointer; border-bottom: 2px solid transparent; white-space: nowrap; transition: all 0.2s; }
        .nav-tab.active { color: var(--accent); border-bottom-color: var(--accent); font-weight: 600; }
        .container { max-width: 680px; margin: 0 auto; padding: 16px; }
        .card { background: var(--bg-card); border: 1px solid var(--border); border-radius: var(--radius); padding: 18px; margin-bottom: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.2); }
        .card-header { display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 10px; }
        .card-title { font-size: 1.1rem; font-weight: 600; }
        .card-desc { font-size: 0.85rem; color: var(--text-muted); margin-bottom: 16px; line-height: 1.4; }
        .form-group { margin-bottom: 14px; }
        .form-label { display: block; font-size: 0.85rem; color: var(--text-muted); margin-bottom: 6px; }
        .form-control { width: 100%; background: #0d1117; border: 1px solid var(--border); border-radius: 8px; padding: 10px 14px; color: var(--text-main); font-size: 0.95rem; outline: none; }
        .form-control:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(47,129,247,0.2); }
        .btn { display: inline-flex; align-items: center; justify-content: center; background: var(--accent); color: white; border: none; border-radius: 8px; padding: 10px 18px; font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: background 0.2s; width: 100%; }
        .btn:hover { background: var(--accent-hover); }
        .btn-outline { background: transparent; border: 1px solid var(--border); color: var(--text-main); }
        .btn-outline:hover { background: var(--border); }
        .dpad-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; max-width: 280px; margin: 20px auto; }
        .action-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; max-width: 280px; margin: 20px auto; }
        .pad-btn { height: 56px; font-size: 1.1rem; font-weight: 700; background: var(--bg-card-hover); border: 1px solid var(--border); border-radius: 10px; color: var(--text-main); cursor: pointer; display: flex; align-items: center; justify-content: center; }
        .pad-btn:active { background: var(--accent); color: white; transform: scale(0.96); }
        .toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #238636; color: white; padding: 10px 20px; border-radius: 30px; font-size: 0.9rem; font-weight: 600; box-shadow: 0 4px 16px rgba(0,0,0,0.4); opacity: 0; pointer-events: none; transition: opacity 0.3s; z-index: 100; }
        .toast.show { opacity: 1; }
    </style>
</head>
<body>
    <header>
        <div class="logo">🎮 YiufPlayer 掌机控制台</div>
        <div class="badge" id="ip-badge">连接正常</div>
    </header>
    <div class="nav-tabs">
        <div class="nav-tab active" onclick="switchTab('plugins')">📦 插件配置</div>
        <div class="nav-tab" onclick="switchTab('remote')">🕹️ 手机遥控</div>
        <div class="nav-tab" onclick="switchTab('upload')">📤 上传插件</div>
    </div>
    <div class="container">
        <!-- 插件配置 Tab -->
        <div id="tab-plugins">
            <div id="plugin-list"><div style="text-align:center;padding:40px;color:var(--text-muted)">正在读取已加载插件...</div></div>
        </div>

        <!-- 手机遥控 Tab -->
        <div id="tab-remote" style="display:none">
            <div class="card">
                <div class="card-title" style="margin-bottom:12px">向掌机输入文字</div>
                <div class="card-desc">方便在手机上长按粘贴复杂密码、直播源链接或搜索词</div>
                <div class="form-group">
                    <input type="text" id="remote-text-input" class="form-control" placeholder="在此粘贴长文本或输入文字...">
                </div>
                <button class="btn" onclick="sendRemoteText()">发送文字到掌机屏幕</button>
            </div>
            <div class="card">
                <div class="card-title">虚拟手柄按键</div>
                <div class="dpad-grid">
                    <div></div>
                    <button class="pad-btn" onclick="sendButton('UP')">▲</button>
                    <div></div>
                    <button class="pad-btn" onclick="sendButton('LEFT')">◀</button>
                    <div></div>
                    <button class="pad-btn" onclick="sendButton('RIGHT')">▶</button>
                    <div></div>
                    <button class="pad-btn" onclick="sendButton('DOWN')">▼</button>
                    <div></div>
                </div>
                <div class="action-grid">
                    <button class="pad-btn" style="background:#da3633" onclick="sendButton('B')">B 返回</button>
                    <button class="pad-btn" style="background:#238636" onclick="sendButton('A')">A 确认</button>
                    <button class="pad-btn" style="background:#1f6feb" onclick="sendButton('X')">X</button>
                    <button class="pad-btn" style="background:#d29922" onclick="sendButton('Y')">Y</button>
                </div>
            </div>
        </div>

        <!-- 上传插件 Tab -->
        <div id="tab-upload" style="display:none">
            <div class="card">
                <div class="card-title" style="margin-bottom:10px">上传并安装插件 (.zip)</div>
                <div class="card-desc">选择手机内下载的第三方源 ZIP 压缩包，自动安装并加载到掌机</div>
                <div class="form-group">
                    <input type="file" id="zip-file-input" class="form-control" accept=".zip">
                </div>
                <button class="btn" onclick="uploadPlugin()">上传并安装</button>
            </div>
        </div>
    </div>
    <div id="toast" class="toast">配置已保存</div>

    <script>
        function showToast(msg) {
            const t = document.getElementById('toast');
            t.innerText = msg;
            t.classList.add('show');
            setTimeout(() => t.classList.remove('show'), 2500);
        }
        function switchTab(name) {
            document.querySelectorAll('.nav-tab').forEach((el, idx) => {
                el.classList.toggle('active', (name === 'plugins' && idx === 0) || (name === 'remote' && idx === 1) || (name === 'upload' && idx === 2));
            });
            document.getElementById('tab-plugins').style.display = name === 'plugins' ? 'block' : 'none';
            document.getElementById('tab-remote').style.display = name === 'remote' ? 'block' : 'none';
            document.getElementById('tab-upload').style.display = name === 'upload' ? 'block' : 'none';
        }

        // ── 访问令牌 ───────────────────────────────────────────
        // 门户面向局域网提供插件配置读写、按键注入与插件安装，因此所有
        // /api/* 都要求令牌。用户扫码时地址里带 ?token=xxx，这里把它取出
        // 写进 Cookie，之后的 fetch 自动携带；同时包一层 portalFetch 兜底。
        function readToken() {
            const m = location.search.match(/[?&]token=([^&]+)/);
            if (m) {
                const t = decodeURIComponent(m[1]);
                document.cookie = 'portal_token=' + encodeURIComponent(t) + '; path=/; SameSite=Strict';
                return t;
            }
            const c = document.cookie.match(/(?:^|;\s*)portal_token=([^;]+)/);
            return c ? decodeURIComponent(c[1]) : '';
        }
        const PORTAL_TOKEN = readToken();
        function portalFetch(url, opts) {
            const o = Object.assign({}, opts || {});
            o.headers = Object.assign({}, o.headers || {});
            if (PORTAL_TOKEN) o.headers['X-Auth-Token'] = PORTAL_TOKEN;
            return fetch(url, o);
        }

        async function loadPlugins() {
            try {
                const res = await portalFetch('/api/plugins');
                if (res.status === 401) {
                    document.getElementById('plugin-list').innerHTML =
                        '<div class="card"><div class="card-desc" style="text-align:center">' +
                        '未授权：请重新扫描掌机上的二维码（地址里需带 token 参数）。</div></div>';
                    return;
                }
                const list = await res.json();
                const container = document.getElementById('plugin-list');
                if (!list.length) {
                    container.innerHTML = '<div class="card"><div class="card-desc" style="text-align:center">掌机当前尚未安装插件，可在“上传插件”标签页上传 ZIP 安装。</div></div>';
                    return;
                }
                container.innerHTML = list.map(p => {
                    const fields = (p.settings && p.settings.length) ? p.settings : [
                        { key: 'token', label: '账号 Token / Cookie / 授权码', type: 'password', default_value: '' },
                        { key: 'server_url', label: '服务器或订阅地址', type: 'string', default_value: '' }
                    ];
                    const cfg = p.config || {};
                    const inputs = fields.map(f => {
                        const val = cfg[f.key] !== undefined ? cfg[f.key] : f.default_value;
                        return `
                            <div class="form-group">
                                <label class="form-label">${f.label || f.key}</label>
                                <input type="${f.type === 'password' ? 'password' : 'text'}" 
                                       class="form-control" 
                                       data-plugin="${p.id}" 
                                       data-key="${f.key}" 
                                       value="${val || ''}">
                            </div>
                        `;
                    }).join('');
                    return `
                        <div class="card">
                            <div class="card-header">
                                <span class="card-title">${p.name || p.id}</span>
                                <span style="font-size:0.8rem;color:var(--text-muted)">v${p.version || '1.0'}</span>
                            </div>
                            <div class="card-desc">${p.description || '暂无描述'}</div>
                            ${inputs}
                            <button class="btn" onclick="savePluginConfig('${p.id}')">保存配置并生效</button>
                        </div>
                    `;
                }).join('');
            } catch (e) {
                document.getElementById('plugin-list').innerHTML = '<div class="card">读取插件失败：' + e.message + '</div>';
            }
        }
        async function savePluginConfig(pluginId) {
            const inputs = document.querySelectorAll(`input[data-plugin="${pluginId}"]`);
            const config = {};
            inputs.forEach(inp => {
                config[inp.dataset.key] = inp.value;
            });
            try {
                const res = await portalFetch('/api/plugins/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: pluginId, config })
                });
                const ret = await res.json();
                if (ret.ok) showToast('已成功保存并同步到掌机！');
                else showToast('保存失败: ' + ret.error);
            } catch (e) {
                showToast('网络错误: ' + e.message);
            }
        }
        async function sendRemoteText() {
            const inp = document.getElementById('remote-text-input');
            const text = inp.value.trim();
            if (!text) return;
            try {
                await portalFetch('/api/remote/input', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ text })
                });
                showToast('已推送到掌机屏幕');
                inp.value = '';
            } catch (e) { showToast('推送失败: ' + e.message); }
        }
        async function sendButton(action) {
            try {
                await portalFetch('/api/remote/input', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ action })
                });
            } catch (e) {}
        }
        async function uploadPlugin() {
            const fileInp = document.getElementById('zip-file-input');
            if (!fileInp.files || !fileInp.files[0]) {
                showToast('请先选择 ZIP 插件文件');
                return;
            }
            const file = fileInp.files[0];
            try {
                showToast('正在上传安装...');
                const res = await portalFetch('/api/plugins/upload', {
                    method: 'POST',
                    body: file
                });
                const ret = await res.json();
                if (ret.ok) {
                    showToast('插件安装成功！');
                    fileInp.value = '';
                    switchTab('plugins');
                    loadPlugins();
                } else {
                    showToast('安装失败: ' + ret.message);
                }
            } catch (e) {
                showToast('上传失败: ' + e.message);
            }
        }
        loadPlugins();
    </script>
</body>
</html>
)HTML";
}

} // namespace muvik
