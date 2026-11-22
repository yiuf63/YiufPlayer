// Muvik 自有主题与主题扩展引擎。
//
// 设计说明：
//   1. 内置提供「深色」「OLED 真黑」「浅色」三种官方基准主题；
//   2. 支持加载本地自定义主题 (~/.config/muvik/themes) 与 Steam 创意工坊已订阅主题 (workshop/content/3290940)；
//   3. 主题包采用声明式的 theme.json 格式定义 Palette 色板，全面赋能创作者社区；
//   4. 切换主题时，同时更新 ui::palette() 与 borealis 内部 Theme，界面即时平滑重绘无需重启。

#include "theme/muvik_theme.hpp"

#ifndef TESTING_WITHOUT_BOREALIS
#include <borealis/core/application.hpp>
#include <borealis/core/style.hpp>
#include <borealis/core/theme.hpp>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "utils/app_paths.hpp"
#include "utils/i18n.hpp"

namespace muvik {

DisabledColors deriveDisabledColors(const ui::Palette& p) {
    const bool is_dark = (p.bg.r < 0.5f);
    DisabledColors out;
    // 禁用文字：从主文字向背景靠 58% —— 明显变暗，但仍可辨认
    out.text   = ui::mix(p.text, p.bg, 0.58f);
    // 禁用底色：从二级表面向背景靠。浅色主题靠得更多（0.55），
    // 否则浅底上的「稍深一点的面」看起来像一块脏斑。
    out.fill   = ui::mix(p.surfaceRaised, p.bg, is_dark ? 0.30f : 0.55f);
    // 禁用描边：介于两者之间，给按钮留一圈轮廓
    out.border = ui::mix(p.outline, p.bg, 0.25f);
    return out;
}

namespace {

bool applied = false;
std::vector<std::function<void(const ThemeItem&)>> g_theme_listeners;

#ifndef TESTING_WITHOUT_BOREALIS
void set(brls::Theme& t, const char* key, NVGcolor c) {
    t.addColor(key, c);
}

void applyPaletteTo(brls::Theme& t, const ui::Palette& p) {
    // ── 禁用态（disabled）必须按主题推导，不能写死深色 ──
    //
    // 这里原先有 8 个硬编码的深灰（0x3A3D43 / 0x24262B / 0x606368 ...）。
    // 它们是照着深色主题选的：深灰字压在深灰底上，对比度刚好够看。
    // 但浅色主题的底是 0xF2F1ED 这种亮米色 —— 深灰**文字**仍然读得清，
    // 而深灰**底色**（0x24262B）在浅色界面上会变成一块突兀的黑斑，
    // 压在上面的深灰字（0x606368）对比度只剩约 1.9:1，基本看不出轮廓。
    //
    // 修法：禁用态一律从调色板推导。
    //   · 深色：在 text 与 bg 之间取偏 bg 的一段 → 灰暗但不消失
    //   · 浅色：同一公式方向相反 → 得到浅灰而不是黑块
    // 三套主题（深 / OLED / 浅）各自得到合理的灰阶，无需分别调参。
    const bool is_dark = (p.bg.r < 0.5f);
    const DisabledColors dc = deriveDisabledColors(p);
    const NVGcolor text_disabled   = dc.text;
    const NVGcolor fill_disabled   = dc.fill;
    const NVGcolor border_disabled = dc.border;

    set(t, "brls/background", p.bg);
    set(t, "brls/clear",      p.bg);
    set(t, "brls/text",       p.text);
    set(t, "brls/text_disabled", text_disabled);
    set(t, "brls/backdrop",   nvgRGBA(0, 0, 0, 190));
    set(t, "brls/accent",     p.accent);

    set(t, "brls/click_pulse", ui::rgba(p.accent, 0.15f));
    set(t, "brls/highlight/background", ui::rgba(p.accent, 0.19f));
    set(t, "brls/highlight/color1", p.accent);
    set(t, "brls/highlight/color2", p.outlineStrong);

    set(t, "brls/sidebar/background",  p.surface);
    set(t, "brls/sidebar/active_item", is_dark ? nvgRGB(255, 255, 255) : p.accent);
    set(t, "brls/sidebar/inactive_item", p.textDim);
    set(t, "brls/sidebar/separator",   p.outline);

    set(t, "brls/header/border",     p.outline);
    set(t, "brls/header/rectangle",  p.accent);
    set(t, "brls/header/subtitle",   p.textDim);

    set(t, "brls/button/primary_enabled_background", p.accent);
    set(t, "brls/button/primary_disabled_background", fill_disabled);
    set(t, "brls/button/primary_enabled_text",  p.accentInk);
    set(t, "brls/button/primary_disabled_text", text_disabled);
    set(t, "brls/button/default_enabled_background",  p.surfaceRaised);
    set(t, "brls/button/default_disabled_background", fill_disabled);
    set(t, "brls/button/default_enabled_text",  p.text);
    set(t, "brls/button/default_disabled_text", text_disabled);
    set(t, "brls/button/highlight_enabled_text",  p.accent);
    set(t, "brls/button/highlight_disabled_text", text_disabled);
    set(t, "brls/button/enabled_border_color",  p.accent);
    set(t, "brls/button/disabled_border_color", border_disabled);

    set(t, "brls/list/listItem_value_color", p.textDim);
    set(t, "brls/spinner/bar_color", ui::rgba(p.accent, 0.43f));
}
#endif

std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

} // namespace

bool parseThemeJson(const std::string& json_str, ThemeItem& out_item, std::string* out_err) {
    try {
        auto root = nlohmann::json::parse(json_str);
        if (!root.is_object()) {
            if (out_err) *out_err = "JSON 根节点必须是对象";
            return false;
        }

        if (root.contains("id") && root["id"].is_string()) {
            out_item.id = root["id"].get<std::string>();
        } else {
            if (out_err) *out_err = "缺少必需字段 'id'";
            return false;
        }

        out_item.name = root.value("name", out_item.id);
        out_item.author = root.value("author", "");
        out_item.version = root.value("version", "1.0.0");
        out_item.desc = root.value("description", root.value("desc", ""));

        // 基础默认基于深色色板继承
        out_item.palette = ui::getBuiltinDarkPalette();

        if (root.contains("palette") && root["palette"].is_object()) {
            const auto& pal = root["palette"];
            if (pal.contains("bg") && pal["bg"].is_string())
                out_item.palette.bg = ui::hex(pal["bg"].get<std::string>());
            if (pal.contains("surface") && pal["surface"].is_string())
                out_item.palette.surface = ui::hex(pal["surface"].get<std::string>());
            if (pal.contains("surfaceRaised") && pal["surfaceRaised"].is_string())
                out_item.palette.surfaceRaised = ui::hex(pal["surfaceRaised"].get<std::string>());
            if (pal.contains("surfaceSunken") && pal["surfaceSunken"].is_string())
                out_item.palette.surfaceSunken = ui::hex(pal["surfaceSunken"].get<std::string>());
            if (pal.contains("outline") && pal["outline"].is_string())
                out_item.palette.outline = ui::hex(pal["outline"].get<std::string>());
            if (pal.contains("outlineStrong") && pal["outlineStrong"].is_string())
                out_item.palette.outlineStrong = ui::hex(pal["outlineStrong"].get<std::string>());
            if (pal.contains("text") && pal["text"].is_string())
                out_item.palette.text = ui::hex(pal["text"].get<std::string>());
            if (pal.contains("textDim") && pal["textDim"].is_string())
                out_item.palette.textDim = ui::hex(pal["textDim"].get<std::string>());
            if (pal.contains("textMute") && pal["textMute"].is_string())
                out_item.palette.textMute = ui::hex(pal["textMute"].get<std::string>());
            if (pal.contains("accent") && pal["accent"].is_string()) {
                out_item.palette.accent = ui::hex(pal["accent"].get<std::string>());
                out_item.palette.accentSoft = ui::rgba(out_item.palette.accent, 0.16f);
            }
            if (pal.contains("accentInk") && pal["accentInk"].is_string())
                out_item.palette.accentInk = ui::hex(pal["accentInk"].get<std::string>());
            if (pal.contains("accentSoft") && pal["accentSoft"].is_string())
                out_item.palette.accentSoft = ui::hex(pal["accentSoft"].get<std::string>());
            if (pal.contains("danger") && pal["danger"].is_string())
                out_item.palette.danger = ui::hex(pal["danger"].get<std::string>());
            if (pal.contains("scrim") && pal["scrim"].is_string())
                out_item.palette.scrim = ui::hex(pal["scrim"].get<std::string>());
            if (pal.contains("scrimAlpha") && pal["scrimAlpha"].is_number())
                out_item.palette.scrimAlpha = pal["scrimAlpha"].get<float>();
        }
        return true;
    } catch (const std::exception& e) {
        if (out_err) *out_err = e.what();
        return false;
    }
}

std::vector<ThemeItem> getAvailableThemes() {
    std::vector<ThemeItem> themes;

    // 1. 三大官方内置主题
    {
        ThemeItem dark;
        dark.id = "dark";
        dark.name = i18n::tr("theme.dark", "深色");
        dark.desc = i18n::tr("theme.dark_desc", "标准深灰蓝影音底色");
        dark.author = "Muvik";
        dark.version = "1.0.0";
        dark.is_builtin = true;
        dark.palette = ui::getBuiltinDarkPalette();
        themes.push_back(dark);
    }
    {
        ThemeItem oled;
        oled.id = "oled";
        oled.name = i18n::tr("theme.oled", "OLED 真黑");
        oled.desc = i18n::tr("theme.oled_desc", "纯黑像素级关屏省电背景");
        oled.author = "Muvik";
        oled.version = "1.0.0";
        oled.is_builtin = true;
        oled.palette = ui::getBuiltinOledPalette();
        themes.push_back(oled);
    }
    {
        ThemeItem light;
        light.id = "light";
        light.name = i18n::tr("theme.light", "浅色");
        light.desc = i18n::tr("theme.light_desc", "暖白纸感明亮主题");
        light.author = "Muvik";
        light.version = "1.0.0";
        light.is_builtin = true;
        light.palette = ui::getBuiltinLightPalette();
        themes.push_back(light);
    }

    auto loadThemeFile = [&](const std::string& filepath, bool is_ws, const std::string& ws_id) {
        std::ifstream ifs(filepath);
        if (!ifs) return;
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ThemeItem item;
        std::string err;
        if (parseThemeJson(content, item, &err)) {
            std::error_code ec;
            item.path = std::filesystem::path(filepath).parent_path().string();
            item.is_builtin = false;
            item.is_workshop = is_ws;
            item.workshop_id = ws_id;
            bool exists = false;
            for (const auto& existing : themes) {
                if (existing.id == item.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) themes.push_back(item);
        }
    };

    auto scanDir = [&](const std::string& dir, bool is_ws) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec) return;
        try {
            std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
            std::filesystem::directory_iterator end;
            while (!ec && it != end) {
                try {
                    std::error_code e2;
                    if (it->is_directory(e2)) {
                        std::string item_id = it->path().filename().string();
                        std::string tjson = (it->path() / "theme.json").string();
                        if (std::filesystem::exists(tjson, e2)) {
                            loadThemeFile(tjson, is_ws, is_ws ? item_id : "");
                        }
                    } else if (it->path().extension() == ".json") {
                        loadThemeFile(it->path().string(), is_ws, "");
                    }
                } catch (...) {}
                it.increment(ec);
            }
        } catch (...) {}
    };

