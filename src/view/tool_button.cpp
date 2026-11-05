#include "view/tool_button.hpp"

#include <algorithm>

#include <borealis.hpp>

#include "ui/design.hpp"

namespace deck_player {

namespace {

float padXOf(bool compact) { return compact ? 11.0f : 14.0f; }
float iconSizeOf(bool compact) { return compact ? 13.0f : 15.0f; }

// 用 nanoVG 实测文字宽度。
//
// 之前用「中文 1 字宽、ASCII 0.56」粗估，实际字体（STHeiti）的全角
// advance 略大于字号，估出来的宽度永远差半个字 —— 菜单里所有胶囊
// 统一截成「按文…」「全部…」，一排按钮没有一个是全的。
// 构造按钮时 Application::init 已完成，NVG 上下文可用，直接量。
// 量不准的兜底（上下文为空）退回粗估 + 15% 余量。
float measureWidth(float size, const std::string& s) {
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg) return s.size() * size * 0.75f * 1.15f;
    const float w = ui::textWidth(vg, size, s);
    return w > 0.0f ? w + 2.0f : s.size() * size * 0.75f;
}

} // namespace

float ToolButton::widthFor(const std::string& text, ui::Icon icon, bool chevron,
                           bool compact) {
    const float pw = padXOf(compact);
    float w = pw * 2.0f;
    if (icon != ui::Icon::None) w += iconSizeOf(compact) + 7.0f;
    w += measureWidth(compact ? 13.0f : 14.0f, text);
    if (chevron) w += 6.0f + 11.0f;
    return w;
}

ToolButton::ToolButton(std::string text, ui::Icon icon, float fontSize)
    : text_(std::move(text)), icon_(icon), font_size_(fontSize) {
    this->setFocusable(true);
    this->setHideHighlight(true);   // 焦点由我们自己画
    this->setHeight(HEIGHT);
    this->setWidth(widthFor(text_, icon_, false, false));
}

void ToolButton::setText(std::string text) {
    text_ = std::move(text);
    this->setWidth(widthFor(text_, icon_, chevron_, compact_));
}

void ToolButton::setIcon(ui::Icon icon) {
    icon_ = icon;
    this->setWidth(widthFor(text_, icon_, chevron_, compact_));
}

void ToolButton::setActive(bool active) { active_ = active; }

void ToolButton::setChevron(bool show) {
    chevron_ = show;
    this->setWidth(widthFor(text_, icon_, chevron_, compact_));
}

void ToolButton::setCompact(bool compact) {
    compact_ = compact;
    font_size_ = compact ? 13.0f : font_size_;
    this->setHeight(compact ? 34.0f : HEIGHT);
    this->setWidth(widthFor(text_, icon_, chevron_, compact));
}

void ToolButton::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    if (width < 8.0f || height < 8.0f) return;

    const auto& pal = ui::palette();
    const bool focused = this->isFocused();
    const float r = height * 0.5f;

    NVGcolor bg = active_ ? ui::rgba(pal.accent, 0.16f) : ui::rgba(pal.surfaceRaised, 0.9f);
    if (focused) bg = ui::mix(bg, pal.accent, active_ ? 0.18f : 0.10f);
    ui::fillRoundRect(vg, x, y, width, height, r, bg);

    if (focused) {
        ui::strokeRoundRect(vg, x, y, width, height, r, ui::rgba(pal.accent, 0.92f), 1.8f);
    } else if (!active_) {
        ui::strokeRoundRect(vg, x, y, width, height, r, ui::rgba(pal.outline, 0.8f), 1.0f);
    }

    const NVGcolor fg = active_ ? pal.accent : (focused ? pal.text : pal.textDim);
    const float cy = y + height * 0.5f;
    float cx = x + padXOf(compact_);

    if (icon_ != ui::Icon::None) {
        const float is = iconSizeOf(compact_);
        ui::drawIcon(vg, icon_, cx + is * 0.5f, cy, is, fg, 2.0f);
        cx += is + 7.0f;
    }

    // 右侧留给箭头，中间文字按可用宽度截断 —— 摘要文字（按文件夹 · 最近添加）
    // 会随选项变化，不截断就会顶到箭头外面
    const float tail = chevron_ ? (6.0f + 11.0f + padXOf(compact_)) : padXOf(compact_);
    const float textMax = width - (cx - x) - tail;
    ui::textClipped(vg, cx, cy, font_size_, fg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                    text_, textMax);

    if (chevron_) {
        ui::drawIcon(vg, ui::Icon::ChevronRight, x + width - padXOf(compact_) - 5.5f, cy,
                     11.0f, ui::rgba(fg, 0.85f), 2.2f);
    }
}

} // namespace deck_player
