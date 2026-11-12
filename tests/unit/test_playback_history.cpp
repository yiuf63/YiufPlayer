// 单元测试：播放历史与「已看完」判定
// 回归保护：短内容曾被错误标记为已看完，导致永远无法续播。

#include "utils/playback_history.hpp"
#include "test_framework.hpp"
#include <cstdlib>

using namespace muvik;

int main() {
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
    h.updateProgress("/m/zero.mkv", 0, 0);
    h.getRecord("/m/zero.mkv", r);
    CHECK_FALSE(r.completed, "时长为 0 时不判为看完");

    h.updateProgress("", 100, 200);
    CHECK_FALSE(h.getRecord("", r), "空路径不记录");

    TEST_END();
    return TEST_SUMMARY();
}