    // 2. 本地预装主题资源目录
    std::vector<std::string> res_candidates = {
        "resources/themes",
        "./resources/themes",
        "../resources/themes",
        AppPaths::homeDir() + "/.local/share/muvik/resources/themes"
    };
    for (const auto& rd : res_candidates) {
        scanDir(rd, false);
    }

    // 3. 用户本地主题目录 ~/.config/muvik/themes
    scanDir(AppPaths::themesDir(), false);

    // 4. Steam 创意工坊已订阅目录
    for (const auto& ws_path : AppPaths::workshopSearchPaths()) {
        scanDir(ws_path, true);
    }

    return themes;
}

static std::string g_active_theme_id;

std::string resolveThemeId() {
    // 1. 环境变量优先（临时覆盖 / CI）
    if (const char* env = std::getenv("MUVIK_THEME"); env && *env) {
        std::string v = trimCopy(env);
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        return v;
    }
    // 2. 内存缓存
    if (!g_active_theme_id.empty()) {
        return g_active_theme_id;
    }
    // 3. 配置文件持久化
    std::ifstream f(AppPaths::configFile("theme"));
    if (f) {
        std::string v = trimCopy({std::istreambuf_iterator<char>(f),
                                  std::istreambuf_iterator<char>()});
        if (!v.empty()) {
            g_active_theme_id = v;
            return v;
        }
    }
    return "dark";
}

