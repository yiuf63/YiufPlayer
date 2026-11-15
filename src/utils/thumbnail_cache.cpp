#include <functional>
#include <string>
#include <unordered_map>
#include "utils/thumbnail_cache.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <borealis.hpp>

#include "utils/app_paths.hpp"

#ifdef MUVIK_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace muvik {

namespace {

constexpr char kMagic[4] = {'D', 'P', 'T', '1'};
constexpr int kThumbW = 320;
constexpr int kMaxThumbH = 200;

// 64 位 FNV-1a，转成 16 位十六进制做文件名。
// 用路径哈希而不是「原文件名」：不同目录下的同名文件会互相覆盖，
// 而列表里 01.mp4 / 02.mp4 这种命名极常见。
std::string hashName(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf) + ".rgba";
}

std::string thumbDir() {
    return AppPaths::configDir() + "/thumbnails";
}

// 不加锁版本：调用方必须已持有 g_mutex。
// filePath() 会对 g_mutex 上锁，requestBatch 里拿着这把锁逐个查缓存，
// 直接调 filePath() 就是「同一线程对非递归互斥量加锁两次」——
// 启动即死锁，表现为窗口出来之前整个进程挂住、日志停在「启动标签」。
std::string filePathUnlocked(const std::string& media_path) {
    if (media_path.empty()) return {};
    const std::string p = thumbDir() + "/" + hashName(media_path);
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return {};
    // 头部校验：写到一半被强杀会留下半个文件，这种要当作没有
    char magic[4] = {0};
    f.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return {};
    return p;
}

// 并发保护：后台线程写缓存、UI 线程读 filePath。
std::mutex g_mutex;
std::set<std::string> g_in_flight;

// 抽帧失败过的路径：本次会话不再重试。
// 失败文件（损坏、纯音频误判、不支持的编码）如果没有这个记录，
// 就是「请求 → 失败 → 回调重建 → 重建又请求」的死循环 ——
// 主线程在 performSyncTasks 里每帧重建一次列表，界面永远出不来。
std::set<std::string> g_failed;

// 查询记忆：媒体路径 → 缩略图路径（空串表示「还没有」）。
// 绘制层每帧都会问一次，不记忆就是一屏十几张卡每帧十几次 open(2)。
std::unordered_map<std::string, std::string> g_memo;

#ifdef MUVIK_HAVE_FFMPEG

bool writeRgba(const std::string& out, int w, int h, const uint8_t* rgba) {
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(kMagic, 4);
    uint16_t ww = static_cast<uint16_t>(w);
    uint16_t hh = static_cast<uint16_t>(h);
    f.write(reinterpret_cast<const char*>(&ww), sizeof(ww));
    f.write(reinterpret_cast<const char*>(&hh), sizeof(hh));
    f.write(reinterpret_cast<const char*>(rgba),
            static_cast<std::streamsize>(w) * h * 4);
    return f.good();
}

bool extractFrame(const std::string& media, const std::string& out) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, media.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    AVStream* st = fmt->streams[vs];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) {
        avformat_close_input(&fmt);
        return false;
    }
    bool ok = false;
    AVFrame* frm = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();

    do {
        if (avcodec_parameters_to_context(cc, st->codecpar) < 0) break;
        cc->thread_count = 1;   // 抽一帧不值得开线程池
        if (avcodec_open2(cc, dec, nullptr) < 0) break;
        if (!frm || !pkt) break;

        // 定位到片头之后：第 0 帧经常是黑场或台标，抽出来整排卡片都是黑的。
        // 取时长的 1/10，上限 120 秒 —— 两小时的电影不会跳到第 12 分钟。
        int64_t target = 10LL * AV_TIME_BASE;
        if (fmt->duration > 0) target = fmt->duration / 10;
        const int64_t cap = 120LL * AV_TIME_BASE;
        if (target > cap) target = cap;
        if (av_seek_frame(fmt, -1, target, AVSEEK_FLAG_BACKWARD) < 0) {
            // 有些容器不支持 seek：退回从头读
            av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
        }
        avcodec_flush_buffers(cc);

        bool decoded = false;
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == vs) {
                if (avcodec_send_packet(cc, pkt) == 0 &&
                    avcodec_receive_frame(cc, frm) == 0) {
                    decoded = true;
                    av_packet_unref(pkt);
                    break;
                }
            }
            av_packet_unref(pkt);
        }
        if (!decoded) break;

        const int sw = frm->width;
        const int sh = frm->height;
        if (sw <= 0 || sh <= 0) break;

        int tw = kThumbW;
        int th = static_cast<int>(static_cast<double>(sh) * tw / sw);
        if (th > kMaxThumbH) {
            th = kMaxThumbH;
            tw = static_cast<int>(static_cast<double>(sw) * th / sh);
        }
        if (tw < 16 || th < 9) break;

        SwsContext* sws = sws_getContext(sw, sh, static_cast<AVPixelFormat>(frm->format),
                                         tw, th, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) break;

        std::vector<uint8_t> buf(static_cast<size_t>(tw) * th * 4);
        uint8_t* dst[4] = {buf.data(), nullptr, nullptr, nullptr};
        int dst_linesize[4] = {tw * 4, 0, 0, 0};
        sws_scale(sws, frm->data, frm->linesize, 0, sh, dst, dst_linesize);
        sws_freeContext(sws);

        ok = writeRgba(out, tw, th, buf.data());
    } while (false);

    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return ok;
}

