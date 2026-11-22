// 单元测试：网络存储源与 WebDAV 客户端
#include "model/network_source.hpp"
#include "utils/webdav_client.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>

using namespace muvik;

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_netsrc", 1);
    std::filesystem::remove_all("/tmp/muvik_test_netsrc");

    auto& store = NetworkSourceStore::getInstance();
    store.clear();

    TEST_BEGIN("网络源模型 / 基础 CRUD");
    CHECK_EQ(store.getSources().size(), 0, "初始网络源列表为空");

    NetworkSource src1;
    src1.id = "net_test_1";
    src1.name = "我的群晖 NAS";
    src1.type = NetworkSourceType::WebDAV;
    src1.url = "http://192.168.1.100:5005";
    src1.username = "admin";
    src1.password = "secret123";
    src1.root_path = "/";

    CHECK_TRUE(store.addSource(src1), "添加网络源成功");
    CHECK_EQ(store.getSources().size(), 1, "源数量为 1");

    NetworkSource found;
    CHECK_TRUE(store.getSource("net_test_1", found), "查找源成功");
    CHECK_EQ(found.name, std::string("我的群晖 NAS"), "源名称一致");
    CHECK_EQ(found.url, std::string("http://192.168.1.100:5005"), "URL 一致");
    CHECK_EQ(found.typeString(), std::string("WebDAV"), "类型正确");

    // 更新源
    found.name = "群晖 NAS (修改)";
    CHECK_TRUE(store.updateSource(found), "更新源成功");

    NetworkSource found2;
    CHECK_TRUE(store.getSource("net_test_1", found2), "重新获取源成功");
    CHECK_EQ(found2.name, std::string("群晖 NAS (修改)"), "更新生效");

    TEST_BEGIN("网络源模型 / 持久化");
    store.save();
    store.clearInMemory();
    CHECK_EQ(store.getSources().size(), 0, "内存清空后源数量为 0");
    store.load();
    CHECK_EQ(store.getSources().size(), 1, "重新加载后恢复源数据");
    CHECK_TRUE(store.getSource("net_test_1", found), "恢复后查找到指定源");
    CHECK_EQ(found.name, std::string("群晖 NAS (修改)"), "持久化数据一致");

    // 删除源
    CHECK_TRUE(store.removeSource("net_test_1"), "删除源成功");
    CHECK_EQ(store.getSources().size(), 0, "删除后数量为 0");

    TEST_BEGIN("WebDAVClient / URL 编解码");
    std::string orig = "电影 2024 [4K]/The Matrix.mkv";
    std::string encoded = WebDAVClient::urlEncode(orig);
    CHECK_CONTAINS(encoded, "%20", "空格编码为 %20");
    std::string decoded = WebDAVClient::urlDecode(encoded);
    CHECK_EQ(decoded, orig, "URL 编解码可逆");

    TEST_BEGIN("WebDAVClient / 播放 URL 构建");
    NetworkSource dav_src;
    dav_src.url = "http://192.168.1.50:5244/dav";
    dav_src.username = "alice";
    dav_src.password = "p@ss";

    std::string play_url = WebDAVClient::buildPlayUrl(dav_src, "/dav/movies/film.mp4");
    CHECK_CONTAINS(play_url, "http://alice:p@ss@192.168.1.50:5244/dav/movies/film.mp4",
                   "播放 URL 自动注入账号密码");

    // 无账号时的 URL
    NetworkSource anon_src;
    anon_src.url = "http://192.168.1.50:8080";
    std::string anon_url = WebDAVClient::buildPlayUrl(anon_src, "video.mkv");
    CHECK_EQ(anon_url, std::string("http://192.168.1.50:8080/video.mkv"),
             "无账号时生成干净的 URL");

    TEST_BEGIN("WebDAVClient / 真实测试服务（如果可用）");
    const char* dav_env = std::getenv("MUVIK_TEST_DAV");
    if (dav_env && dav_env[0]) {
        NetworkSource live_src;
        live_src.id = "live_test";
        live_src.name = "Live Test";
        live_src.url = dav_env;
        live_src.username = "u";
        live_src.password = "p";

        std::string err;
        bool ok = WebDAVClient::testConnection(live_src, err);
        CHECK_TRUE(ok, "连接测试服务器成功");

        std::vector<MediaItem> items;
        bool list_ok = WebDAVClient::listDirectory(live_src, "/", items, err);
        CHECK_TRUE(list_ok, "读取根目录成功");
        CHECK_FALSE(items.empty(), "读取到目录或文件");

        // 测试错误凭证
        live_src.password = "wrong";
        bool bad_ok = WebDAVClient::testConnection(live_src, err);
        CHECK_FALSE(bad_ok, "错误密码应当连接失败");
        CHECK_CONTAINS(err, "401", "错误信息包含 401");
    }

    TEST_END();
    return TEST_SUMMARY();
}