ThemeKind resolveThemeKind() {
    std::string id = resolveThemeId();
    std::string lower = id;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "oled" || lower == "amoled" || lower == "trueblack") return ThemeKind::Oled;
    if (lower == "light" || lower == "lightmode") return ThemeKind::Light;
    if (lower == "dark" || lower == "default") return ThemeKind::Dark;
    return ThemeKind::Custom;
}

ThemeItem getCurrentTheme() {
    std::string id = resolveThemeId();
    auto all = getAvailableThemes();
    for (const auto& t : all) {
        if (t.id == id) return t;
    }
    if (!all.empty()) return all[0];
    ThemeItem fallback;
    fallback.id = "dark";
    fallback.name = "深色";
    fallback.is_builtin = true;
    fallback.palette = ui::getBuiltinDarkPalette();
    return fallback;
}

void registerThemeChangeListener(std::function<void(const ThemeItem&)> listener) {
    if (listener) g_theme_listeners.push_back(listener);
}

bool applyThemeById(const std::string& theme_id) {
    std::string target_id = theme_id;
    if (target_id.empty()) target_id = "dark";

    g_active_theme_id = target_id;

    {
        std::ofstream f(AppPaths::configFile("theme"), std::ios::trunc);
        if (f) {
            f << target_id << "\n";
            f.flush();
        }
    }

    applyMuvikTheme(true);

    ThemeItem cur = getCurrentTheme();
    for (const auto& l : g_theme_listeners) {
        if (l) l(cur);
    }
    return true;
}

