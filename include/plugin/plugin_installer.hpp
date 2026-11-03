#pragma once

#include <string>
#include <vector>
#include <string>
#include <vector>
#include <functional>

namespace deck_player {

// 插件包安装器：支持从本地 ZIP 包安装插件。
//
// 安全约束（重要）：
//  - 只接受 .zip 且必须包含合法的插件动态库
//  - 解包前校验每个条目的路径，拒绝绝对路径与 ".." 穿越（Zip Slip 防护）
//  - 解压目标限定在插件目录下的独立子目录，出错可整体回滚
class PluginInstaller {
public:
    struct Result {
        bool ok = false;
        std::string message;      // 用户可读的说明
        std::string install_dir;  // 成功时的安装目录
    };

    // 从 ZIP 包安装插件。install_name 决定子目录名（留空则用包文件名）
    static Result installFromZip(const std::string& zip_path,
                                 const std::string& install_name = "");

    // 列出当前已安装的插件子目录
    static std::vector<std::string> listInstalledDirs();

    // 卸载某个插件子目录
    static bool uninstall(const std::string& install_dir);

private:
    // 校验 ZIP 内条目是否安全（拒绝绝对路径 / 路径穿越 / 符号链接）
    static bool validateZipEntries(const std::string& zip_path, std::string& err);
};

} // namespace deck_player
