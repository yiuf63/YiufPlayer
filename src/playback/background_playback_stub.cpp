// 非 macOS 平台的 BackgroundPlayback 空实现。
//
// 为什么需要这个文件：
//   background_playback.mm 里有完整的跨平台实现（含 #else 空实现分支），
//   但 CMake 只在 `if (APPLE)` 时才把 .mm 收进源文件列表。
//   在 Linux / SteamOS 上，该文件完全不参与编译，于是
//   BackgroundPlayback::begin / end / isActive 三个符号无人定义，
//   audio_player.cpp 对它们的调用会导致链接失败。
//
//   macOS 上 App Nap 需要抑制；Linux 没有这个机制，
//   且 SteamOS 的调度策略不依赖此类 API，空实现即可满足需求。

#include "playback/background_playback.hpp"

#if !defined(__APPLE__)

namespace muvik {

void BackgroundPlayback::begin() {}
void BackgroundPlayback::end() {}
bool BackgroundPlayback::isActive() { return false; }

} // namespace muvik

#endif
