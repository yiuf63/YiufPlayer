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
    TEST_SUITE();
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

    // 经由 PluginManager 检索视频源
    CHECK_TRUE(pm.canSearch(fake_plugin_dir), "PluginManager 检测到搜索能力");
    std::vector<MediaItem> pm_search_items;
    bool pm_has_more = false;
    CHECK_TRUE(pm.searchVideoSource(fake_plugin_dir, "动漫", 1, pm_search_items, pm_has_more), "PluginManager 搜索执行成功");
    CHECK_EQ(pm_search_items.size(), 1, "PluginManager 搜索返回 1 项");
    CHECK_EQ(pm_search_items[0].name, "测试动漫 第一季", "搜索结果标题一致");

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

    // ── 回归：文档写法的 manifest 必须真的生效 ────────────────────────
    //
    // 实测事故：docs/PLUGIN-DEV.md 与 examples/tmdb_scraper_plugin 用的是
    //   "capabilities": ["video_source"] 与 "configSchema": [...]
    // 而代码只认 media_stream/storage 与 settings。于是「完全照文档写」的插件
    // 既不出现在「文件 → 插件视频源」里，配置项也一条都不显示 ——
    // 反而「不写 capabilities」靠自动探测能工作。文档与实现直接冲突。
    //
    // 下面用文档里的写法造插件，断言两条别名都生效。
    TEST_BEGIN("JSPlugin / 文档写法（video_source + configSchema）别名");
    std::string doc_plugin_dir = "/tmp/test_deckplayer_doc_style_plugin";
    fs::remove_all(doc_plugin_dir);
    fs::create_directories(doc_plugin_dir);

    std::ofstream(doc_plugin_dir + "/manifest.json") << R"({
        "id": "com.test.doc-style",
        "name": "文档写法插件",
        "version": "1.0.0",
        "capabilities": ["video_source"],
        "configSchema": [
            { "key": "api_key", "title": "接口密钥", "type": "string", "default": "doc_default" },
            { "key": "lang",    "title": "语言",     "type": "string", "default": "zh-CN" }
        ]
    })";
    std::ofstream(doc_plugin_dir + "/main.js") << R"JS(
        function init(cfg) { return "ok"; }
        async function listDirectory(path) {
            return { directories: [], files: [{ name: "a.mkv", path: "/a.mkv", type: "video" }] };
        }
    )JS";

    auto doc_plug = JSPlugin::loadFromDirectory(doc_plugin_dir, &err);
    CHECK_TRUE(doc_plug != nullptr, "文档写法的插件能加载");
    if (doc_plug) {
        // "video_source" 应被翻译成 media_stream + storage，
        // 这样 PluginManager 才会把它当成视频源（has_video_source）。
        CHECK_TRUE(doc_plug->hasMediaStream(), "video_source 别名 → media_stream");
        CHECK_TRUE(doc_plug->hasStorage(), "video_source 别名 → storage");

        // configSchema 应被解析成设置项，且 label 取自 title
        const auto& st = doc_plug->getSettings();
        CHECK_EQ(st.size(), (size_t)2, "configSchema 解析出 2 个设置项");
        if (st.size() == 2) {
            CHECK_EQ(st[0].key, "api_key", "设置项 key 正确");
            CHECK_EQ(st[0].label, "接口密钥", "label 回退到 title（文档写法）");
            CHECK_EQ(st[1].key, "lang", "第二个设置项 key 正确");
        }
        // 默认值应写入 PluginConfigStore，init 才能拿到
        CHECK_EQ(PluginConfigStore::getInstance().get("com.test.doc-style", "api_key"),
                 "doc_default", "默认值写入配置库");
    }
    fs::remove_all(doc_plugin_dir);

    // ── 回归：onLinkExpired 降级路径不得自死锁 ──────────────────────
    //
    // 早先 onLinkExpired 持有 runtime_mutex_ 后直接调 resolvePlayUrl()，
    // 而后者也锁同一把**非递归** mutex —— 同一线程二次加锁 = 永久挂死。
    // 触发条件很常见：插件实现了 resolvePlayUrl 但没实现 onLinkExpired
    // （绝大多数网盘插件就是这样），一旦接上「直链过期自动刷新」就会踩到。
    //
    // 这里直接调用它。修复前这个测试会挂住不返回；修复后正常返回 true。
    TEST_BEGIN("JSPlugin / onLinkExpired 降级路径不自死锁（回归）");
    std::string dl_dir = "/tmp/test_deckplayer_onlinkexpired";
    fs::remove_all(dl_dir);
    fs::create_directories(dl_dir);
    std::ofstream(dl_dir + "/manifest.json") << R"({
        "id": "com.test.onlinkexpired",
        "name": "直链刷新降级测试",
        "version": "1.0.0",
        "capabilities": ["storage"]
    })";
    // 只实现 resolvePlayUrl，故意不实现 onLinkExpired
    std::ofstream(dl_dir + "/main.js") << R"JS(
        function init(cfg) { return "ok"; }
        async function resolvePlayUrl(path) {
            return { url: "https://cdn.example.com" + path + "?sign=fresh" };
        }
    )JS";

    auto dl_plug = JSPlugin::loadFromDirectory(dl_dir, &err);
    CHECK_TRUE(dl_plug != nullptr, "降级测试插件加载成功");
    if (dl_plug) {
        StreamSource expired;
        // 若仍持有锁调公开 resolvePlayUrl，这一行会卡死（unit test 会超时）
        bool ok = dl_plug->onLinkExpired("/movies/a.mkv", expired);
        CHECK_TRUE(ok, "onLinkExpired 降级到 resolvePlayUrl 成功返回");
        CHECK_TRUE(expired.url.find("sign=fresh") != std::string::npos,
                   "降级路径拿到了新直链");
    }
    fs::remove_all(dl_dir);

    TEST_END();
    return TEST_SUMMARY();
}
