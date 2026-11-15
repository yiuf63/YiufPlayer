#include "plugin/js_plugin.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/plugin_config_store.hpp"
#include "utils/crypto_utils.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace muvik;
namespace fs = std::filesystem;

int main() {
    TEST_BEGIN("Crypto / MD5 与 SHA256 与 Base64");
    std::string text = "Hello Muvik 2026";
    std::string md5_res = crypto::md5(text);
    CHECK_EQ(md5_res.size(), 32, "MD5 长度为 32 位");
    std::string sha256_res = crypto::sha256(text);
    CHECK_EQ(sha256_res.size(), 64, "SHA256 长度为 64 位");

    std::string b64 = crypto::base64Encode(text);
    CHECK_FALSE(b64.empty(), "Base64 编码非空");
    std::string decoded = crypto::base64Decode(b64);
    CHECK_EQ(decoded, text, "Base64 编解码对称一致");

    std::string hmac = crypto::hmacSha256("secret_key", "data_to_sign");
    CHECK_EQ(hmac.size(), 64, "HMAC-SHA256 长度为 64 位");

    TEST_BEGIN("JSRuntimeHost / 基础执行与异步 Promise");
    JSRuntimeHost rt("test-plugin");
    std::string err;
    bool ok = rt.evaluate("var x = 10 + 32; function getAnswer() { return x; }", "<test>", &err);
    CHECK_TRUE(ok, "基础 JS 代码求值成功");
    CHECK_TRUE(rt.hasFunction("getAnswer"), "检测到 getAnswer 全局函数");

    std::string out_res;
    ok = rt.callFunction("getAnswer", {}, out_res, &err);
    CHECK_TRUE(ok, "调用 getAnswer 成功");
    CHECK_EQ(out_res, "42", "返回值等于 42");

    // 测试异步 async/await 与 Promise 自动解析
    ok = rt.evaluate(
        "async function asyncAdd(a, b) {\n"
        "    return { sum: a + b, source: 'async-quickjs' };\n"
        "}\n", "<async>", &err);
    CHECK_TRUE(ok, "定义 async 函数成功");

    ok = rt.callFunction("asyncAdd", { "100", "25" }, out_res, &err);
    CHECK_TRUE(ok, "同步驱动 async 函数成功");
    CHECK_TRUE(out_res.find("\"sum\":125") != std::string::npos, "Promise 解析出计算结果");

    TEST_BEGIN("JSRuntimeHost / host.crypto 与 host.storage 沙箱 API");
    ok = rt.evaluate(
        "function testHostApi() {\n"
        "    host.storage.set('user_token', 'token_xyz_888');\n"
        "    var t = host.storage.get('user_token');\n"
        "    var h = host.crypto.md5('deck');\n"
        "    var b = host.crypto.base64Encode('hello');\n"
        "    console.log('Test log message');\n"
        "    return { token: t, md5: h, b64: b };\n"
        "}\n", "<host_api>", &err);
    CHECK_TRUE(ok, "定义 host API 测试脚本成功");

    ok = rt.callFunction("testHostApi", {}, out_res, &err);
    CHECK_TRUE(ok, "执行 testHostApi 成功");
    CHECK_TRUE(out_res.find("token_xyz_888") != std::string::npos, "host.storage 读写正常");
    CHECK_TRUE(out_res.find("236c893434b3ef223e61a4efd0090276") != std::string::npos, "host.crypto.md5 计算准确");

    TEST_BEGIN("JSPlugin / 从目录加载全功能三合一插件");
    std::string fake_plugin_dir = "/tmp/test_deckplayer_js_plugin";
    fs::create_directories(fake_plugin_dir);

    // 写入 manifest.json
    std::ofstream(fake_plugin_dir + "/manifest.json") << R"({
        "id": "com.test.universal-source",
        "name": "全能三合一插件",
        "version": "1.2.0",
        "author": "Community",
        "description": "集流媒体、网盘与刮削于一体",
        "capabilities": ["media_stream", "storage", "scraper"],
        "settings": [
            { "key": "server_url", "label": "网盘地址", "type": "string", "default": "https://pan.example.com" }
        ]
    })";

    // 写入 main.js
    std::ofstream(fake_plugin_dir + "/main.js") << R"JS(
        var g_inited_cfg = {};
        function init(cfg) {
            g_inited_cfg = cfg || {};
        }

        // MediaStreamProvider
        async function search(keyword, page) {
            return {
                hasMore: false,
                items: [
                    { id: "anime_01", title: "测试动漫 第一季", type: "video", duration: 1440 }
                ]
            };
        }

        async function loadDetail(id) {
            return {
                id: id,
                title: "测试动漫 详情",
                overview: "这是一部精彩的测试动漫",
                poster: "https://example.com/poster.jpg",
                releaseDate: "2026-10-01",
                episodes: [
                    { id: "ep_1", title: "第 01 话 启程", episodeNumber: 1 },
                    { id: "ep_2", title: "第 02 话 战斗", episodeNumber: 2 }
                ]
            };
        }

        async function loadLinks(episodeId) {
            return {
                streams: [
                    {
                        name: "1080P 高清 (HLS)",
                        url: "https://stream.example.com/" + episodeId + ".m3u8",
                        format: "hls",
                        headers: {
                            "Referer": "https://player.example.com/",
                            "User-Agent": "Muvik/Test"
                        }
                    }
                ],
                subtitles: [
                    { name: "中文 (ASS)", url: "https://sub.example.com/" + episodeId + ".ass", format: "ass" }
                ]
            };
        }

        // StorageProvider
        async function listDirectory(path) {
            return {
                directories: [
                    { name: "电影库", path: "/movies" }
                ],
                files: [
                    { name: "Demo.mkv", path: "/movies/Demo.mkv", size: 104857600, type: "video" }
                ]
            };
        }

        async function resolvePlayUrl(path) {
            return {
                url: "https://pan.example.com/download" + path + "?token=sign123",
                name: "网盘直链",
                format: "mp4",
                expiresAt: 1790000000,
                headers: { "Authorization": "Bearer sign123" }
            };
        }

        // ScraperProvider
        async function scrapeSearch(query, year) {
            return [
                { id: "sc_123", title: query + " 电影", year: year, poster: "https://example.com/p.jpg" }
            ];
        }

        async function scrapeDetail(id) {
            return {
                title: "刮削出的测试影片",
                overview: "刮削简介信息",
                poster_url: "https://example.com/p_detail.jpg",
                release_date: "2026-05-20",
                genres: "科幻 / 动作",
                rating: 8.9,
                runtime_minutes: 128
            };
        }
    )JS";

    auto js_plug = JSPlugin::loadFromDirectory(fake_plugin_dir, &err);
    CHECK_TRUE(js_plug != nullptr, "成功从目录加载 JSPlugin");
    CHECK_EQ(PluginConfigStore::getInstance().get("com.test.universal-source", "server_url"), "https://pan.example.com", "自动同步默认配置项到 PluginConfigStore");
    if (js_plug) {
        CHECK_EQ(js_plug->getId(), "com.test.universal-source", "插件 ID 匹配");
        CHECK_EQ(js_plug->getName(), "全能三合一插件", "插件名称匹配");
        CHECK_TRUE(js_plug->hasMediaStream(), "具备 media_stream 能力");
        CHECK_TRUE(js_plug->hasStorage(), "具备 storage 能力");
        CHECK_TRUE(js_plug->hasScraper(), "具备 scraper 能力");

        // 验证 MediaStreamProvider 接口
        std::vector<MediaItem> search_items;
        bool has_more = false;
        CHECK_TRUE(js_plug->search("动漫", 1, search_items, has_more), "搜索执行成功");
        CHECK_EQ(search_items.size(), 1, "搜索结果数量为 1");
        CHECK_EQ(search_items[0].name, "测试动漫 第一季", "条目标题正确");

        MediaDetail detail;
        CHECK_TRUE(js_plug->loadDetail("anime_01", detail), "详情获取成功");
        CHECK_EQ(detail.episodes.size(), 2, "分集数量为 2");
        CHECK_EQ(detail.episodes[0].title, "第 01 话 启程", "第 1 集标题正确");

        PlayLinksResult links;
        CHECK_TRUE(js_plug->loadLinks("ep_1", links), "播放链接解析成功");
        CHECK_EQ(links.streams.size(), 1, "包含 1 条流");
        CHECK_EQ(links.streams[0].url, "https://stream.example.com/ep_1.m3u8", "流 URL 正确");
        CHECK_EQ(links.streams[0].headers["Referer"], "https://player.example.com/", "防盗链 Referer 注入正确");
        CHECK_EQ(links.subtitles.size(), 1, "包含 1 条字幕");

        // 验证 StorageProvider 接口
        std::vector<MediaItem> dir_items;
        CHECK_TRUE(js_plug->listDirectory("/", dir_items), "列目录成功");
        CHECK_EQ(dir_items.size(), 2, "包含 1 个目录和 1 个文件");
        CHECK_TRUE(dir_items[0].is_directory(), "第 1 项为目录");
        CHECK_TRUE(dir_items[1].is_video(), "第 2 项为视频");

        StreamSource pan_stream;
        CHECK_TRUE(js_plug->resolvePlayUrl("/movies/Demo.mkv", pan_stream), "解析直链成功");
        CHECK_TRUE(pan_stream.url.find("token=sign123") != std::string::npos, "直链包含签名参数");
        CHECK_EQ(pan_stream.headers["Authorization"], "Bearer sign123", "直链包含鉴权头");

        // 验证 ScraperProvider 接口
        std::string scrape_json;
        CHECK_TRUE(js_plug->scrapeSearch("Matrix", 1999, scrape_json), "刮削搜索成功");
        CHECK_TRUE(scrape_json.find("sc_123") != std::string::npos, "刮削候选 ID 正确");

        ScrapeDetail s_detail;
        CHECK_TRUE(js_plug->scrapeDetail("sc_123", s_detail), "刮削详情拉取成功");
        CHECK_EQ(s_detail.title, "刮削出的测试影片", "刮削电影标题正确");
        CHECK_EQ(s_detail.rating, 8.9, "刮削电影评分正确");
    }

    TEST_BEGIN("PluginManager / 统一调度 JS 插件");
    auto& pm = PluginManager::getInstance();
    pm.unloadAll();
    size_t loaded_count = pm.loadAll(fake_plugin_dir);
    CHECK_EQ(loaded_count, 1, "PluginManager 成功扫描并加载 JS 插件");

    auto plugins = pm.getPlugins();
    CHECK_EQ(plugins.size(), 1, "插件列表包含 1 个插件");
    CHECK_TRUE(plugins[0].is_js_plugin, "标记为 is_js_plugin");

    // 经由 PluginManager 调用列目录
    std::vector<MediaItem> pm_items;
    CHECK_TRUE(pm.listVideoSourceItems(fake_plugin_dir, "/", pm_items), "PluginManager 列目录成功");
    CHECK_EQ(pm_items.size(), 2, "PluginManager 列目录结果为 2 项");

    // 经由 PluginManager 解析播放流
    PlayLinksResult pm_links;
    CHECK_TRUE(pm.loadStreamLinks(fake_plugin_dir, "ep_1", pm_links), "PluginManager loadStreamLinks 成功");
    CHECK_EQ(pm_links.streams[0].format, "hls", "流格式为 HLS");

    // 经由 PluginManager 执行完整刮削链路
    ScrapeDetail auto_detail;
    CHECK_TRUE(pm.scrapeForFile(fake_plugin_dir, "Interstellar", 2014, auto_detail), "scrapeForFile 完整链路通过");
    CHECK_EQ(auto_detail.title, "刮削出的测试影片", "刮削出正确影视元数据");

    pm.unloadAll();
    fs::remove_all(fake_plugin_dir);

    TEST_END();
    return TEST_SUMMARY();
}
