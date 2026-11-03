#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <borealis.hpp>
#include "activity/file_browser_activity.hpp"
#include "playback/audio_player.hpp"
#include "utils/resource_locator.hpp"
#include "theme/deck_theme.hpp"
#include "selftest/functional_smoke.hpp"
#include "utils/frame_grabber.hpp"
#include "utils/scanner.hpp"
#include "activity/player_activity.hpp"
#include "activity/plugin_manager_activity.hpp"
#include "activity/plugin_browser_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include "model/metadata_store.hpp"
#include <filesystem>
#include <cstdio>
#include <chrono>
#include <thread>
#include <algorithm>
#if !defined(ANDROID)
// GLFW 只在桌面平台可用；Android 走 SDL2，没有这个头文件。
#include <borealis/platforms/glfw/glfw_video.hpp>
#endif
#if defined(ANDROID)
// Android 上 SDL 的 Java 层通过 nativeRunMain(lib, "SDL_main") 启动程序，
// 所以必须导出 **SDL_main** 而不是 main。
// SDL_main.h 里 `#define main SDL_main` 正是做这件事。
//
// 不做这一步的症状：Activity 正常创建、窗口也建好了，
// 但 native 侧的 main() 永远不执行，日志一片空白，
// Activity 在 1.5 秒后自己 finish（SDLActivity 认为 main 返回了）。
#include <SDL2/SDL_main.h>
#else
#include <GLFW/glfw3.h>
#endif
#include <algorithm>
#include <vector>

int main(int argc, char* argv[]) {
    // 支持直接打开媒体文件：
    //   deck-player /path/to/movie.mkv
    // 这样从文件管理器、Steam 快捷方式或命令行都能直接播放，
    // 也是自动化测试（抓帧验证）的唯一入口。
    std::string startup_file;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!arg.empty() && arg[0] != '-') {
            std::error_code ec;
            if (std::filesystem::is_regular_file(arg, ec)) {
                startup_file = arg;
                break;
            }
        }
    }

    // 0. 定位资源目录。
    //    borealis 把资源路径编译进二进制且为相对路径，因此必须把工作目录
    //    切到资源目录的父目录，否则从桌面启动器 / Flatpak 启动时
    //    字体与图标会加载失败。
    deck_player::ResourceLocator::setup();

    // 1. 设置 Borealis 日志级别
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    // 2. 初始化 Borealis 引擎
    if (!brls::Application::init()) {
        brls::Logger::error("无法初始化 Borealis 应用引擎");
        return EXIT_FAILURE;
    }

    // 允许通过环境变量强制窗口尺寸，用于验证不同屏幕下的适配：
    //   DECKPLAYER_WINDOW=1280x800   （Steam Deck 原生分辨率）
    //   DECKPLAYER_WINDOW=640x360    （小屏）
    // 这既是测试手段，也方便用户在自己的显示器上按需指定。
    // 默认创建窗口（针对 macOS Retina 大屏与外接显示器自适应）
    brls::Application::createWindow("DeckPlayer");

    // 应用本项目自有配色，覆盖 borealis 默认的 Switch 风格深色主题。
    // 必须在窗口创建之后（主题在此阶段初始化完成）。
    deck_player::applyDeckTheme();

    // 允许强制窗口尺寸，用于验证不同屏幕下的适配：
    //   DECKPLAYER_WINDOW=1280x800   （Steam Deck 原生分辨率）
    //   DECKPLAYER_WINDOW=640x360    （小屏）
    //
    // 必须在 createWindow 之后再调 glfwSetWindowSize：
    // 直接改 Application::windowWidth 会被窗口创建流程覆盖（实测无效，
    // 三种尺寸抓出来的帧都是 1280x720）。
    if (const char* spec = std::getenv("DECKPLAYER_WINDOW")) {
        unsigned int w = 0, h = 0;
        if (std::sscanf(spec, "%ux%u", &w, &h) == 2 && w >= 320 && h >= 200) {
#if defined(ANDROID)
            // Android 上没有 GLFW 窗口；尺寸由设备决定，
            // DECKPLAYER_WINDOW 在此平台无效（保留变量以免脚本报错）。
            brls::Logger::info("Android：忽略 DECKPLAYER_WINDOW={}（尺寸由设备决定）", spec);
#else
            auto* vc = dynamic_cast<brls::GLFWVideoContext*>(
                brls::Application::getPlatform()->getVideoContext());
            if (vc && vc->getGLFWWindow()) {
                glfwSetWindowSize(vc->getGLFWWindow(), (int)w, (int)h);
                // 必须同步更新 borealis 的缩放状态：
                // glfwSetWindowSize 不会触发 Application::setWindowSize，
                // 否则 windowScale / contentHeight 仍是旧窗口的值，
                // 导致 UI 按错误比例布局（实测 1024x600 下界面错乱）。
                brls::Application::setWindowSize((int)w, (int)h);
                brls::Logger::info("窗口尺寸被指定为 {}x{}", w, h);
            } else {
                brls::Logger::warning("无法获取窗口句柄，忽略 DECKPLAYER_WINDOW");
            }
#endif
        } else {
            brls::Logger::warning("DECKPLAYER_WINDOW 格式应为 宽x高，收到: {}", spec);
        }
    }

    // 不假定目标屏幕尺寸：以当前窗口实际尺寸为最小值基准，允许用户自由放大到
    // 1080p / 2K / 4K 或缩小，最大尺寸不设限。窗口初始尺寸由 borealis 依据
    // 显示器分辨率与缩放因子自动决定，掌机 800p 与桌面 4K 均可正常使用。
    uint32_t min_w = VideoContext::sizeW > 0 ? VideoContext::sizeW : 640;
    uint32_t min_h = VideoContext::sizeH > 0 ? VideoContext::sizeH : 360;
