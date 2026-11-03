// DeckPlayer 自有主题。
//
// 为什么需要这个文件：
//   borealis 的默认深色主题是「任天堂 Switch 系统界面」的视觉语言——
//   青绿色强调色 (0,255,204)、Switch 风格的侧边栏与高亮块。
//   本项目是播放器，不是游戏机系统菜单，需要自己的观感。
//
// 做法：不改 borealis 内部实现，只在启动时覆盖主题里的颜色项。
//   Theme::operator[] 返回的是可写引用，直接赋值即可生效，
//   这样既能定制外观，又不会与上游代码产生难以同步的分歧。
//
// 设计取向：
//   - 深灰蓝底（#14161A），比纯灰更接近影音类软件，暗环境下不刺眼
//   - 强调色用暖橙（#FF9F45），与播放/媒体的语义契合，也区别于 Switch 的青绿
//   - 高亮块用低饱和的深色，避免「游戏机菜单」那种整块高亮
//   - 文字分级更明确：主文字亮、次文字灰，便于长时间阅读片名

#include "theme/deck_theme.hpp"

#include <borealis/core/theme.hpp>

namespace deck_player {

namespace {

// 主色板
constexpr unsigned char BG_R = 0x14, BG_G = 0x16, BG_B = 0x1A;  // #14161A 背景
constexpr unsigned char FG_R = 0xF2, FG_G = 0xF2, FG_B = 0xF5;  // #F2F2F5 主文字
constexpr unsigned char MU_R = 0x8A, MU_G = 0x8F, MU_B = 0x98;  // #8A8F98 次文字
constexpr unsigned char AC_R = 0xFF, AC_G = 0x9F, AC_B = 0x45;  // #FF9F45 强调
constexpr unsigned char HL_R = 0x22, HL_G = 0x26, HL_B = 0x2D;  // #22262D 高亮块

bool applied = false;

// 注意：brls::Theme::operator[] 返回的是 NVGcolor **值拷贝**，
// 所以 `t["key"] = color` 只会改到临时对象，不生效（实测踩过）。
// 必须用 addColor() 写入内部 map。
void set(brls::Theme& t, const char* key, NVGcolor c) {
    t.addColor(key, c);
}

} // namespace

void applyDeckTheme() {
    if (applied) return;
    applied = true;

    brls::Theme& t = brls::Theme::getDarkTheme();

    // ── 基础色 ──
    set(t, "brls/background", nvgRGB(BG_R, BG_G, BG_B));
    set(t, "brls/clear",      nvgRGB(BG_R, BG_G, BG_B));
    set(t, "brls/text",       nvgRGB(FG_R, FG_G, FG_B));
    set(t, "brls/text_disabled", nvgRGB(0x50, 0x53, 0x58));
    set(t, "brls/backdrop",   nvgRGBA(0, 0, 0, 190));
    set(t, "brls/accent",     nvgRGB(AC_R, AC_G, AC_B));

    // 点击脉冲用强调色的低透明度版本
    set(t, "brls/click_pulse", nvgRGBA(AC_R, AC_G, AC_B, 38));

    // ── 高亮 ──
    // 用同一色系的不同明度，而不是 Switch 的青绿双色
    set(t, "brls/highlight/background", nvgRGB(HL_R, HL_G, HL_B));
    set(t, "brls/highlight/color1", nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/highlight/color2", nvgRGB(0xFF, 0xC0, 0x7A));

    // ── 侧边栏 ──
    // 与背景同色系但略亮，让它自然「浮」出来而不是分栏切开
    set(t, "brls/sidebar/background",  nvgRGB(0x1A, 0x1C, 0x21));
    set(t, "brls/sidebar/active_item", nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/sidebar/separator",   nvgRGB(0x2A, 0x2D, 0x33));

    // ── 页眉 ──
    set(t, "brls/header/border",     nvgRGB(0x2E, 0x31, 0x38));
    set(t, "brls/header/rectangle",  nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/header/subtitle",   nvgRGB(MU_R, MU_G, MU_B));

    // ── 按钮 ──
    set(t, "brls/button/primary_enabled_background", nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/button/primary_disabled_background", nvgRGB(0x3A, 0x3D, 0x43));
    set(t, "brls/button/primary_enabled_text",  nvgRGB(0x1A, 0x12, 0x08));
    set(t, "brls/button/primary_disabled_text", nvgRGB(0x70, 0x73, 0x78));
    set(t, "brls/button/default_enabled_background",  nvgRGB(0x2A, 0x2D, 0x33));
    set(t, "brls/button/default_disabled_background", nvgRGB(0x24, 0x26, 0x2B));
    set(t, "brls/button/default_enabled_text",  nvgRGB(FG_R, FG_G, FG_B));
    set(t, "brls/button/default_disabled_text", nvgRGB(0x60, 0x63, 0x68));
    set(t, "brls/button/highlight_enabled_text",  nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/button/highlight_disabled_text", nvgRGB(0x80, 0x83, 0x88));
    set(t, "brls/button/enabled_border_color",  nvgRGB(AC_R, AC_G, AC_B));
    set(t, "brls/button/disabled_border_color", nvgRGB(0x40, 0x43, 0x48));

    // ── 列表条目 ──
    set(t, "brls/list/listItem_value_color", nvgRGB(MU_R, MU_G, MU_B));

    // ── 载入指示 ──
    set(t, "brls/spinner/bar_color", nvgRGBA(AC_R, AC_G, AC_B, 110));
}

} // namespace deck_player
