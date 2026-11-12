#include "activity/now_playing_activity.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>
#include <borealis/core/touch/tap_gesture.hpp>
#include "ui/design.hpp"
#include "ui/icons.hpp"
#include "utils/scanner.hpp"

namespace muvik {

class NowPlayingView : public brls::Box {
public:
    NowPlayingView() {
        this->setFocusable(true);
        t0_ = std::chrono::steady_clock::now();

        // 手柄键位绑定
        this->registerAction("播放/暂停", brls::ControllerButton::BUTTON_A, [](brls::View*) {
            AudioPlayer::getInstance().togglePlayPause();
            return true;
        }, false);

        this->registerAction("返回", brls::ControllerButton::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false);

        this->registerAction("快退", brls::ControllerButton::BUTTON_LEFT, [](brls::View*) {
            AudioPlayer::getInstance().seek(-10);
            return true;
        }, false, true);

        this->registerAction("快进", brls::ControllerButton::BUTTON_RIGHT, [](brls::View*) {
            AudioPlayer::getInstance().seek(10);
            return true;
        }, false, true);

        this->registerAction("上一首", brls::ControllerButton::BUTTON_LB, [](brls::View*) {
            AudioPlayer::getInstance().previous();
            return true;
        }, false);

        this->registerAction("下一首", brls::ControllerButton::BUTTON_RB, [](brls::View*) {
            AudioPlayer::getInstance().next();
            return true;
        }, false);

        this->registerAction("音量减", brls::ControllerButton::BUTTON_LT, [](brls::View*) {
            auto st = AudioPlayer::getInstance().getState();
            AudioPlayer::getInstance().setVolume(std::max(0, st.volume - 5));
            return true;
        }, false, true);

        this->registerAction("音量加", brls::ControllerButton::BUTTON_RT, [](brls::View*) {
            auto st = AudioPlayer::getInstance().getState();
            AudioPlayer::getInstance().setVolume(std::min(100, st.volume + 5));
            return true;
        }, false, true);

        // 触控支持
        auto* tap = new brls::TapGestureRecognizer(this);
        tap->getTapGestureEvent().subscribe([this](const brls::TapGestureStatus& status, brls::Sound*) {
            if (status.state != brls::GestureState::END) return;
            handleTap(status.position.x, status.position.y);
        });
        this->addGestureRecognizer(tap);
    }

    void handleTap(float tx, float ty) {
        // 返回按钮区域
        if (tx >= back_hit_.x && tx <= back_hit_.x + back_hit_.w &&
            ty >= back_hit_.y && ty <= back_hit_.y + back_hit_.h) {
            brls::Application::popActivity();
            return;
        }

        // 进度条判定
        if (tx >= bar_hit_.x && tx <= bar_hit_.x + bar_hit_.w &&
            ty >= bar_hit_.y && ty <= bar_hit_.y + bar_hit_.h) {
            auto st = AudioPlayer::getInstance().getState();
            if (st.duration > 0 && bar_hit_.w > 0.0f) {
                float ratio = std::clamp((tx - bar_hit_.x) / bar_hit_.w, 0.0f, 1.0f);
                AudioPlayer::getInstance().seekTo((int64_t)(ratio * st.duration));
            }
            return;
        }

        // 播控按钮命中判定
        auto hitCircle = [](float px, float py, float cx, float cy, float r) {
            float dx = px - cx, dy = py - cy;
            return (dx * dx + dy * dy) <= (r * r);
        };

        if (hitCircle(tx, ty, prev_cx_, trans_cy_, 28.0f)) {
            AudioPlayer::getInstance().previous();
        } else if (hitCircle(tx, ty, seek_b_cx_, trans_cy_, 28.0f)) {
            AudioPlayer::getInstance().seek(-15);
        } else if (hitCircle(tx, ty, play_cx_, trans_cy_, 36.0f)) {
            AudioPlayer::getInstance().togglePlayPause();
        } else if (hitCircle(tx, ty, seek_f_cx_, trans_cy_, 28.0f)) {
            AudioPlayer::getInstance().seek(15);
        } else if (hitCircle(tx, ty, next_cx_, trans_cy_, 28.0f)) {
            AudioPlayer::getInstance().next();
        }
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        (void)style;
        (void)ctx;
        if (width < 200.0f || height < 200.0f) return;

        const auto& pal = ui::palette();
        const auto state = AudioPlayer::getInstance().getState();

        // 1. 深度暗调背景
        ui::fillRoundRect(vg, x, y, width, height, 0.0f, ui::hex(0x0C0E14));

        // 2. 漫射呼吸式氛围背光（Apple Music / OLED 悬浮质感）
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - t0_).count();
        float pulse = state.playing ? (0.5f + 0.5f * std::sin(elapsed * 1.8f)) : 0.0f;

