// 单元测试：插件 ZIP 安装器
// 重点：路径穿越（Zip Slip）、越界卸载、回滚等安全边界。
// 测试会构造真实的恶意 ZIP 包，确认防护确实生效。

#include "plugin/plugin_installer.hpp"
#include "plugin/plugin_manager.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>
#include <filesystem>

using namespace muvik;
namespace fs = std::filesystem;

namespace {

// 生成一个正常插件包（内含假的动态库文件）
bool makeGoodZip(const std::string& zip_path) {
    std::string dir = "/tmp/muvik_test_zip/good";
    std::system("rm -rf /tmp/muvik_test_zip && mkdir -p /tmp/muvik_test_zip/good");
    std::ofstream(dir + "/demo_plugin.dylib") << "fake dylib content";
    std::ofstream(dir + "/README.txt") << "plugin readme";
    std::string cmd = "cd " + dir + " && zip -q " + zip_path +
                      " demo_plugin.dylib README.txt";
    return std::system(cmd.c_str()) == 0;
}

// 用真实的已编译插件构造一个可加载的包（用于验证「扫描器会进入子目录」）
bool makeRealZip(const std::string& zip_path) {
    namespace fs = std::filesystem;
    const char* cands[] = {
        "/Users/yiuf/.config/muvik/plugins/demo_plugin.dylib",
    };
    for (const char* c : cands) {
        if (!fs::exists(c)) continue;
        std::system("rm -rf /tmp/muvik_test_zip/real && "
                    "mkdir -p /tmp/muvik_test_zip/real");
        std::error_code ec;
        fs::copy_file(c, "/tmp/muvik_test_zip/real/demo_plugin.dylib",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) continue;
        std::string cmd = "cd /tmp/muvik_test_zip/real && zip -q " +
                          zip_path + " demo_plugin.dylib";
        return std::system(cmd.c_str()) == 0;
    }
    return false;
}

// 生成一个不含动态库的包
bool makeNoLibZip(const std::string& zip_path) {
    std::system("rm -rf /tmp/muvik_test_zip/nolib && mkdir -p /tmp/muvik_test_zip/nolib");
    std::ofstream("/tmp/muvik_test_zip/nolib/note.txt") << "no library here";
    std::string cmd = "cd /tmp/muvik_test_zip/nolib && zip -q " + zip_path + " note.txt";
    return std::system(cmd.c_str()) == 0;
}

// 生成一个带路径穿越条目的恶意包
bool makeSlipZip(const std::string& zip_path) {
    std::string py =
        "import zipfile\n"
        "z = zipfile.ZipFile('" + zip_path + "', 'w')\n"
        "z.writestr('ok.dylib', 'x')\n"
        "z.writestr('../../../tmp/MUVIK_PWNED.txt', 'malicious')\n"
        "z.close()\n";
    std::ofstream("/tmp/muvik_test_zip/mk.py") << py;
    return std::system("python3 /tmp/muvik_test_zip/mk.py") == 0;
}

} // namespace

