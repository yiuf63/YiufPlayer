#pragma once

namespace deck_player {

// macOS 专属：抑制 App Nap，使应用在窗口失焦或被隐藏时仍能持续进行音频解码，
// 否则系统会降低进程优先级，导致后台音乐播放卡顿或进度停滞。
//
// 在 Linux / SteamOS 上这些函数为空实现，调用方无需条件编译。
class BackgroundPlayback {
public:
    // 开始后台播放活动（应在音频开始时调用）
    static void begin();

    // 结束后台播放活动（音频停止时调用）
    static void end();

    // 是否已处于激活状态
    static bool isActive();
};

} // namespace deck_player
