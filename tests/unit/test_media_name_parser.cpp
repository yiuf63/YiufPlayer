// 单元测试：文件名 -> 刮削搜索词 的解析逻辑
// 覆盖真实模块 utils/media_name_parser，不再复刻实现。

#include "utils/media_name_parser.hpp"
#include "test_framework.hpp"

using namespace muvik;

namespace {

struct Case {
    const char* filename;
    const char* expect_query;
    int64_t expect_year;
};

void runCase(const Case& c) {
    std::string q;
    int64_t y = 0;
    MediaNameParser::parse(c.filename, q, y);

    ++testing::g_checks;
    if (q == c.expect_query && y == c.expect_year) {
        std::printf("    \033[32mPASS\033[0m %-46s -> q='%s' y=%lld\n",
                    c.filename, q.c_str(), (long long)y);
    } else {
        std::printf("    \033[31mFAIL\033[0m %-46s -> q='%s' y=%lld\n",
                    c.filename, q.c_str(), (long long)y);
        std::printf("         期望 q='%s' y=%lld\n",
                    c.expect_query, (long long)c.expect_year);
        ++testing::g_failures;
    }
}

} // namespace

int main() {
    TEST_SUITE();
    TEST_BEGIN("文件名解析 / 常见影视命名");

    const Case cases[] = {
        // 标准电影命名
        {"The.Matrix.1999.1080p.BluRay.x264.mkv", "The Matrix", 1999},
        {"Inception.2010.720p.BRRip.mkv",          "Inception", 2010},
        {"Interstellar (2014) remux.mkv",          "Interstellar", 2014},
        {"My_Movie_2020_WEB-DL.mkv",               "My Movie", 2020},

        // 已经清洗过的名字
        {"Simple Movie.mkv",            "Simple Movie", 0},
        {"No.Extension.In.Name.2015.mp4", "No Extension In Name", 2015},

        // 中文片名
        {"纪录片.2019.4K.mkv", "纪录片", 2019},
        {"流浪地球.2019.1080p.mkv", "流浪地球", 2019},

        // 无年份
        {"[Group] Some Anime - 01 [1080p].mkv", "Group Some Anime 01", 0},

        // 边界：无扩展名
        {"SomeShowWithoutExtension.2018", "SomeShowWithoutExtension", 2018},
    };
    for (const auto& c : cases) runCase(c);

    TEST_BEGIN("文件名解析 / 边界与异常输入");

    {
        std::string q;
        int64_t y = 0;
        MediaNameParser::parse("", q, y);
        CHECK_TRUE(q.empty(), "空文件名返回空查询");
        CHECK_EQ(y, (int64_t)0, "空文件名年份为 0");
    }
    {
        // 长度不足 4 位数字不应被当作年份
        std::string q;
        int64_t y = 0;
        MediaNameParser::parse("Movie.123.mkv", q, y);
        CHECK_EQ(y, (int64_t)0, "3 位数字不被识别为年份");
    }
    {
        // 超出合理年份范围
        std::string q;
        int64_t y = 0;
        MediaNameParser::parse("Weird.1800.mkv", q, y);
        CHECK_EQ(y, (int64_t)0, "1800 年超出范围被忽略");
    }
    {
        // 长数字串中的 4 位不应被误取（如 12345 里取 2345）
        std::string q;
        int64_t y = 0;
        MediaNameParser::parse("Code.1234567.mkv", q, y);
        CHECK_EQ(y, (int64_t)0, "长数字串内不误取年份");
    }
    {
        // 便捷接口一致性
        CHECK_EQ(MediaNameParser::queryFromFilename("A.B.2001.mkv"),
                 std::string("A B"), "queryFromFilename 与 parse 一致");
    }

    TEST_END();
    return TEST_SUMMARY();
}
