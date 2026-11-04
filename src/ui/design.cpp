#include "ui/design.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <borealis/core/application.hpp>

#include "theme/deck_theme.hpp"

namespace deck_player::ui {

namespace {

// NVGcolor 的四个分量都是 0..1 的 float，不能直接塞 0..255 的整数，
// 否则 0x18 会变成 18.0f（远超 1.0），颜色全部溢出成纯白。
constexpr NVGcolor c8(unsigned r, unsigned g, unsigned b, float a = 1.0f) {
    return NVGcolor{r / 255.0f, g / 255.0f, b / 255.0f, a};
}

// 标准深色：中性偏冷的三级表面。
// 之前用的是 #14161A 背景 + #1D1F26 侧栏，两者只差 3 个灰阶，
// 在掌机小屏上几乎看不出分层，于是整页像「一片黑 + 一堆文字」。
constexpr Palette kDark = {
    /* bg            */ c8(0x0F, 0x11, 0x14),
    /* surface       */ c8(0x18, 0x1B, 0x20),
    /* surfaceRaised */ c8(0x21, 0x25, 0x2C),
    /* surfaceSunken */ c8(0x2A, 0x2F, 0x37),
    /* outline       */ c8(0x2B, 0x30, 0x38),
    /* outlineStrong */ c8(0x3C, 0x43, 0x4E),
    /* text          */ c8(0xF3, 0xF4, 0xF6),
    /* textDim       */ c8(0xA8, 0xAF, 0xBA),
    /* textMute      */ c8(0x71, 0x78, 0x84),
    /* accent        */ c8(0xFF, 0x9F, 0x45),
    /* accentInk     */ c8(0x1A, 0x12, 0x08),
    /* accentSoft    */ c8(0xFF, 0x9F, 0x45, 0.16f),
    /* danger        */ c8(0xFF, 0x6B, 0x6B),
    /* scrim         */ c8(0x00, 0x00, 0x00),
};

// OLED 真黑：纯黑背景，表面只提 2-3 个灰阶。
// A 屏上「发光像素 / 熄灭像素」的对比远比 LCD 强烈，
// 步进过大反而会把纯黑变成灰底，失去 A 屏黑的意义。
constexpr Palette kOled = {
    /* bg            */ c8(0x00, 0x00, 0x00),
    /* surface       */ c8(0x0B, 0x0C, 0x0F),
    /* surfaceRaised */ c8(0x15, 0x17, 0x1B),
    /* surfaceSunken */ c8(0x1E, 0x21, 0x26),
    /* outline       */ c8(0x1E, 0x21, 0x26),
    /* outlineStrong */ c8(0x33, 0x38, 0x41),
    /* text          */ c8(0xF3, 0xF4, 0xF6),
    /* textDim       */ c8(0xA8, 0xAF, 0xBA),
    /* textMute      */ c8(0x6C, 0x73, 0x7E),
    /* accent        */ c8(0xFF, 0x9F, 0x45),
    /* accentInk     */ c8(0x1A, 0x12, 0x08),
    /* accentSoft    */ c8(0xFF, 0x9F, 0x45, 0.16f),
    /* danger        */ c8(0xFF, 0x6B, 0x6B),
    /* scrim         */ c8(0x00, 0x00, 0x00),
};

// 主题只在启动时解析一次，之后保持不变。
const Palette& cachedPalette() {
    static const Palette& p =
        resolveThemeKind() == ThemeKind::Oled ? kOled : kDark;
    return p;
}

int fontId(NVGcontext* vg) {
    (void)vg;
    return brls::Application::getDefaultFont();
}

} // namespace

const Palette& palette() { return cachedPalette(); }

NVGcolor rgba(NVGcolor c, float alpha) {
    c.a = std::clamp(alpha, 0.0f, 1.0f);
    return c;
}

NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return nvgRGBAf(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                    a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

NVGcolor hex(unsigned int rgb, float alpha) {
    return nvgRGBA((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF,
                   (unsigned char)std::clamp(alpha * 255.0f, 0.0f, 255.0f));
}

void fillRoundRect(NVGcontext* vg, float x, float y, float w, float h,
                   float r, NVGcolor color) {
    if (w <= 0.0f || h <= 0.0f) return;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, std::min(r, std::min(w, h) * 0.5f));
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void strokeRoundRect(NVGcontext* vg, float x, float y, float w, float h,
                     float r, NVGcolor color, float width) {
    if (w <= 0.0f || h <= 0.0f) return;
    nvgBeginPath(vg);
    // 描边居中，向内收半线宽，避免 1px 描边被父容器裁掉一半。
    const float i = width * 0.5f;
    nvgRoundedRect(vg, x + i, y + i, w - width, h - width,
                   std::max(0.0f, std::min(r, std::min(w, h) * 0.5f) - i));
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

void fillCircle(NVGcontext* vg, float cx, float cy, float r, NVGcolor color) {
    if (r <= 0.0f) return;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void strokeCircle(NVGcontext* vg, float cx, float cy, float r, NVGcolor color,
                  float width) {
    if (r <= 0.0f) return;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

void scrimTop(NVGcontext* vg, float x, float y, float w, float h, float alpha) {
    if (alpha <= 0.0f || h <= 0.0f) return;
    // 分三段：顶部实、中段过渡、下段收干。两段渐变的「折线」在纯黑视频上
    // 会看出一条明显的横向接缝，三段后 CW 值过渡肉眼不可辨。
    const float stops[] = {0.55f, 0.85f, 1.0f};
    const float alphas[] = {0.80f, 0.42f, 0.0f};
    float prev_y = y;
    float prev_a = alpha * alphas[0];
    for (int i = 0; i < 3; ++i) {
        const float seg_y = y + h * stops[i];
        NVGpaint p = nvgLinearGradient(vg, x, prev_y, x, seg_y,
                                       rgba(nvgRGBA(0, 0, 0, 255), prev_a),
                                       rgba(nvgRGBA(0, 0, 0, 255), alpha * alphas[i]));
        nvgBeginPath(vg);
        nvgRect(vg, x, prev_y, w, seg_y - prev_y);
        nvgFillPaint(vg, p);
        nvgFill(vg);
        prev_y = seg_y;
        prev_a = alpha * alphas[i];
    }
    (void)prev_a;
    (void)prev_y;
}

void scrimBottom(NVGcontext* vg, float x, float y, float w, float h, float alpha) {
    if (alpha <= 0.0f || h <= 0.0f) return;
    const float stops[] = {0.34f, 0.66f, 1.0f};
    const float alphas[] = {0.0f, 0.52f, 0.90f};
    float prev_y = y;
    float prev_a = alpha * alphas[0];
    for (int i = 0; i < 3; ++i) {
        const float seg_y = y + h * stops[i];
        NVGpaint p = nvgLinearGradient(vg, x, prev_y, x, seg_y,
                                       rgba(nvgRGBA(0, 0, 0, 255), prev_a),
                                       rgba(nvgRGBA(0, 0, 0, 255), alpha * alphas[i]));
        nvgBeginPath(vg);
        nvgRect(vg, x, prev_y, w, seg_y - prev_y);
        nvgFillPaint(vg, p);
        nvgFill(vg);
        prev_y = seg_y;
        prev_a = alpha * alphas[i];
    }
    (void)prev_a;
    (void)prev_y;
}

float textWidth(NVGcontext* vg, float size, const std::string& s) {
    if (s.empty()) return 0.0f;
    nvgFontFaceId(vg, fontId(vg));
    nvgFontSize(vg, size);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];
}

void text(NVGcontext* vg, float x, float y, float size, NVGcolor color,
          int align, const std::string& s) {
    if (s.empty()) return;
    nvgFontFaceId(vg, fontId(vg));
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, s.c_str(), nullptr);
}

std::string ellipsize(NVGcontext* vg, float size, const std::string& s, float maxW) {
    if (s.empty() || maxW <= 0.0f) return std::string();
    if (textWidth(vg, size, s) <= maxW) return s;

    static const std::string dots = "\xE2\x80\xA6"; // …
    std::string out = s;
    // 按 UTF-8 码点从尾部裁剪：先退到上一个非续字节（0b10xxxxxx）
    while (!out.empty()) {
        size_t cut = out.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.erase(cut);
        // 末尾的空白和标点一并去掉，避免出现「…,」这种组合
        while (!out.empty() &&
               (out.back() == ' ' || out.back() == '-' || out.back() == '_' ||
                out.back() == '.' || out.back() == '|'))
            out.pop_back();
        if (textWidth(vg, size, out + dots) <= maxW) return out + dots;
    }
    return dots;
}

float textClipped(NVGcontext* vg, float x, float y, float size, NVGcolor color,
                  int align, const std::string& s, float maxW) {
    std::string shown = ellipsize(vg, size, s, maxW);
    text(vg, x, y, size, color, align, shown);
    return textWidth(vg, size, shown);
}

float drawPill(NVGcontext* vg, float x, float y, const std::string& label,
               float fontSize, NVGcolor bg, NVGcolor fg, float padX,
               float height) {
    const float w = textWidth(vg, fontSize, label) + padX * 2.0f;
    fillRoundRect(vg, x, y, w, height, height * 0.5f, bg);
    text(vg, x + w * 0.5f, y + height * 0.5f + 0.5f, fontSize, fg,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, label);
    return w;
}

} // namespace deck_player::ui