        const float glow_cx = x + width * 0.5f;
        const float glow_cy = y + height * 0.38f;
        const float glow_r  = height * 0.44f;

        NVGpaint ambient_glow = nvgRadialGradient(
            vg, glow_cx, glow_cy, 40.0f, glow_r,
            ui::rgba(pal.accent, 0.16f + 0.08f * pulse),
            ui::rgba(pal.accent, 0.0f));
        nvgBeginPath(vg);
        nvgCircle(vg, glow_cx, glow_cy, glow_r);
        nvgFillPaint(vg, ambient_glow);
        nvgFill(vg);

        // 3. 顶部导航行（返回按钮 + 模式文字）
        const float top_y = y + 36.0f;
        back_hit_ = {x + 32.0f, top_y - 18.0f, 96.0f, 36.0f};
        ui::drawIcon(vg, ui::Icon::Back, x + 44.0f, top_y, 20.0f, pal.text);
        ui::text(vg, x + 62.0f, top_y + 0.5f, 15.0f, pal.text,
                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, "返回");

        ui::text(vg, x + width * 0.5f, top_y, 12.0f, pal.textMute,
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, "NOW PLAYING");

        // 4. 居中悬浮黑胶/专辑卡片
        const float cover_size = std::clamp(height * 0.38f, 160.0f, 260.0f);
        const float cover_cx   = glow_cx;
        const float cover_cy   = glow_cy;
        const float cover_r    = 24.0f;
        const float cover_x    = cover_cx - cover_size * 0.5f;
        const float cover_y    = cover_cy - cover_size * 0.5f;

        // 软阴影
        ui::fillRoundRect(vg, cover_x - 4.0f, cover_y + 10.0f, cover_size + 8.0f,
                          cover_size + 8.0f, cover_r + 4.0f, ui::hex(0x000000, 0.45f));

        // 唱片主体
        ui::fillRoundRect(vg, cover_x, cover_y, cover_size, cover_size, cover_r,
                          ui::hex(0x16181F));

        // 唱片内圈同心微凹槽
        ui::strokeCircle(vg, cover_cx, cover_cy, cover_size * 0.36f,
                         ui::hex(0xFFFFFF, 0.06f), 1.2f);
        ui::strokeCircle(vg, cover_cx, cover_cy, cover_size * 0.24f,
                         ui::hex(0xFFFFFF, 0.05f), 1.0f);

        // 中心音符与氛围光环
        if (state.playing) {
            ui::strokeRoundRect(vg, cover_x, cover_y, cover_size, cover_size, cover_r,
                                ui::rgba(pal.accent, 0.40f + 0.20f * pulse), 2.0f);
        } else {
            ui::strokeRoundRect(vg, cover_x, cover_y, cover_size, cover_size, cover_r,
                                ui::rgba(pal.outline, 0.70f), 1.0f);
        }
        ui::fillCircle(vg, cover_cx, cover_cy, 36.0f, ui::rgba(pal.accent, 0.16f));
        ui::drawIcon(vg, ui::Icon::Music, cover_cx, cover_cy, 32.0f, pal.accent, 2.4f);

        // 5. 曲目信息（标题与副标题）
        const float title_y = cover_y + cover_size + 36.0f;
        const std::string title_text = state.title.empty() ? "未知音频曲目" : state.title;
        ui::textClipped(vg, x + width * 0.5f, title_y, 22.0f, ui::hex(0xFFFFFF),
                        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, title_text, width - 160.0f);

        std::string sub_text = state.artist.empty() ? "Muvik Audio Engine" : state.artist;
        if (state.duration > 0) {
            sub_text += std::string(" · ") + (state.playing ? "正在播放" : "已暂停");
        }
        ui::text(vg, x + width * 0.5f, title_y + 26.0f, 14.0f, pal.textMute,
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, sub_text);

