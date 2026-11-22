// 单元测试：SeriesDetector 剧集启发式识别与聚合
#include "model/series_detector.hpp"
#include "test_framework.hpp"

using namespace muvik;

int main() {
    TEST_SUITE();
    TEST_BEGIN("剧集识别与聚合 / SeriesDetector");

    // 1. 单集编号提取测试
    {
        int s = 0, ep = 0;
        std::string title;

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("第01话 传说中的斗士！ 炎之火神兽登场.mkv", s, ep, title), "识别中文分集 第01话");
        CHECK_EQ(ep, 1, "集号为 1");

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("第06话 传说中的五斗士vs新的斗士！.mkv", s, ep, title), "识别中文分集 第06话");
        CHECK_EQ(ep, 6, "集号为 6");

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("Breaking.Bad.S01E05.Gray.Matter.mkv", s, ep, title), "识别欧美分集 S01E05");
        CHECK_EQ(s, 1, "季号为 1");
        CHECK_EQ(ep, 5, "集号为 5");

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("Attack.on.Titan.4x12.mkv", s, ep, title), "识别分集 4x12");
        CHECK_EQ(s, 4, "季号为 4");
        CHECK_EQ(ep, 12, "集号为 12");

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("[VCB-Studio] Steins;Gate [08][1080p].mkv", s, ep, title), "识别压制分集 [08]");
        CHECK_EQ(ep, 8, "集号为 8");

        CHECK_TRUE(SeriesDetector::extractEpisodeInfo("02. Title.mp4", s, ep, title), "识别前缀序号 02.");
        CHECK_EQ(ep, 2, "集号为 2");

        // 电影或非剧集不应匹配成单集
        CHECK_TRUE(!SeriesDetector::extractEpisodeInfo("Interstellar.2014.1080p.mkv", s, ep, title), "电影不匹配分集");
        CHECK_TRUE(!SeriesDetector::extractEpisodeInfo("数码宝贝4：无限地带OP.mkv", s, ep, title), "OP/ED 不匹配分集");
    }

    // 2. 文件夹剧集聚合测试
    {
        std::vector<MediaItem> items;

        // 构造数码宝贝目录下的文件
        MediaItem ep1;
        ep1.name = "第01话 传说中的斗士！ 炎之火神兽登场.mkv";
        ep1.path = "/mnt/SDCARD/Videos/S04 数码宝贝无限地带（日国粤）/" + ep1.name;
        ep1.type = MediaType::Video;
        items.push_back(ep1);

        MediaItem ep2;
        ep2.name = "第02话 光之野狼兽 地下迷宫之战！.mkv";
        ep2.path = "/mnt/SDCARD/Videos/S04 数码宝贝无限地带（日国粤）/" + ep2.name;
        ep2.type = MediaType::Video;
        items.push_back(ep2);

        MediaItem op;
        op.name = "数码宝贝4：无限地带OP.mkv";
        op.path = "/mnt/SDCARD/Videos/S04 数码宝贝无限地带（日国粤）/" + op.name;
        op.type = MediaType::Video;
        items.push_back(op);

        // 构造一部独立电影
        MediaItem movie;
        movie.name = "Inception.2010.mkv";
        movie.path = "/mnt/SDCARD/Videos/Movies/" + movie.name;
        movie.type = MediaType::Video;
        items.push_back(movie);

        auto result = SeriesDetector::detect(items);

        // 断言：应当识别出 1 部剧集，1 部独立电影
        CHECK_EQ(result.series.size(), (size_t)1, "识别出 1 部剧集");
        CHECK_EQ(result.standalone_videos.size(), (size_t)1, "识别出 1 部独立电影");

        if (!result.series.empty()) {
            const auto& sg = result.series[0];
            CHECK_EQ(sg.title, "数码宝贝无限地带", "清洗后的剧名为 数码宝贝无限地带");
            CHECK_EQ(sg.episodes.size(), (size_t)3, "包含 3 个分集（含花絮）");
            CHECK_EQ(sg.episodes[0].name, ep1.name, "01 在前");
            CHECK_EQ(sg.episodes[1].name, ep2.name, "02 在后");
            CHECK_EQ(sg.episodes[2].name, op.name, "OP 花絮在末尾");
        }
    }

    TEST_END();
}
