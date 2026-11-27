#include <string>
#include "ui/design.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef TESTING_WITHOUT_BOREALIS
#include <borealis/core/application.hpp>
#endif

#include "theme/muvik_theme.hpp"

namespace muvik::ui {

namespace {

// NVGcolor 的四个分量都是 0..1 的 float，不能直接塞 0..255 的整数，
// 否则 0x18 会变成 18.0f（远超 1.0），颜色全部溢出成纯白。
constexpr NVGcolor c8(unsigned r, unsigned g, unsigned b, float a = 1.0f) {
    return NVGcolor{{{r / 255.0f, g / 255.0f, b / 255.0f, a}}};
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
    /* accent        */ c8(0x1A, 0x9F, 0xFF),
    /* accentInk     */ c8(0x0A, 0x16, 0x24),
    /* accentSoft    */ c8(0x1A, 0x9F, 0xFF, 0.16f),
    /* danger        */ c8(0xFF, 0x6B, 0x6B),
    /* scrim         */ c8(0x00, 0x00, 0x00),
    /* scrimAlpha    */ 0.55f,
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
    /* accent        */ c8(0x1A, 0x9F, 0xFF),
    /* accentInk     */ c8(0x0A, 0x16, 0x24),
    /* accentSoft    */ c8(0x1A, 0x9F, 0xFF, 0.16f),
    /* danger        */ c8(0xFF, 0x6B, 0x6B),
    /* scrim         */ c8(0x00, 0x00, 0x00),
    /* scrimAlpha    */ 0.55f,
};

// 浅色：暖白纸感底，色值取自 web/ui-review 原型 [data-theme="light"]。
// 浅色下的「压暗」变成「蒙白」——浮层遮罩用暖白低透明度盖一层，
// 让背后的内容退后，而不是深色主题那种盖黑。
constexpr Palette kLight = {
    /* bg            */ c8(0xF2, 0xF1, 0xED),
    /* surface       */ c8(0xFF, 0xFF, 0xFF),
    /* surfaceRaised */ c8(0xFF, 0xFF, 0xFF),
    /* surfaceSunken */ c8(0xE9, 0xE7, 0xE2),
    /* outline       */ c8(0xDF, 0xDC, 0xD5),
    /* outlineStrong */ c8(0xC6, 0xC2, 0xBA),
    /* text          */ c8(0x1B, 0x1A, 0x18),
    /* textDim       */ c8(0x5F, 0x5C, 0x56),
    /* textMute      */ c8(0x6E, 0x6A, 0x63),
    /* accent        */ c8(0x0E, 0x73, 0xB9),
    /* accentInk     */ c8(0xFF, 0xFF, 0xFF),
    /* accentSoft    */ c8(0x0E, 0x73, 0xB9, 0.14f),
    /* danger        */ c8(0xC0, 0x39, 0x2B),
    /* scrim         */ c8(0xE2, 0xDF, 0xD9),
    /* scrimAlpha    */ 0.68f,
};

int fontId(NVGcontext* vg) {
    (void)vg;
#ifndef TESTING_WITHOUT_BOREALIS
    return brls::Application::getDefaultFont();
#else
    return 0;
#endif
}

} // namespace

static Palette g_currentPalette = kDark;
static bool g_paletteInitialized = false;

// 字号缩放存储。见 design.hpp 的说明。
//
// 上下限收紧到 0.85~1.30：再小则 caption（12.5 × 0.85 ≈ 10.6）在
// 640×360 下糊成一团；再大则一行放不下两个中文词，卡片全部截断。
static float g_fontScale = 1.0f;

const Palette& getBuiltinDarkPalette() { return kDark; }
const Palette& getBuiltinOledPalette() { return kOled; }
const Palette& getBuiltinLightPalette() { return kLight; }

void setPalette(const Palette& p) {
    g_currentPalette = p;
    g_paletteInitialized = true;
}

void resetPalette() {
    g_paletteInitialized = false;
}

const Palette& palette() {
    if (!g_paletteInitialized) {
        switch (resolveThemeKind()) {
            case ThemeKind::Oled:  g_currentPalette = kOled; break;
            case ThemeKind::Light: g_currentPalette = kLight; break;
            default:               g_currentPalette = kDark; break;
        }
        g_paletteInitialized = true;
    }
    return g_currentPalette;
}

inline NVGcolor cfloat(float r, float g, float b, float a = 1.0f) {
    return NVGcolor{{{r, g, b, a}}};
}

NVGcolor rgba(NVGcolor c, float alpha) {
    c.a = std::clamp(alpha, 0.0f, 1.0f);
    return c;
}

NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return cfloat(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                  a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    // 二次缓出：y = 1 - (1-t)^2
    //   t=0.0 → 0.00   起步斜率 2.0（立刻有响应）
    //   t=0.5 → 0.75   中点已走完 3/4 路程
    //   t=1.0 → 1.00   收尾斜率为 0，停得稳
    // 用二次而非三次：120ms 里三次缓出的「快段」太短，肉眼几乎看不出差别，
    // 反而在低帧率设备上更容易显出跳帧。
    return 1.0f - (1.0f - t) * (1.0f - t);
}

NVGcolor hex(unsigned int rgb, float alpha) {
    return c8((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF,
              std::clamp(alpha, 0.0f, 1.0f));
}

NVGcolor hex(const std::string& hexStr, float defaultAlpha) {
    std::string s = hexStr;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() == 3) {
        std::string exp;
        for (char c : s) { exp += c; exp += c; }
        s = exp;
    }
    if (s.size() == 6) {
        try {
            unsigned int v = std::stoul(s, nullptr, 16);
            return hex(v, defaultAlpha);
        } catch (...) {}
    } else if (s.size() == 8) {
        try {
            unsigned long v = std::stoul(s, nullptr, 16);
            unsigned char r = (v >> 24) & 0xFF;
            unsigned char g = (v >> 16) & 0xFF;
            unsigned char b = (v >> 8) & 0xFF;
            unsigned char a = v & 0xFF;
            return c8(r, g, b, a / 255.0f);
        } catch (...) {}
    }
    return hex(0xFFFFFF, defaultAlpha);
}

#ifndef TESTING_WITHOUT_BOREALIS
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
    const float stops[] = {0.26f, 0.60f, 1.0f};
    const float alphas[] = {0.06f, 0.60f, 0.94f};
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

void drawFocusGlow(NVGcontext* vg, float x, float y, float w, float h, float r,
                   NVGcolor accent, float strength) {
    if (w <= 0.0f || h <= 0.0f || strength <= 0.0f) return;
    // 外层环境柔光晕染（Apple TV / OLED 硬件级悬浮质感）
    fillRoundRect(vg, x - 5.0f, y - 5.0f, w + 10.0f, h + 10.0f, r + 5.0f,
                  rgba(accent, 0.08f * strength));
    // 中层柔光光环
    strokeRoundRect(vg, x - 2.5f, y - 2.5f, w + 5.0f, h + 5.0f, r + 2.5f,
                    rgba(accent, 0.28f * strength), 4.0f);
    // 内层高亮定边
    strokeRoundRect(vg, x, y, w, h, r, accent, 2.2f);
}

float tabularTextWidth(NVGcontext* vg, float size, const std::string& s) {
    if (s.empty()) return 0.0f;
    float max_d = 0.0f;
    for (char c = '0'; c <= '9'; ++c) {
        max_d = std::max(max_d, textWidth(vg, size, std::string(1, c)));
    }
    const float colon_w = textWidth(vg, size, ":");
    const float slash_w = textWidth(vg, size, "/");
    const float dot_w   = textWidth(vg, size, ".");
    const float space_w = textWidth(vg, size, " ");

    float total = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') total += max_d;
        else if (c == ':') total += colon_w;
        else if (c == '/') total += slash_w;
        else if (c == '.') total += dot_w;
        else if (c == ' ') total += space_w;
        else total += textWidth(vg, size, std::string(1, c));
    }
    return total;
}

