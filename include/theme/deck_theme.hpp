#pragma once

namespace deck_player {

// 应用 DeckPlayer 自有配色，覆盖 borealis 默认的 Switch 风格深色主题。
// 在窗口创建、主题初始化之后调用一次即可（内部有幂等保护）。
void applyDeckTheme();

} // namespace deck_player
