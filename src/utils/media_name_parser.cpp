#include <string>
#include "utils/media_name_parser.hpp"
#include <algorithm>
#include <cctype>

namespace muvik {

namespace {

// 技术标记：出现即截断，其后的内容通常是编码/发行信息，不属于片名。
// 按长度从长到短匹配可避免 "web-dl" 被 "web" 之类误截。
const char* const kTechnicalMarkers[] = {
    "blu-ray", "web-dl", "2160p", "1080p", "720p", "480p",
    "bluray", "brrip", "webrip", "webdl", "hdtv", "dvdrip", "bdrip",
    "remux", "proper", "repack", "extended",
    "x264", "x265", "h264", "h265", "hevc",
    "aac", "ac3", "dts", "dd5", "4k",
};

bool isFourDigitYear(const std::string& s, size_t pos, int& out) {
    if (pos + 4 > s.size()) return false;
    for (size_t k = 0; k < 4; ++k) {
        if (!std::isdigit(static_cast<unsigned char>(s[pos + k]))) return false;
    }
    try {
        int y = std::stoi(s.substr(pos, 4));
        if (y >= 1900 && y <= 2100) {
            out = y;
            return true;
        }
    } catch (...) {
        // 转换失败视为未匹配
    }
    return false;
}

} // namespace

void MediaNameParser::parse(const std::string& filename,
                            std::string& out_query,
                            int64_t& out_year) {
    out_query.clear();
    out_year = 0;

    if (filename.empty()) return;

    // 去掉扩展名。仅当「扩展名」看起来像真正的媒体后缀时才剥离，
    // 否则 "SomeShow.2018" 这种以年份结尾的片名会被错误截断。
    std::string stem = filename;
    auto dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < stem.size()) {
        std::string ext = stem.substr(dot + 1);
        // 媒体后缀必备条件：1-5 个字符且全为字母（mkv/mp4/ts 等）
        bool looks_like_ext = !ext.empty() && ext.size() <= 5;
        for (char c : ext) {
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                looks_like_ext = false;
                break;
            }
        }
        if (looks_like_ext) stem = stem.substr(0, dot);
    }

    // 1. 定位年份
    size_t year_pos = std::string::npos;
    for (size_t i = 0; i + 4 <= stem.size(); ++i) {
        // 年份必须是独立数字段，不能是更长数字的一部分
        if (i > 0 && std::isdigit(static_cast<unsigned char>(stem[i - 1]))) continue;
        if (i + 4 < stem.size() &&
            std::isdigit(static_cast<unsigned char>(stem[i + 4]))) continue;
        int y = 0;
        if (isFourDigitYear(stem, i, y)) {
            out_year = y;
            year_pos = i;
            break;
        }
    }

    std::string cut = (year_pos != std::string::npos) ? stem.substr(0, year_pos) : stem;

    // 2. 截断技术标记（大小写不敏感，取最靠前的出现位置）
    std::string lower = cut;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    size_t best = cut.size();
    for (const char* marker : kTechnicalMarkers) {
        size_t p = lower.find(marker);
        if (p != std::string::npos && p < best) best = p;
    }
    cut = cut.substr(0, best);

    // 3. 分隔符统一为空格
    for (char& c : cut) {
        switch (c) {
            case '.': case '_': case '-': case '[': case ']':
            case '(': case ')': case '{': case '}':
                c = ' ';
                break;
            default:
                break;
        }
    }

    // 4. 压缩多余空格并去掉首尾空白
    std::string cleaned;
    cleaned.reserve(cut.size());
    bool prev_space = true;
    for (char c : cut) {
        if (c == ' ') {
            if (!prev_space) cleaned.push_back(' ');
            prev_space = true;
        } else {
            cleaned.push_back(c);
            prev_space = false;
        }
    }
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    while (!cleaned.empty() && cleaned.front() == ' ') cleaned.erase(cleaned.begin());

    out_query = cleaned;
}

std::string MediaNameParser::queryFromFilename(const std::string& filename) {
    std::string q;
    int64_t y = 0;
    parse(filename, q, y);
    return q;
}

} // namespace muvik
