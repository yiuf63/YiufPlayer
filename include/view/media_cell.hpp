#pragma once

// 主界面列表的三种基础视图：MediaCell（卡片行）/ SectionHeader / EmptyState。
//
// 为什么不继续用 borealis 的 DetailCell：
//   DetailCell 是「左边一行文字 + 右边一行灰色文字 + 底部一条分割线」，
//   本质是设置页的样式。用它堆媒体库的结果就是：
//     · 每条都靠 [视频] / [音乐] 这种方括号前缀区分类型，像终端输出；
//     · 没有卡片、没有图标、没有层级，整页看上去是「一列文字」；
//     · 选中态只有一圈细边框，电视距离下几乎看不出焦点在哪。
//   这里改成自绘卡片：类型图标块 + 两级文字 + 右侧元信息 + 续播进度条，
//   选中态是「底色提亮 + 强调色描边 + 左侧短竖条」，一眼能看出焦点。
//
// 自绘（而不是拼 borealis 子控件）的原因：
//   1. 行内所有元素的垂直基线需要精确对齐，用 Yoga 嵌套 4 层 Box 反而更难控；
//   2. 文字截断、进度条、描边这些细节 borealis 控件不提供；
//   3. 一次 draw() 画完，没有子视图，滚动长列表时开销更低。

#include <chrono>
#include <string>

#include <borealis/core/box.hpp>

#include "ui/icons.hpp"
#include "ui/scene_tile.hpp"

namespace brls { class ScrollingFrame; }

namespace muvik {

// 列表行。高度固定 76：手掌机触屏上小于 64 的点击区容易误触，
// 大于 88 一屏又放不下几项。
class MediaCell : public brls::Box {
public:
    static constexpr float HEIGHT = 76.0f;

    MediaCell(ui::Icon icon, std::string title, std::string subtitle = "");

    void setTitle(std::string title);
    void setSubtitle(std::string subtitle);
    // 右侧元信息（文件大小 / 时长 / 评分）。空字符串表示不显示。
    void setBadge(std::string badge);
    // 续播进度 0..1；传 <0 隐藏。
    void setProgress(float ratio);
    // 目录 / 需要进入下一层的条目显示右箭头
    void setChevron(bool show);
    // 用 16:9 缩略图替代左侧的类型图标块。
    // 只在视频行上调用 —— 音频没有画面，硬塞一张占位图只会让列表更花。
    void setThumbnail(ui::SceneSpec spec);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    ui::Icon icon_;
    std::string title_;
    std::string subtitle_;
    std::string badge_;
    float progress_ = -1.0f;
    bool chevron_ = false;
    bool has_thumb_ = false;
    ui::SceneSpec thumb_;

    // ── 焦点过渡（0 = 未选中，1 = 完全选中）──
    //
    // 为什么需要：原本选中态是硬切 —— 焦点一移动，底色、光晕、图标颜色
    // 在同一帧全部跳变。手柄连按方向键时，一屏十几行会变成一片闪烁，
    // 眼睛抓不住「现在选的是哪个」。加约 120ms 的插值之后，选中态像被
    // 推过去，视线能跟住。
    //
    // 实现上不引 tween 库：borealis 主循环每帧都会调 draw()，这里按墙钟
    // 时间算进度即可（与 mpv_player 的 OSD 用同一套办法）。
    float focus_anim_ = 0.0f;                           // 当前插值结果
    bool focus_last_ = false;                           // 上一帧是否选中
    std::chrono::steady_clock::time_point focus_t0_{};  // 本次过渡起点
    float focus_from_ = 0.0f;                           // 过渡起点的值
};

// 分组标题：小号大写感 + 大留白，负责把列表切成「段落」。
class SectionHeader : public brls::Box {
public:
    static constexpr float HEIGHT = 46.0f;
    explicit SectionHeader(std::string title);
    void setTitle(std::string title) { title_ = std::move(title); }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    std::string title_;
};

// 统一滚动条的观感。
// borealis 的滚动指示器是「白色 4px 竖条 + 0.3 透明度」，贴在内容右缘；
// 列表只有一屏多时，它的高度约等于整屏高，看上去就像一条来路不明的
// 分割线。这里把它调成几乎不可见的提示色——列表本身有卡片半露，
// 已经足够暗示「还能往下滚」。
void styleScrollingFrame(brls::ScrollingFrame* scroller);

// 空状态：图标 + 主文案 + 说明。比一行灰字更像「软件」而不是「报错」。
class EmptyState : public brls::Box {
public:
    EmptyState(ui::Icon icon, std::string title, std::string hint);
    void setHint(std::string hint) { hint_ = std::move(hint); }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    ui::Icon icon_;
    std::string title_;
    std::string hint_;
};

} // namespace muvik
