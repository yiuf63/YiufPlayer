#pragma once

namespace deck_player {

// 主题变体。
enum class ThemeKind {
    Dark,  // 标准深色（深灰蓝底 #0F1114）
    Oled,  // OLED 真黑（#000000 背景，A 屏省电 + 无光晕）
    Light, // 浅色（暖白纸感底，色值取自 web/ui-review 原型）
};

// 应用 DeckPlayer 自有配色，覆盖 borealis 默认的 Switch 风格主题。
// 深 / 浅两个 borealis 主题都会被写成同一套 DeckPlayer 色，
// 因此系统「浅色模式」不会把界面染回 Switch 风格。
// 在窗口创建、主题初始化之后调用一次即可（内部有幂等保护）。
void applyDeckTheme();

// 解析主题选择（供测试与设置 UI 复用同一逻辑）：
//   1. DECKPLAYER_THEME 环境变量（oled / dark / light）
//   2. <configDir>/theme 文件内容（一行 "oled" / "dark" / "light"，持久化）
//   3. 默认 Dark
ThemeKind resolveThemeKind();

// 主题的中文显示名（设置页用）："深色" / "OLED 真黑" / "浅色"。
const char* themeDisplayName(ThemeKind kind);

} // namespace deck_player
