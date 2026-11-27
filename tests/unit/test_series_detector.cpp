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

    // ── 缺集时保留真实季集号（fixture: mixed-directory-with-missing-episodes）──
    //
    // 实测缺陷：同目录放 S01E05、S01E08 和一个赛车录屏时，
    // 界面用「下标 + 1」显示集号，E08 的下标是 1 → 写成「第 2 集」。
    // 用户会以为文件放错了。
    TEST_BEGIN("缺集：episode_numbers 保存真实集号，不是下标");
    {
        std::vector<MediaItem> items;
        auto add = [&items](const std::string& folder, const std::string& name) {
            MediaItem it;
            it.name = name;
            it.path = "/fixture/" + folder + "/" + name;
            it.type = MediaType::Video;
            items.push_back(it);
        };
        add("Harbor", "Harbor.S01E05.mkv");
        add("Harbor", "Harbor.S01E08.mkv");
        add("Harbor", "RacingCapture.mp4");

        auto result = SeriesDetector::detect(items);
        CHECK_EQ(result.series.size(), (size_t)1, "识别出 1 部剧集");

        if (!result.series.empty()) {
            const auto& sg = result.series[0];
            CHECK_EQ(sg.episodes.size(), (size_t)3, "两集 + 一条录屏同目录（录屏仍被归入本目录）");
            // 关键：真实集号 5 和 8，而不是 1 和 2
            CHECK_EQ(sg.episodeOf(0), 5, "第 1 条的真实集号是 5");
            CHECK_EQ(sg.episodeOf(1), 8, "第 2 条的真实集号是 8（不是下标+1=2）");
            CHECK_EQ(sg.seasonOf(0), 1, "季号为 1");

            // 界面文案应写「第 8 集」
            const int real_ep = sg.episodeOf(1);
            const int shown = (real_ep > 0) ? real_ep : 2;
            CHECK_EQ(shown, 8, "续播文案显示第 8 集");
        }
    }

    TEST_BEGIN("未编号素材：episodeOf 返回 0，界面应退回下标");
    {
        std::vector<MediaItem> items;
        auto add = [&items](const std::string& name) {
            MediaItem it;
            it.name = name;
            it.path = "/fixture/Show/" + name;
            it.type = MediaType::Video;
            items.push_back(it);
        };
        add("Show.S01E01.mkv");
        add("Show.S01E02.mkv");
        add("Show OP.mkv");          // 无编号

        auto result = SeriesDetector::detect(items);
        if (!result.series.empty()) {
            const auto& sg = result.series[0];
            // 找到无编号的那条，它的 episode 必须是 0
            bool found_unnumbered = false;
            for (size_t i = 0; i < sg.episodes.size(); ++i) {
                if (sg.episodes[i].name == "Show OP.mkv") {
                    found_unnumbered = true;
                    CHECK_EQ(sg.episodeOf(i), 0, "未编号素材集号为 0");
                }
            }
            CHECK_TRUE(found_unnumbered, "未编号素材仍在剧集列表里可访问");
        }
    }

    TEST_END();
}
