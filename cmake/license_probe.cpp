// ============================================================================
// 许可证构建期信息
//
// 本项目自 v0.7 起改为 GPL-3.0（见根目录 LICENSE），因此不再需要「禁止链接
// GPL 版 FFmpeg」这道防线——GPL 代码与本项目同许可，链接完全合法。
//
// 但这个探针保留，原因有二：
//
// 1. GPL-3.0 要求分发时提供「对应源码」（§6）。产物链接的是哪个 FFmpeg
//    构建（GPL 还是 LGPL）、版本是多少，必须能查得到，否则无法履行义务。
//    构建时打印出来，日志即是记录。
//
// 2. 发布包必须包含完整的第三方许可声明（见 THIRD-PARTY-NOTICES.md）。
//    探测结果用于核对声明是否与实际链接一致。
//
// 判定方式仍是 FFmpeg 官方 API avutil_license()，不是配置字符串匹配。
//
// 历史：v0.1–v0.6 期间本项目是闭源的，该探针在检测到 GPL 时会中止构建，
// 防止误用 GPL 组件发布闭源产品。改为 GPL-3.0 后该约束自然消失。
// ============================================================================

#include <cstdio>
#include <cstring>

// FFmpeg 9.x 的头文件在 C++ 下需要这个宏（common.h 会主动检查）。
// 它必须在包含 FFmpeg 头之前定义。
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

extern "C" {
#include <libavutil/avutil.h>
}

int main() {
    const char* lic = avutil_license();
    if (!lic) lic = "(unknown)";

    const bool is_gpl = (std::strstr(lic, "GPL") != nullptr);

    std::printf("==============================================================\n");
    std::printf(" 依赖许可证信息（用于发布时的源码与声明核对）\n");
    std::printf("==============================================================\n");
    // 用运行时函数而不是版本宏：FFmpeg 9.x 的宏在 C++ 下展开有问题
    // （LIBAVUTIL_VERSION 会展开成 60.101 这种非法浮点字面量）。
    std::printf(" avutil 版本 : %u\n", avutil_version());
    std::printf(" 许可        : %s\n", lic);
    std::printf(" 类别        : %s\n", is_gpl ? "GPL（含 --enable-gpl 组件）"
                                              : "LGPL / 更宽松");
    std::printf("--------------------------------------------------------------\n");
    std::printf(" 本项目以 GPL-3.0 分发，链接 GPL 版 FFmpeg %s。\n",
                is_gpl ? "合法" : "同样合法（LGPL 更宽松）");
    std::printf(" 分发时须遵守 GPL-3.0 §6：提供对应源码。\n");
    std::printf(" 第三方声明见 THIRD-PARTY-NOTICES.md。\n");
    std::printf("==============================================================\n");

    // 永远返回 0：这里只提供信息，不再阻断构建。
    return 0;
}
