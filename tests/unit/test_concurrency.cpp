// 并发安全测试：模拟后台扫描与 UI 线程同时访问媒体库
//
// 背景：MediaLibrary 被后台扫描线程写、被 UI 线程读，之前此路径未经验证。
// 本测试用多线程高并发读写 + 数据校验，配合 TSAN/ASAN 可以发现竞态。

#include "model/media_library.hpp"
#include "model/metadata_store.hpp"
#include "utils/playback_history.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>

using namespace muvik;
namespace fs = std::filesystem;

namespace {
void writeFile(const std::string& p) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream(p) << "x";
}
}

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_concurrency", 1);
    std::system("rm -rf /tmp/muvik_test_concurrency /tmp/muvik_test_conc_media");

    const std::string root = "/tmp/muvik_test_conc_media";
    for (int i = 0; i < 40; ++i) {
        writeFile(root + "/v/movie" + std::to_string(i) + ".mp4");
    }

    auto& lib = MediaLibrary::getInstance();
    lib.addFolder(root + "/v", "并发测试");

    TEST_BEGIN("并发 / 扫描与读取并行");

    std::atomic<bool> stop{false};
    std::atomic<int> read_ops{0};
    std::atomic<int> bad_reads{0};

    // 后台：反复重建
    std::thread scanner([&]() {
        for (int i = 0; i < 3; ++i) {
            lib.rebuild();
        }
        stop.store(true);
    });

    // 多个读者：不断读取快照，校验数据一致性（不能出现半成品）
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            while (!stop.load()) {
                // 注意：必须在【单次】getItems() 返回的快照内做全部校验。
                // 跨两次调用比较（如先 getItems 再 getVideos）本身就不成立，
                // 因为两次调用之间后台扫描可能已替换数据集，
                // 那是调用方的用法问题，不是数据竞争。
                auto items = lib.getItems();
                for (const auto& it : items) {
                    if (it.path.empty()) bad_reads.fetch_add(1);
                    if (!it.is_video() && !it.is_audio() && !it.is_directory())
                        bad_reads.fetch_add(1);
                }
                read_ops.fetch_add(1);
            }
        });
    }

    scanner.join();
    for (auto& t : readers) t.join();

    CHECK_TRUE(true, "并发扫描与读取未崩溃");
    CHECK_TRUE(bad_reads.load() == 0, "未观察到不一致的快照");
    CHECK_TRUE(read_ops.load() > 0, "读者确实执行了读取");
    std::printf("    读取次数=%d 不一致=%d\n",
                read_ops.load(), bad_reads.load());

    TEST_BEGIN("并发 / 元数据并发写入");
    {
        std::atomic<int> writes{0};
        std::vector<std::thread> writers;
        for (int w = 0; w < 4; ++w) {
            writers.emplace_back([&, w]() {
                for (int i = 0; i < 50; ++i) {
                    MediaMetadata m;
                    m.path = "/c/" + std::to_string(w) + "/" + std::to_string(i) + ".mkv";
                    m.title = "t" + std::to_string(i);
                    MetadataStore::getInstance().put(m);
                    writes.fetch_add(1);
                }
            });
        }
        for (auto& t : writers) t.join();
        CHECK_EQ(writes.load(), 200, "全部写入完成");
        CHECK_EQ(MetadataStore::getInstance().count(), (size_t)200,
                 "记录数正确，无丢失");
    }

    TEST_BEGIN("并发 / 保存与读取交错");
    {
        std::atomic<bool> done{false};
        std::thread saver([&]() {
            for (int i = 0; i < 5; ++i) {
                MetadataStore::getInstance().save();
            }
            done.store(true);
        });
        std::thread reader([&]() {
            while (!done.load()) {
                (void)MetadataStore::getInstance().getAll();
            }
        });
        saver.join();
        reader.join();
        CHECK_TRUE(true, "保存与读取交错执行未崩溃");
    }

    TEST_BEGIN("并发 / 播放历史并发读写（冒烟）");
    {
        // PlaybackHistory::updateProgress 由主循环调用，
        // getRecord / getRecentList 由 UI 线程调用，两者会同时访问内部的
        // unordered_map。历史上这里没有任何锁（第 32 轮补上）。
        //
        // 注意：这是一条【冒烟测试】，不是真正的竞态检测。
        // 实测把锁去掉后，即使 5 万次并发写入也不会让测试失败——
        // unordered_map 的数据竞争往往只是让个别条目丢失或指针错乱，
        // 不一定会当场崩溃。真正能发现竞态的是 ThreadSanitizer，
        // 但本项目在 macOS 上跑不起来（见 AGENTS.md「未验证」表）。
        // 保留此用例的价值仅在于：多线程路径至少被执行过一次、不崩溃。
        auto& hist = PlaybackHistory::getInstance();
        std::atomic<bool> stop{false};
        std::atomic<int> writes{0}, reads{0};

        std::thread writer([&]() {
            for (int i = 0; i < 2000; ++i) {
                hist.updateProgress("/m/x" + std::to_string(i % 50) + ".mkv",
                                    i, 1000);
                writes++;
            }
        });
        std::thread reader([&]() {
            HistoryRecord r;
            while (!stop.load()) {
                hist.getRecord("/m/x1.mkv", r);
                auto list = hist.getRecentList(10);
                (void)list;
                reads++;
            }
        });

        writer.join();
        stop.store(true);
        reader.join();

        CHECK_TRUE(writes.load() > 0, "写入有执行");
        CHECK_TRUE(reads.load() > 0, "读取有执行");
        // 只能验证「跑完没崩」，不能证明没有数据竞争
        CHECK_TRUE(true, "并发读写未崩溃（不代表无竞态）");
    }

    TEST_END();
    std::system("rm -rf /tmp/muvik_test_conc_media "
                "/tmp/muvik_test_concurrency");
    return TEST_SUMMARY();
}
