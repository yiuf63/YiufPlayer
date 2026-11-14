#include "model/network_source.hpp"
#include "utils/app_paths.hpp"

#include <borealis/extern/nlohmann/json.hpp>

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

#include <fstream>
#include <chrono>

using json = nlohmann::json;

namespace muvik {

NetworkSourceStore& NetworkSourceStore::getInstance() {
    static NetworkSourceStore instance;
    return instance;
}

NetworkSourceStore::NetworkSourceStore() {
    load();
}

std::string NetworkSourceStore::getStoreFilePath() const {
    return AppPaths::configFile("network_sources.json");
}

void NetworkSourceStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();

    std::string path = getStoreFilePath();
    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        json j;
        file >> j;
        if (!j.is_array()) return;

        for (const auto& item : j) {
            NetworkSource src;
            src.id = item.value("id", "");
            src.name = item.value("name", "");
            src.type = NetworkSource::typeFromString(item.value("type", "WebDAV"));
            src.url = item.value("url", "");
            src.username = item.value("username", "");
            src.password = item.value("password", "");
            src.root_path = item.value("root_path", "/");

            if (!src.id.empty() && !src.url.empty()) {
                sources_.push_back(std::move(src));
            }
        }
        brls::Logger::info("NetworkSourceStore: 已加载 {} 个网络存储源", sources_.size());
    } catch (const std::exception& e) {
        brls::Logger::warning("NetworkSourceStore 加载失败: {}", e.what());
    }
}

void NetworkSourceStore::save() {
    AppPaths::ensureDir(AppPaths::configDir());
    std::string path = getStoreFilePath();
    json j = json::array();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& src : sources_) {
            json item;
            item["id"] = src.id;
            item["name"] = src.name;
            item["type"] = src.typeString();
            item["url"] = src.url;
            item["username"] = src.username;
            item["password"] = src.password;
            item["root_path"] = src.root_path;
            j.push_back(item);
        }
    }

    try {
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            file << j.dump(2);
        }
    } catch (const std::exception& e) {
        brls::Logger::error("NetworkSourceStore 保存失败: {}", e.what());
    }
}

std::vector<NetworkSource> NetworkSourceStore::getSources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

bool NetworkSourceStore::getSource(const std::string& id, NetworkSource& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& src : sources_) {
        if (src.id == id) {
            out = src;
            return true;
        }
    }
    return false;
}

bool NetworkSourceStore::addSource(const NetworkSource& source) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : sources_) {
            if (s.id == source.id) return false;
        }
        sources_.push_back(source);
    }
    save();
    return true;
}

bool NetworkSourceStore::updateSource(const NetworkSource& source) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : sources_) {
            if (s.id == source.id) {
                s = source;
                found = true;
                break;
            }
        }
    }
    if (found) save();
    return found;
}

bool NetworkSourceStore::removeSource(const std::string& id) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(sources_.begin(), sources_.end(),
                                 [&id](const NetworkSource& s) { return s.id == id; });
        if (it != sources_.end()) {
            sources_.erase(it, sources_.end());
            removed = true;
        }
    }
    if (removed) save();
    return removed;
}

void NetworkSourceStore::clear() {
    clearInMemory();
    save();
}

void NetworkSourceStore::clearInMemory() {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();
}

} // namespace muvik
