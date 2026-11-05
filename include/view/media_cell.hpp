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

#include <string>

#include <borealis/core/box.hpp>

#include "ui/icons.hpp"
#include "ui/scene_tile.hpp"

namespace brls { class ScrollingFrame; }

namespace deck_player {

// 列表行。高度固定 76：手掌机触屏上小于 64 的点击区容易误触，
// 大于 88 一屏又放不下几项。
class MediaCell : public brls::Box {
public:
    static constexpr float HEIGHT = 76.0f;

    MediaCell(ui::Icon icon, std::string title, std::string subtitle);

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

} // namespace deck_player
