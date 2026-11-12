#pragma once

#include <string>

namespace muvik {

class InputSettings {
public:
    // 解析启动时是否交换 A/B 键（优先级：MUVIK_SWAP_AB 环境变量 > 配置文件 > 默认 false）
    static bool resolveSwapAB();

    // 检查当前运行时是否开启了 A/B 键反转（Switch 布局）
    static bool isSwapAB();

    // 设置 A/B 键反转状态（可选是否持久化保存）
    static void setSwapAB(bool swap, bool persist = true);

    // 切换 A/B 键反转状态
    static void toggleSwapAB(bool persist = true);

    // 应用设置到 Borealis 运行时（启动时调用）
    static void applySettings();
};

} // namespace muvik