#if !defined(ANDROID)
    // Android 的窗口就是屏幕，没有「调整窗口大小」的概念，跳过。
    brls::Application::getPlatform()->setWindowSizeLimits(min_w, min_h, 0, 0);
#endif

    // 启用全局退出拦截
    brls::Application::setGlobalQuit(true);

    // 3. 进入主界面；若指定了文件则直接开始播放
    if (!startup_file.empty()) {
        std::vector<deck_player::MediaItem> playlist;
        deck_player::MediaItem item;
        item.path = startup_file;
        item.name = std::filesystem::path(startup_file).filename().string();
        std::string ext = std::filesystem::path(startup_file).extension().string();
        item.extension = ext;
        item.type = deck_player::MediaScanner::isSupportedAudioFormat(ext)
                        ? deck_player::MediaType::Audio
                        : deck_player::MediaType::Video;
        playlist.push_back(item);

        // 若同目录下还有其他同类媒体，一并加入播放列表，支持自动连播。
        // 这让「用文件管理器打开某一集」也能顺带播完整季。
        {
            namespace fs = std::filesystem;
            fs::path dir = fs::path(startup_file).parent_path();
            std::error_code ec;
            if (!dir.empty()) {
                std::vector<deck_player::MediaItem> siblings;
                for (const auto& entry : fs::directory_iterator(dir, ec)) {
                    if (ec || !entry.is_regular_file()) continue;
                    std::string p2 = entry.path().string();
                    if (p2 == startup_file) continue;
                    std::string e2 = entry.path().extension().string();
                    if (!deck_player::MediaScanner::isSupportedVideoFormat(e2))
                        continue;
                    if (item.type != deck_player::MediaType::Video) continue;
                    deck_player::MediaItem sib;
                    sib.path = p2;
                    sib.name = entry.path().filename().string();
                    sib.extension = e2;
                    sib.type = deck_player::MediaType::Video;
                    siblings.push_back(sib);
                }
                std::sort(siblings.begin(), siblings.end(),
                          [](const deck_player::MediaItem& a, const deck_player::MediaItem& b) {
                              return a.name < b.name;
                          });
                playlist.insert(playlist.end(), siblings.begin(), siblings.end());
                if (!siblings.empty())
                    brls::Logger::info("同目录另有 {} 个视频，已加入播放列表", siblings.size());
            }
        }

        auto* browser = new deck_player::FileBrowserActivity();

        brls::Application::pushActivity(browser);

        // 音频必须走全局 AudioPlayer，才能保持后台播放；
        // 若错误地交给 PlayerActivity（视频播放器），退出界面音乐就停了。
        if (item.type == deck_player::MediaType::Audio) {
            brls::Logger::info("直接播放音频: {}", startup_file);
            deck_player::AudioPlayer::getInstance().playPlaylist(playlist, 0);
        } else {
            brls::Logger::info("直接播放视频: {}", startup_file);
            brls::Application::pushActivity(
                new deck_player::PlayerActivity(playlist, 0));
        }
    } else if (std::getenv("DECKPLAYER_SEED_META")) {
        // 测试用：预置一条刮削元数据，验证视频库是否正确显示
        {
            deck_player::MediaMetadata m;
            m.path = std::getenv("DECKPLAYER_SEED_META");
            m.title = "星际穿越";
            m.overview = "一支探险队穿越虫洞寻找人类新家园。";
            m.rating = 8.7;
            m.release_date = "2014-11-07";
            m.runtime_minutes = 169;
            m.scraper_id = "demo";
            m.updated_at = 1700000000;
            deck_player::MetadataStore::getInstance().put(m);
            deck_player::MetadataStore::getInstance().save();
            brls::Logger::info("已预置元数据: {}", m.title);
        }
        brls::Application::pushActivity(new deck_player::FileBrowserActivity());
    } else if (std::getenv("DECKPLAYER_OPEN_BROWSER")) {
        // 直接进入第一个含视频源插件的浏览界面，验证远程目录渲染
        brls::Application::pushActivity(new deck_player::FileBrowserActivity());
        auto plugins = deck_player::PluginManager::getInstance().getPlugins();
        for (const auto& p : plugins) {
            if (p.has_video_source) {
                brls::Logger::info("打开插件浏览: {}", p.name);
                auto* act = new deck_player::PluginBrowserActivity(p.path, p.name);
                if (const char* sub = std::getenv("DECKPLAYER_BROWSE_PATH")) {
                    act->setInitialPath(sub);
                }
                brls::Application::pushActivity(act);
                break;
            }
        }
    } else if (std::getenv("DECKPLAYER_OPEN_PLUGINS")) {
        // 直接打开插件管理页，便于自动化验证其渲染
        brls::Application::pushActivity(new deck_player::FileBrowserActivity());
        brls::Application::pushActivity(new deck_player::PluginManagerActivity());
    } else {
        brls::Application::pushActivity(new deck_player::FileBrowserActivity());
    }

    // 4. 运行主循环
    //    每帧驱动后台音频引擎，使其在离开播放器界面后仍能持续解码与推进进度。
    //    若设置了 DECKPLAYER_FRAMEGRAB，还会在主循环中抓取一帧画面后退出，
    //    用于在无法使用系统截图的环境下验证 UI 渲染。
    int frame_index = 0;
    // 帧率限制：macOS 上 GLFW 的 swapInterval 在实测中【并不生效】
    // （独立测试：swapInterval=1 仍跑到 3354 FPS），
    // 导致主循环全速空转、CPU 占用稳定在 85% 以上。
    // 对掌机来说这是致命问题：发热、风扇狂转、电量几分钟耗尽。
    // 这里按目标帧率主动休眠。桌面与掌机都适用，不依赖平台 vsync 行为。
    const char* fps_env = std::getenv("DECKPLAYER_FPS");
    const int target_fps = fps_env ? std::max(1, atoi(fps_env)) : 60;
    const auto frame_budget = std::chrono::microseconds(1000000 / target_fps);
    auto frame_start = std::chrono::steady_clock::now();

    // 自检：打印当前输入类型与按键图标，用于验证键盘/手柄区隔
    const bool print_keyicon = std::getenv("DECKPLAYER_PRINT_KEYICON") != nullptr;
    // 自检：模拟一次键盘操作，验证输入类型能正确切到 KEYBOARD
    if (std::getenv("DECKPLAYER_FAKE_KEYBOARD")) {
        brls::Application::reportInputSource(true);
        brls::Logger::info("已模拟键盘输入");
    }

    // 功能级冒烟测试（仅 DECKPLAYER_SMOKE 时运行）
    deck_player::runFunctionalSmoke();

    while (brls::Application::mainLoop()) {
        deck_player::AudioPlayer::getInstance().tick();

        if (deck_player::FrameGrabber::enabled()) {
            if (deck_player::FrameGrabber::tick(frame_index)) {
                brls::Logger::info("FrameGrabber: 抓帧完成，退出");
                break;
            }
        }
        ++frame_index;

        if (print_keyicon && frame_index == 120) {
            auto type = brls::Application::getInputType();
            const char* tn = (type == brls::InputType::KEYBOARD) ? "KEYBOARD"
                           : (type == brls::InputType::GAMEPAD)  ? "GAMEPAD"
                                                                : "TOUCH";
            brls::Logger::info("输入类型: {}", tn);
            brls::Logger::info("A 键图标: [{}]",
                brls::Hint::getKeyIcon(brls::ControllerButton::BUTTON_A));
            brls::Logger::info("B 键图标: [{}]",
                brls::Hint::getKeyIcon(brls::ControllerButton::BUTTON_B));
        }

        // 补足本帧剩余时间，避免忙等空转
        if (!deck_player::FrameGrabber::enabled()) {
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            if (elapsed < frame_budget) {
                std::this_thread::sleep_for(frame_budget - elapsed);
            }
            frame_start = std::chrono::steady_clock::now();
        }
    }

    // 退出前落盘进度并关闭音频引擎
    deck_player::AudioPlayer::getInstance().shutdown();

    return EXIT_SUCCESS;
}
