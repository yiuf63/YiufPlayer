#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace muvik {

enum class NetworkSourceType {
    WebDAV,
    SMB,
    Jellyfin,
    Custom
};

struct NetworkSource {
    std::string id;          // 唯一 ID，例如 "net_1731651234"
    std::string name;        // 显示名称，例如 "群晖 NAS", "AList 聚合盘"
    NetworkSourceType type = NetworkSourceType::WebDAV;
    std::string url;         // 服务器基础地址，例如 "http://192.168.1.100:5244/dav"
    std::string username;    // 账号（可为空）
    std::string password;    // 密码（可为空）
    std::string root_path;   // 根子路径（默认为空或 "/"）

    std::string typeString() const {
        switch (type) {
            case NetworkSourceType::WebDAV: return "WebDAV";
            case NetworkSourceType::SMB: return "SMB";
            case NetworkSourceType::Jellyfin: return "Jellyfin";
            default: return "Custom";
        }
    }

    static NetworkSourceType typeFromString(const std::string& str) {
        if (str == "SMB" || str == "smb") return NetworkSourceType::SMB;
        if (str == "Jellyfin" || str == "jellyfin") return NetworkSourceType::Jellyfin;
        return NetworkSourceType::WebDAV;
    }
};

class NetworkSourceStore {
public:
    static NetworkSourceStore& getInstance();

    // 加载与保存
    void load();
    void save();

    // 数据操作
    std::vector<NetworkSource> getSources() const;
    bool getSource(const std::string& id, NetworkSource& out) const;
    bool addSource(const NetworkSource& source);
    bool updateSource(const NetworkSource& source);
    bool removeSource(const std::string& id);

    // 默认测试配置或重置
    void clear();
    void clearInMemory();

private:
    NetworkSourceStore();
    ~NetworkSourceStore() = default;

    std::string getStoreFilePath() const;

    mutable std::mutex mutex_;
    std::vector<NetworkSource> sources_;
};

} // namespace muvik
