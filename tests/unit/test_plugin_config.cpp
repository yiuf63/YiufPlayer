// 单元测试：插件配置存储
#include "plugin/plugin_config_store.hpp"
#include "test_framework.hpp"
#include <cstdlib>

using namespace muvik;

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_plugincfg", 1);
    std::system("rm -rf /tmp/muvik_test_plugincfg");

    auto& store = PluginConfigStore::getInstance();

    TEST_BEGIN("插件配置 / 基础读写");
    CHECK_FALSE(store.hasConfig("webdav"), "初始无配置");
    store.set("webdav", "url", "http://192.168.1.5:5244/dav");
    store.set("webdav", "user", "alice");
    CHECK_TRUE(store.hasConfig("webdav"), "设置后有配置");
    CHECK_EQ(store.get("webdav", "url"),
             std::string("http://192.168.1.5:5244/dav"), "读取 url 正确");
    CHECK_EQ(store.get("webdav", "nonexistent"), std::string(""), "缺失键返回空");

    TEST_BEGIN("插件配置 / 特殊字符");
    store.set("webdav", "pass", "p@ss:word\"带引号\n换行");
    CHECK_EQ(store.get("webdav", "pass"),
             std::string("p@ss:word\"带引号\n换行"), "特殊字符原样保存");

    TEST_BEGIN("插件配置 / JSON 导出");
    {
        std::string j = store.toJson("webdav");
        CHECK_CONTAINS(j, std::string("\"url\""), "JSON 含 url 键");
        CHECK_CONTAINS(j, std::string("\"user\""), "JSON 含 user 键");
        CHECK_EQ(store.toJson("no_such_plugin"), std::string("{}"),
                 "无配置时返回空对象");
    }

    TEST_BEGIN("插件配置 / 持久化往返");
    store.save();
    store.clear("webdav");
    CHECK_FALSE(store.hasConfig("webdav"), "清除后无配置");
    store.load();
    CHECK_TRUE(store.hasConfig("webdav"), "重载后恢复");
    CHECK_EQ(store.get("webdav", "pass"),
             std::string("p@ss:word\"带引号\n换行"), "重载后特殊字符仍正确");

    TEST_BEGIN("插件配置 / 多插件隔离");
    store.set("pluginA", "k", "A");
    store.set("pluginB", "k", "B");
    CHECK_EQ(store.get("pluginA", "k"), std::string("A"), "插件 A 配置独立");
    CHECK_EQ(store.get("pluginB", "k"), std::string("B"), "插件 B 配置独立");

    TEST_BEGIN("插件配置 / 空值删除与边界");
    store.set("pluginA", "k", "");
    CHECK_FALSE(store.hasConfig("pluginA"), "空值删除该键");
    store.set("", "k", "v");
    store.set("p", "", "v");
    CHECK_FALSE(store.hasConfig(""), "空插件 id 被忽略");

    TEST_END();
    return TEST_SUMMARY();
}
