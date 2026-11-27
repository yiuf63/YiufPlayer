#pragma once

// 界面字号档位（持久化 + 档位定义）。
//
// 为什么需要：掌机的握持距离差别很大。Steam Deck 双手握持时屏幕离眼睛
// 约 40cm，TrimUI Smart Pro 更近；同一套字号在前者清晰、在后者偏小。
// 桌面 / 外接电视又是另一种距离。写死一个系数只能讨好其中一种。
//
// 与窗口分辨率无关：本项目的布局建立在**逻辑像素**上
// （`Application::contentWidth` 恒为 1280），物理分辨率只影响缩放。
// 所以字号档位不是「小屏才需要」，而是**按观看距离**选。
//
// 状态只有一份：实际系数存在 ui::font::scaleRef()（见 ui/design.hpp），
// 这里只负责「档位怎么定义、怎么读写配置文件、怎么应用」。
// 不要在别处再存一个 float —— 两处状态一定会不同步。

#include <string>
#include <vector>

namespace muvik {

// 档位定义与持久化。
class FontScale {
public:
    struct Option {
        float scale;
        const char* key;    // 稳定标识，写进配置文件（不随界面语言变）
        const char* label;  // 界面显示
        const char* desc;   // 副标题
    };

    // 可选档位。刻意只有三档：
    // 再多就变成「调参」而不是「选一个舒服的」，用户反而不知道选哪个。
    static const std::vector<Option>& options();

    // 启动时解析并**应用**（优先级：MUVIK_FONT_SCALE 环境变量 > 配置文件 > 默认）。
    // 返回实际生效的系数。
    static float resolve();

    // 当前生效系数（读的是 ui::font::scale，不重复存）。
    static float current();

    // 设置并（可选）持久化。
    static void set(float scale, bool persist = true);

    // 切到下一个档位（循环）。
    static void cycle();

    // 写配置文件。
    static void save();

    // 当前系数对应的档位下标（浮点比较取最近）。
    static size_t currentIndex();

private:
    static std::string filePath();
};

} // namespace muvik
