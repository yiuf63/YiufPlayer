#include "view/media_cell.hpp"

#include <algorithm>
#include <utility>

#include <borealis.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/rectangle.hpp>

#include "ui/design.hpp"

namespace deck_player {

MediaCell::MediaCell(ui::Icon icon, std::string title, std::string subtitle)
    : icon_(icon), title_(std::move(title)), subtitle_(std::move(subtitle)) {
    this->setAxis(brls::Axis::COLUMN);
    this->setWidthPercentage(100.0f);
    this->setHeight(HEIGHT);
    this->setFocusable(true);
    // 焦点态由我们自己画：borealis 默认的高亮是「整块半透明底 + 边框」，
    // 在这套卡片里会盖住图标和文字，也和强调条打架。
    this->setHideHighlight(true);
    // 触摸点击：TapGestureRecognizer 会先 giveFocus 再派发 A 键动作，
    // 因此触屏点哪一项，焦点就落在哪一项（手柄与触屏状态一致）。
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void MediaCell::setTitle(std::string title) { title_ = std::move(title); }
void MediaCell::setSubtitle(std::string subtitle) { subtitle_ = std::move(subtitle); }
void MediaCell::setBadge(std::string badge) { badge_ = std::move(badge); }
void MediaCell::setProgress(float ratio) { progress_ = ratio; }
void MediaCell::setChevron(bool show) { chevron_ = show; }
void MediaCell::setThumbnail(ui::SceneSpec spec) {
    has_thumb_ = true;
    thumb_ = std::move(spec);
}

void MediaCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    if (width < 60.0f || height < 24.0f) return;

    const auto& pal = ui::palette();
    const bool focused = this->isFocused();

    // 卡片：左右各留 14，上下各留 6 —— 相邻两行之间有 12px 空隙，
    // 卡片才会「浮」起来，而不是像表格一样贴在一起。
    const float card_x = x + 14.0f;
    const float card_y = y + 6.0f;
    const float card_w = width - 28.0f;
    const float card_h = height - 12.0f;

    ui::fillRoundRect(vg, card_x, card_y, card_w, card_h, ui::radius::md,
                      focused ? pal.surfaceRaised : pal.surface);
    ui::strokeRoundRect(vg, card_x, card_y, card_w, card_h, ui::radius::md,
                        focused ? ui::rgba(pal.accent, 0.65f)
                                : ui::rgba(pal.outline, 0.75f),
                        focused ? 1.6f : 1.0f);

    // 选中态左侧短竖条：与描边一起构成「双层强调」，
    // 电视观看距离下只靠描边仍然容易看不清焦点。
    if (focused) {
        ui::fillRoundRect(vg, card_x + 5.0f, y + height * 0.5f - 13.0f, 3.0f, 26.0f,
                          1.5f, pal.accent);
    }

    // 左侧：16:9 缩略图或类型图标块。
    // 缩略图的宽度固定 96（16:9 → 54 高），行高 76 里放得下；
    // 不用百分比 —— 列表在窄窗口下会让缩略图缩到认不出的尺寸。
    float content_x = 0.0f;
    if (has_thumb_) {
        const float tw = 96.0f;
        const float th = tw * 9.0f / 16.0f;
        const float tx = card_x + 18.0f;
        const float ty = y + height * 0.5f - th * 0.5f;
        ui::drawScene(vg, tx, ty, tw, th, 8.0f, 8.0f, 8.0f, 8.0f, thumb_);
        if (focused) {
            ui::strokeRoundRect(vg, tx, ty, tw, th, 8.0f, ui::rgba(pal.accent, 0.9f), 1.6f);
        }
        content_x = tx + tw + 14.0f;
    } else {
        // 类型图标块：随行高自适应（对话框里的行更矮，图标也要跟着收）
        const float tile = std::clamp(height - 24.0f, 32.0f, 46.0f);
        const float tile_x = card_x + 22.0f;
        const float tile_y = y + height * 0.5f - tile * 0.5f;
        ui::fillRoundRect(vg, tile_x, tile_y, tile, tile, 13.0f,
                          focused ? pal.surfaceSunken
                                  : ui::rgba(pal.surfaceSunken, 0.55f));
        // 图标只在选中时用强调色。以前整列都是橙色，一屏十几个橙色图标，
        // 焦点反而看不出来 —— 强调色必须稀缺才有意义。
        ui::drawIcon(vg, icon_, tile_x + tile * 0.5f, tile_y + tile * 0.5f, tile * 0.5f,
                     focused ? pal.accent : ui::rgba(pal.textDim, 0.85f), 1.9f);
        content_x = tile_x + tile + 16.0f;
    }

    // 右侧：元信息 + 指示箭头
    const float right = card_x + card_w - 18.0f;
    float badge_right = right - (chevron_ ? 22.0f : 0.0f);
    const float badge_w = badge_.empty()
                              ? 0.0f
                              : ui::textWidth(vg, ui::font::detail, badge_);
    if (!badge_.empty()) {
        ui::text(vg, badge_right, y + height * 0.5f, ui::font::detail,
                 focused ? ui::rgba(pal.textDim, 1.0f) : pal.textMute,
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, badge_);
    }
    if (chevron_) {
        ui::drawIcon(vg, ui::Icon::ChevronRight, right - 7.0f, y + height * 0.5f, 16.0f,
                     focused ? pal.textDim : ui::rgba(pal.textMute, 0.8f), 1.8f);
    }

    // 文字区
    const float text_x = content_x;
    const float text_right = badge_.empty() ? badge_right - 16.0f
                                            : badge_right - badge_w - 20.0f;
    const float text_max = std::max(40.0f, text_right - text_x);
    const bool has_progress = progress_ >= 0.0f;

    ui::textClipped(vg, text_x, y + height * 0.5f - (has_progress ? 12.0f : 9.0f),
                    ui::font::body, pal.text, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                    title_, text_max);
    if (!subtitle_.empty()) {
        ui::textClipped(vg, text_x, y + height * 0.5f + (has_progress ? 12.0f : 14.0f),
                        ui::font::caption, pal.textDim,
                        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, subtitle_, text_max);
    }

    // 续播进度：贴卡片底部内侧，细到不抢文字，但一眼能看到「看了多少」
    if (has_progress) {
        const float pw = std::max(0.0f, text_right - text_x);
        const float py = card_y + card_h - 7.0f;
        ui::fillRoundRect(vg, text_x, py, pw, 3.0f, 1.5f,
                          ui::rgba(pal.text, 0.12f));
        ui::fillRoundRect(vg, text_x, py, pw * std::clamp(progress_, 0.0f, 1.0f), 3.0f,
                          1.5f, ui::rgba(pal.accent, 0.95f));
    }
}

// ─────────────────────────────────────────────────────────────────────────

void styleScrollingFrame(brls::ScrollingFrame* scroller) {
    if (!scroller) return;
    // 指示器是 ScrollingFrame 在构造时加进去的唯一一个 Rectangle 子视图。
    for (auto* child : scroller->getChildren()) {
        if (auto* rect = dynamic_cast<brls::Rectangle*>(child)) {
            rect->setColor(nvgRGBA(255, 255, 255, 24));
        }
    }
}

SectionHeader::SectionHeader(std::string title) : title_(std::move(title)) {
    this->setAxis(brls::Axis::ROW);
    this->setWidthPercentage(100.0f);
    this->setHeight(HEIGHT);
    this->setFocusable(false);
}

void SectionHeader::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    (void)width;
    const auto& pal = ui::palette();
    // 基线贴近底部：分组标题与它下面的第一张卡片之间留 8px，
    // 与上面的内容之间留 38px，形成「段间距 > 行间距」的层级。
    const float baseline = y + height - 14.0f;
    ui::fillRoundRect(vg, x + 16.0f, baseline - 6.0f, 3.0f, 12.0f, 1.5f,
                      ui::rgba(pal.accent, 0.85f));
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgTextLetterSpacing(vg, 0.6f);
    ui::text(vg, x + 27.0f, baseline, 13.0f, pal.textMute,
             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, title_);
    nvgTextLetterSpacing(vg, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────

EmptyState::EmptyState(ui::Icon icon, std::string title, std::string hint)
    : icon_(icon), title_(std::move(title)), hint_(std::move(hint)) {
    this->setAxis(brls::Axis::COLUMN);
    this->setWidthPercentage(100.0f);
    this->setHeight(260.0f);
    this->setFocusable(false);
}

void EmptyState::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    const auto& pal = ui::palette();
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f - 24.0f;

    ui::fillCircle(vg, cx, cy - 34.0f, 38.0f, ui::rgba(pal.surface, 0.9f));
    ui::strokeCircle(vg, cx, cy - 34.0f, 38.0f, ui::rgba(pal.outline, 0.9f), 1.0f);
    ui::drawIcon(vg, icon_, cx, cy - 34.0f, 32.0f, ui::rgba(pal.accent, 0.9f), 1.8f);

    ui::text(vg, cx, cy + 22.0f, ui::font::body, pal.text,
             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, title_);

    // 说明文字允许用 \n 手动断行：中文没有空格，自动折行容易断在
    // 半个词上；这里由调用方控制断点更可靠。
    float line_y = cy + 48.0f;
    size_t start = 0;
    while (start <= hint_.size()) {
        const size_t nl = hint_.find('\n', start);
        const std::string line = hint_.substr(start, nl == std::string::npos
                                                        ? std::string::npos
                                                        : nl - start);
        if (!line.empty())
            ui::text(vg, cx, line_y, ui::font::caption,
                     ui::rgba(pal.textDim, 0.78f),
                     NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, line);
        line_y += 20.0f;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

} // namespace deck_player