#else

bool extractFrame(const std::string&, const std::string&) { return false; }

#endif // MUVIK_HAVE_FFMPEG

} // namespace

ThumbnailCache& ThumbnailCache::getInstance() {
    static ThumbnailCache inst;
    return inst;
}

bool ThumbnailCache::enabled() const {
#ifdef MUVIK_HAVE_FFMPEG
    return true;
#else
    return false;
#endif
}

std::string ThumbnailCache::filePath(const std::string& media_path) const {
    if (media_path.empty()) return {};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_memo.find(media_path);
        if (it != g_memo.end()) return it->second;
    }

    std::string found;
    const std::string p = thumbDir() + "/" + hashName(media_path);
    std::ifstream f(p, std::ios::binary);
    if (f.is_open()) {
        // 头部校验：写到一半被强杀会留下半个文件，这种要当作没有
        char magic[4] = {0};
        f.read(magic, 4);
        if (std::memcmp(magic, kMagic, 4) == 0) found = p;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_memo[media_path] = found;
    return found;
}

void ThumbnailCache::invalidateMemo() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_memo.clear();
}

void ThumbnailCache::requestBatch(const std::vector<std::string>& media_paths,
                                  std::function<void()> on_done) {
    std::vector<std::string> todo;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& p : media_paths) {
            if (p.empty()) continue;
            if (!filePathUnlocked(p).empty()) continue;
            if (g_in_flight.count(p)) continue;
            if (g_failed.count(p)) continue;   // 失败过的不重试，见 g_failed 注释
            g_in_flight.insert(p);
            todo.push_back(p);
        }
    }

    if (todo.empty()) {
        // 没有新任务就**不回调**。on_done 的语义是「有新帧可以看了」，
        // 回调里做的是重建列表，重建又会请求同一批路径 ——
        // 全部已缓存时 todo 恒为空，一回调就是重建死循环，
        // 主线程在 performSyncTasks 里永远出不来。
        return;
    }

    if (!AppPaths::ensureDir(thumbDir())) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& p : todo) g_in_flight.erase(p);
        return;
    }

    std::thread([todo = std::move(todo), on_done]() {
        std::vector<std::string> failed;
        for (const auto& media : todo) {
            const std::string out = thumbDir() + "/" + hashName(media);
            if (!extractFrame(media, out)) failed.push_back(media);
            std::lock_guard<std::mutex> lock(g_mutex);
            g_in_flight.erase(media);
        }
        if (!failed.empty()) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_failed.insert(failed.begin(), failed.end());
        }
        // 先让记忆失效再通知：回调里会重建列表，重建时问的就是新状态
        ThumbnailCache::getInstance().invalidateMemo();
        if (on_done) brls::sync([on_done]() { on_done(); });
    }).detach();
}

void ThumbnailCache::clear() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_memo.clear();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string dir = thumbDir();
    // 只删 .rgba：这个目录归本类独占，但仍然按后缀过滤，
    // 避免以后往这里放别的东西时被误删
    std::error_code ec;
    try {
        std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::directory_iterator end;
        while (!ec && it != end) {
            try {
                const std::string name = it->path().filename().string();
                if (name.size() > 5 && name.compare(name.size() - 5, 5, ".rgba") == 0) {
                    std::error_code rm_ec;
                    std::filesystem::remove(it->path(), rm_ec);
                }
            } catch (...) {}
            it.increment(ec);
        }
    } catch (...) {}
}

} // namespace muvik
