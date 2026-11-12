#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "model/metadata_store.hpp"
#include "utils/app_paths.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

namespace muvik {

namespace {

// 字段用 \x1f (Unit Separator) 分隔，避免与路径/简介中的常规字符冲突。
// 简介可能含换行，用 \x1f 作分隔符时换行是安全的。
constexpr char kSep = '\x1f';

// 转义：换行/回车/分隔符本身必须编码，否则一条记录会被拆成多行。
// 用 '\\' 作转义前缀，保证 writePipeLine / getline 的行式存储是安全的。
std::string escapeField(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n':  out += "\\n";  break;
            case '\r':  out += "\\r";  break;
            case kSep:   out += "\\s";  break;
            default:     out.push_back(c); break;
        }
    }
    return out;
}

std::string unescapeField(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        char e = in[++i];
        switch (e) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 's': out.push_back(kSep);  break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back('\\'); out.push_back(e); break;
        }
    }
    return out;
}

std::string joinFields(const std::vector<std::string>& fields) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out.push_back(kSep);
        out += escapeField(fields[i]);
    }
    return out;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == kSep) { out.push_back(unescapeField(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(unescapeField(cur));
    return out;
}

int64_t nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

MetadataStore& MetadataStore::getInstance() {
    static MetadataStore instance;
    return instance;
}

MetadataStore::MetadataStore() {
    load();
}

std::string MetadataStore::filePath() const {
    return AppPaths::configFile("metadata.dat");
}

std::optional<MediaMetadata> MetadataStore::get(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = records_.find(path);
    if (it == records_.end()) return std::nullopt;
    return it->second;
}

void MetadataStore::put(const MediaMetadata& meta) {
    if (meta.path.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MediaMetadata copy = meta;
    copy.updated_at = nowSec();
    records_[copy.path] = std::move(copy);
}

void MetadataStore::remove(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    records_.erase(path);
}

bool MetadataStore::has(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return records_.find(path) != records_.end();
}

std::vector<MediaMetadata> MetadataStore::getAll() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MediaMetadata> out;
    out.reserve(records_.size());
    for (const auto& kv : records_) out.push_back(kv.second);
    return out;
}

size_t MetadataStore::count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return records_.size();
}

void MetadataStore::load() {
    records_.clear();
    std::ifstream in(filePath());
    if (!in.is_open()) return;

    std::string line;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto f = splitFields(line);
            // 字段顺序：path title overview poster backdrop date genres rating runtime scraper updated
            if (f.size() < 11) continue;
            MediaMetadata m;
            m.path = f[0];
            m.title = f[1];
            m.overview = f[2];
            m.poster_url = f[3];
            m.backdrop_url = f[4];
            m.release_date = f[5];
            m.genres = f[6];
            try { m.rating = std::stod(f[7]); } catch (...) { m.rating = -1.0; }
            try { m.runtime_minutes = std::stoi(f[8]); } catch (...) { m.runtime_minutes = -1; }
            m.scraper_id = f[9];
            try { m.updated_at = std::stoll(f[10]); } catch (...) { m.updated_at = 0; }
            records_[m.path] = std::move(m);
        }
    }
}

void MetadataStore::save() const {
    AppPaths::ensureDir(AppPaths::configDir());

    // 先写临时文件再原子替换，避免写入中断损坏元数据
    std::string target = filePath();
    std::string tmp = target + ".tmp";

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        out << "# Muvik metadata v1 (fields separated by 0x1f)\n";
        for (const auto& kv : records_) {
            const MediaMetadata& m = kv.second;
            std::ostringstream rating;
            rating << m.rating;
            out << joinFields({
                m.path, m.title, m.overview, m.poster_url, m.backdrop_url,
                m.release_date, m.genres, rating.str(),
                std::to_string(m.runtime_minutes), m.scraper_id,
                std::to_string(m.updated_at)
            }) << '\n';
        }
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        // 跨设备等情况下 rename 失败，退化为覆盖写
        std::error_code ec2;
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec2);
        fs::remove(tmp, ec2);
    }
}

} // namespace muvik
