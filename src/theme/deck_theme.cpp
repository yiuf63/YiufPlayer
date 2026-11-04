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
//
// OLED 真黑变体（A 屏黑）：
//   Steam Deck OLED / TrimUI Smart Pro 等 A（AMOLED）屏设备，
//   像素发光而非背光——显示 #000000 的像素**完全不耗电**，且无光晕。
//   为此提供 ThemeKind::Oled：背景纯黑，各层级色差收敛（A 屏上
//   2-3 个灰阶的差别比 LCD 上显得更明显，不需要大步进），
//   文字与强调色不变。
//
//   切换方式（见 resolveThemeKind）：
//     - 环境变量 DECKPLAYER_THEME=oled（临时 / CI）
//     - <configDir>/theme 文件持久化（设置 UI 写入，格式为一行 "oled"）

#include "theme/deck_theme.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/style.hpp>
#include <borealis/core/theme.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "utils/app_paths.hpp"

namespace deck_player {

namespace {

// ── 色板 ──
// 每个层级一个字段，两套变体共用同一结构，保证改层级不漏改。
struct Palette {
    // 基础
    unsigned char bg[3];      // 页面背景
    unsigned char fg[3];      // 主文字
    unsigned char muted[3];   // 次文字
    unsigned char accent[3];  // 强调
    unsigned char highlight[3]; // 列表/条目选中底
    // 分层
    unsigned char sidebar[3];    // 侧边栏（比背景略亮，自然分层）
    unsigned char separator[3];  // 分隔线 / 边框
    unsigned char btn_default[3]; // 默认按钮底
};

// 标准深色：深灰蓝底，影音软件惯例。
// highlight 对背景约 1.5:1 —— 电视端观看距离下选中态必须一眼可辨，
// 原 #22262D 只有 1.19:1，选中项和普通项几乎无法区分。
constexpr Palette kDark = {
    {0x0F, 0x11, 0x14},   // bg        #0F1114（与 ui::palette 同源）
    {0xF3, 0xF4, 0xF6},   // fg        #F3F4F6
    {0xA8, 0xAF, 0xBA},   // muted     #A8AFBA
    {0xFF, 0x9F, 0x45},   // accent    #FF9F45
    {0x21, 0x25, 0x2C},   // highlight #21252C（选中底 = 卡片悬停色）
    {0x0A, 0x0C, 0x0F},   // sidebar   #0A0C0F（比背景更暗，自然分栏）
    {0x23, 0x27, 0x2E},   // separator #23272E
    {0x21, 0x25, 0x2C},   // btn_default
};

// OLED 真黑（A 屏黑）：纯黑背景。
// 层级保留但步进收小——A 屏对比度高，小步进已足够分层，
// 步进太大会重新产生「灰底」的观感，失去 A 屏黑的意义。
// highlight #1A1C22：A 屏上「发光像素 vs 熄灭像素」的差别
// 远比 LCD 明显，1.24:1 在 A 屏上的观感约等于 LCD 上的 1.5:1。
constexpr Palette kOled = {
    {0x00, 0x00, 0x00},   // bg        #000000 纯黑
    {0xF3, 0xF4, 0xF6},   // fg        不变
    {0xA8, 0xAF, 0xBA},   // muted     不变
    {0xFF, 0x9F, 0x45},   // accent    不变
    {0x15, 0x17, 0x1B},   // highlight #15171B
    {0x00, 0x00, 0x00},   // sidebar   纯黑（A 屏上侧栏不发光）
    {0x1E, 0x21, 0x26},   // separator #1E2126
    {0x15, 0x17, 0x1B},   // btn_default
};

bool applied = false;

// 注意：brls::Theme::operator[] 返回的是 NVGcolor **值拷贝**，
// 所以 `t["key"] = color` 只会改到临时对象，不生效（实测踩过）。
// 必须用 addColor() 写入内部 map。
void set(brls::Theme& t, const char* key, NVGcolor c) {
    t.addColor(key, c);
}

std::string trimCopy(std::string s) {
    // 去掉首尾空白与可能的 \r（跨平台换行）
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

} // namespace

ThemeKind resolveThemeKind() {
    // 1. 环境变量优先（临时覆盖 / CI 用）
    if (const char* env = std::getenv("DECKPLAYER_THEME"); env && *env) {
        std::string v = trimCopy(env);
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "oled" || v == "amoled" || v == "trueblack") return ThemeKind::Oled;
        if (v == "dark" || v == "default") return ThemeKind::Dark;
    }
    // 2. 配置文件持久化：一行 "oled" / "dark"
    std::ifstream f(AppPaths::configFile("theme"));
    if (f) {
        std::string v = trimCopy({std::istreambuf_iterator<char>(f),
                                  std::istreambuf_iterator<char>()});
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "oled") return ThemeKind::Oled;
    }
    // 3. 默认
    return ThemeKind::Dark;
}

