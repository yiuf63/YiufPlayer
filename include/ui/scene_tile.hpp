#pragma once

// 媒体画面的统一绘制入口。
//
// 三处地方要画「这一条媒体长什么样」：文件夹卡片的四宫格、平铺网格的大卡、
// 列表行的 16:9 小缩略图。三处各画一套的结果是同一部片在三个位置颜色不同，
// 用户会以为看错了。这里只有一份实现，参数只决定尺寸与圆角。
//
// 现在的画面是占位（按路径哈希取色 + 山脊剪影）。抽帧管线接上之后
// 只需要在 drawScene() 里优先读 `thumb_path` 指向的 PNG，所有位置一起换掉。

#include <cstdint>
#include <filesystem>
#include <string>
#include <string>

#include <borealis/extern/nanovg/nanovg.h>

namespace muvik::ui {

struct SceneSpec {
    // 已抽出的帧（PNG 绝对路径）。空或读不到时退回占位画面。
    std::string thumb_path;
    // 占位画面的取色种子：传路径的哈希，同一个文件恒定
    uint32_t seed = 0;
    // 音频没有画面，右下角补一枚音符标
    bool audio = false;
};

void drawScene(NVGcontext* vg, float x, float y, float w, float h,
               float tl, float tr, float br, float bl, const SceneSpec& spec);

// FNV-1a：短字符串上分布够散，且同一条路径永远得到同一个值
uint32_t hashString(const std::string& s);

// 缩略图缓存是否已经有这张图（抽帧完成后用于判断要不要重画占位）
//
// ⚠ 注意它检查的是 **nanoVG 句柄是否已加载**，而句柄只在 drawScene()
// 里懒加载。因此在「还没绘制过」的时机（比如控件构造时）它恒为 false，
// 即使文件明明存在。判断「要不要按有图布局」请用下面那个。
bool sceneImageReady(const std::string& path);

// 缩略图文件是否存在且可读（不要求已经加载成纹理）。
//
// 用于构造期决定布局：有图走大卡、无图收成紧凑行。用上一个是错的 ——
// 那时句柄必然还没加载，会永远判成无图。
//
// 内联实现是为了能脱离 nanoVG 单独测试：它纯粹是文件系统判断，
// 和渲染无关，不该被拖进 GL 依赖里。
inline bool sceneImageFileExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    // 太小的多半是写到一半被强杀留下的空壳
    return !ec && sz > 128;
}

} // namespace muvik::ui
