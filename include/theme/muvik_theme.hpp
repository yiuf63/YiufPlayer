#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/design.hpp"

namespace muvik {

// 主题变体（保留向后兼容）。
enum class ThemeKind {
    Dark,   // 标准深色（深灰蓝底 #0F1114）
    Oled,   // OLED 真黑（#000000 背景，A 屏省电 + 无光晕）
    Light,  // 浅色（暖白纸感底）
    Custom, // 自定义或创意工坊主题
};

// 完整的主题项模型（支持内置主题、本地自定义主题与 Steam 创意工坊主题）
struct ThemeItem {
    std::string id;            // 唯一标识："dark", "oled", "light", 或自定义/创意工坊 ID
    std::string name;          // 显示名称
    std::string author;        // 作者
    std::string version;       // 版本
    std::string desc;          // 简介描述
    std::string path;          // theme.json 所在目录（内置主题为空）
    bool is_builtin = false;   // 是否为官方内置主题
    bool is_workshop = false;  // 是否来自 Steam 创意工坊订阅
    std::string workshop_id;   // 创意工坊条目 ID
    ui::Palette palette;       // 对应的 Design Token 色板
};

// 应用 Muvik 自有配色，覆盖 borealis 默认的 Switch 风格主题。
// force_reapply 为 true 时用于在运行时动态切换配色重绘界面。
void applyMuvikTheme(bool force_reapply = false);

// 获取所有可用主题（内置 + 本地自定义 ~/.config/muvik/themes + Steam 创意工坊）
std::vector<ThemeItem> getAvailableThemes();

// 解析当前主题 ID（优先级：MUVIK_THEME 环境变量 -> <configDir>/theme 文件 -> 默认 "dark"）
std::string resolveThemeId();

// 解析主题枚举（保留向后兼容）
ThemeKind resolveThemeKind();

// 获取当前生效的主题项
ThemeItem getCurrentTheme();

// 切换并持久化主题（立即更新 Borealis 和全局 UI Palette）
bool applyThemeById(const std::string& theme_id);

// 主题显示名（设置页与选择对话框用）
std::string themeDisplayName(ThemeKind kind);
std::string themeDisplayName(const std::string& theme_id);
inline std::string themeName(ThemeKind kind) { return themeDisplayName(kind); }

// 解析 theme.json 文件内容
bool parseThemeJson(const std::string& json_str, ThemeItem& out_item, std::string* out_err = nullptr);

// 注册主题切换监听
void registerThemeChangeListener(std::function<void(const ThemeItem&)> listener);

} // namespace muvik
