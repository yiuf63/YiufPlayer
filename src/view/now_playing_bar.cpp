#include <algorithm>
#include <string>
#include "view/now_playing_bar.hpp"
#include "activity/now_playing_activity.hpp"
#include "utils/scanner.hpp"
#include "utils/i18n.hpp"
#include "ui/design.hpp"

namespace muvik {

// 音乐条高度（逻辑像素）。列表底部需要预留同样高度，避免最后一项被遮住。
static constexpr float BAR_HEIGHT = 64.0f;

NowPlayingBar* NowPlayingBar::install(brls::AppletFrame* frame) {
    if (instance_) return instance_;
    if (!frame) return nullptr;
    instance_ = new NowPlayingBar(frame);
    return instance_;
}

NowPlayingBar::NowPlayingBar(brls::AppletFrame* frame) {
    this->setAxis(brls::Axis::ROW);
    this->setVisibility(brls::Visibility::GONE);
    this->setHeight(BAR_HEIGHT);
    this->setWidthPercentage(100.0f);
    this->setPadding(0, 0, 0, 0);
    this->setFocusable(true);

    this->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new NowPlayingActivity());
        return true;
    });

    // 必须用**绝对定位**贴在底部。
    //
    // 不能只是 frame->addView(this)：
    // AppletFrame 的唯一内容是 contentView，且它 setGrow(1.0f) 占满剩余空间，
    // 同级的音乐条会被挤出可视区 —— 表现为「音乐在播但界面没有任何控件」。
    //
    // ABSOLUTE 让它脱离 Yoga 的常规流，固定在父容器底部，
    // 且不占用 contentView 的空间（列表可以滚到它下面被它遮住）。
    //
    // 底边偏移 = 底部提示栏高度：直接贴 0 会把「A/B/X/Y 提示」整条盖住，
    // 用户就看不到按键说明了。
    const float footer =
        brls::Application::getStyle()["brls/applet_frame/footer_height"];
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionBottom(footer > 0.0f ? footer : 0.0f);
    this->setPositionLeft(0.0f);
    this->setWidthPercentage(100.0f);

    if (frame) {
        frame->addView(this);
    }
}

void NowPlayingBar::refresh(const AudioPlayer::State& state) {
    const bool active = !state.path.empty();
    this->setVisibility(active ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!active) return;

    title_ = state.title;
    playing_ = state.playing;
    status_ = state.playing ? i18n::tr("player.playing", "正在播放") : i18n::tr("player.pause", "已暂停");
    icon_ = state.playing ? ui::Icon::Play : ui::Icon::Pause;
    time_text_ = MediaScanner::formatDuration(state.position) + " / " +
                 MediaScanner::formatDuration(state.duration);

    progress_ = state.duration > 0
        ? (float)state.position / (float)state.duration : 0.0f;
    progress_ = std::clamp(progress_, 0.0f, 1.0f);
}

void NowPlayingBar::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    const auto& pal = ui::palette();
    const bool focused = this->isFocused();

    // 不透明底 + 顶部细描边：与列表拉开一层，像「贴在窗口底部的播放器」
    ui::fillRoundRect(vg, x, y, width, height, 0.0f,
                      ui::rgba(pal.surface, 0.98f));
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, 1.0f);
    nvgFillColor(vg, ui::rgba(pal.outline, 0.9f));
    nvgFill(vg);

    if (focused) {
        ui::drawFocusGlow(vg, x, y, width, height, 0.0f, pal.accent);
    }

    const float cy = y + height * 0.5f;
    const float tile = 40.0f;
    const float tile_x = x + 22.0f;

    ui::fillRoundRect(vg, tile_x, cy - tile * 0.5f, tile, tile, 12.0f,
                      ui::rgba(pal.accent, 0.16f));
    ui::drawIcon(vg, icon_, tile_x + tile * 0.5f, cy, 18.0f,
                 ui::rgba(pal.accent, 0.95f), 2.0f);

    // 右侧展开箭头与时间（等宽数字排版，平稳无抖）
    ui::drawIcon(vg, ui::Icon::ChevronRight, x + width - 20.0f, cy, 14.0f, pal.textMute, 1.6f);
    const float time_w = ui::tabularTextWidth(vg, ui::font::detail, time_text_);
    ui::tabularText(vg, x + width - 38.0f, cy, ui::font::detail, pal.textDim,
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, time_text_);

    const float text_x = tile_x + tile + 14.0f;
    const float text_max =
        std::max(60.0f, (x + width - 38.0f - time_w - 20.0f) - text_x);
    ui::textClipped(vg, text_x, cy - 10.0f, ui::font::body, pal.text,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, title_, text_max);
    ui::textClipped(vg, text_x, cy + 11.0f, ui::font::caption, pal.textMute,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, status_, text_max);

    // 进度线贴底：不抢内容，但一眼能看到播到哪
    ui::fillRoundRect(vg, x, y + height - 2.0f, width, 2.0f, 0.0f,
                      ui::rgba(pal.text, 0.10f));
    ui::fillRoundRect(vg, x, y + height - 2.0f, width * progress_, 2.0f, 0.0f,
                      ui::rgba(pal.accent, 0.95f));
}

} // namespace muvik
