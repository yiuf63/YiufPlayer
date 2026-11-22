#include <cstdlib>
#include <string>
#include <vector>
#include "utils/resource_locator.hpp"
#include <filesystem>
#include <vector>
#include <cstdlib>

#if !defined(TESTING_WITHOUT_BOREALIS)
#include <borealis/core/logger.hpp>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace muvik {

namespace {

std::string g_resource_dir;

// 判断某目录是否是有效的资源目录（至少含 font 子目录）
bool isValidResourceDir(const std::string& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return false;
    return fs::exists(fs::path(dir) / "font", ec);
}

std::string canonical(const fs::path& p) {
    std::error_code ec;
    auto c = fs::weakly_canonical(p, ec);
    return ec ? p.string() : c.string();
}

// 当前工作目录
std::string currentDir() {
    std::error_code ec;
    auto p = fs::current_path(ec);
    return ec ? "" : p.string();
}

} // namespace

std::string ResourceLocator::executableDir() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
    std::error_code ec;
    auto p = fs::canonical(fs::path(buf.data()), ec);
    if (ec) return fs::path(buf.data()).parent_path().string();
    return p.parent_path().string();
#elif defined(__linux__)
    char buf[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    return fs::path(buf).parent_path().string();
#else
    return "";
#endif
}

std::string ResourceLocator::findResourceDir() {
    const std::string exe_dir = executableDir();

    std::vector<std::string> candidates;

    // 1. 环境变量优先（便于调试与特殊部署）
    if (const char* env = std::getenv("MUVIK_RESOURCES")) {
        candidates.emplace_back(env);
    }

    // 2. 当前工作目录（开发时在 build/ 下运行，资源是软链）
    candidates.push_back((fs::path(currentDir()) / "resources").string());

    // 3. 相对可执行文件的位置（安装布局）
    //
    // 目录名必须同时覆盖两个：
    //   · "yiufplayer" —— 当前 CMakeLists 的 install(DIRECTORY ... DESTINATION
    //                     share/yiufplayer)，也即实际安装出来的位置；
    //   · "muvik"      —— 旧版安装布局与 Flatpak manifest，以及 /app 下的
    //                     打包路径，保留以免升级后失效。
    //
    // 之前这里只查 "muvik"，而安装规则写的是 "share/yiufplayer"，
    // 于是 `cmake --install` 装出来的程序一启动就报
    // 「未能定位资源目录，字体与图标可能缺失」—— 桌面上从启动器点图标
    // 进去就是没有文字的界面。实测复现。
    //
    // 顺带修掉一处笔误：原先 exe.parent_path().parent_path() / "share" / "muvik"
    // 连写了两遍，第二行完全多余。
    if (!exe_dir.empty()) {
        fs::path exe(exe_dir);
        for (const char* app : {"yiufplayer", "muvik"}) {
            candidates.push_back((exe / "resources").string());                            // bin/resources
            candidates.push_back((exe / "share" / app / "resources").string());            // bin/share/<app>
            candidates.push_back((exe.parent_path() / "resources").string());              // ../resources
            candidates.push_back((exe.parent_path() / "share" / app / "resources").string()); // /usr/bin -> /usr/share/<app>
            candidates.push_back(
                (exe.parent_path().parent_path() / "share" / app / "resources").string()); // /app/bin -> /app/share/<app>
        }
    }

    // 4. 系统级安装路径
    for (const char* app : {"yiufplayer", "muvik"}) {
        candidates.push_back(std::string("/app/share/") + app + "/resources");       // Flatpak
        candidates.push_back(std::string("/usr/share/") + app + "/resources");       // 系统安装
        candidates.push_back(std::string("/usr/local/share/") + app + "/resources");
    }

    for (const auto& c : candidates) {
        if (isValidResourceDir(c)) {
            return canonical(c);
        }
    }
    return "";
}

bool ResourceLocator::setup() {
    std::string dir = findResourceDir();
    if (dir.empty()) {
#if !defined(TESTING_WITHOUT_BOREALIS)
        brls::Logger::warning(
            "未能定位资源目录，字体与图标可能缺失。"
            "可通过 MUVIK_RESOURCES 环境变量指定。");
#endif
        return false;
    }

    g_resource_dir = dir;

#if defined(__linux__) && !defined(ANDROID)
    // 针对嵌入式 Linux（如 TrimUI / Buildroot 等无系统字体的设备）：
    // fontconfig 默认会扫描 ~/.fonts 与 /usr/share/fonts 目录。在此处自动建立软链接，
    // 确保 libass / fontconfig 能扫描并使用内置中文字体，杜绝字幕无法显示或方块乱码。
    const char* home = std::getenv("HOME");
    fs::path target_font = fs::path(dir) / "font" / "font.ttf";
    std::error_code fec;

    if (fs::exists(target_font, fec)) {
        if (home && *home) {
            fs::path user_fonts = fs::path(home) / ".fonts";
            fs::create_directories(user_fonts, fec);
            fs::path link_sc = user_fonts / "NotoSansSC.ttf";
            fs::path link_f  = user_fonts / "font.ttf";
            // 无论此前是否存在（包括更名改版后的失效死链），先移除再重新建立正确链接
            fs::remove(link_sc, fec);
            fs::remove(link_f, fec);
            fs::create_symlink(target_font, link_sc, fec);
            fs::create_symlink(target_font, link_f, fec);
        }

        // 若拥有系统字体目录写权限，一并建立系统级目录链接
        fs::path sys_fonts_dir = "/usr/share/fonts/yiufplayer";
        fs::remove_all(sys_fonts_dir, fec);
        fs::create_symlink(fs::path(dir) / "font", sys_fonts_dir, fec);
    }
#endif

    // borealis 的 BRLS_RESOURCES 是相对路径 "./resources/"，
    // 把工作目录切到资源目录的父目录即可让该相对路径成立。
    fs::path parent = fs::path(dir).parent_path();
    std::error_code ec;
    fs::current_path(parent, ec);
    if (ec) {
#if !defined(TESTING_WITHOUT_BOREALIS)
        brls::Logger::warning("切换工作目录失败: {}", ec.message());
#endif
    }

#if !defined(TESTING_WITHOUT_BOREALIS)
    brls::Logger::info("资源目录: {}", dir);
#endif
    return true;
}

const std::string& ResourceLocator::resourceDir() {
    return g_resource_dir;
}

} // namespace muvik
