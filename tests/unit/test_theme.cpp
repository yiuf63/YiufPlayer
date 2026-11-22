// 单元测试：主题扩展系统与创意工坊自定义主题
#include "theme/muvik_theme.hpp"
#include "ui/design.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace muvik;
namespace fs = std::filesystem;

int main() {
    TEST_SUITE();
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

    TEST_BEGIN("禁用态颜色在三套主题下都可辨认（回归）");
    // 背景：applyPaletteTo 里原本有 8 个硬编码深灰 disabled 色，
    // 只适合深色主题。浅色主题下「深灰底 + 深灰字」对比度约 1.9:1，
    // 按钮看起来像一块黑斑。现已改为从 palette 推导。
    //
    // 这里直接复算推导公式的结果，断言对比度达标。
    // 阈值取 2.5：禁用态本来就要「弱化」，不能要求正文级的 4.5，
    // 但低于 2.5 就真的读不出字了（WCAG 对纯装饰性元素的下限参考）。
    auto rel_lum = [](NVGcolor c) {
        auto lin = [](float v) {
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
    };
    auto contrast = [&](NVGcolor a, NVGcolor b) {
        const float la = rel_lum(a), lb = rel_lum(b);
        const float hi = std::max(la, lb), lo = std::min(la, lb);
        return (hi + 0.05f) / (lo + 0.05f);
    };

    for (const auto& item : getAvailableThemes()) {
        if (!item.is_builtin) continue;
        const auto& p = item.palette;

        // 调用**真实实现**，而不是在测试里复制一份公式。
        // 复制公式的话，把实现改回硬编码时测试依旧全绿 —— 那正是
        // 本项目反复踩过的「测试恒真」。
        const DisabledColors dc = deriveDisabledColors(p);
        const float c_text_on_fill = contrast(dc.text, dc.fill);   // 禁用字压在禁用底上
        const float c_fill_on_bg   = contrast(dc.fill, p.bg);      // 禁用底压在页面背景上

        const std::string msg1 = item.id + " 禁用文字/禁用底色 对比度 >= 2.5（实测 " +
                                 std::to_string(c_text_on_fill).substr(0, 4) + "）";
        // 第二项是真正抓 bug 的那条。
        //
        // 旧的实现把禁用底色写死成 0x24262B（深灰）。深色主题下它与背景
        // 很接近（对比度约 3.1），看不出问题；但浅色主题的背景是
        // 0xF2F1ED 米白，"深灰块压在米白上"的对比度高达 13.4 ——
        // 用户看到的是一块突兀的黑斑，而不是"禁用状态"。
        //
        // 所以判据是**上限**：禁用底必须与页面背景足够接近（< 5:1），
        // 太接近了说明是"压下去"的观感，太远说明是一块异物。
        const std::string msg2 = item.id + " 禁用底色/页面背景 对比度 <= 5（实测 " +
                                 std::to_string(c_fill_on_bg).substr(0, 4) + "）";
        CHECK_TRUE(c_text_on_fill >= 2.5f, msg1.c_str());
        CHECK_TRUE(c_fill_on_bg <= 5.0f, msg2.c_str());
    }

    TEST_BEGIN("动效缓动工具 easeOut");
    // 端点：必须精确落在 0 / 1，否则动画会残留半截不透明度
    CHECK_TRUE(std::abs(ui::easeOut(0.0f) - 0.0f) < 1e-6f, "t=0 → 0");
    CHECK_TRUE(std::abs(ui::easeOut(1.0f) - 1.0f) < 1e-6f, "t=1 → 1");
    // 越界输入要被 clamp，不能把 NaN／负值放大到颜色里
    CHECK_TRUE(std::abs(ui::easeOut(-5.0f) - 0.0f) < 1e-6f, "t<0 被钳到 0");
    CHECK_TRUE(std::abs(ui::easeOut(9.0f) - 1.0f) < 1e-6f, "t>1 被钳到 1");
    // 单调递增：中途不能回退，否则焦点动画会「抖」
    {
        float prev = -1.0f;
        bool monotonic = true;
        for (int i = 0; i <= 20; ++i) {
            const float v = ui::easeOut(static_cast<float>(i) / 20.0f);
            if (v < prev - 1e-6f) monotonic = false;
            prev = v;
        }
        CHECK_TRUE(monotonic, "0..1 区间内单调不减");
    }
    // 缓出特征：前半程必须走得比线性多（这是「立刻有响应」的来源）
    CHECK_TRUE(ui::easeOut(0.5f) > 0.5f, "中点已过半（缓出而非线性）");
    CHECK_TRUE(ui::easeOut(0.5f) > 0.70f, "中点至少走了 70% 路程");
    // 起步斜率要明显大于 1：把 t 从 0 挪一点点，返回值应明显大于 t
    {
        const float small = 0.05f;
        const float v = ui::easeOut(small);
        CHECK_TRUE(v > small * 1.5f, "起步斜率足（缓出初见即动）");
    }

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

    return TEST_SUMMARY();
}
