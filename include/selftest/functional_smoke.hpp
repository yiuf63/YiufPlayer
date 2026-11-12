#pragma once

namespace muvik {

// 功能级冒烟测试：注入真实按键并断言状态变化。
// 仅在设置了 MUVIK_SMOKE 环境变量时运行。
// 通过则进程以 0 退出，任一断言失败以 1 退出。
void runFunctionalSmoke();

} // namespace muvik
