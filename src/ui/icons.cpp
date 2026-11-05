#include "ui/icons.hpp"

#include <cmath>

#include "ui/design.hpp"

namespace deck_player::ui {

namespace {

constexpr float kPi = 3.14159265358979f;

void iconPlay(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 8.0f, 5.2f);
    nvgLineTo(vg, 19.2f, 12.0f);
    nvgLineTo(vg, 8.0f, 18.8f);
    nvgClosePath(vg);
    nvgFill(vg);
}

void iconPause(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 6.8f, 5.2f, 4.0f, 13.6f, 1.5f);
    nvgRoundedRect(vg, 13.2f, 5.2f, 4.0f, 13.6f, 1.5f);
    nvgFill(vg);
}

void iconChevrons(NVGcontext* vg, float dir) {
    // 双箭头：比「10s」文字更耐看，尺寸再小也不会糊成一团。
    for (int i = 0; i < 2; ++i) {
        const float apex  = dir > 0.0f ? (16.0f - 6.0f * i) : (8.0f + 6.0f * i);
        const float start = apex - 6.0f * dir;
        nvgBeginPath(vg);
        nvgMoveTo(vg, start, 6.0f);
        nvgLineTo(vg, apex, 12.0f);
        nvgLineTo(vg, start, 18.0f);
        nvgStroke(vg);
    }
}

void iconSkip(NVGcontext* vg, bool next) {
    // 竖条 + 三角，方向由 next 决定
    nvgBeginPath(vg);
    if (next) {
        nvgRect(vg, 16.4f, 6.0f, 2.0f, 12.0f);
    } else {
        nvgRect(vg, 5.6f, 6.0f, 2.0f, 12.0f);
    }
    nvgFill(vg);

    nvgBeginPath(vg);
    if (next) {
        nvgMoveTo(vg, 6.0f, 6.0f);
        nvgLineTo(vg, 14.6f, 12.0f);
        nvgLineTo(vg, 6.0f, 18.0f);
    } else {
        nvgMoveTo(vg, 18.0f, 6.0f);
        nvgLineTo(vg, 9.4f, 12.0f);
        nvgLineTo(vg, 18.0f, 18.0f);
    }
    nvgClosePath(vg);
    nvgFill(vg);
}

void iconSubtitles(NVGcontext* vg, bool off) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 3.0f, 5.5f, 18.0f, 13.0f, 2.8f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 6.6f, 12.9f, 4.6f, 2.2f, 1.0f);
    nvgRoundedRect(vg, 12.6f, 12.9f, 4.8f, 2.2f, 1.0f);
    nvgFill(vg);

    if (off) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, 3.6f, 20.0f);
        nvgLineTo(vg, 20.4f, 4.0f);
        nvgStroke(vg);
    }
}

void iconAudio(NVGcontext* vg) {
    const float xs[4]    = {5.6f, 10.0f, 14.4f, 18.0f};
    const float hs[4]    = {6.0f, 13.0f, 9.0f, 4.4f};
    nvgBeginPath(vg);
    for (int i = 0; i < 4; ++i)
        nvgRoundedRect(vg, xs[i], 12.0f - hs[i] * 0.5f, 2.2f, hs[i], 1.1f);
    nvgFill(vg);
}

void iconVolume(NVGcontext* vg, bool mute) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 3.6f, 9.4f);
    nvgLineTo(vg, 7.2f, 9.4f);
    nvgLineTo(vg, 11.6f, 5.4f);
    nvgLineTo(vg, 11.6f, 18.6f);
    nvgLineTo(vg, 7.2f, 14.6f);
    nvgLineTo(vg, 3.6f, 14.6f);
    nvgClosePath(vg);
    nvgStroke(vg);

    if (mute) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, 15.2f, 9.2f);
        nvgLineTo(vg, 20.4f, 14.8f);
        nvgMoveTo(vg, 20.4f, 9.2f);
        nvgLineTo(vg, 15.2f, 14.8f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgArc(vg, 12.2f, 12.0f, 3.4f, -0.85f, 0.85f, NVG_CW);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgArc(vg, 12.2f, 12.0f, 6.4f, -0.85f, 0.85f, NVG_CW);
        nvgStroke(vg);
    }
}