        // 6. 等宽数字平滑进度条
        const float bar_w = std::clamp(width * 0.56f, 260.0f, 620.0f);
        const float bar_x = x + (width - bar_w) * 0.5f;
        const float bar_y = title_y + 68.0f;

        bar_hit_ = {bar_x, bar_y - 18.0f, bar_w, 36.0f};

        float prog = (state.duration > 0)
            ? std::clamp((float)state.position / (float)state.duration, 0.0f, 1.0f)
            : 0.0f;

        const std::string pos_str = MediaScanner::formatDuration(state.position);
        const std::string dur_str = MediaScanner::formatDuration(state.duration);

        // 左侧已播时间、右侧总时长：等宽数字排版，平稳无晃动
        ui::tabularText(vg, bar_x - 18.0f, bar_y, 13.0f, pal.text,
                        NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, pos_str);
        ui::tabularText(vg, bar_x + bar_w + 18.0f, bar_y, 13.0f, pal.textDim,
                        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, dur_str);

        // 进度条轨道
        ui::fillRoundRect(vg, bar_x, bar_y - 2.5f, bar_w, 5.0f, 2.5f,
                          ui::rgba(pal.text, 0.12f));
        ui::fillRoundRect(vg, bar_x, bar_y - 2.5f, bar_w * prog, 5.0f, 2.5f,
                          pal.accent);
        ui::fillCircle(vg, bar_x + bar_w * prog, bar_y, 6.0f, ui::hex(0xFFFFFF));

        // 7. 播控按钮组（居中对称排布）
        trans_cy_ = bar_y + 58.0f;
        play_cx_  = x + width * 0.5f;
        seek_b_cx_ = play_cx_ - 72.0f;
        prev_cx_   = play_cx_ - 140.0f;
        seek_f_cx_ = play_cx_ + 72.0f;
        next_cx_   = play_cx_ + 140.0f;

        // 上一首
        ui::drawIconButton(vg, ui::Icon::Prev, prev_cx_, trans_cy_, 44.0f, 18.0f,
                           ui::rgba(pal.surface, 0.90f), pal.text, ui::rgba(pal.outline, 0.7f));
        // 快退 15s
        ui::drawIconButton(vg, ui::Icon::SeekBack, seek_b_cx_, trans_cy_, 44.0f, 18.0f,
                           ui::rgba(pal.surface, 0.90f), pal.text, ui::rgba(pal.outline, 0.7f));

        // 播放/暂停（大尺寸核心胶囊/圆盘）
        ui::fillCircle(vg, play_cx_, trans_cy_, 32.0f, pal.accent);
        ui::strokeCircle(vg, play_cx_, trans_cy_, 32.0f, ui::rgba(pal.accent, 0.5f), 2.0f);
        ui::drawIcon(vg, state.playing ? ui::Icon::Pause : ui::Icon::Play,
                     play_cx_, trans_cy_, 24.0f, ui::hex(0xFFFFFF), 2.6f);

        // 快进 15s
        ui::drawIconButton(vg, ui::Icon::SeekForward, seek_f_cx_, trans_cy_, 44.0f, 18.0f,
                           ui::rgba(pal.surface, 0.90f), pal.text, ui::rgba(pal.outline, 0.7f));
        // 下一首
        ui::drawIconButton(vg, ui::Icon::Next, next_cx_, trans_cy_, 44.0f, 18.0f,
                           ui::rgba(pal.surface, 0.90f), pal.text, ui::rgba(pal.outline, 0.7f));

        Box::draw(vg, x, y, width, height, style, ctx);
    }

private:
    struct Rect { float x = 0, y = 0, w = 0, h = 0; };
    Rect back_hit_;
    Rect bar_hit_;
    float trans_cy_ = 0;
    float play_cx_ = 0;
    float prev_cx_ = 0;
    float next_cx_ = 0;
    float seek_b_cx_ = 0;
    float seek_f_cx_ = 0;
    std::chrono::steady_clock::time_point t0_;
};

NowPlayingActivity::NowPlayingActivity() = default;

NowPlayingActivity::~NowPlayingActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* NowPlayingActivity::createContentView() {
    view_ = new NowPlayingView();
    view_->setGrow(1.0f);
    return view_;
}

void NowPlayingActivity::onContentAvailable() {
    Activity::onContentAvailable();
    if (view_) {
        brls::Application::giveFocus(view_);
    }
}

} // namespace muvik
