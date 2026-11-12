#include "model/library_view_options.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "utils/app_paths.hpp"
#include "utils/i18n.hpp"
#include "utils/playback_history.hpp"

namespace muvik {

namespace {

const char* groupKey(LibraryGroup g) {
    return g == LibraryGroup::Flat ? "flat" : "folder";
}
const char* contentKey(LibraryContent c) {
    switch (c) {
        case LibraryContent::VideoOnly: return "video";
        case LibraryContent::AudioOnly: return "audio";
        default: return "all";
    }
}
const char* sortKey(LibrarySort s) {
    switch (s) {
        case LibrarySort::Recent: return "recent";
        case LibrarySort::Size: return "size";
        case LibrarySort::Progress: return "progress";
        default: return "name";
    }
}
const char* filterKey(LibraryFilter f) {
    switch (f) {
        case LibraryFilter::Unfinished: return "unfinished";
        case LibraryFilter::Finished: return "finished";
        default: return "all";
    }
}

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// 播放进度 0..1；没有记录或时长为 0 时返回 0。
float progressOf(const MediaItem& item) {
    HistoryRecord rec;
    if (!PlaybackHistory::getInstance().getRecord(item.path, rec)) return 0.0f;
    if (rec.completed) return 1.0f;
    if (rec.duration <= 0) return 0.0f;
    return static_cast<float>(static_cast<double>(rec.position) /
                              static_cast<double>(rec.duration));
}

bool isFinished(const MediaItem& item) {
    HistoryRecord rec;
    if (!PlaybackHistory::getInstance().getRecord(item.path, rec)) return false;
    return rec.completed;
}

bool isUnfinished(const MediaItem& item) {
    HistoryRecord rec;
    if (!PlaybackHistory::getInstance().getRecord(item.path, rec)) return false;
    return !rec.completed;
}

} // namespace

bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        const bool da = std::isdigit(ca) != 0;
        const bool db = std::isdigit(cb) != 0;

        if (da && db) {
            // 取整段数字比较数值：先跳过前导零，再比长度，最后逐位比
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == '0') ++si;
            while (sj < b.size() && b[sj] == '0') ++sj;
            size_t ei = si, ej = sj;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            const size_t la = ei - si;
            const size_t lb = ej - sj;
            if (la != lb) return la < lb;   // 位数多的一定更大
            for (size_t k = 0; k < la; ++k) {
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k];
            }
            // 数值相同（如 "02" 与 "2"）：短的先，视觉上更自然
            const size_t ra = ei - i;
            const size_t rb = ej - j;
            if (ra != rb) return ra < rb;
            i = ei;
            j = ej;
            continue;
        }

        // 数字优先于字母，让「01. 片名」成组排在前面
        if (da != db) return da;
        const unsigned char la = static_cast<unsigned char>(std::tolower(ca));
        const unsigned char lb = static_cast<unsigned char>(std::tolower(cb));
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    return a.size() < b.size();
}

LibraryViewOptions& LibraryViewOptions::get() {
    static LibraryViewOptions inst;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        inst.load();
    }
    return inst;
}

void LibraryViewOptions::load() {
    const std::string path = AppPaths::configFile("library_view.conf");
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);

        if (k == "group") {
            group = (v == "flat") ? LibraryGroup::Flat : LibraryGroup::Folder;
        } else if (k == "content") {
            content = (v == "video") ? LibraryContent::VideoOnly
                    : (v == "audio") ? LibraryContent::AudioOnly
                                     : LibraryContent::All;
        } else if (k == "sort") {
            sort = (v == "recent") ? LibrarySort::Recent
                 : (v == "size") ? LibrarySort::Size
                 : (v == "progress") ? LibrarySort::Progress
                                     : LibrarySort::Name;
        } else if (k == "filter") {
            filter = (v == "unfinished") ? LibraryFilter::Unfinished
                   : (v == "finished") ? LibraryFilter::Finished
                                       : LibraryFilter::All;
        } else if (k == "descending") {
            descending = (v == "1");
        }
    }
}

