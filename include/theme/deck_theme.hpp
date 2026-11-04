#pragma once

namespace deck_player {

// 主题变体。
enum class ThemeKind {
    Dark,  // 标准深色（深灰蓝底 #14161A）
    Oled,  // OLED 真黑（#000000 背景，A 屏省电 + 无光晕）
};

// 应用 DeckPlayer 自有配色，覆盖 borealis 默认的 Switch 风格深色主题。
// 在窗口创建、主题初始化之后调用一次即可（内部有幂等保护）。
void applyDeckTheme();

// 解析主题选择（供测试与设置 UI 复用同一逻辑）：
//   1. DECKPLAYER_THEME 环境变量（oled / dark）
//   2. <configDir>/theme 文件内容（一行 "oled" 或 "dark"，持久化）
//   3. 默认 Dark
ThemeKind resolveThemeKind();

} // namespace deck_player
