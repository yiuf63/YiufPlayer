#pragma once

#include <string>

namespace muvik {

class DisplayMode {
public:
    // 解析启动时是否全屏（优先级：MUVIK_WINDOW > MUVIK_FULLSCREEN > 配置文件 > 默认全屏）
    static bool resolveFullscreen();

    // 检查当前是否全屏
    static bool isFullscreen();

    // 切换全屏状态（可选是否持久化保存）
    static void toggleFullscreen(bool persist = true);

    // 设置全屏状态
    static void setFullscreen(bool fullscreen, bool persist = true);

    // 初始化全局快捷键监听（F11 / Alt+Enter / macOS Cmd+F）
    static void setupGlobalKeyBindings();

    // 目标帧率上限：0 表示不设软上限（跟随屏幕 VSync，释放 OLED 90Hz / 外接 120Hz/144Hz）
    // 其它正整数表示强制帧率上限（如 30, 40, 48, 60, 90, 120, 144）
    static int getTargetFps();

    // 设置目标帧率上限（可选是否持久化到配置文件）
    static void setTargetFps(int fps, bool persist = true);

    // 获取帧率选项的显示标签（如 "48 FPS（24fps 影视匹配）"）
    static std::string getFpsLimitLabel(int fps);
};

} // namespace muvik
