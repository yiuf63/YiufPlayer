/*
    Copyright 2021 natinusala
    Copyright 2023 xfangfang

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <unistd.h>

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/platforms/desktop/desktop_font.hpp>

#define INTER_FONT_PATH BRLS_ASSET("font/switch_font.ttf")
#define INTER_ICON_PATH BRLS_ASSET("font/switch_icons.ttf")

namespace brls
{

const static std::vector<std::string> fontExts = {
    ".ttc",
    ".ttf",
    ".otf",
};

bool DesktopFontLoader::loadFontsExist(NVGcontext* vg, std::vector<std::string> fontPaths, std::string fontName, std::string fallbackFont) {
    for (auto &fontPath: fontPaths) {
        for (auto &fontExt: fontExts) {
            std::string fullPath = fontPath + fontExt;
            if (access(fullPath.c_str(), F_OK) != -1) {
                this->loadFontFromFile(fontName, fullPath);
                if (!fallbackFont.empty()) {
                    nvgAddFallbackFontId(vg, Application::getFont(fallbackFont), Application::getFont(fontName));
                }
                brls::Logger::info("Using {} font: {}", fontName, fullPath);
                return true;
            }
        }
    }
    return false;
}

bool DesktopFontLoader::loadFont(const std::string& name, const std::string& path) {
#ifdef USE_LIBROMFS
    if (path.empty()) return false;
    if (path.rfind("@res/", 0) == 0)
    {
        // font is inside the romfs
        try
        {
            auto& font = romfs::get(path.substr(5));
            brls::Logger::info("[font] romfs 命中 {} size={} valid={}",
                               path.substr(5), font.size(), font.valid());
            if (font.valid() && Application::loadFontFromMemory(name, (void*)font.data(), font.size(), false))
                return true;
            brls::Logger::warning("[font] romfs 数据无效或 loadFontFromMemory 失败: {}",
                                  path.substr(5));
        }
        catch (const std::exception& e)
        {
            // Muvik 诊断：原来这里是 catch(...) 静默吞掉，
            // 导致「资源嵌入了却读不到」完全没有任何线索。
            brls::Logger::error("[font] romfs 读取失败 {}: {}", path.substr(5), e.what());
        }
        catch (...)
        {
            brls::Logger::error("[font] romfs 读取失败（未知异常）: {}", path.substr(5));
        }
    } else
#endif
    if (access(path.c_str(), F_OK) != -1 && Application::loadFontFromFile(name, path)) {
        return true;
    }

    return false;
}

void DesktopFontLoader::loadFonts()
{
    NVGcontext* vg = brls::Application::getNVGContext();

    // Text font
    if (loadFont(FONT_REGULAR, USER_FONT_PATH))
    {
        // Using internal font as fallback
        if (loadFont("default", INTER_FONT_PATH))
        {
            nvgAddFallbackFontId(vg, Application::getFont(FONT_REGULAR), Application::getFont("default"));
        }
    } else {
        brls::Logger::warning("Cannot find custom font, (Searched at: {})", USER_FONT_PATH);
        brls::Logger::info("Trying to use internal font: {}", INTER_FONT_PATH);
        if (!loadFont(FONT_REGULAR, INTER_FONT_PATH))
        {
            Logger::warning("Failed to load internal font, text may not be displayed");
        }
    }

    // Using system font as fallback
#if defined(__APPLE__) && !defined(IOS)
    std::vector<std::string> koreanFonts = {
        "/System/Library/Fonts/AppleSDGothicNeo",
    };
    // Muvik 补丁：恢复中文字体回退。
    // 上游这里整段被注释掉，只保留韩文回退，导致简体中文在 macOS 上
    // 没有任何字体可供 fontstash 回退 —— 界面中文全部显示为乱码/方块。
    // 实测系统上这些字体均存在。
    std::vector<std::string> simplifiedChineseFonts = {
        "/System/Library/Fonts/STHeiti Light",              // 黑体
        "/System/Library/Fonts/Hiragino Sans GB",           // 冬青黑体简体中文
        "/System/Library/Fonts/Supplemental/Songti",        // 宋体
        "/System/Library/Fonts/Supplemental/Arial Unicode", // Arial Unicode
        "/System/Library/Fonts/PingFang",                   // 苹方
    };
#elif defined(_WIN32)
    std::string prefix = "C:\\Windows\\Fonts\\";
    char* winDir = getenv("systemroot");
    if (winDir) {
        prefix = std::string{winDir} + "\\Fonts\\";
    }
    std::vector<std::string> koreanFonts = {
        prefix+"malgun",
    };
    std::vector<std::string> simplifiedChineseFonts = {
        prefix+"msyh",
    };
#elif defined(ANDROID)
    std::vector<std::string> koreanFonts;
    std::vector<std::string> simplifiedChineseFonts = {
        "/system/fonts/NotoSansCJK-Regular",
        "/system/fonts/DroidSansFallback",
        "/system/fonts/NotoSansSC-Regular",
        "/system/fonts/DroidSansChinese",
    };
#else
    std::vector<std::string> koreanFonts;
    std::vector<std::string> simplifiedChineseFonts;
#endif
    if (!simplifiedChineseFonts.empty()) {
        loadFontsExist(vg, simplifiedChineseFonts, FONT_CHINESE_SIMPLIFIED, FONT_REGULAR);
    }
    if (!koreanFonts.empty()) {
        loadFontsExist(vg, koreanFonts, FONT_KOREAN_REGULAR, FONT_REGULAR);
    }

    // Load Emoji
    if (loadFont(FONT_EMOJI, USER_EMOJI_PATH))
    {
        nvgAddFallbackFontId(vg, Application::getFont(FONT_REGULAR), Application::getFont(FONT_EMOJI));
    }

    // bottom bar icons
    if (loadFont(FONT_SWITCH_ICONS, USER_ICON_PATH))
    {
        // User-provided icons
        nvgAddFallbackFontId(vg, Application::getFont(FONT_REGULAR), Application::getFont(FONT_SWITCH_ICONS));
    }
    else
    {
        brls::Logger::warning("Cannot find custom icon, (Searched at: {})", USER_ICON_PATH);
        brls::Logger::info("Trying to use internal icon: {}", INTER_ICON_PATH);
        if (loadFont(FONT_SWITCH_ICONS, INTER_ICON_PATH))
        {
            // Internal icons
            nvgAddFallbackFontId(vg, Application::getFont(FONT_REGULAR), Application::getFont(FONT_SWITCH_ICONS));
        }
        else
        {
            Logger::warning("Failed to load internal icons, bottom bar icons may not be displayed");
        }
    }

    // Material icons
    if (this->loadMaterialFromResources())
    {
        nvgAddFallbackFontId(vg, Application::getFont(FONT_REGULAR), Application::getFont(FONT_MATERIAL_ICONS));
    }
    else
    {
        Logger::error("switch: could not load Material icons font from resources");
    }
}

} // namespace brls