void iconSpeed(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgArc(vg, 12.0f, 14.2f, 8.2f, kPi * 1.06f, kPi * 1.94f, NVG_CW);
    nvgStroke(vg);

    // 指针指向 2 点方向：不写数字也一眼看出「快」。
    nvgBeginPath(vg);
    nvgMoveTo(vg, 12.0f, 14.2f);
    nvgLineTo(vg, 16.4f, 9.6f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 14.2f, 1.5f);
    nvgFill(vg);
}

void iconAspect(NVGcontext* vg) {
    // 外框 = 屏幕，内框 = 实际画面。两个同心描边比「实心条」更像
    // 「画面在屏幕里的比例」，实心块在 20px 下会被误读成银行卡。
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 2.8f, 5.6f, 18.4f, 12.8f, 2.6f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 6.4f, 8.8f, 11.2f, 6.4f, 1.4f);
    nvgStroke(vg);
}

void iconPlaylist(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 4.6f, 6.2f, 12.0f, 1.9f, 0.95f);
    nvgRoundedRect(vg, 4.6f, 11.0f, 8.6f, 1.9f, 0.95f);
    nvgRoundedRect(vg, 4.6f, 15.8f, 12.0f, 1.9f, 0.95f);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, 15.2f, 9.0f);
    nvgLineTo(vg, 20.2f, 12.0f);
    nvgLineTo(vg, 15.2f, 15.0f);
    nvgClosePath(vg);
    nvgFill(vg);
}

void iconLock(NVGcontext* vg, bool open) {
    if (open) {
        // 开锁：锁梁向右上开口
        nvgBeginPath(vg);
        nvgArc(vg, 12.0f, 9.6f, 3.5f, kPi, kPi * 1.62f, NVG_CW);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 15.5f, 9.6f);
        nvgLineTo(vg, 15.5f, 11.2f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgArc(vg, 12.0f, 9.6f, 3.5f, kPi, kPi * 2.0f, NVG_CW);
        nvgStroke(vg);
    }
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 5.4f, 10.6f, 13.2f, 9.2f, 2.6f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 15.2f, 1.3f);
    nvgFill(vg);
}

void iconBack(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 15.0f, 5.2f);
    nvgLineTo(vg, 7.6f, 12.0f);
    nvgLineTo(vg, 15.0f, 18.8f);
    nvgStroke(vg);
}

void iconFolder(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 3.4f, 8.2f);
    nvgLineTo(vg, 3.4f, 17.6f);
    nvgQuadTo(vg, 3.4f, 19.2f, 5.0f, 19.2f);
    nvgLineTo(vg, 19.0f, 19.2f);
    nvgQuadTo(vg, 20.6f, 19.2f, 20.6f, 17.6f);
    nvgLineTo(vg, 20.6f, 9.8f);
    nvgQuadTo(vg, 20.6f, 8.2f, 19.0f, 8.2f);
    nvgLineTo(vg, 11.6f, 8.2f);
    nvgLineTo(vg, 9.6f, 6.0f);
    nvgLineTo(vg, 5.0f, 6.0f);
    nvgQuadTo(vg, 3.4f, 6.0f, 3.4f, 8.2f);
    nvgClosePath(vg);
    nvgStroke(vg);
}

void iconVideo(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 3.0f, 5.4f, 18.0f, 13.2f, 2.8f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 10.0f, 9.2f);
    nvgLineTo(vg, 15.4f, 12.0f);
    nvgLineTo(vg, 10.0f, 14.8f);
    nvgClosePath(vg);
    nvgFill(vg);
}

void iconMusic(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 10.4f, 17.4f);
    nvgLineTo(vg, 10.4f, 6.6f);
    nvgLineTo(vg, 18.2f, 4.8f);
    nvgLineTo(vg, 18.2f, 15.4f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 7.9f, 17.4f, 2.6f);
    nvgCircle(vg, 15.7f, 15.4f, 2.6f);
    nvgFill(vg);
}

