#include <string>
#include <vector>
#include "view/media_folder_card.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <borealis.hpp>
#include <borealis/core/touch/tap_gesture.hpp>

#include "ui/design.hpp"
#include "ui/scene_tile.hpp"
#include "view/media_thumb.hpp"
#include "ui/icons.hpp"
#include "utils/i18n.hpp"

namespace muvik {

namespace {

// 卡片内缩：左右 8、上 2、下 8。相邻两张卡各让 8，缝就是 16 ——
// 与列表卡片（MediaCell 左右各 14）同一个思路：卡片自己让边，
// 排布容器不需要知道间距。
constexpr float INSET_X = 8.0f;
constexpr float INSET_TOP = 2.0f;
constexpr float INSET_BOTTOM = 8.0f;
constexpr float CAPTION_H = 58.0f;   // 两行文字 + 呼吸
constexpr float GAP = 2.0f;          // 四宫格的缝

void fillRoundedVarying(NVGcontext* vg, float x, float y, float w, float h,
                        float tl, float tr, float br, float bl, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, h, tl, tr, br, bl);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

} // namespace

MediaFolderCard::MediaFolderCard(std::string name, std::string path,
                                 std::vector<MediaItem> covers,
                                 int video_count, int audio_count)
    : name_(std::move(name)), path_(std::move(path)),
      covers_(std::move(covers)), videos_(video_count), audios_(audio_count) {
    this->setFocusable(true);
    this->setHideHighlight(true);   // 焦点由我们自己画
    this->setHeight(HEIGHT);
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void MediaFolderCard::draw(NVGcontext* vg, float x, float y, float width, float height,
                           brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    if (width < 96.0f || height < 96.0f) return;

    const auto& pal = ui::palette();
    const bool focused = this->isFocused();

    const float cx = x + INSET_X;
    const float cy = y + INSET_TOP;
    const float cw = width - INSET_X * 2.0f;
    const float ch = height - INSET_TOP - INSET_BOTTOM;
    const float thumbH = ch - CAPTION_H;
    const float r = ui::radius::md;

    ui::fillRoundRect(vg, cx, cy, cw, ch, r,
                      focused ? ui::mix(pal.surfaceRaised, pal.accent, 0.16f) : pal.surface);

    const size_t n = covers_.size();
    if (n == 0) {
        // 空目录：一块凹陷底 + 一枚文件夹图标。
        // 只铺一块纯色，读起来像「缩略图没加载出来」；放图标才说明
        // 「这里本来就没有东西」。
        fillRoundedVarying(vg, cx, cy, cw, thumbH, r, r, 0.0f, 0.0f, pal.surfaceSunken);
        const float iconSize = std::min(34.0f, thumbH * 0.34f);
        ui::drawIcon(vg, ui::Icon::Folder, cx + cw * 0.5f, cy + thumbH * 0.5f, iconSize,
                     ui::rgba(pal.textMute, 0.55f), 1.7f);
    } else if (n == 1) {
        // 只有一项：整张给它，不切成四格 —— 切了会读成「还有四部」
        ui::drawScene(vg, cx, cy, cw, thumbH, r, r, 0.0f, 0.0f, sceneSpecFor(covers_[0]));
    } else if (n < 4) {
        // 2–3 项：等分竖条。
        // 保留四宫格的空位会被读成「这里有东西没加载出来」—— 空格子比
        // 少一格更像故障。宁可换成另一种排布，也不留洞。
        const float tw = (cw - GAP * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) {
            const float tx = cx + static_cast<float>(i) * (tw + GAP);
            ui::drawScene(vg, tx, cy, tw, thumbH,
                          i == 0 ? r : 0.0f, i + 1 == n ? r : 0.0f, 0.0f, 0.0f,
                          sceneSpecFor(covers_[i]));
        }
    } else {
        const float hw = (cw - GAP) * 0.5f;
        const float hh = (thumbH - GAP) * 0.5f;
        const float xs[2] = {cx, cx + hw + GAP};
        const float ys[2] = {cy, cy + hh + GAP};
        // 只有贴在卡片顶角的格子需要圆角
        const float tls[4] = {r, 0.0f, 0.0f, 0.0f};
        const float trs[4] = {0.0f, r, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            const MediaItem& item = covers_[static_cast<size_t>(i)];
            ui::drawScene(vg, xs[i % 2], ys[i / 2], hw, hh, tls[i], trs[i], 0.0f, 0.0f,
                          sceneSpecFor(item));
        }
    }

    // 计数角标：数字是这一屏最有信息量的东西（「这个桶里有多少」）。
    // 0 不画 —— 空卡片上挂个「0」，像在提醒用户这里出了问题。
    if (videos_ + audios_ > 0) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", videos_ + audios_);
        const std::string count = buf;
        const float bw = std::max(26.0f, ui::textWidth(vg, ui::font::caption(), count) + 14.0f);
        const float bx = cx + cw - 8.0f - bw;
        const float by = cy + thumbH - 8.0f - 22.0f;
        ui::fillRoundRect(vg, bx, by, bw, 22.0f, 6.0f, ui::hex(0x000000, 0.72f));
        ui::text(vg, bx + bw * 0.5f, by + 11.0f, ui::font::caption(), ui::hex(0xF3F4F6),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, count);
    }

    // 文字区
    const float tx = cx + 12.0f;
    const float maxW = cw - 24.0f;
    ui::textClipped(vg, tx, cy + thumbH + 22.0f, ui::font::body(), focused ? ui::hex(0xFFFFFF) : pal.text,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, name_, maxW);

    std::string sub;
    if (videos_ > 0) sub += std::to_string(videos_) + " " + i18n::tr("common.videos", "视频");
    if (audios_ > 0) {
        if (!sub.empty()) sub += " · ";
        sub += std::to_string(audios_) + " " + i18n::tr("common.audios", "音乐");
    }
    if (sub.empty()) sub = i18n::tr("common.empty", "空文件夹");
    ui::textClipped(vg, tx, cy + thumbH + 44.0f, ui::font::caption(), focused ? pal.text : pal.textMute,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, sub, maxW);

    if (focused) {
        // 多层氛围光晕：细描边定边界，外侧柔光漫射赋予硬件级悬浮发光质感
        ui::drawFocusGlow(vg, cx, cy, cw, ch, r, pal.accent);
    } else {
        ui::strokeRoundRect(vg, cx, cy, cw, ch, r, ui::rgba(pal.outline, 0.75f), 1.0f);
    }
}

} // namespace muvik
