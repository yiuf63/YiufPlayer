// 单元测试：元数据存储（持久化、转义、边界）
// 重点覆盖曾真实发生的 bug：简介含换行导致记录被拆行而丢失。

#include "model/metadata_store.hpp"
#include "test_framework.hpp"
#include <cstdlib>

using namespace deck_player;

namespace {

// 每次运行使用独立配置目录，避免污染用户数据
void isolateConfig(const char* dir) {
    setenv("XDG_CONFIG_HOME", dir, 1);
}

} // namespace

int main() {
    isolateConfig("/tmp/deckplayer_test_metadata");
    std::system("rm -rf /tmp/deckplayer_test_metadata");

    auto& store = MetadataStore::getInstance();

    TEST_BEGIN("元数据存储 / 基础读写");
    CHECK_EQ(store.count(), (size_t)0, "初始为空");

    MediaMetadata m;
    m.path = "/movies/The.Matrix.1999.mkv";
    m.title = "黑客帝国";
    m.overview = "程序员发现自己所处世界是虚拟现实。";
    m.poster_url = "https://example.invalid/p.jpg";
    m.release_date = "1999-03-31";
    m.genres = "动作,科幻";
    m.rating = 8.7;
    m.runtime_minutes = 136;
    m.scraper_id = "tmdb";
    store.put(m);

    CHECK_EQ(store.count(), (size_t)1, "写入后计数为 1");
    CHECK_TRUE(store.has("/movies/The.Matrix.1999.mkv"), "has 返回 true");

    auto got = store.get("/movies/The.Matrix.1999.mkv");
    CHECK_TRUE(got.has_value(), "可以读回");
    if (got) {
        CHECK_EQ(got->title, std::string("黑客帝国"), "中文标题正确");
        CHECK_EQ(got->genres, std::string("动作,科幻"), "类型字段正确");
        CHECK_TRUE(got->rating > 8.69 && got->rating < 8.71, "浮点评分精度正确");
        CHECK_EQ(got->runtime_minutes, 136, "片长正确");
        CHECK_TRUE(got->updated_at > 0, "自动填入更新时间");
    }
    CHECK_FALSE(store.get("/nonexistent").has_value(), "不存在的路径返回空");

    TEST_BEGIN("元数据存储 / 换行转义（回归测试）");
    {
        // 这正是之前导致整条记录丢失的场景
        MediaMetadata nl;
        nl.path = "/movies/newline.mkv";
        nl.title = "多行简介测试";
        nl.overview = "第一行\n第二行\n第三行";
        store.put(nl);
        store.save();
        store.load();

        auto r = store.get("/movies/newline.mkv");
        CHECK_TRUE(r.has_value(), "含换行的记录在重载后仍存在");
        if (r) {
            CHECK_EQ(r->overview, std::string("第一行\n第二行\n第三行"),
                     "换行完整保留");
        }
    }

    TEST_BEGIN("元数据存储 / 特殊字符");
    {
        MediaMetadata sp;
        sp.path = "/movies/special.mkv";
        sp.title = "反斜杠\\与分隔符\x1f与回车\r";
        sp.overview = "字面量 \\n 不应被误解码";
        store.put(sp);
        store.save();
        store.load();

        auto r = store.get("/movies/special.mkv");
        CHECK_TRUE(r.has_value(), "特殊字符记录可读回");
        if (r) {
            CHECK_EQ(r->title, std::string("反斜杠\\与分隔符\x1f与回车\r"),
                     "反斜杠/分隔符/回车正确还原");
            CHECK_EQ(r->overview, std::string("字面量 \\n 不应被误解码"),
                     "字面反斜杠 n 未被误解码");
        }
    }

    TEST_BEGIN("元数据存储 / 多条记录隔离");
    {
        for (int i = 0; i < 5; ++i) {
            MediaMetadata t;
            t.path = "/m/" + std::to_string(i) + ".mkv";
            t.title = "影片" + std::to_string(i);
            store.put(t);
        }
        store.save();
        store.load();

        bool all_ok = true;
        for (int i = 0; i < 5; ++i) {
            auto r = store.get("/m/" + std::to_string(i) + ".mkv");
            if (!r || r->title != "影片" + std::to_string(i)) all_ok = false;
        }
        CHECK_TRUE(all_ok, "多条记录内容互不干扰");
    }

    TEST_BEGIN("元数据存储 / 删除与覆盖");
    {
        store.remove("/m/0.mkv");
        CHECK_FALSE(store.has("/m/0.mkv"), "删除后不存在");

        // 覆盖写
        MediaMetadata upd;
        upd.path = "/m/1.mkv";
        upd.title = "新标题";
        store.put(upd);
        auto r = store.get("/m/1.mkv");
        CHECK_TRUE(r && r->title == "新标题", "覆盖写入生效");
    }

    TEST_BEGIN("元数据存储 / 空路径防御");
    {
        size_t before = store.count();
        MediaMetadata empty;
        empty.path = "";
        empty.title = "x";
        store.put(empty);
        CHECK_EQ(store.count(), before, "空路径被忽略");
    }

    TEST_END();
    return TEST_SUMMARY();
}