void tabularText(NVGcontext* vg, float x, float y, float size, NVGcolor color,
                 int align, const std::string& s) {
    if (s.empty()) return;
    const float total_w = tabularTextWidth(vg, size, s);
    float cur_x = x;
    if (align & NVG_ALIGN_RIGHT) {
        cur_x -= total_w;
    } else if (align & NVG_ALIGN_CENTER) {
        cur_x -= total_w * 0.5f;
    }

    const int vert_align = align & (NVG_ALIGN_TOP | NVG_ALIGN_MIDDLE | NVG_ALIGN_BOTTOM | NVG_ALIGN_BASELINE);

    float max_d = 0.0f;
    for (char c = '0'; c <= '9'; ++c) {
        max_d = std::max(max_d, textWidth(vg, size, std::string(1, c)));
    }
    const float colon_w = textWidth(vg, size, ":");
    const float slash_w = textWidth(vg, size, "/");
    const float dot_w   = textWidth(vg, size, ".");
    const float space_w = textWidth(vg, size, " ");

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        float slot_w = 0.0f;
        if (c >= '0' && c <= '9') slot_w = max_d;
        else if (c == ':') slot_w = colon_w;
        else if (c == '/') slot_w = slash_w;
        else if (c == '.') slot_w = dot_w;
        else if (c == ' ') slot_w = space_w;
        else slot_w = textWidth(vg, size, std::string(1, c));

        text(vg, cur_x + slot_w * 0.5f, y, size, color,
             NVG_ALIGN_CENTER | vert_align, std::string(1, c));
        cur_x += slot_w;
    }
}
#endif

namespace font {

float& scaleRef() { return g_fontScale; }

void setScale(float s) {
    if (s < 0.85f) s = 0.85f;
    if (s > 1.30f) s = 1.30f;
    g_fontScale = s;
}

} // namespace font

} // namespace muvik::ui