void iconClock(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 12.0f, 8.2f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 12.0f, 7.4f);
    nvgLineTo(vg, 12.0f, 12.2f);
    nvgLineTo(vg, 15.4f, 13.8f);
    nvgStroke(vg);
}

void iconClose(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 6.2f, 6.2f);
    nvgLineTo(vg, 17.8f, 17.8f);
    nvgMoveTo(vg, 17.8f, 6.2f);
    nvgLineTo(vg, 6.2f, 17.8f);
    nvgStroke(vg);
}

void iconCheck(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 4.8f, 12.6f);
    nvgLineTo(vg, 9.8f, 17.6f);
    nvgLineTo(vg, 19.2f, 6.6f);
    nvgStroke(vg);
}

void iconError(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 12.0f, 8.4f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 12.0f, 7.4f);
    nvgLineTo(vg, 12.0f, 13.2f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 16.4f, 1.3f);
    nvgFill(vg);
}

void iconInfo(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 12.0f, 8.4f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 12.0f, 11.0f);
    nvgLineTo(vg, 12.0f, 16.4f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 7.8f, 1.3f);
    nvgFill(vg);
}

void iconBrightness(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgCircle(vg, 12.0f, 12.0f, 4.2f);
    nvgStroke(vg);
    for (int i = 0; i < 8; ++i) {
        const float a = kPi * 2.0f * i / 8.0f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, 12.0f + std::cos(a) * 6.4f, 12.0f + std::sin(a) * 6.4f);
        nvgLineTo(vg, 12.0f + std::cos(a) * 8.6f, 12.0f + std::sin(a) * 8.6f);
        nvgStroke(vg);
    }
}

void iconDrive(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 3.0f, 6.6f, 18.0f, 10.8f, 2.6f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 3.0f, 13.2f);
    nvgLineTo(vg, 21.0f, 13.2f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 17.2f, 10.0f, 1.1f);
    nvgFill(vg);
}

void iconPlugin(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 7.4f, 9.2f, 9.2f, 7.2f, 2.0f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 10.0f, 9.2f);
    nvgLineTo(vg, 10.0f, 5.0f);
    nvgMoveTo(vg, 14.0f, 9.2f);
    nvgLineTo(vg, 14.0f, 5.0f);
    nvgMoveTo(vg, 12.0f, 16.4f);
    nvgLineTo(vg, 12.0f, 19.6f);
    nvgStroke(vg);
}

void iconSearch(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgCircle(vg, 10.6f, 10.6f, 6.0f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 15.2f, 15.2f);
    nvgLineTo(vg, 20.0f, 20.0f);
    nvgStroke(vg);
}

void iconPlus(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 12.0f, 5.0f);
    nvgLineTo(vg, 12.0f, 19.0f);
    nvgMoveTo(vg, 5.0f, 12.0f);
    nvgLineTo(vg, 19.0f, 12.0f);
    nvgStroke(vg);
}

// 三条滑轨，每条一个把手 —— 「可以调的东西」的通用符号。
// 把手位置故意不对齐，否则三条线读成三条横杠（≡）。
void iconSliders(NVGcontext* vg) {
    const float ys[3] = {6.8f, 12.0f, 17.2f};
    const float handles[3] = {9.4f, 15.0f, 7.0f};
    for (int i = 0; i < 3; ++i) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, 3.6f, ys[i]);
        nvgLineTo(vg, 20.4f, ys[i]);
        nvgStroke(vg);
        // 把手画成实心小圆，压在线上（drawIcon 已把填充色与描边色设成同一个）
        nvgBeginPath(vg);
        nvgCircle(vg, handles[i], ys[i], 2.5f);
        nvgFill(vg);
    }
}

void iconGrid(NVGcontext* vg) {
    const float s = 7.2f;
    const float xs[2] = {3.8f, 13.0f};
    const float ys[2] = {3.8f, 13.0f};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, xs[c], ys[r], s, s, 1.8f);
            nvgStroke(vg);
        }
    }
}

void iconList(NVGcontext* vg) {
    const float ys[3] = {7.0f, 12.0f, 17.0f};
    for (int i = 0; i < 3; ++i) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, 3.8f, ys[i]);
        nvgLineTo(vg, 20.2f, ys[i]);
        nvgStroke(vg);
    }
}