int main() {
    TEST_SUITE();
    setenv("MUVIK_ENABLE_NATIVE_PLUGINS", "1", 1);
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_installer", 1);
    std::system("rm -rf /tmp/muvik_test_installer /tmp/MUVIK_PWNED.txt "
                "/tmp/muvik_test_zip /tmp/good_plugin.zip /tmp/nolib.zip /tmp/slip.zip");

    CHECK_TRUE(makeGoodZip("/tmp/good_plugin.zip"), "构造正常插件包");
    CHECK_TRUE(makeNoLibZip("/tmp/nolib.zip"), "构造无动态库的包");
    CHECK_TRUE(makeSlipZip("/tmp/slip.zip"), "构造路径穿越的恶意包");

    TEST_BEGIN("安装器 / 正常安装");
    auto good = PluginInstaller::installFromZip("/tmp/good_plugin.zip");
    CHECK_TRUE(good.ok, "合法插件包安装成功");
    CHECK_FALSE(good.install_dir.empty(), "返回安装目录");

    TEST_BEGIN("安装器 / Zip Slip 防护");
    auto slip = PluginInstaller::installFromZip("/tmp/slip.zip");
    CHECK_FALSE(slip.ok, "路径穿越包被拒绝");
    CHECK_CONTAINS(slip.message, std::string("穿越"), "错误信息指明路径穿越");
    CHECK_FALSE(fs::exists("/tmp/MUVIK_PWNED.txt"),
                "恶意文件未被写到插件目录之外");

    TEST_BEGIN("安装器 / 内容校验");
    {
        auto nolib = PluginInstaller::installFromZip("/tmp/nolib.zip");
        CHECK_FALSE(nolib.ok, "不含动态库的包被拒绝");
        CHECK_CONTAINS(nolib.message, std::string("动态库"), "错误信息指明缺少动态库");
    }
    {
        auto missing = PluginInstaller::installFromZip("/tmp/does_not_exist.zip");
        CHECK_FALSE(missing.ok, "不存在的文件被拒绝");
    }
    {
        auto wrongext = PluginInstaller::installFromZip("/tmp/good_plugin.zip.bak");
        CHECK_FALSE(wrongext.ok, "非 .zip 后缀被拒绝");
    }

    TEST_BEGIN("安装器 / 重复安装");
    {
        auto dup = PluginInstaller::installFromZip("/tmp/good_plugin.zip");
        CHECK_FALSE(dup.ok, "同名插件不被覆盖");
        CHECK_CONTAINS(dup.message, std::string("同名"), "错误信息指明同名");
    }

    TEST_BEGIN("安装器 / 列表与卸载");
    {
        auto dirs = PluginInstaller::listInstalledDirs();
        CHECK_EQ(dirs.size(), (size_t)1, "列出 1 个已安装插件目录");
        if (!dirs.empty()) {
            CHECK_TRUE(PluginInstaller::uninstall(dirs[0]), "卸载成功");
            CHECK_TRUE(PluginInstaller::listInstalledDirs().empty(), "卸载后列表为空");
        }
    }

    TEST_BEGIN("安装器 / 卸载越界防护");
    CHECK_FALSE(PluginInstaller::uninstall("/tmp"), "拒绝卸载 /tmp");
    CHECK_FALSE(PluginInstaller::uninstall("/etc"), "拒绝卸载 /etc");
    CHECK_FALSE(PluginInstaller::uninstall(""), "拒绝空路径");

    TEST_BEGIN("安装器 / 子目录插件能被扫描加载（回归）");
    // 安装器把插件解压到 plugins/<包名>/ 子目录。
    // 扫描器曾只处理顶层文件，导致「从 ZIP 安装的插件永远不加载」。
    {
        // 把已有的散装插件临时移走，确保加载到的只可能来自本次安装
        const fs::path pdir = AppPaths::pluginsDir();
        std::error_code ec0;
        fs::create_directories(pdir, ec0);
        std::vector<std::pair<fs::path, fs::path>> stash;
        for (const auto& e : fs::directory_iterator(pdir, ec0)) {
            if (ec0) break;
            fs::path dst = fs::path("/tmp") / ("stash_" + e.path().filename().string());
            std::error_code ec;
            fs::rename(e.path(), dst, ec);
            if (!ec) stash.emplace_back(dst, e.path().filename());
        }

        // 必须用真实 dylib，否则 dlopen 会因「非 Mach-O」失败，
        // 那样测的是二进制有效性而非「扫描器是否进入子目录」。
        const bool have_real = makeRealZip("/tmp/real_plugin.zip");
        if (!have_real) {
            printf("跳过：找不到可用的真实插件用于构造测试包\n");
        } else {
        auto r = PluginInstaller::installFromZip("/tmp/real_plugin.zip");
        CHECK_TRUE(r.ok, "安装成功");
        if (r.ok) {
            auto& pm = PluginManager::getInstance();
            pm.unloadAll();
            size_t n = pm.loadAll();
            printf("plugins 目录清空后安装，加载到 %zu 个插件\n", n);
            CHECK_TRUE(n >= 1, "子目录中的插件被加载（回归：曾为 0）");
            pm.unloadAll();
            PluginInstaller::uninstall(r.install_dir);
        }
        }

        for (const auto& [src, name] : stash) {
            std::error_code ec;
            fs::rename(src, pdir / name, ec);
        }
    }

    TEST_END();
    std::system("rm -rf /tmp/muvik_test_zip /tmp/good_plugin.zip "
                "/tmp/nolib.zip /tmp/slip.zip /tmp/muvik_test_installer");
    return TEST_SUMMARY();
}
