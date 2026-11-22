#pragma once

// 极简测试框架。
//
// 设计取舍：不引入 gtest/catch2 等第三方测试库。
// 原因：① 减少依赖与许可证负担；② 本项目测试多为「断言 + 打印」形态，
// 自建 100 行足够；③ 保持与项目零外部测试依赖的风格一致。
//
// 用法：
//   TEST_BEGIN("媒体库");
//   CHECK_TRUE(cond, "描述");
//   CHECK_EQ(a, b, "描述");
//   TEST_END();

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>

namespace testing {

inline int g_failures = 0;
inline int g_checks = 0;
inline std::string g_current_suite;

inline void reportPass(const char* what) {
    std::printf("    \033[32mPASS\033[0m %s\n", what);
}

inline void reportFail(const char* what, const char* detail = nullptr) {
    std::printf("    \033[31mFAIL\033[0m %s", what);
    if (detail && *detail) std::printf("  (%s)", detail);
    std::printf("\n");
    ++g_failures;
}

template <typename T>
std::string toStr(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

// ── 退出码守卫 ────────────────────────────────────────────────────────
//
// 这里曾经有一个**让所有测试失效**的缺陷：
//
//   int main() {
//       ...
//       TEST_SUMMARY();      // ← 宏返回 0/1，但这条语句的返回值没人接
//   }                        //   main 隐式返回 0
//
// 于是无论多少断言失败，进程退出码恒为 0；而 run_tests.sh 只看退出码。
// 结果是「测试永远不会失败」——本项目历史上反复踩的「测试恒真」，
// 这次藏在框架本身。实测发现 3 个套件（keymap/theme/workshop_dlc）
// 完全不可失败，另有 3 个（display_mode/input_settings/series_detector）
// 连 TEST_SUMMARY 都没调用。
//
// 修法：不能靠每个测试文件记得写 `return`——那是在用纪律防守。
// 改成编译器保证：
//
//   1. 定义一个 main 返回值对象（Guard），析构时把 g_failures 映射成退出码；
//   2. TEST_SUITE() 宏创建局部 Guard，它随 main 一起析构；
//   3. 只要测试文件用了 TEST_SUITE()，退出码就一定正确。
//
// 为什么不用 atexit：atexit 无法改变已经确定的退出码。
// 为什么不用全局对象的析构：析构顺序不确定，可能晚于 main 返回。
struct ExitGuard {
    ~ExitGuard() {
        // 注意：析构函数里调用 std::exit 是安全的（C++ 标准允许），
        // 且能保证即使测试里有提前 return 也走正确退出码。
        std::fflush(stdout);
        std::exit(g_failures == 0 ? 0 : 1);
    }
};

} // namespace testing

// TEST_SUITE()：在 main() 开头调用一次，负责最终的退出码。
//
// 用法：
//   int main() {
//       TEST_SUITE();
//       TEST_BEGIN("...");
//       ...
//       TEST_END();
//   }
//
// 即使忘了写 TEST_END / TEST_SUMMARY，退出码依然正确。
#define TEST_SUITE() ::testing::ExitGuard _testing_exit_guard_;

// 兼容旧写法：把 TEST_SUMMARY() 保留为「打印 + 返回值」，
// 但现在它只是一个可选的收尾打印，退出码由 TEST_SUITE() 保证。
#define TEST_BEGIN(name)                                                      \
    do {                                                                      \
        ::testing::g_current_suite = (name);                                  \
        std::printf("\n\033[36m== %s ==\033[0m\n", name);                     \
    } while (0)

#define TEST_END()                                                            \
    do {                                                                      \
        std::printf("\n----------------------------------------\n");         \
        std::printf("共 %d 项检查，%d 项失败\n",                              \
                    ::testing::g_checks, ::testing::g_failures);              \
    } while (0)

#define TEST_SUMMARY()                                                        \
    (::testing::g_failures == 0                                             \
        ? (std::printf("\033[32mRESULT: PASS 全部通过\033[0m\n"), 0)          \
        : (std::printf("\033[31mRESULT: FAIL\033[0m\n"), 1))

#define CHECK_TRUE(cond, what)                                                \
    do {                                                                      \
        ++::testing::g_checks;                                                \
        if (cond) ::testing::reportPass(what);                                \
        else ::testing::reportFail(what);                                     \
    } while (0)

#define CHECK_FALSE(cond, what) CHECK_TRUE(!(cond), what)

#define CHECK_EQ(actual, expected, what)                                      \
    do {                                                                      \
        ++::testing::g_checks;                                                \
        auto _a = (actual);                                                   \
        auto _e = (expected);                                                 \
        if (_a == _e) {                                                       \
            ::testing::reportPass(what);                                      \
        } else {                                                              \
            std::string _d = "实际=" + ::testing::toStr(_a) +                  \
                             " 期望=" + ::testing::toStr(_e);                 \
            ::testing::reportFail(what, _d.c_str());                          \
        }                                                                     \
    } while (0)

#define CHECK_NE(actual, unexpected, what)                                    \
    do {                                                                      \
        ++::testing::g_checks;                                                \
        auto _a = (actual);                                                   \
        auto _u = (unexpected);                                               \
        if (!(_a == _u)) {                                                    \
            ::testing::reportPass(what);                                      \
        } else {                                                              \
            std::string _d = "不应等于 " + ::testing::toStr(_u);              \
            ::testing::reportFail(what, _d.c_str());                          \
        }                                                                     \
    } while (0)

#define CHECK_CONTAINS(haystack, needle, what)                                \
    do {                                                                      \
        ++::testing::g_checks;                                                \
        std::string _h = (haystack);                                          \
        std::string _n = (needle);                                            \
        if (_h.find(_n) != std::string::npos) {                               \
            ::testing::reportPass(what);                                      \
        } else {                                                              \
            std::string _d = "'" + _h + "' 不含 '" + _n + "'";                \
            ::testing::reportFail(what, _d.c_str());                          \
        }                                                                     \
    } while (0)
