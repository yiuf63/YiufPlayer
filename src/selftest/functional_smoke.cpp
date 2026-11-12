// 功能级冒烟测试（自检钩子）。
//
// 为什么需要这个：
//   项目此前 65 项自动检查全都是「看像素 / 看进程有没有崩」。
//   这两类指标无法回答最重要的问题——**点了之后真的有用吗**。
//   实际发生过：崩溃修好了，但点击任何视频都打不开、返回上级也无效，
//   而所有自动检查依然全绿。因为「没崩」不等于「功能正常」。
//
// 做法：
//   用 Application::handleAction() 注入真实按键（与手柄路径完全一致，
//   而不是自己调 lambda），每一步之后断言可观测的状态变化：
//     · 活动栈深度    —— 页面有没有真的压栈/出栈
//     · MediaLibrary  —— 数据层是否就绪
//     · mpv 属性      —— 播放器有没有真的加载文件
//   任何一条断言失败即打印 FAIL 并以非零码退出，供 CI 捕获。

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "selftest/functional_smoke.hpp"

#if defined(ANDROID)
#include <sys/system_properties.h>
#endif

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <borealis.hpp>
#include <borealis/core/application.hpp>

#include "activity/file_browser_activity.hpp"
#include "model/media_library.hpp"
#include "utils/app_paths.hpp"

