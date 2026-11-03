#pragma once

#include <string>
#include <string>

namespace deck_player {

// 资源路径解析。
//
// 背景：borealis 把资源根目录编译进二进制（BRLS_RESOURCES），当前值为相对路径
// "./resources/"，相对于进程工作目录。这在开发时可用（在 build/ 下运行），
// 但在 Flatpak / 桌面启动器 / 任何从别的目录启动的场景下都会失效，
// 导致字体与图标加载失败。
//
// 本模块在程序启动时定位真实资源目录，并把工作目录切过去，
// 使相对路径的 BRLS_RESOURCES 仍然成立。
class ResourceLocator {
public:
    // 尝试定位资源目录并把工作目录切换过去。
    // 返回是否成功。失败时程序仍可运行，但字体/图标会缺失。
    static bool setup();

    // 上一次定位到的资源目录（绝对路径），失败时为空
    static const std::string& resourceDir();

    // 可执行文件所在目录（绝对路径）
    static std::string executableDir();

private:
    // 按优先级尝试的候选路径
    static std::string findResourceDir();
};

} // namespace deck_player
