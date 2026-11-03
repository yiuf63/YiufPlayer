#include <string>
#include <vector>
#include "plugin/plugin_installer.hpp"
#include "utils/app_paths.hpp"
#include <borealis/core/logger.hpp>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <sstream>
#include <sys/wait.h>
#include <cctype>

namespace fs = std::filesystem;

namespace deck_player {

namespace {

// 执行命令并收集 stdout。参数以数组传入，不经过 shell，
// 从根本上避免文件名中的空格/引号导致的命令注入。
std::string runCommand(const std::vector<std::string>& argv, int& exit_code) {
    exit_code = -1;
    if (argv.empty()) return "";

    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        // 单引号包裹，内部单引号转义为 '\''
        cmd += "'";
        for (char c : argv[i]) {
            if (c == '\'') cmd += "'\\''";
            else cmd += c;
        }
        cmd += "'";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    std::string out;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), (int)buf.size(), pipe)) out += buf.data();
    int status = pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool PluginInstaller::validateZipEntries(const std::string& zip_path, std::string& err) {
    int rc = -1;
    std::string listing = runCommand({"unzip", "-Z1", zip_path}, rc);
    if (rc != 0) {
        err = "无法读取压缩包内容（文件损坏或不是有效 ZIP）";
        return false;
    }

    std::istringstream iss(listing);
    std::string entry;
    bool has_library = false;
    while (std::getline(iss, entry)) {
        if (!entry.empty() && entry.back() == '\r') entry.pop_back();
        if (entry.empty()) continue;

        // Zip Slip 防护：绝对路径与上跳目录一律拒绝
        if (!entry.empty() && entry[0] == '/') {
            err = "压缩包内含有绝对路径条目，已拒绝安装：" + entry;
            return false;
        }
        if (entry.find("..") != std::string::npos) {
            err = "压缩包内含有路径穿越条目，已拒绝安装：" + entry;
            return false;
        }
        if (endsWith(entry, ".so") || endsWith(entry, ".dylib") || endsWith(entry, ".dll")) {
            has_library = true;
        }
    }

    if (!has_library) {
        err = "压缩包内未找到插件动态库（.so / .dylib / .dll）";
        return false;
    }
    return true;
}

PluginInstaller::Result PluginInstaller::installFromZip(const std::string& zip_path,
                                                        const std::string& install_name) {
    Result r;

    std::error_code ec;
    if (!fs::exists(zip_path, ec) || !fs::is_regular_file(zip_path, ec)) {
        r.message = "文件不存在：" + zip_path;
        return r;
    }
    if (!endsWith(zip_path, ".zip")) {
        r.message = "目前仅支持 .zip 格式的插件包";
        return r;
    }

    std::string err;
    if (!validateZipEntries(zip_path, err)) {
        r.message = err;
        return r;
    }

    // 子目录名：优先使用调用方指定，否则用包文件名
    std::string name = install_name;
    if (name.empty()) {
        name = fs::path(zip_path).stem().string();
    }
    // 目录名净化：只允许字母数字下划线和短横线
    for (char& c : name) {
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
    }
    if (name.empty()) name = "plugin";

    std::string base = AppPaths::pluginsDir();
    AppPaths::ensureDir(base);
    std::string target = base + "/" + name;

    // 目标已存在则拒绝，避免覆盖正在使用的插件
    if (fs::exists(target, ec)) {
        r.message = "已存在同名插件目录：" + name + "\n请先卸载或换一个包名";
        return r;
    }

    if (!AppPaths::ensureDir(target)) {
        r.message = "无法创建插件目录：" + target;
        return r;
    }

    // 解压到子目录
    int rc = -1;
    runCommand({"unzip", "-o", "-q", zip_path, "-d", target}, rc);
    if (rc != 0) {
        std::error_code rc2;
        fs::remove_all(target, rc2);
        r.message = "解压失败，已回滚";
        return r;
    }

    // 校验解压结果里确实有插件
    bool found = false;
    try {
        for (const auto& e : fs::recursive_directory_iterator(target)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            if (ext == ".so" || ext == ".dylib" || ext == ".dll") { found = true; break; }
        }
    } catch (...) {}

    if (!found) {
        std::error_code rc2;
        fs::remove_all(target, rc2);
        r.message = "解压后未找到插件动态库，已回滚";
        return r;
    }

    r.ok = true;
    r.install_dir = target;
    r.message = "安装成功：" + name;
    brls::Logger::info("插件包已安装到 {}", target);
    return r;
}

std::vector<std::string> PluginInstaller::listInstalledDirs() {
    std::vector<std::string> out;
    std::string base = AppPaths::pluginsDir();
    std::error_code ec;
    if (!fs::exists(base, ec)) return out;
    try {
        for (const auto& e : fs::directory_iterator(base)) {
            if (e.is_directory()) out.push_back(e.path().string());
        }
    } catch (...) {}
    return out;
}

bool PluginInstaller::uninstall(const std::string& install_dir) {
    if (install_dir.empty()) return false;
    // 安全检查：只允许删除插件目录之下的路径
    std::string base = AppPaths::pluginsDir();
    std::string canonical_base, canonical_target;
    std::error_code ec;
    canonical_base = fs::weakly_canonical(base, ec).string();
    canonical_target = fs::weakly_canonical(install_dir, ec).string();
    if (canonical_base.empty() || canonical_target.empty()) return false;
    if (canonical_target.rfind(canonical_base, 0) != 0) return false;
    if (canonical_target == canonical_base) return false;

    std::error_code rc;
    fs::remove_all(canonical_target, rc);
    return !rc;
}

} // namespace deck_player