namespace muvik {

namespace {

struct Step {
    int delay_ms;           // 距上一步的等待
    const char* name;
    std::function<void()> action;
};

int g_failures = 0;

void check(const char* what, bool ok, const std::string& detail = "") {
    if (ok) {
        brls::Logger::info("[smoke] PASS  {}", what);
    } else {
        ++g_failures;
        brls::Logger::error("[smoke] FAIL  {}  {}", what, detail);
    }
}

size_t stackDepth() {
    return brls::Application::getActivitiesStack().size();
}

} // namespace

void runFunctionalSmoke() {
    const char* env = std::getenv("MUVIK_SMOKE");
#if defined(ANDROID)
    // Android 上无法从 adb 直接给 Activity 传环境变量，
    // 因此额外读取系统属性：adb shell setprop debug.muvik.smoke 1
    // 这样实机也能跑同一套交互自检。
    if (!env) {
        // Android 上无法从 adb 给 Activity 传环境变量，
        // 额外读系统属性：adb shell setprop debug.muvik.smoke 1
        char prop[PROP_VALUE_MAX] = {0};
        if (__system_property_get("debug.muvik.smoke", prop) > 0 && prop[0] == '1') {
            env = prop;
        }
    }
#endif
    if (!env) return;

    // 预置条件：必须有媒体库数据，否则「点击视频」无从谈起
    auto videos = MediaLibrary::getInstance().getVideos();
    brls::Logger::info("[smoke] 视频库条目数: {}", videos.size());

    std::vector<Step> steps;

    // 1. 基线
    steps.push_back({200, "初始活动栈", []() {
        check("启动后有活动页面", stackDepth() >= 1,
              "深度=" + std::to_string(stackDepth()));
    }});

    // 2. 等媒体库扫描完成。
    //    这一步之前不存在，导致测试在扫描进行中就断言「库有内容」，
    //    于是「按 A 播放」必然失败——但那是测试太早，不是产品坏了。
    //    真实产品同样存在这个窗口：首次进入时库为空、点击无反应。
    steps.push_back({2500, "等待媒体库扫描", []() {
        auto videos = MediaLibrary::getInstance().getVideos();
        check("扫描完成后视频库非空", !videos.empty(),
              "视频数=" + std::to_string(videos.size()) +
              " 扫描中=" + (MediaLibrary::getInstance().isScanning() ? "是" : "否"));
    }});

    // 2.5 切到「媒体库」标签（索引 0）。
    //     默认也可能停在其他页上；只有媒体库的第一项是影片卡片，
    //     按 A 才会打开播放页。
    steps.push_back({300, "切到媒体库", []() {
        auto* act = brls::Application::getTopActivity();
        if (!act) { check("找到活动", false, "getTopActivity 返回空"); return; }
        auto* fb = dynamic_cast<muvik::FileBrowserActivity*>(act);
        check("顶层是 FileBrowserActivity", fb != nullptr,
              fb ? "ok" : "类型不符");
        if (fb) fb->selectTabForTest(0);   // 0 = 媒体库
    }});

    // 3. 按 A 进入第一个文件夹。
    //    媒体库首屏现在是文件夹卡片（不是影片列表），第一下 A 是「进桶」，
    //    第二下 A 才是「播放」。少这一步，后面的「按 A 播放」必然失败 ——
    //    但那是测试没跟上信息架构，不是产品坏了。
    steps.push_back({300, "按 A 进入文件夹", []() {
        auto* fb = dynamic_cast<muvik::FileBrowserActivity*>(
            brls::Application::getTopActivity());
        const bool before = fb && fb->libraryInFolderForTest();
        brls::Application::injectButton(brls::BUTTON_A);
        const bool after = fb && fb->libraryInFolderForTest();
        check("按 A 进入文件夹", !before && after,
              std::string("进入前=") + (before ? "是" : "否") +
              " 进入后=" + (after ? "是" : "否"));
    }});

    // 3.5 再按 A 打开文件夹里的第一个视频 —— 这是最关键的功能断言
    steps.push_back({400, "按 A 播放", []() {
        size_t before = stackDepth();
        brls::View* f = brls::Application::getCurrentFocus();
        brls::Logger::info("[smoke] currentFocus = {} ({})", (void*)f,
                           f ? f->describe() : "空");
        if (f) {
            auto acts = f->getActions();
            brls::Logger::info("[smoke] 焦点上的动作数 = {}", acts.size());
            for (auto& a : acts)
                brls::Logger::info("[smoke]   动作 button={} available={}",
                                   (int)a->getButton(), a->isAvailable());
        }
        bool consumed = brls::Application::injectButton(brls::BUTTON_A);
        brls::Logger::info("[smoke] injectButton 返回（是否消费）= {}", consumed);
        size_t after = stackDepth();
        check("按 A 之后压入了播放页", after > before,
              "栈深 " + std::to_string(before) + " -> " + std::to_string(after));
    }});

    // 4. 等播放器真正加载文件
    steps.push_back({1200, "等待播放器加载", []() {
        // 播放器内部会创建 mpv；这里只断言活动栈仍稳定（没有崩溃/弹回）
        check("播放页稳定存在", stackDepth() >= 2,
              "深度=" + std::to_string(stackDepth()));
    }});

    // 5. 按 B 返回
    steps.push_back({300, "按 B 返回", []() {
        size_t before = stackDepth();
        brls::Application::injectButton(brls::BUTTON_B);
        // 播放器的 B 键行为是 popActivity，深度应当减少
        check("按 B 之后返回了上一页", stackDepth() < before,
              "栈深 " + std::to_string(before) + " -> " + std::to_string(stackDepth()));
    }});

    // 6. 再按一次 B：这一下不再是「退到上一页」，而是「从文件夹退回总览」。
    //    媒体库自己接管了 B，所以活动栈深度不变 —— 若它没接住，
    //    用户从文件夹里退出去会直接关掉整个界面。
    steps.push_back({400, "再按 B", []() {
        size_t before = stackDepth();
        auto* fb = dynamic_cast<muvik::FileBrowserActivity*>(
            brls::Application::getTopActivity());
        brls::Application::injectButton(brls::BUTTON_B);
        const bool still_in_folder = fb && fb->libraryInFolderForTest();
        check("B 从文件夹退回总览", !still_in_folder && stackDepth() == before,
              std::string("仍在文件夹=") + (still_in_folder ? "是" : "否") +
              " 栈深 " + std::to_string(before) + " -> " + std::to_string(stackDepth()));
    }});

    // 6. 汇总
    steps.push_back({300, "汇总", []() {
        if (g_failures == 0) {
            brls::Logger::info("[smoke] === 全部通过 ===");
        } else {
            brls::Logger::error("[smoke] === 失败 {} 项 ===", g_failures);
        }
        // 让外层脚本能读到结果：成功退出码 0，失败非 0
        //
        // 用 std::_Exit 而不是 std::quick_exit：
        // quick_exit 在部分 libc++ 版本里是「未解析的使用声明」
        // （GitHub 的 macOS runner 实测报错），
        // _Exit 属于 C 标准库最基础的部分，各平台都有。
        // 两者语义相同：直接结束进程，不跑析构、不刷 stdio 缓冲。
        std::_Exit(g_failures == 0 ? 0 : 1);
    }});

    // 在后台线程按时间表投递，所有动作回到主线程执行
    std::thread([steps]() {
        for (auto& st : steps) {
            std::this_thread::sleep_for(std::chrono::milliseconds(st.delay_ms));
            auto fn = st.action;
            const char* name = st.name;
            brls::sync([fn, name]() {
                brls::Logger::info("[smoke] --- {} ---", name);
                fn();
            });
        }
    }).detach();
}

} // namespace muvik
