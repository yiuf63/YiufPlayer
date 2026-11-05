#include "view/media_folder_card.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <borealis.hpp>
#include <borealis/core/touch/tap_gesture.hpp>

#include "ui/design.hpp"
#include "ui/icons.hpp"

namespace deck_player {

namespace {

// 卡片内缩：左右 8、上 2、下 8。相邻两张卡各让 8，缝就是 16 ——
// 与列表卡片（MediaCell 左右各 14）同一个思路：卡片自己让边，
// 排布容器不需要知道间距。
constexpr float INSET_X = 8.0f;
constexpr float INSET_TOP = 2.0f;
constexpr float INSET_BOTTOM = 8.0f;
constexpr float CAPTION_H = 58.0f;   // 两行文字 + 呼吸
constexpr float GAP = 2.0f;          // 四宫格的缝

struct TilePalette {
    unsigned int skyTop;
    unsigned int skyBottom;
    unsigned int sun;
};

// 八套「天空 → 地面」配色。同一屏里 8 张卡共 32 格，靠这里的色相
// 和下面按哈希取的太阳位置拉开差异，不会读成一片同色色块。
const TilePalette kPalettes[] = {
    {0x3A5566, 0x16232C, 0xFFEBC6},  // 海港黄昏
    {0x2B3340, 0x11161D, 0xFFCF9B},  // 城市傍晚
    {0x24402F, 0x0E1A14, 0xD7F0B8},  // 林间晨光
    {0x43536B, 0x1A222E, 0xE8F1FF},  // 雪原
    {0x241E33, 0x0D0A14, 0xC9B6FF},  // 夜山
    {0x5A4A34, 0x221A12, 0xFFD9A0},  // 沙滩
    {0x2A2036, 0x100B17, 0xFF9F69},  // 都市霓虹
    {0x1D3A44, 0x08161B, 0x9FE6F0},  // 深海
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));

uint32_t hashPath(const std::string& s) {
    uint32_t h = 2166136261u;   // FNV-1a：短字符串上分布够散，且稳定
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

void fillRoundedVarying(NVGcontext* vg, float x, float y, float w, float h,
                        float tl, float tr, float br, float bl, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, h, tl, tr, br, bl);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

// 一道山脊剪影。
//
// 这是「像画面」的关键：渐变 + 圆点只能读成色斑，人眼认画面靠的是
// **轮廓**。一条不规则的深色山脊压在渐变的天空上，缩到 80×70 也还是
// 「落日下的山」，而同样的尺寸下纯渐变只是一块发光的雾。
void hillShape(NVGcontext* vg, float x, float y, float w, float h,
               float baseY, float amp, float phase01, NVGcolor color) {
    // phase 决定主峰落在横向的哪个位置（0.18..0.52），
    // 相邻格子因此不会长成同一座山
    const float p1 = 0.18f + phase01 * 0.34f;
    const float p2 = p1 + 0.30f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, baseY + amp * 0.20f);
    nvgBezierTo(vg, x + w * p1 * 0.60f, baseY + amp * 0.10f,
                x + w * (p1 - 0.08f), baseY - amp * 0.90f,
                x + w * p1, baseY - amp);
    nvgBezierTo(vg, x + w * (p1 + 0.12f), baseY - amp * 1.05f,
                x + w * (p2 - 0.10f), baseY - amp * 0.25f,
                x + w * p2, baseY - amp * 0.35f);
    nvgBezierTo(vg, x + w * (p2 + 0.12f), baseY - amp * 0.15f,
                x + w * 0.92f, baseY - amp * 0.55f,
                x + w, baseY - amp * 0.30f);
    nvgLineTo(vg, x + w, y + h);
    nvgLineTo(vg, x, y + h);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

// 一格画面。
//
// 三层：天空渐变（有方向）→ 落日（一个明确的亮源）→ 两道山脊（轮廓）。
// 少任何一层都会退化成色块：只有渐变是「雾」，只有渐变加圆点是「发光球」。
// 山脊只压到格子底边，而格子底边本来就不需要圆角（拼贴的缝与文字区都在
// 下面），所以可以直接画多边形，不用裁剪。
void drawTile(NVGcontext* vg, float x, float y, float w, float h,
              float tl, float tr, float br, float bl,
              uint32_t seed, bool audio) {
    if (w < 8.0f || h < 8.0f) return;
    const TilePalette& p = kPalettes[seed % kPaletteCount];
    const float jitter = static_cast<float>((seed >> 9) % 100) / 100.0f;
    const float jitter2 = static_cast<float>((seed >> 17) % 100) / 100.0f;

    NVGpaint sky = nvgLinearGradient(vg, x, y, x, y + h,
                                     ui::hex(p.skyTop), ui::hex(p.skyBottom));
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, h, tl, tr, br, bl);
    nvgFillPaint(vg, sky);
    nvgFill(vg);

    // 落日：压在远山后面，别放在画面正中
    const float sunR = std::max(3.0f, h * 0.13f);
    const float sunX = x + w * (0.30f + jitter * 0.42f);
    const float sunY = y + h * (0.50f + jitter2 * 0.10f);
    NVGpaint glow = nvgRadialGradient(vg, sunX, sunY, sunR * 0.5f, sunR * 3.6f,
                                      ui::rgba(ui::hex(p.sun), 0.75f),
                                      ui::rgba(ui::hex(p.sun), 0.0f));
    nvgBeginPath(vg);
    nvgCircle(vg, sunX, sunY, sunR * 3.6f);
    nvgFillPaint(vg, glow);
    nvgFill(vg);
    ui::fillCircle(vg, sunX, sunY, sunR, ui::hex(p.sun, 0.92f));

    // 远山 / 近山：远山更接近天空色，形成空气透视
    const NVGcolor far = ui::mix(ui::hex(p.skyBottom), ui::hex(0x000000), 0.35f);
    const NVGcolor near = ui::mix(ui::hex(p.skyBottom), ui::hex(0x000000), 0.70f);
    hillShape(vg, x, y, w, h, y + h * 0.68f, h * 0.26f, jitter, far);
    hillShape(vg, x, y, w, h, y + h * 0.88f, h * 0.16f, jitter2, near);

    // 音乐用一枚角标区分：音频没有「画面」，光靠配色分不出来
    if (audio && w >= 44.0f) {
        const float chip = 20.0f;
        const float chipX = x + 6.0f;
        const float chipY = y + h - chip - 6.0f;
        ui::fillRoundRect(vg, chipX, chipY, chip, chip, 6.0f, ui::hex(0x000000, 0.6f));
        ui::drawIcon(vg, ui::Icon::Music, chipX + chip * 0.5f, chipY + chip * 0.5f,
                     chip * 0.62f, ui::hex(0xE7EDF5, 0.92f), 1.8f);
    }
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
                      focused ? pal.surfaceRaised : pal.surface);

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
        drawTile(vg, cx, cy, cw, thumbH, r, r, 0.0f, 0.0f,
                 hashPath(covers_[0].path), covers_[0].is_audio());
    } else if (n < 4) {
        // 2–3 项：等分竖条。
        // 保留四宫格的空位会被读成「这里有东西没加载出来」—— 空格子比
        // 少一格更像故障。宁可换成另一种排布，也不留洞。
        const float tw = (cw - GAP * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) {
            const float tx = cx + static_cast<float>(i) * (tw + GAP);
            drawTile(vg, tx, cy, tw, thumbH,
                     i == 0 ? r : 0.0f, i + 1 == n ? r : 0.0f, 0.0f, 0.0f,
                     hashPath(covers_[i].path), covers_[i].is_audio());
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
            drawTile(vg, xs[i % 2], ys[i / 2], hw, hh, tls[i], trs[i], 0.0f, 0.0f,
                     hashPath(item.path), item.is_audio());
        }
    }

    // 计数角标：数字是这一屏最有信息量的东西（「这个桶里有多少」）。
    // 0 不画 —— 空卡片上挂个「0」，像在提醒用户这里出了问题。
    if (videos_ + audios_ > 0) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", videos_ + audios_);
        const std::string count = buf;
        const float bw = std::max(26.0f, ui::textWidth(vg, ui::font::caption, count) + 14.0f);
        const float bx = cx + cw - 8.0f - bw;
        const float by = cy + thumbH - 8.0f - 22.0f;
        ui::fillRoundRect(vg, bx, by, bw, 22.0f, 6.0f, ui::hex(0x000000, 0.72f));
        ui::text(vg, bx + bw * 0.5f, by + 11.0f, ui::font::caption, ui::hex(0xF3F4F6),
                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, count);
    }

    // 文字区
    const float tx = cx + 12.0f;
    const float maxW = cw - 24.0f;
    ui::textClipped(vg, tx, cy + thumbH + 22.0f, ui::font::body, pal.text,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, name_, maxW);

    std::string sub;
    if (videos_ > 0) sub += std::to_string(videos_) + " 视频";
    if (audios_ > 0) {
        if (!sub.empty()) sub += " · ";
        sub += std::to_string(audios_) + " 首音乐";
    }
    if (sub.empty()) sub = "空文件夹";
    ui::textClipped(vg, tx, cy + thumbH + 44.0f, ui::font::caption, pal.textMute,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, sub, maxW);

    if (focused) {
        // 两级强调：细描边定边界，外侧一圈软光让它在电视距离下也跳出来
        ui::strokeRoundRect(vg, cx - 2.0f, cy - 2.0f, cw + 4.0f, ch + 4.0f, r + 2.0f,
                            ui::rgba(pal.accent, 0.20f), 3.0f);
        ui::strokeRoundRect(vg, cx, cy, cw, ch, r, ui::rgba(pal.accent, 0.9f), 1.8f);
    } else {
        ui::strokeRoundRect(vg, cx, cy, cw, ch, r, ui::rgba(pal.outline, 0.75f), 1.0f);
    }
}

} // namespace deck_player