void LibraryViewOptions::save() const {
    AppPaths::ensureDir(AppPaths::configDir());
    std::ofstream out(AppPaths::configFile("library_view.conf"), std::ios::trunc);
    if (!out.is_open()) return;
    out << "# Muvik library view v1\n";
    out << "# key=value\n";
    out << "group=" << groupKey(group) << "\n";
    out << "content=" << contentKey(content) << "\n";
    out << "sort=" << sortKey(sort) << "\n";
    out << "descending=" << (descending ? "1" : "0") << "\n";
    out << "filter=" << filterKey(filter) << "\n";
}

void LibraryViewOptions::reset() {
    group = LibraryGroup::Folder;
    content = LibraryContent::All;
    sort = LibrarySort::Name;
    descending = false;
    filter = LibraryFilter::All;
    save();
}

std::string LibraryViewOptions::summary() const {
    std::string g = (group == LibraryGroup::Flat) ? i18n::tr("view_sheet.group_flat", "全部平铺")
                                                  : i18n::tr("view_sheet.group_folder", "按文件夹");
    std::string s = i18n::tr("view_sheet.sort_name", "名称");
    switch (sort) {
        case LibrarySort::Recent: s = i18n::tr("view_sheet.sort_recent", "最近添加"); break;
        case LibrarySort::Size: s = i18n::tr("view_sheet.sort_size", "大小"); break;
        case LibrarySort::Progress: s = i18n::tr("view_sheet.sort_progress", "进度"); break;
        default: break;
    }
    std::string out = g + " · " + s;
    if (content == LibraryContent::VideoOnly) out += " · " + i18n::tr("view_sheet.content_video", "仅视频");
    else if (content == LibraryContent::AudioOnly) out += " · " + i18n::tr("view_sheet.content_audio", "仅音乐");
    if (filter == LibraryFilter::Unfinished) out += " · " + i18n::tr("view_sheet.filter_unfinished", "未看完");
    else if (filter == LibraryFilter::Finished) out += " · " + i18n::tr("view_sheet.filter_finished", "已看完");
    return out;
}

bool LibraryViewOptions::showsRecent() const {
    return group == LibraryGroup::Folder && filter == LibraryFilter::All &&
           content == LibraryContent::All;
}

bool LibraryViewOptions::matches(const MediaItem& item) const {
    if (content == LibraryContent::VideoOnly && !item.is_video()) return false;
    if (content == LibraryContent::AudioOnly && !item.is_audio()) return false;
    if (filter == LibraryFilter::Unfinished && !isUnfinished(item)) return false;
    if (filter == LibraryFilter::Finished && !isFinished(item)) return false;
    return true;
}

std::vector<MediaItem> LibraryViewOptions::apply(std::vector<MediaItem> items,
                                                const std::string& query) const {
    const std::string q = lowerAscii(query);

    items.erase(std::remove_if(items.begin(), items.end(),
        [&](const MediaItem& it) {
            if (!matches(it)) return true;
            if (!q.empty() && lowerAscii(it.name).find(q) == std::string::npos) return true;
            return false;
        }), items.end());

    // 每个排序键自带「自然方向」，descending 再翻转。
    // 「最近添加」的自然方向是新的在前（比较函数里已经写成降序）——
    // 否则默认升序关着的时候，用户点「最近添加」看到的是最旧的一批，
    // 等于这个选项没生效。原型 `sortMedia()` 里的 `added` 也是这样：
    // 它记的是「几天前」，升序天然就是最新在前。
    switch (sort) {
        case LibrarySort::Name:
            std::stable_sort(items.begin(), items.end(),
                [](const MediaItem& a, const MediaItem& b) {
                    return naturalLess(a.name, b.name);
                });
            break;
        case LibrarySort::Recent:
            std::stable_sort(items.begin(), items.end(),
                [](const MediaItem& a, const MediaItem& b) {
                    return a.modified > b.modified;   // 新的在前
                });
            break;
        case LibrarySort::Size:
            std::stable_sort(items.begin(), items.end(),
                [](const MediaItem& a, const MediaItem& b) {
                    return a.size < b.size;
                });
            break;
        case LibrarySort::Progress:
            std::stable_sort(items.begin(), items.end(),
                [](const MediaItem& a, const MediaItem& b) {
                    return progressOf(a) < progressOf(b);
                });
            break;
    }
    if (descending) std::reverse(items.begin(), items.end());
    return items;
}

} // namespace muvik
