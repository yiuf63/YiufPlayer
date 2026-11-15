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

#include <sstream>
#include <vector>
#include <cstring>
#include <fstream>

using json = nlohmann::json;

namespace muvik {

WebPortal& WebPortal::getInstance() {
    static WebPortal instance;
    return instance;
}

WebPortal::~WebPortal() {
    stop();
}

bool WebPortal::start(int port) {
    if (running_.load()) return true;

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
            handleClient(client_fd);
            close(client_fd);
        }
    }
}

void WebPortal::handleClient(int client_fd) {
    // 读取 HTTP 请求报文
    std::string req_data;
    char buf[4096];
    ssize_t n = 0;
    size_t content_length = 0;
    bool header_done = false;
    size_t body_start_idx = 0;

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
        resp << "HTTP/1.1 " << code << " OK\r\n"
             << "Content-Type: " << type << "\r\n"
             << "Content-Length: " << content.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n\r\n"
             << content;
        std::string s = resp.str();
        send(client_fd, s.data(), s.size(), 0);
    };

    if (method == "OPTIONS") {
        sendResponse(204, "text/plain", "");
        return;
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
            json item = {
                {"id", p.id},
                {"name", p.name},
                {"version", p.version},
                {"author", p.author},
                {"description", p.description},
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
                            {"key", sf.key},
                            {"label", sf.label},
                            {"type", sf.type},
                            {"default_value", sf.default_value}
                        });
                    }
                }
            }
            arr.push_back(item);
        }
        sendResponse(200, "application/json; charset=utf-8", arr.dump());
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
        // 保存上传的二进制 ZIP 并解压安装
        std::string tmp_zip = "/tmp/uploaded_plugin.zip";
        std::ofstream ofs(tmp_zip, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
            ofs.close();

            auto res = PluginInstaller::installFromZip(tmp_zip);
            if (res.ok) {
                PluginManager::getInstance().loadAll();
                brls::sync([res]() {
                    brls::Application::notify("已成功安装插件！");
                });
                sendResponse(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"安装成功\"}");
            } else {
                sendResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"" + res.message + "\"}");
            }
            std::remove(tmp_zip.c_str());
            return;
        }
        sendResponse(500, "application/json; charset=utf-8", "{\"error\":\"cannot save file\"}");
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
                    std::string v = pair.substr(eq + 1);
                    std::string decoded;
                    for (size_t i = 0; i < v.size(); ++i) {
                        if (v[i] == '%' && i + 2 < v.size()) {
                            int hex_val = 0;
                            std::istringstream hex_ss(v.substr(i + 1, 2));
                            if (hex_ss >> std::hex >> hex_val) {
                                decoded.push_back(static_cast<char>(hex_val));
                                i += 2;
                                continue;
                            }
                        } else if (v[i] == '+') {
                            decoded.push_back(' ');
                            continue;
                        }
                        decoded.push_back(v[i]);
                    }
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
            res["items"].push_back({
                {"name", item.name},
                {"path", item.path},
                {"type", item.is_directory() ? "directory" : (item.is_audio() ? "audio" : "video")},
                {"size", item.size},
                {"thumbnail", item.thumbnail_url}
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
        async function loadPlugins() {
            try {
                const res = await fetch('/api/plugins');
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
                const res = await fetch('/api/plugins/save', {
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
                await fetch('/api/remote/input', {
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
                await fetch('/api/remote/input', {
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
                const res = await fetch('/api/plugins/upload', {
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
