#pragma once

// 工具行与视图菜单共用的小胶囊按钮。
//
// 为什么不用 borealis 的 Button：它的默认高亮是「一圈细边框 + 整块变亮」，
// 而这里的按钮有选中态（当前排序键、当前筛选）—— 需要在同一套视觉里同时
// 表达「可聚焦」「已选中」两件事，否则用户分不清「焦点在这一项」和
// 「这一项是当前设置」。自绘后：焦点是外圈描边，选中是强调色底。
//
// 宽度不参与 Yoga 测量（文字是自绘的，Yoga 拿不到尺寸），
// 由调用方用 ui::approxTextWidth() 算好传进来。这是刻意的取舍：
// 让按钮参与测量就要把文字拆成子 Label，图标也跟着只能走 PNG。

#include <string>

#include <borealis/core/box.hpp>

#include "ui/icons.hpp"

namespace deck_player {

class ToolButton : public brls::Box {
public:
    static constexpr float HEIGHT = 38.0f;

    ToolButton(std::string text, ui::Icon icon = ui::Icon::None, float fontSize = 14.0f);

    void setText(std::string text);
    void setIcon(ui::Icon icon);
    // 选中态：强调色底 + 强调色文字
    void setActive(bool active);
    // 右侧一个 > 箭头，表示「点了会展开一个面板」
    void setChevron(bool show);
    // 紧凑档：菜单里的选项用（高 34、内边距更小）
    void setCompact(bool compact);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    std::string text_;
    ui::Icon icon_;
    float font_size_;
    bool active_ = false;
    bool chevron_ = false;
    bool compact_ = false;

public:
    // 按文字估算宽度（中文按 1 个字宽，ASCII 按 0.55）。构造按钮前先算宽。
    static float widthFor(const std::string& text, ui::Icon icon, bool chevron,
                          bool compact);
};

} // namespace deck_player
