// 单元测试：界面字号档位
//
// 这条功能最容易「看起来做了、其实没生效」：
// 界面确实变了几个像素，但用户感知不到；或者只变了一部分
//（只在列表变了、侧栏没变，就是实测踩到的现象）。
//
// 测试重点不是「函数能跑」，而是：
//   1. 档位确实改变了 ui::font 的取值（而不只是存了个数字）
//   2. 持久化能读回（否则重启就丢）
//   3. 上下限被夹住（避免用户把界面调成不可用）
//   4. currentIndex 在浮点误差下仍能对上档位

#include "utils/font_scale.hpp"
#include "ui/design.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main() {
    TEST_SUITE();

    const std::string cfg = "/tmp/muvik_test_fontscale";
    std::system(("rm -rf " + cfg).c_str());
    fs::create_directories(cfg);
    setenv("XDG_CONFIG_HOME", cfg.c_str(), 1);
    unsetenv("MUVIK_FONT_SCALE");

    TEST_BEGIN("字号档位：三档，覆盖小到大的可用范围");
    {
        const auto& opts = muvik::FontScale::options();
        CHECK_EQ(opts.size(), (size_t)3, "三档");
        CHECK_TRUE(opts[0].scale < opts[1].scale, "档位递增");
        CHECK_TRUE(opts[1].scale < opts[2].scale, "档位递增");
        // 下限不能太小：caption 12.5 × 0.85 ≈ 10.6，再小就糊了
        CHECK_TRUE(opts[0].scale >= 0.8f, "最小档不低于 0.8");
        // 上限：再大则列表一行放不下两个中文词
        CHECK_TRUE(opts[2].scale <= 1.3f, "最大档不超过 1.3");
    }

    TEST_BEGIN("设置档位会真正改变 ui::font 取值");
    {
        muvik::FontScale::set(0.85f, false);
        const float small = muvik::ui::font::body();
        muvik::FontScale::set(1.20f, false);
        const float large = muvik::ui::font::body();

        CHECK_TRUE(large > small, "大字档的 body 确实更大");
        // 差值要明显：0.85 → 1.20 是 41%
        CHECK_TRUE(large > small * 1.3f, "两档差距超过 30%");
        CHECK_TRUE(muvik::ui::font::display() > muvik::ui::font::caption(),
                   "五级字号仍保持相对大小");
    }

    TEST_BEGIN("上下限被夹住，不会把界面调坏");
    {
        muvik::FontScale::set(0.01f, false);
        CHECK_TRUE(muvik::FontScale::current() >= 0.8f, "低于下限被夹到 0.8 以上");

        muvik::FontScale::set(99.0f, false);
        CHECK_TRUE(muvik::FontScale::current() <= 1.3f, "高于上限被夹到 1.3 以内");
    }

    TEST_BEGIN("持久化：保存后能读回同一档");
    {
        muvik::FontScale::set(1.20f, true);
        // 模拟重启：把内存里的值改掉，再 resolve
        muvik::ui::font::setScale(1.0f);
        const float restored = muvik::FontScale::resolve();
        CHECK_TRUE(restored > 1.1f, "从配置文件读回了大字档");
        CHECK_EQ(muvik::FontScale::currentIndex(), (size_t)2, "档位下标为最大档");
    }

    TEST_BEGIN("持久化：默认值（无配置文件）为标准档");
    {
        std::system(("rm -f " + cfg + "/yiufplayer/ui_settings.dat").c_str());
        std::system(("rm -f " + cfg + "/muvik/ui_settings.dat").c_str());
        muvik::ui::font::setScale(1.0f);
        const float v = muvik::FontScale::resolve();
        CHECK_TRUE(v > 0.95f && v < 1.05f, "无配置时回到标准档");
    }

    TEST_BEGIN("环境变量优先于配置文件");
    {
        muvik::FontScale::set(1.20f, true);      // 配置文件存大字
        setenv("MUVIK_FONT_SCALE", "0.85", 1);
        const float v = muvik::FontScale::resolve();
        CHECK_TRUE(v < 0.9f, "环境变量覆盖了配置文件");
        unsetenv("MUVIK_FONT_SCALE");
    }

    TEST_BEGIN("cycle：循环切换会回到起点");
    {
        muvik::FontScale::set(0.85f, false);
        muvik::FontScale::cycle();
        const size_t a = muvik::FontScale::currentIndex();
        muvik::FontScale::cycle();
        const size_t b = muvik::FontScale::currentIndex();
        muvik::FontScale::cycle();
        const size_t c = muvik::FontScale::currentIndex();

        CHECK_TRUE(a != 0, "第一次切换离开起点");
        CHECK_TRUE(b != a, "第二次切换继续前进");
        CHECK_EQ(c, (size_t)0, "第三次切回起点（三档循环）");
    }

    TEST_BEGIN("currentIndex：浮点误差下仍能对上档位");
    {
        // set(1.0) 后内部可能存成 1.0000001，不能因为不等于就返回 0
        muvik::FontScale::set(1.0f, false);
        CHECK_EQ(muvik::FontScale::currentIndex(), (size_t)1, "标准档下标为 1");

        muvik::FontScale::set(0.85f, false);
        CHECK_EQ(muvik::FontScale::currentIndex(), (size_t)0, "小档下标为 0");
    }

    std::system(("rm -rf " + cfg).c_str());
    TEST_SUMMARY();
}
