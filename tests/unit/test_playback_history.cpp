// 单元测试：播放历史与「已看完」判定
// 回归保护：短内容曾被错误标记为已看完，导致永远无法续播。

#include "utils/playback_history.hpp"
#include "test_framework.hpp"
#include <cstdlib>

using namespace muvik;

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_history", 1);
    std::system("rm -rf /tmp/muvik_test_history");

    auto& h = PlaybackHistory::getInstance();

    TEST_BEGIN("播放历史 / 基础读写");
    h.updateProgress("/m/a.mkv", 120, 3600, /*sid*/2, /*aid*/3);
    HistoryRecord r;
    CHECK_TRUE(h.getRecord("/m/a.mkv", r), "能读回记录");
    CHECK_EQ(r.position, (int64_t)120, "进度正确");
    CHECK_EQ(r.duration, (int64_t)3600, "时长正确");
    CHECK_EQ(r.sid, 2, "字幕轨持久化");
    CHECK_EQ(r.aid, 3, "音轨持久化");
    CHECK_FALSE(r.completed, "播 120/3600 秒不算看完");

    TEST_BEGIN("播放历史 / 已看完判定（回归）");
    // 短音频：30 秒，只播 1 秒。曾因 duration-30 <= 0 被误判为已看完
    h.updateProgress("/m/short.mp3", 1, 30);
    CHECK_TRUE(h.getRecord("/m/short.mp3", r), "短音频记录存在");
    CHECK_FALSE(r.completed, "30 秒音频播 1 秒【不】算看完");

    h.updateProgress("/m/short.mp3", 10, 30);
    CHECK_TRUE(h.getRecord("/m/short.mp3", r), "中段记录存在");
    CHECK_FALSE(r.completed, "30 秒音频播 10 秒不算看完");

    h.updateProgress("/m/short.mp3", 29, 30);
    CHECK_TRUE(h.getRecord("/m/short.mp3", r), "接近结尾记录存在");
    CHECK_TRUE(r.completed, "30 秒音频播 29 秒算看完");

    TEST_BEGIN("播放历史 / 长视频的结尾判定");
    h.updateProgress("/m/long.mkv", 3590, 3600);   // 距结尾 10 秒
    h.getRecord("/m/long.mkv", r);
    CHECK_TRUE(r.completed, "距结尾 10 秒算看完");

    h.updateProgress("/m/long.mkv", 1700, 3600);   // 约 47%
    h.getRecord("/m/long.mkv", r);
    CHECK_FALSE(r.completed, "播 47% 不算看完");

    h.updateProgress("/m/long.mkv", 3500, 3600);   // 97%
    h.getRecord("/m/long.mkv", r);
    CHECK_TRUE(r.completed, "播 97% 算看完");

    TEST_BEGIN("播放历史 / 边界");
    // 0/0 表示「没有有效进度」：不创建记录（或已有记录时保持原样），
    // 都不应被判成「已看完」。注意不能用 r.completed 断言 ——
    // getRecord 返回 false 时 r 还保留着上一条记录的旧值，
    // 必须先确认返回值再读字段。
    h.updateProgress("/m/zero.mkv", 0, 0);
    HistoryRecord zero_rec;
    if (h.getRecord("/m/zero.mkv", zero_rec)) {
        CHECK_FALSE(zero_rec.completed, "时长为 0 时不判为看完");
    } else {
        CHECK_TRUE(true, "时长为 0 时不创建记录");
    }

    h.updateProgress("", 100, 200);
    CHECK_FALSE(h.getRecord("", r), "空路径不记录");

    // ── 回归：0/0 落盘曾把「已看完」抹成从头开始 ────────────────────
    //
    // 实测事故：MPVPlayerView::selectTrack() 在文件刚加载、time-pos 事件
    // 还没到的时候无条件落盘，把 3|20|...|completed=1 覆盖成 0|0|...|0。
    // 用户看到的是「已看完的片子点开一下，徽标变回 0%，续播点丢失」。
    TEST_BEGIN("播放历史 / 0/0 不覆盖已有进度（回归）");
    h.updateProgress("/m/finished.mkv", 19, 20, /*sid*/1, /*aid*/1);
    h.getRecord("/m/finished.mkv", r);
    CHECK_TRUE(r.completed, "先标记为已看完");

    // 模拟「刚打开文件就自动选轨」：此时 position/duration 都还是 0
    h.updateProgress("/m/finished.mkv", 0, 0, /*sid*/2, /*aid*/3);
    h.getRecord("/m/finished.mkv", r);
    CHECK_TRUE(r.completed, "0/0 落盘后仍保持「已看完」");
    CHECK_EQ(r.position, (int64_t)19, "0/0 落盘不抹掉进度");
    CHECK_EQ(r.duration, (int64_t)20, "0/0 落盘不抹掉时长");
    CHECK_EQ(r.sid, 2, "但轨道选择仍然更新");
    CHECK_EQ(r.aid, 3, "音轨选择也仍然更新");

    // 未播放过的文件收到 0/0 时应当**建一条空进度记录**，只为存住轨道偏好。
    // 这一条必须存在，否则「第一次打开就选字幕轨」的偏好无处可写，
    // 下次打开又变回第一条（第 38 轮修过的「选过不记住」会复发）。
    TEST_BEGIN("播放历史 / 0/0 首次写入只记轨道、不记进度");
    h.updateProgress("/m/never-played.mkv", 0, 0, /*sid*/3, /*aid*/2);
    HistoryRecord np;
    CHECK_TRUE(h.getRecord("/m/never-played.mkv", np),
               "没有旧记录时 0/0 也要建条目（供轨道偏好落地）");
    if (h.getRecord("/m/never-played.mkv", np)) {
        CHECK_EQ(np.position, (int64_t)0, "进度保持 0");
        CHECK_EQ(np.duration, (int64_t)0, "时长保持 0");
        CHECK_FALSE(np.completed, "不会被误解为已看完");
        CHECK_EQ(np.sid, 3, "字幕轨偏好被记住");
        CHECK_EQ(np.aid, 2, "音轨偏好被记住");
    }

    TEST_BEGIN("播放历史 / 0/0 不抹掉续播点");
    h.updateProgress("/m/half.mkv", 600, 3600);
    h.updateProgress("/m/half.mkv", 0, 0);
    h.getRecord("/m/half.mkv", r);
    CHECK_EQ(r.position, (int64_t)600, "续播点保留");
    CHECK_FALSE(r.completed, "仍未看完");

    TEST_BEGIN("播放历史 / 正常进度仍可覆盖");
    h.updateProgress("/m/finished.mkv", 100, 3600, 1, 1);
    h.getRecord("/m/finished.mkv", r);
    CHECK_EQ(r.position, (int64_t)100, "带进度的写入正常覆盖");
    CHECK_FALSE(r.completed, "重看时 completed 被正确清掉");

    TEST_END();
    return TEST_SUMMARY();
}
