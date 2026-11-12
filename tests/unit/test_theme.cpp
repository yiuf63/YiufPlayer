// 单元测试：主题扩展系统与创意工坊自定义主题
#include "theme/muvik_theme.hpp"
#include "ui/design.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace muvik;
namespace fs = std::filesystem;

int main() {
    TEST_BEGIN("内置三大官方主题");
    auto themes = getAvailableThemes();
    CHECK_TRUE(themes.size() >= 3, "至少包含 3 个内置主题");

    bool has_dark = false, has_oled = false, has_light = false;
    for (const auto& t : themes) {
        if (t.id == "dark") has_dark = true;
        if (t.id == "oled") has_oled = true;
        if (t.id == "light") has_light = true;
    }
    CHECK_TRUE(has_dark, "包含 dark 主题");
    CHECK_TRUE(has_oled, "包含 oled 主题");
    CHECK_TRUE(has_light, "包含 light 主题");

    TEST_BEGIN("颜色 Hex 解析工具");
    NVGcolor c1 = ui::hex("#FF0000"); // Red
    CHECK_TRUE(std::abs(c1.r - 1.0f) < 0.02f, "红色 R 分量为 1.0");
    CHECK_TRUE(std::abs(c1.g - 0.0f) < 0.02f, "红色 G 分量为 0.0");
    CHECK_TRUE(std::abs(c1.b - 0.0f) < 0.02f, "红色 B 分量为 0.0");
    CHECK_TRUE(std::abs(c1.a - 1.0f) < 0.02f, "不带 alpha 时透明度为 1.0");

    NVGcolor c2 = ui::hex("#00FF0080"); // Green with 0x80 alpha
    CHECK_TRUE(std::abs(c2.g - 1.0f) < 0.02f, "绿色 G 分量为 1.0");
    CHECK_TRUE(std::abs(c2.a - 128.0f / 255.0f) < 0.02f, "透明度解析正确");

    NVGcolor c3 = ui::hex("#00F"); // Blue 3-digit shorthand
    CHECK_TRUE(std::abs(c3.b - 1.0f) < 0.02f, "三位简写 Hex 解析");

    TEST_BEGIN("theme.json 解析 - 完整配置");
    std::string sample_json = R"JSON({
        "id": "neon_test",
        "name": "测试霓虹主题",
        "author": "Tester",
        "version": "1.2.0",
        "description": "用于单测的主题",
        "palette": {
            "bg": "#010203",
            "surface": "#102030",
            "accent": "#00F0FF",
            "scrimAlpha": 0.75
        }
    })JSON";

    ThemeItem parsed;
    std::string err;
    bool ok = parseThemeJson(sample_json, parsed, &err);
    CHECK_TRUE(ok, "解析完整 theme.json 成功");
    CHECK_EQ(parsed.id, "neon_test", "ID 正确");
    CHECK_EQ(parsed.name, "测试霓虹主题", "Name 正确");
    CHECK_EQ(parsed.author, "Tester", "Author 正确");
    CHECK_TRUE(std::abs(parsed.palette.bg.r - 1.0f / 255.0f) < 0.01f, "自定义 bg.r");
    CHECK_TRUE(std::abs(parsed.palette.scrimAlpha - 0.75f) < 0.01f, "自定义 scrimAlpha");
    // 未指定的字段继承 dark 默认
    CHECK_TRUE(std::abs(parsed.palette.text.r - ui::getBuiltinDarkPalette().text.r) < 0.01f, "未配置的 text 继承自默认");

    TEST_BEGIN("theme.json 解析 - 容错与异常处理");
    ThemeItem invalid_item;
    CHECK_FALSE(parseThemeJson("not a json", invalid_item, &err), "畸形 JSON 报错不崩溃");
    CHECK_FALSE(parseThemeJson("{\"name\":\"no_id\"}", invalid_item, &err), "缺少 id 报错");

    TEST_BEGIN("创意工坊与自定义主题扫描");
    std::string fake_ws = "/tmp/test_muvik_ws_themes";
    fs::create_directories(fake_ws + "/112233");
    std::string ws_theme_file = fake_ws + "/112233/theme.json";
    {
        std::ofstream ofs(ws_theme_file);
        ofs << R"JSON({
            "id": "workshop_mod_theme",
            "name": "创意工坊 Mod 主题",
            "author": "WorkshopAuthor",
            "palette": {
                "accent": "#FF9900"
            }
        })JSON";
    }

    setenv("STEAM_WORKSHOP_PATH", fake_ws.c_str(), 1);
    auto all_scanned = getAvailableThemes();
    bool found_ws = false;
    for (const auto& t : all_scanned) {
        if (t.id == "workshop_mod_theme") {
            found_ws = true;
            CHECK_TRUE(t.is_workshop, "识别为创意工坊主题");
            CHECK_EQ(t.workshop_id, "112233", "创意工坊 ID 为 112233");
            CHECK_EQ(t.author, "WorkshopAuthor", "作者信息保留");
        }
    }
    CHECK_TRUE(found_ws, "成功扫描并载入创意工坊主题");

    unsetenv("STEAM_WORKSHOP_PATH");
    fs::remove_all(fake_ws);

    TEST_BEGIN("主题解析与环境变量切换");
    setenv("MUVIK_THEME", "light", 1);
    CHECK_EQ(resolveThemeId(), "light", "MUVIK_THEME 切换为 light");
    CHECK_EQ((int)resolveThemeKind(), (int)ThemeKind::Light, "ThemeKind 映射正确");

    setenv("MUVIK_THEME", "oled", 1);
    CHECK_EQ(resolveThemeId(), "oled", "MUVIK_THEME 切换为 oled");
    CHECK_EQ((int)resolveThemeKind(), (int)ThemeKind::Oled, "ThemeKind 映射正确");

    unsetenv("MUVIK_THEME");

    TEST_BEGIN("主题动态切换与监听器");
    bool listener_called = false;
    std::string received_theme_id;
    registerThemeChangeListener([&](const ThemeItem& t) {
        listener_called = true;
        received_theme_id = t.id;
    });

    applyThemeById("oled");
    CHECK_TRUE(listener_called, "主题切换监听器被调用");
    CHECK_EQ(received_theme_id, "oled", "切换到目标主题 oled");
    CHECK_EQ(resolveThemeId(), "oled", "当前主题解析为 oled");

    // 切回 dark 还原
    applyThemeById("dark");
    CHECK_EQ(resolveThemeId(), "dark", "还原为 dark");

    TEST_SUMMARY();
}
