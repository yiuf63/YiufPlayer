#include <cstdlib>
#include <string>
#include <vector>
#include "utils/resource_locator.hpp"
#include <borealis/core/logger.hpp>
#include <filesystem>
#include <vector>
#include <cstdlib>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace deck_player {

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
    if (const char* env = std::getenv("DECKPLAYER_RESOURCES")) {
        candidates.emplace_back(env);
    }

    // 2. 当前工作目录（开发时在 build/ 下运行，资源是软链）
    candidates.push_back((fs::path(currentDir()) / "resources").string());

    // 3. 相对可执行文件的位置（安装布局）
    if (!exe_dir.empty()) {
        fs::path exe(exe_dir);
        candidates.push_back((exe / "resources").string());              // bin/resources
        candidates.push_back((exe.parent_path() / "resources").string()); // ../resources
        candidates.push_back((exe.parent_path() / "share" / "deck-player" / "resources").string());
        candidates.push_back((exe.parent_path().parent_path() / "share" / "deck-player" / "resources").string());
        // Flatpak 常见布局：/app/bin/deck-player -> /app/share/deck-player/resources
        candidates.push_back((exe.parent_path().parent_path() / "share" / "deck-player" / "resources").string());
    }

    // 4. 系统级安装路径
    candidates.push_back("/app/share/deck-player/resources");           // Flatpak
    candidates.push_back("/usr/share/deck-player/resources");            // 系统安装
    candidates.push_back("/usr/local/share/deck-player/resources");

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
        brls::Logger::warning(
            "未能定位资源目录，字体与图标可能缺失。"
            "可通过 DECKPLAYER_RESOURCES 环境变量指定。");
        return false;
    }

    g_resource_dir = dir;

    // borealis 的 BRLS_RESOURCES 是相对路径 "./resources/"，
    // 把工作目录切到资源目录的父目录即可让该相对路径成立。
    fs::path parent = fs::path(dir).parent_path();
    std::error_code ec;
    fs::current_path(parent, ec);
    if (ec) {
        brls::Logger::warning("切换工作目录失败: {}", ec.message());
    }

    brls::Logger::info("资源目录: {}", dir);
    return true;
}

const std::string& ResourceLocator::resourceDir() {
    return g_resource_dir;
}

} // namespace deck_player
