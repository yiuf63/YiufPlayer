// 单元测试：插件刮削链路
//
// 覆盖 scrapeSearch → scrapeForFile 的完整流程。
// 使用 demo_plugin（离线假数据），不依赖网络。
//
// 另有一条需要真实 WebDAV 服务的用例，通过环境变量启用：
//   python3 tests/tools/dav_test_server.py /tmp/davroot 18081 u p &
//   MUVIK_TEST_DAV=http://127.0.0.1:18081/ ./tests/run_tests.sh scraper
// 该用例用于回归「listVideoSourceItems 漏调 init」——
// 不启用时它会被跳过，因此默认运行不会因缺少服务器而失败。

#include "plugin/plugin_manager.hpp"
#include "utils/app_paths.hpp"
#include "plugin/plugin_config_store.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <filesystem>

using namespace muvik;
namespace fs = std::filesystem;

int main() {
    TEST_SUITE();
    auto& pm = PluginManager::getInstance();

    // 需要显式加载：测试进程不经过 main()，没有自动扫描。
    // 用 build 目录里的演示插件（与主程序同一份产物）。
    // 显式开启原生 C 插件加载开关以测试遗留 ABI
    setenv("MUVIK_ENABLE_NATIVE_PLUGINS", "1", 1);
    std::string plugins_dir = AppPaths::configDir() + "/plugins";
    if (!fs::exists(plugins_dir)) {
        printf("跳过：%s 不存在\n", plugins_dir.c_str());
        return 0;
    }
    size_t n = pm.loadAll(plugins_dir);
    printf("已加载 %zu 个插件（目录 %s）\n", n, plugins_dir.c_str());

    TEST_BEGIN("刮削 / 能找到刮削器插件");
    auto scrapers = pm.getScraperPluginPaths();
    CHECK_FALSE(scrapers.empty(), "至少存在一个刮削器插件");
    if (scrapers.empty()) {
        TEST_END();
        return TEST_SUMMARY();
    }
    const std::string plugin = scrapers.front();

    TEST_BEGIN("刮削 / 搜索返回 JSON");
    std::string json;
    bool searched = pm.scrapeSearch(plugin, "Interstellar", 2014, json);
    CHECK_TRUE(searched, "搜索成功");
    CHECK_FALSE(json.empty(), "返回内容非空");
    CHECK_TRUE(json.find("id") != std::string::npos, "JSON 含 id 字段");

    TEST_BEGIN("刮削 / 取详情");
    ScrapeDetail detail;
    bool got = pm.scrapeForFile(plugin, "Interstellar", 2014, detail);
    CHECK_TRUE(got, "取到详情");
    CHECK_FALSE(detail.title.empty(), "标题非空");
    CHECK_TRUE(detail.overview.size() > 0, "简介非空");

    TEST_BEGIN("刮削 / 不存在的影片应优雅失败");
    ScrapeDetail none;
    // 演示插件对任何 query 都返回数据，这里只验证不会崩溃
    bool r = pm.scrapeForFile(plugin, "", 0, none);
    (void)r;
    CHECK_TRUE(true, "空查询未导致崩溃");

    TEST_BEGIN("插件浏览 / listVideoSourceItems 需要独立的 init（回归）");
    // 该函数内部会 create 一个新的 video_source 实例。
    // 曾经漏掉对它的 init()，导致插件拿不到配置，
    // 表现为「插件浏览页永远读不到任何内容」。
    // 用一个不存在的目录来调用：只要不崩溃、且返回 false 即可，
    // 关键是它能进入 list_items 分支而不是在 init 阶段被挡下。
    if (!scrapers.empty()) {
        // 找一个需要网络配置的视频源插件（如 WebDAV），验证配置生效性
        std::string vs_path;
        for (const auto& pl : pm.getPlugins()) {
            if (pl.has_video_source && pl.name.find("WebDAV") != std::string::npos) {
                vs_path = pl.path;
                break;
            }
        }
        if (vs_path.empty()) {
            for (const auto& pl : pm.getPlugins()) {
                if (pl.has_video_source && !pl.is_js_plugin) {
                    vs_path = pl.path;
                    break;
                }
            }
        }
        if (vs_path.empty()) {
            for (const auto& pl : pm.getPlugins()) {
                if (pl.has_video_source) { vs_path = pl.path; break; }
            }
        }
        if (!vs_path.empty()) {
            std::string stem = fs::path(vs_path).stem().string();
            auto& cfg = PluginConfigStore::getInstance();
            const bool had_cfg = cfg.hasConfig(stem);
            const std::string old_url = cfg.get(stem, "url");

            // 0) 若外部已配置了可达的服务，应能列出内容
            if (std::getenv("MUVIK_TEST_DAV")) {
                cfg.set(stem, "url", std::getenv("MUVIK_TEST_DAV"));
                cfg.set(stem, "user", "u");
                cfg.set(stem, "pass", "p");
                cfg.save();
            }
            if (!old_url.empty() || std::getenv("MUVIK_TEST_DAV")) {
                std::vector<MediaItem> items;
                bool listed = pm.listVideoSourceItems(vs_path, "", items);
                printf("已配置 -> 列表%s（%zu 项）\n",
                       listed ? "成功" : "失败", items.size());
                CHECK_TRUE(listed, "已配置可达服务时应能列出目录");
            }

            // 1) 未配置时必须优雅失败（返回 false，不崩溃）
            cfg.clear(stem);
            cfg.save();
            {
                std::vector<MediaItem> items;
                bool listed = pm.listVideoSourceItems(vs_path, "", items);
                CHECK_FALSE(listed, "未配置 url 时返回失败而非崩溃");
            }

            // 2) 配置一个不可达地址，仍应优雅失败
            cfg.set(stem, "url", "http://127.0.0.1:1/nonexistent");
            cfg.save();
            {
                std::vector<MediaItem> items;
                bool listed = pm.listVideoSourceItems(vs_path, "", items);
                CHECK_FALSE(listed, "不可达地址返回失败而非崩溃");
            }

            // 还原
            cfg.clear(stem);
            if (had_cfg && !old_url.empty()) cfg.set(stem, "url", old_url);
            cfg.save();
        }
    }

    TEST_BEGIN("刮削 / 路径无效时返回失败");
    std::string bad_json;
    CHECK_FALSE(pm.scrapeSearch("/nonexistent/plugin.dylib", "x", 0, bad_json),
                "无效插件路径返回失败");

    TEST_END();
    return TEST_SUMMARY();
}
