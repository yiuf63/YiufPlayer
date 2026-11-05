#pragma once

// 矢量图标库（nanoVG 路径绘制，不依赖字体字形）。
//
// 为什么不用字体图标 / emoji：
//   · emoji 在 macOS / Android / 掌机上的字形完全不同，甚至渲染成方框；
//   · 项目只带了一个中文字体，没有图标字体，硬编码 "[播放]" 这类文字
//     正是「OSD 就是一排文字堆在画面上」的根源。
//   用路径画图标，任何分辨率、任何平台、任何字号都完全一致，
//   并且可以按需上色 / 描边粗细，跟设计系统同一套 token。
//
// 所有图标定义在 24×24 的坐标系里，由 drawIcon() 缩放到目标尺寸。

#include <borealis/extern/nanovg/nanovg.h>

namespace deck_player::ui {

enum class Icon {
    None,         // 占位：只画文字、不画图标
    Play,
    Pause,
    SeekBack,     // «  后退 10 秒
    SeekForward,  // »
    Prev,         // |◀
    Next,         // ▶|
    Subtitles,
    SubtitlesOff,
    Audio,        // 波形（音轨）
    Volume,
    VolumeMute,
    Speed,        // 仪表盘（倍速）
    Aspect,       // 信箱模式（画面比例）
    Playlist,
    Lock,
    Unlock,
    Back,         // 返回箭头
    ChevronRight, // 列表行指示
    Folder,
    Video,
    Music,
    Clock,
    Close,
    Check,
    Error,
    Info,
    Brightness,
    Drive,
    Plugin,
    Search,
    Plus,
    Sliders,      // 三条滑轨（视图与排序）
    Grid,         // 2×2（平铺视图）
    List,         // 三行（列表视图）
    Filter,       // 漏斗（筛选）
};

// 以 (cx, cy) 为中心绘制，size 为图标外框边长（逻辑像素）。
// strokeWidth 也是逻辑像素，内部会按缩放比换算，保证不同尺寸观感一致。
void drawIcon(NVGcontext* vg, Icon icon, float cx, float cy, float size,
              NVGcolor color, float strokeWidth = 2.0f);

// 图标 + 底部圆形底的组合按钮视觉（只画，不处理命中）。
void drawIconButton(NVGcontext* vg, Icon icon, float cx, float cy, float diameter,
                    float iconSize, NVGcolor bg, NVGcolor fg, NVGcolor border,
                    float borderWidth = 1.0f);

} // namespace deck_player::ui