void iconFilter(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 4.0f, 5.4f);
    nvgLineTo(vg, 20.0f, 5.4f);
    nvgLineTo(vg, 13.6f, 12.6f);
    nvgLineTo(vg, 13.6f, 18.6f);
    nvgLineTo(vg, 10.4f, 16.6f);
    nvgLineTo(vg, 10.4f, 12.6f);
    nvgClosePath(vg);
    nvgStroke(vg);
}

} // namespace

void drawIcon(NVGcontext* vg, Icon icon, float cx, float cy, float size,
              NVGcolor color, float strokeWidth) {
    if (size <= 0.0f) return;
    // 每个图标都在 6..18 的安全区内，视觉重心已经在 12,12，
    // 这里只按外框尺寸做等比缩放。
    const float k = size / 24.0f;

    nvgSave(vg);
    nvgTranslate(vg, cx - size * 0.5f, cy - size * 0.5f);
    nvgScale(vg, k, k);

    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, strokeWidth / k);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    switch (icon) {
        case Icon::Play:          iconPlay(vg); break;
        case Icon::Pause:         iconPause(vg); break;
        case Icon::SeekBack:      iconChevrons(vg, -1.0f); break;
        case Icon::SeekForward:   iconChevrons(vg, 1.0f); break;
        case Icon::Prev:          iconSkip(vg, false); break;
        case Icon::Next:          iconSkip(vg, true); break;
        case Icon::Subtitles:     iconSubtitles(vg, false); break;
        case Icon::SubtitlesOff:  iconSubtitles(vg, true); break;
        case Icon::Audio:         iconAudio(vg); break;
        case Icon::Volume:        iconVolume(vg, false); break;
        case Icon::VolumeMute:    iconVolume(vg, true); break;
        case Icon::Speed:         iconSpeed(vg); break;
        case Icon::Aspect:        iconAspect(vg); break;
        case Icon::Playlist:      iconPlaylist(vg); break;
        case Icon::Lock:          iconLock(vg, false); break;
        case Icon::Unlock:        iconLock(vg, true); break;
        case Icon::Back:          iconBack(vg); break;
        case Icon::ChevronRight:
            nvgBeginPath(vg);
            nvgMoveTo(vg, 9.0f, 5.6f);
            nvgLineTo(vg, 16.0f, 12.0f);
            nvgLineTo(vg, 9.0f, 18.4f);
            nvgStroke(vg);
            break;
        case Icon::Folder:        iconFolder(vg); break;
        case Icon::Video:         iconVideo(vg); break;
        case Icon::Music:         iconMusic(vg); break;
        case Icon::Clock:         iconClock(vg); break;
        case Icon::Close:         iconClose(vg); break;
        case Icon::Check:         iconCheck(vg); break;
        case Icon::Error:         iconError(vg); break;
        case Icon::Info:          iconInfo(vg); break;
        case Icon::Brightness:    iconBrightness(vg); break;
        case Icon::Drive:         iconDrive(vg); break;
        case Icon::Plugin:        iconPlugin(vg); break;
        case Icon::Search:        iconSearch(vg); break;
        case Icon::Plus:          iconPlus(vg); break;
        case Icon::Sliders:       iconSliders(vg); break;
        case Icon::Grid:          iconGrid(vg); break;
        case Icon::List:          iconList(vg); break;
        case Icon::Filter:        iconFilter(vg); break;
        case Icon::None:          break;
    }

    nvgRestore(vg);
}

void drawIconButton(NVGcontext* vg, Icon icon, float cx, float cy, float diameter,
                    float iconSize, NVGcolor bg, NVGcolor fg, NVGcolor border,
                    float borderWidth) {
    if (bg.a > 0.001f) fillCircle(vg, cx, cy, diameter * 0.5f, bg);
    if (border.a > 0.001f && borderWidth > 0.0f)
        strokeCircle(vg, cx, cy, diameter * 0.5f - borderWidth * 0.5f, border,
                     borderWidth);
    drawIcon(vg, icon, cx, cy, iconSize, fg);
}

} // namespace deck_player::ui
