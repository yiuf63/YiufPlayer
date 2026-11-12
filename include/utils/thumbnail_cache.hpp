#pragma once

// 视频首帧缩略图。
//
// 为什么自己抽帧而不是让 mpv 截：mpv 的截图要走完整播放管线（起 window、
// 加载、等第一帧），在列表里为几十个文件各来一次不现实；抽帧只要
// demux + 单帧 decode，一个文件几十毫秒。
//
// 编译期可关：找不到 avformat/avcodec/swscale 时（例如 Android 的 jniLibs
// 里只有 libmpv），`enabled()` 返回 false，`filePath()` 恒为空，
// 所有画面退回 scene_tile 的占位。界面照常可用，不引入新的构建要求。
//
// 缓存格式是自己定义的裸 RGBA（"DPT1" + w + h + 像素），不是 PNG：
//   · nanoVG 有 nvgCreateImageRGBA，读进来就能直接上屏；
//   · 存 PNG 要额外接编码器（libavcodec 的 png encoder 不一定编进去），
//     而这张图只给本程序看，没有交换需求。

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace muvik {

class ThumbnailCache {
public:
    static ThumbnailCache& getInstance();

    // 是否具备抽帧能力（编译期决定）
    bool enabled() const;

    // 已缓存的缩略图文件路径；没有缓存时返回空串。
    // 绘制层每帧都会问一次，所以结果按路径记忆 —— 否则一屏十几张卡
    // 每帧要做十几次 open(2)。
    std::string filePath(const std::string& media_path) const;

    // 让记忆失效。抽帧完成、清空缓存之后必须调用，
    // 否则列表会一直显示占位画面。
    void invalidateMemo();

    bool has(const std::string& media_path) const { return !filePath(media_path).empty(); }

    // 后台批量生成。回调在主线程执行（用于让列表重绘一次）。
    // 已在队列里的路径不会重复排队。
    void requestBatch(const std::vector<std::string>& media_paths,
                      std::function<void()> on_done = nullptr);

    // 清空所有缩略图（设置页「清空缓存」连带调用）
    void clear();

private:
    ThumbnailCache() = default;
};

} // namespace muvik