void applyDeckTheme() {
    if (applied) return;
    applied = true;

    const Palette& p = resolveThemeKind() == ThemeKind::Oled ? kOled : kDark;

    brls::Theme& t = brls::Theme::getDarkTheme();

    // ── 基础色 ──
    set(t, "brls/background", nvgRGB(p.bg[0], p.bg[1], p.bg[2]));
    set(t, "brls/clear",      nvgRGB(p.bg[0], p.bg[1], p.bg[2]));
    set(t, "brls/text",       nvgRGB(p.fg[0], p.fg[1], p.fg[2]));
    set(t, "brls/text_disabled", nvgRGB(0x50, 0x53, 0x58));
    set(t, "brls/backdrop",   nvgRGBA(0, 0, 0, 190));
    set(t, "brls/accent",     nvgRGB(p.accent[0], p.accent[1], p.accent[2]));

    // 点击脉冲用强调色的低透明度版本
    set(t, "brls/click_pulse", nvgRGBA(p.accent[0], p.accent[1], p.accent[2], 38));

    // ── 高亮 ──
    // 用同一色系的不同明度，而不是 Switch 的青绿双色
    set(t, "brls/highlight/background", nvgRGB(p.highlight[0], p.highlight[1], p.highlight[2]));
    set(t, "brls/highlight/color1", nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/highlight/color2", nvgRGB(0xFF, 0xC0, 0x7A));

    // ── 侧边栏 ──
    // 与背景同色系但略亮，让它自然「浮」出来而不是分栏切开
    set(t, "brls/sidebar/background",  nvgRGB(p.sidebar[0], p.sidebar[1], p.sidebar[2]));
    set(t, "brls/sidebar/active_item", nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/sidebar/separator",   nvgRGB(p.separator[0], p.separator[1], p.separator[2]));

    // ── 页眉 ──
    set(t, "brls/header/border",     nvgRGB(p.separator[0], p.separator[1], p.separator[2]));
    set(t, "brls/header/rectangle",  nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/header/subtitle",   nvgRGB(p.muted[0], p.muted[1], p.muted[2]));

    // ── 按钮 ──
    set(t, "brls/button/primary_enabled_background", nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/button/primary_disabled_background", nvgRGB(0x3A, 0x3D, 0x43));
    set(t, "brls/button/primary_enabled_text",  nvgRGB(0x1A, 0x12, 0x08));
    set(t, "brls/button/primary_disabled_text", nvgRGB(0x70, 0x73, 0x78));
    set(t, "brls/button/default_enabled_background",  nvgRGB(p.btn_default[0], p.btn_default[1], p.btn_default[2]));
    set(t, "brls/button/default_disabled_background", nvgRGB(0x24, 0x26, 0x2B));
    set(t, "brls/button/default_enabled_text",  nvgRGB(p.fg[0], p.fg[1], p.fg[2]));
    set(t, "brls/button/default_disabled_text", nvgRGB(0x60, 0x63, 0x68));
    set(t, "brls/button/highlight_enabled_text",  nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/button/highlight_disabled_text", nvgRGB(0x80, 0x83, 0x88));
    set(t, "brls/button/enabled_border_color",  nvgRGB(p.accent[0], p.accent[1], p.accent[2]));
    set(t, "brls/button/disabled_border_color", nvgRGB(0x40, 0x43, 0x48));

    // ── 列表条目 ──
    set(t, "brls/list/listItem_value_color", nvgRGB(p.muted[0], p.muted[1], p.muted[2]));

    // ── 载入指示 ──
    set(t, "brls/spinner/bar_color", nvgRGBA(p.accent[0], p.accent[1], p.accent[2], 110));

    // ── 尺寸/间距覆盖 ──
    // borealis 的默认值是给「Switch 系统菜单」调的：侧边栏 410px（占
    // 1280 屏宽的 32%）、菜单项 22px 字号。放在播放器上会显得空、字大、
    // 内容区被挤窄。这里统一收到电视端影音软件的密度。
    brls::Style style = brls::Application::getStyle();
    style.addMetric("brls/tab_frame/sidebar_width", 300.0f);
    style.addMetric("brls/sidebar/padding_left", 48.0f);
    style.addMetric("brls/sidebar/padding_top", 40.0f);
    style.addMetric("brls/sidebar/padding_bottom", 36.0f);
    style.addMetric("brls/sidebar/item_height", 62.0f);
    style.addMetric("brls/sidebar/item_font_size", 20.0f);
    style.addMetric("brls/sidebar/item_accent_rect_width", 3.0f);
    style.addMetric("brls/sidebar/item_accent_margin_sides", 0.0f);
    style.addMetric("brls/sidebar/item_accent_margin_top_bottom", 14.0f);

    style.addMetric("brls/applet_frame/header_height", 92.0f);
    style.addMetric("brls/applet_frame/header_padding_sides", 40.0f);
    style.addMetric("brls/applet_frame/header_title_font_size", 29.0f);
    style.addMetric("brls/applet_frame/footer_height", 64.0f);

    // 焦点高亮的圆角与卡片保持同一档（14），否则选中框是方的、卡片是圆的
    style.addMetric("brls/highlight/corner_radius", 14.0f);
    style.addMetric("brls/highlight/stroke_width", 2.0f);
    style.addMetric("brls/highlight/padding", 0.0f);
}

} // namespace deck_player
