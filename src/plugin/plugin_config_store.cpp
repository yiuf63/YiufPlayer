#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "plugin/plugin_config_store.hpp"
#include "utils/app_paths.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace deck_player {

namespace {

// 字段分隔符，与其它存储模块保持一致的做法（0x1f）
constexpr char kSep = '\x1f';

// 转义：换行与分隔符必须编码，否则会破坏行式存储
std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case kSep: out += "\\s";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        switch (in[++i]) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 's': out.push_back(kSep); break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back('\\'); out.push_back(in[i]); break;
        }
    }
    return out;
}

// 极简 JSON 字符串转义
std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

} // namespace

PluginConfigStore& PluginConfigStore::getInstance() {
    static PluginConfigStore instance;
    return instance;
}

PluginConfigStore::PluginConfigStore() {
    load();
}

std::string PluginConfigStore::filePath() const {
    return AppPaths::configFile("plugin_config.dat");
}

std::string PluginConfigStore::get(const std::string& plugin_id,
                                   const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto pit = data_.find(plugin_id);
    if (pit == data_.end()) return "";
    auto kit = pit->second.find(key);
    return kit == pit->second.end() ? "" : kit->second;
}

void PluginConfigStore::set(const std::string& plugin_id,
                            const std::string& key,
                            const std::string& value) {
    if (plugin_id.empty() || key.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (value.empty()) {
        auto pit = data_.find(plugin_id);
        if (pit != data_.end()) {
            pit->second.erase(key);
            if (pit->second.empty()) data_.erase(pit);
        }
        return;
    }
    data_[plugin_id][key] = value;
}

std::map<std::string, std::string>
PluginConfigStore::getAll(const std::string& plugin_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto pit = data_.find(plugin_id);
    return pit == data_.end() ? std::map<std::string, std::string>{} : pit->second;
}

bool PluginConfigStore::hasConfig(const std::string& plugin_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto pit = data_.find(plugin_id);
    return pit != data_.end() && !pit->second.empty();
}

void PluginConfigStore::clear(const std::string& plugin_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    data_.erase(plugin_id);
}

std::string PluginConfigStore::toJson(const std::string& plugin_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto pit = data_.find(plugin_id);
    if (pit == data_.end() || pit->second.empty()) return "{}";

    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : pit->second) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << jsonEscape(kv.first) << "\":\""
            << jsonEscape(kv.second) << "\"";
    }
    oss << "}";
    return oss.str();
}

void PluginConfigStore::load() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    data_.clear();

    std::ifstream in(filePath());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        // 格式：plugin_id<sep>key<sep>value
        std::vector<std::string> parts;
        std::string cur;
        for (char c : line) {
            if (c == kSep) { parts.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        parts.push_back(cur);
        if (parts.size() < 3) continue;

        const std::string pid = unescape(parts[0]);
        const std::string key = unescape(parts[1]);
        const std::string val = unescape(parts[2]);
        if (pid.empty() || key.empty()) continue;
        data_[pid][key] = val;
    }
}

void PluginConfigStore::save() const {
    AppPaths::ensureDir(AppPaths::configDir());

    std::string target = filePath();
    std::string tmp = target + ".tmp";

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        out << "# DeckPlayer plugin config v1 (fields separated by 0x1f)\n";
        for (const auto& pit : data_) {
            for (const auto& kv : pit.second) {
                out << escape(pit.first) << kSep
                    << escape(kv.first) << kSep
                    << escape(kv.second) << '\n';
            }
        }
    }

    // 原子替换，避免写入中断损坏配置
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code ec2;
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec2);
        fs::remove(tmp, ec2);
    }
}

} // namespace deck_player
