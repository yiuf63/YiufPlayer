#pragma once

#include <string>

namespace muvik {

/**
 * 手柄按键提示图标风格。
 * 支持 Xbox/Steam Deck、PlayStation 与 Nintendo Switch 三种手柄字形集。
 */
enum class KeymapStyle {
    Auto = 0,        // 自动根据当前连接的手柄类型与系统平台判定
    Xbox = 1,        // Xbox / Steam Deck 风格（绿 A / 红 B / 蓝 X / 黄 Y）
    PlayStation = 2, // PlayStation 风格（× / ○ / □ / △）
    Switch = 3       // Nintendo Switch 风格（红白圆形 Switch 字形）
};

class Keymap {
public:
    // 获取用户配置的偏好（Auto / Xbox / PlayStation / Switch）
    static KeymapStyle getStyle();

    // 设置偏好并持久化至 ~/.config/muvik/keymap
    static void setStyle(KeymapStyle style, bool persist = true);

    // 解析当前实际应该生效的风格（Auto 时探测已连接手柄）
    static KeymapStyle resolveActiveStyle();

    // 获取当前风格对应的字体资源文件路径
    static std::string resolveFontAssetPath(KeymapStyle style = KeymapStyle::Auto);

    // 在窗口创建与字体加载前应用至 borealis
    static void applyKeymapFont();

    // 运行时热切换按键提示字体并触发底栏重绘
    static bool reloadKeymapFont();

    // 风格显示名称（用于设置项与对话框）
    static std::string styleToString(KeymapStyle style);

    // 简短标签（如 "Xbox / Steam Deck"）
    static std::string styleToShortString(KeymapStyle style);
};

} // namespace muvik
