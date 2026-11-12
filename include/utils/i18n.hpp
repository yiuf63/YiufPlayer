#pragma once

#include <string>
#include <vector>

namespace muvik::i18n {

struct LanguageOption {
    std::string code;
    std::string name;
};

// 初始化多语言系统（读取偏好设置并加载翻译资源）
void init();

// 获取当前语言偏好（"auto", "zh-Hans", "en-US"）
std::string getLanguage();

// 获取实际解析后的语言（"zh-Hans" 或 "en-US"）
std::string getResolvedLanguage();

// 设置语言并持久化（保存至 ~/.config/muvik/locale）
void setLanguage(const std::string& lang);

// 获取可选语言列表
const std::vector<LanguageOption>& getAvailableLanguages();

// 多语言翻译接口：按 key 获取翻译文本。若找不到对应条目，回退到 fallback（若 fallback 为空则返回 key）
// key 支持点分或斜杠路径，如 "tabs.library" 或 "tabs/library"
std::string tr(const std::string& key, const std::string& fallback = "");

} // namespace muvik::i18n
