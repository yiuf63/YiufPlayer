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

} // namespace testing

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
