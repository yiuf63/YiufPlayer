// Objective-C++：访问 Foundation 以抑制 macOS App Nap。
// 使用 .mm 后缀是必需的——Foundation 头文件无法在纯 C++ 模式下编译。
#include "playback/background_playback.hpp"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#include <borealis/core/logger.hpp>
#endif

namespace deck_player {

#if defined(__APPLE__)

// 保留 activity token，必须成对调用 end。
//
// 注意：本项目未启用 ARC，静态 id 不会自动持有对象。
// beginActivityWithOptions 返回的对象在 @autoreleasepool 结束后就会被释放，
// 之后再用它调用 endActivity 会崩溃（实测 SIGTRAP /
// _NSSwiftProcessInfo.endActivity）。因此必须显式 retain，释放时再 release。
static id g_activity = nil;

void BackgroundPlayback::begin() {
    if (g_activity != nil) return;
    @autoreleasepool {
        NSProcessInfo* info = [NSProcessInfo processInfo];
        // 声明「用户可感知的后台活动」，阻止 App Nap 降低调度优先级
        NSObject* token = [info beginActivityWithOptions:(NSActivityUserInitiated |
                                                              NSActivityLatencyCritical)
                                                     reason:@"DeckPlayer background audio playback"];
        // 非 ARC 环境，手动持有，确保生命周期覆盖到 end()
        g_activity = [token retain];
        brls::Logger::info("macOS: 已抑制 App Nap，后台音频可继续播放");
    }
}

void BackgroundPlayback::end() {
    if (g_activity == nil) return;
    @autoreleasepool {
        id token = g_activity;
        g_activity = nil;              // 先清空，避免重入
        [[NSProcessInfo processInfo] endActivity:token];
        [token release];               // 与 begin() 中的 retain 配对
        brls::Logger::info("macOS: 已恢复 App Nap");
    }
}

bool BackgroundPlayback::isActive() {
    return g_activity != nil;
}

#else

void BackgroundPlayback::begin() {}
void BackgroundPlayback::end() {}
bool BackgroundPlayback::isActive() { return false; }

#endif

} // namespace deck_player
