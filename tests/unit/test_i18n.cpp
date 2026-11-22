#include "test_framework.hpp"
#include "utils/i18n.hpp"
#include "utils/app_paths.hpp"
#include <cstdlib>
#include <fstream>

using namespace muvik;

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_i18n", 1);
    std::system("rm -rf /tmp/muvik_test_i18n");

    TEST_BEGIN("i18n / 初始化与可选语言");
    i18n::init();
    const auto& langs = i18n::getAvailableLanguages();
    CHECK_EQ(langs.size(), 3, "提供3种语言选项");
    CHECK_EQ(langs[0].code, "auto", "第1项是auto");
    CHECK_EQ(langs[1].code, "zh-Hans", "第2项是zh-Hans");
    CHECK_EQ(langs[2].code, "en-US", "第3项是en-US");

    TEST_BEGIN("i18n / 中文翻译与点/斜杠路径");
    i18n::setLanguage("zh-Hans");
    CHECK_EQ(i18n::getLanguage(), "zh-Hans", "当前配置语言为zh-Hans");
    CHECK_EQ(i18n::getResolvedLanguage(), "zh-Hans", "当前生效语言为zh-Hans");
    CHECK_EQ(i18n::tr("tabs.library"), "媒体库", "点号分割 tabs.library");
    CHECK_EQ(i18n::tr("tabs/files"), "文件", "斜杠分割 tabs/files");
    CHECK_EQ(i18n::tr("common.exit"), "退出", "common.exit");
    CHECK_EQ(i18n::tr("dialog.exit_confirm"), "确定要退出 Yiuf Player 吗？", "退出确认提示");
    CHECK_EQ(i18n::tr("library.empty_title"), "还没有添加任何媒体目录", "媒体库空状态标题");
    CHECK_EQ(i18n::tr("settings.appearance"), "外观", "设置-外观");
    CHECK_EQ(i18n::tr("theme.dark"), "深色", "主题-深色");
    CHECK_EQ(i18n::tr("fps.auto"), "自动（跟随屏幕 / 垂直同步）", "帧率-自动");
    CHECK_EQ(i18n::tr("keymap.xbox"), "Xbox / Steam Deck（A/B/X/Y）", "手柄-Xbox");
    CHECK_EQ(i18n::tr("view_sheet.title"), "视图与排序", "视图面板标题");

    TEST_BEGIN("i18n / 英文翻译与切换");
    i18n::setLanguage("en-US");
    CHECK_EQ(i18n::getLanguage(), "en-US", "当前配置语言为en-US");
    CHECK_EQ(i18n::getResolvedLanguage(), "en-US", "当前生效语言为en-US");
    CHECK_EQ(i18n::tr("tabs.library"), "Library", "英文 tabs.library");
    CHECK_EQ(i18n::tr("tabs.files"), "Files", "英文 tabs.files");
    CHECK_EQ(i18n::tr("common.exit"), "Exit", "英文 common.exit");
    CHECK_EQ(i18n::tr("dialog.exit_confirm"), "Are you sure you want to exit Yiuf Player?", "英文退出确认提示");
    CHECK_EQ(i18n::tr("library.empty_title"), "No Media Folders Added", "英文媒体库空状态标题");
    CHECK_EQ(i18n::tr("settings.appearance"), "Appearance", "英文设置-外观");
    CHECK_EQ(i18n::tr("theme.dark"), "Dark", "英文主题-Dark");
    CHECK_EQ(i18n::tr("fps.auto"), "Auto (Follow Display / V-Sync)", "英文帧率-Auto");
    CHECK_EQ(i18n::tr("keymap.xbox"), "Xbox / Steam Deck (A/B/X/Y)", "英文手柄-Xbox");
    CHECK_EQ(i18n::tr("view_sheet.title"), "View & Sort", "英文视图面板标题");

    TEST_BEGIN("i18n / Fallback 容错机制");
    CHECK_EQ(i18n::tr("non.existent.key", "后备文本"), "后备文本", "未找到时回退到 fallback");
    CHECK_EQ(i18n::tr("some.random.key"), "some.random.key", "未提供 fallback 时回退到 key");
    CHECK_EQ(i18n::tr("", "默认"), "默认", "空 key 时返回默认");

    TEST_BEGIN("i18n / 恢复为 auto");
    i18n::setLanguage("auto");
    CHECK_EQ(i18n::getLanguage(), "auto", "恢复为 auto");

    return TEST_SUMMARY();
}
