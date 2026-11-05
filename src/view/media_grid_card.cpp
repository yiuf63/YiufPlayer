#include "view/media_grid_card.hpp"

#include <algorithm>

#include <borealis.hpp>
#include <borealis/core/touch/tap_gesture.hpp>

#include "ui/design.hpp"
#include "ui/scene_tile.hpp"
#include "view/media_thumb.hpp"

namespace deck_player {

namespace {

constexpr float INSET_X = 8.0f;
constexpr float INSET_TOP = 2.0f;
constexpr float INSET_BOTTOM = 8.0f;
constexpr float CAPTION_H = 52.0f;

} // namespace

MediaGridCard::MediaGridCard(MediaItem item, std::string title, std::string subtitle,
                             float progress)
    : item_(std::move(item)), title_(std::move(title)), subtitle_(std::move(subtitle)),
      progress_(progress) {
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setHeight(HEIGHT);
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void MediaGridCard::setTitle(std::string title) { title_ = std::move(title); }
void MediaGridCard::setSubtitle(std::string subtitle) { subtitle_ = std::move(subtitle); }

void MediaGridCard::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    if (width < 80.0f || height < 80.0f) return;

    const auto& pal = ui::palette();
    const bool focused = this->isFocused();
    const float r = ui::radius::md;

    const float cx = x + INSET_X;
    const float cy = y + INSET_TOP;
    const float cw = width - INSET_X * 2.0f;
    const float ch = height - INSET_TOP - INSET_BOTTOM;
    const float thumbH = ch - CAPTION_H;

    ui::fillRoundRect(vg, cx, cy, cw, ch, r,
                      focused ? pal.surfaceRaised : pal.surface);

    ui::drawScene(vg, cx, cy, cw, thumbH, r, r, 0.0f, 0.0f, sceneSpecFor(item_));

    // 缩略图下缘压一条暗渐变，避免浅色画面把标题区「顶」上去
    NVGpaint fade = nvgLinearGradient(vg, cx, cy + thumbH - 44.0f, cx, cy + thumbH,
                                      ui::rgba(ui::hex(0x000000), 0.0f),
                                      ui::rgba(ui::hex(0x000000), 0.45f));
    nvgBeginPath(vg);
    nvgRect(vg, cx, cy + thumbH - 44.0f, cw, 44.0f);
    nvgFillPaint(vg, fade);
    nvgFill(vg);

    // 续播进度：贴在缩略图底边，比列表里的进度条更短更细
    if (progress_ >= 0.0f) {
        const float ph = 3.0f;
        ui::fillRoundRect(vg, cx, cy + thumbH - ph, cw, ph, 1.5f,
                          ui::rgba(ui::hex(0xFFFFFF), 0.22f));
        const float w = std::max(2.0f, cw * std::min(1.0f, progress_));
        ui::fillRoundRect(vg, cx, cy + thumbH - ph, w, ph, 1.5f, pal.accent);
    }

    // 右上角短标签
    if (!badge_.empty() && cw > 90.0f) {
        const float bw = ui::textWidth(vg, ui::font::caption, badge_) + 14.0f;
        const float bx = cx + cw - 7.0f - bw;
        const float by = cy + 7.0f;
        ui::fillRoundRect(vg, bx, by, bw, 21.0f, 6.0f, ui::hex(0x000000, 0.68f));
        ui::text(vg, bx + bw * 0.5f, by + 10.5f, ui::font::caption, ui::hex(0xF3F4F6),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, badge_);
    }

    const float tx = cx + 12.0f;
    const float maxW = cw - 24.0f;
    ui::textClipped(vg, tx, cy + thumbH + 20.0f, ui::font::body, pal.text,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, title_, maxW);
    ui::textClipped(vg, tx, cy + thumbH + 39.0f, ui::font::caption, pal.textMute,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, subtitle_, maxW);

    if (focused) {
        ui::strokeRoundRect(vg, cx - 2.0f, cy - 2.0f, cw + 4.0f, ch + 4.0f, r + 2.0f,
                            ui::rgba(pal.accent, 0.20f), 3.0f);
        ui::strokeRoundRect(vg, cx, cy, cw, ch, r, ui::rgba(pal.accent, 0.9f), 1.8f);
    } else {
        ui::strokeRoundRect(vg, cx, cy, cw, ch, r, ui::rgba(pal.outline, 0.75f), 1.0f);
    }
}

} // namespace deck_player