std::string themeDisplayName(ThemeKind kind) {
    switch (kind) {
        case ThemeKind::Oled:   return i18n::tr("theme.oled", "OLED 真黑");
        case ThemeKind::Light:  return i18n::tr("theme.light", "浅色");
        case ThemeKind::Custom: return getCurrentTheme().name;
        default:                return i18n::tr("theme.dark", "深色");
    }
}

std::string themeDisplayName(const std::string& theme_id) {
    auto all = getAvailableThemes();
    for (const auto& t : all) {
        if (t.id == theme_id) return t.name;
    }
    return theme_id;
}

void applyMuvikTheme(bool force_reapply) {
    if (applied && !force_reapply) return;
    applied = true;

    ThemeItem cur = getCurrentTheme();
    ui::setPalette(cur.palette);

#ifndef TESTING_WITHOUT_BOREALIS
    applyPaletteTo(brls::Theme::getDarkTheme(), cur.palette);
    applyPaletteTo(brls::Theme::getLightTheme(), cur.palette);

    static bool style_applied = false;
    if (!style_applied) {
        style_applied = true;
        brls::Style style = brls::Application::getStyle();
        style.addMetric("brls/tab_frame/sidebar_width", 176.0f);
        style.addMetric("brls/sidebar/padding_left", 20.0f);
        style.addMetric("brls/sidebar/padding_right", 16.0f);
        style.addMetric("brls/sidebar/padding_top", 24.0f);
        style.addMetric("brls/sidebar/padding_bottom", 24.0f);
        style.addMetric("brls/sidebar/item_height", 56.0f);
        style.addMetric("brls/sidebar/item_font_size", 18.0f);
        style.addMetric("brls/sidebar/item_accent_rect_width", 3.0f);
        style.addMetric("brls/sidebar/item_accent_margin_sides", 0.0f);
        style.addMetric("brls/sidebar/item_accent_margin_top_bottom", 0.0f);

        style.addMetric("brls/applet_frame/header_height", 92.0f);
        style.addMetric("brls/applet_frame/header_padding_sides", 40.0f);
        style.addMetric("brls/applet_frame/header_title_font_size", 29.0f);
        style.addMetric("brls/applet_frame/footer_height", 64.0f);

        style.addMetric("brls/highlight/corner_radius", 14.0f);
        style.addMetric("brls/highlight/stroke_width", 3.0f);
    }
#endif
}

} // namespace muvik
