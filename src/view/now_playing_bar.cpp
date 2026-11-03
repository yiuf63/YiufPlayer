#include <algorithm>
#include <string>
#include "view/now_playing_bar.hpp"
#include "utils/scanner.hpp"
#include <borealis/views/label.hpp>
#include <borealis/views/rectangle.hpp>

namespace deck_player {

// 音乐条高度（逻辑像素）。列表底部需要预留同样高度，避免最后一项被遮住。
static constexpr float BAR_HEIGHT = 58.0f;

NowPlayingBar* NowPlayingBar::install(brls::AppletFrame* frame) {
    if (instance_) return instance_;
    if (!frame) return nullptr;
    instance_ = new NowPlayingBar(frame);
    return instance_;
}

NowPlayingBar::NowPlayingBar(brls::AppletFrame* frame) {
    this->setAxis(brls::Axis::COLUMN);
    this->setVisibility(brls::Visibility::GONE);
    this->setHeight(BAR_HEIGHT);
    this->setWidthPercentage(100.0f);
    this->setBackgroundColor(nvgRGBA(16, 16, 16, 240));
    this->setPadding(8.0f, 16.0f, 8.0f, 16.0f);
    buildUi();

    // 必须用**绝对定位**贴在底部。
    //
    // 不能只是 frame->addView(this)：
    // AppletFrame 的唯一内容是 contentView，且它 setGrow(1.0f) 占满剩余空间，
    // 同级的音乐条会被挤出可视区 —— 表现为「音乐在播但界面没有任何控件」。
    //
    // ABSOLUTE 让它脱离 Yoga 的常规流，固定在父容器底部，
    // 且不占用 contentView 的空间（列表可以滚到它下面被它遮住）。
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionBottom(0.0f);
    this->setPositionLeft(0.0f);
    this->setWidthPercentage(100.0f);

    if (frame) {
        frame->addView(this);
    }
}

void NowPlayingBar::buildUi() {
    brls::Box* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setWidthPercentage(100.0f);

    title_label_ = new brls::Label();
    title_label_->setText("");
    title_label_->setFontSize(17.0f);
    title_label_->setGrow(1.0f);
    row->addView(title_label_);

    time_label_ = new brls::Label();
    time_label_->setText("");
    time_label_->setFontSize(14.0f);
    time_label_->setTextColor(nvgRGBA(175, 175, 175, 255));
    row->addView(time_label_);

    this->addView(row);

    brls::Box* bg = new brls::Box();
    bg->setWidthPercentage(100.0f);
    bg->setHeight(3.0f);
    bg->setBackgroundColor(nvgRGBA(55, 55, 55, 255));
    bg->setMarginTop(5.0f);

    progress_fill_ = new brls::Rectangle();
    progress_fill_->setWidthPercentage(0.0f);
    progress_fill_->setHeight(3.0f);
    progress_fill_->setColor(nvgRGBA(0, 168, 255, 255));
    bg->addView(progress_fill_);

    this->addView(bg);
}

void NowPlayingBar::refresh(const AudioPlayer::State& state) {
    bool active = !state.path.empty();
    this->setVisibility(active ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!active) return;

    std::string text = (state.playing ? "[音乐] " : "[暂停] ") + state.title;
    if (text != last_title_) {
        title_label_->setText(text);
        last_title_ = text;
    }

    time_label_->setText(MediaScanner::formatDuration(state.position) + " / " +
                         MediaScanner::formatDuration(state.duration));

    float pct = state.duration > 0
        ? (float)state.position / (float)state.duration * 100.0f : 0.0f;
    pct = std::clamp(pct, 0.0f, 100.0f);
    progress_fill_->setWidthPercentage(pct);
}

} // namespace deck_player
