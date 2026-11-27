#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <borealis.hpp>
#include "activity/file_browser_activity.hpp"
#include "playback/audio_player.hpp"
#include "utils/resource_locator.hpp"
#include "utils/i18n.hpp"
#include "utils/display_mode.hpp"
#include "utils/font_scale.hpp"
#include "utils/input_settings.hpp"
#include "utils/keymap.hpp"
#include "theme/muvik_theme.hpp"
#include "selftest/functional_smoke.hpp"
#include "utils/frame_grabber.hpp"
#include "utils/scanner.hpp"
#include "activity/player_activity.hpp"
#include "activity/plugin_manager_activity.hpp"
#include "activity/plugin_browser_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/js_runtime.hpp"
#include "view/qr_code_view.hpp"
#include "network/web_portal.hpp"
#include "utils/app_paths.hpp"
#include "model/metadata_store.hpp"
#include <filesystem>
#include <cstdio>
#include <chrono>
#include <thread>
#include <algorithm>
#if defined(ANDROID)
#include <SDL2/SDL_main.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#else
#include <borealis/platforms/glfw/glfw_video.hpp>
#include <GLFW/glfw3.h>
#endif
#include <algorithm>
#include <vector>

int main(int argc, char* argv[]) {
    // 支持直接打开媒体文件：
    //   muvik /path/to/movie.mkv
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
    muvik::ResourceLocator::setup();

    // 初始化多语言支持
    muvik::i18n::init();

    // 1. 设置 Borealis 日志级别
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    // 2. 初始化 Borealis 引擎
    if (!brls::Application::init()) {
        brls::Logger::error("无法初始化 Borealis 应用引擎");
        return EXIT_FAILURE;
    }

    // 全屏显示机制：掌机 / 主机 / 电视场景默认全屏沉浸，
    // 同时支持配置文件持久化、MUVIK_WINDOW/MUVIK_FULLSCREEN 环境变量覆盖。
    VideoContext::FULLSCREEN = muvik::DisplayMode::resolveFullscreen();

    // 界面字号档位。必须在创建窗口和任何界面之前应用 ——
    // 各视图在构造时就取了字号，之后再改只影响新构建的视图。
    // 优先级：MUVIK_FONT_SCALE 环境变量 > 配置文件 > 默认标准。
    {
        const float applied = muvik::FontScale::resolve();
        brls::Logger::info("[FONT] 字号档位 scale={} (body={}px)",
                           applied, muvik::ui::font::body());
    }

    // 适配连接手柄的按键提示字体（Xbox/Steam Deck vs PlayStation vs Switch）
    muvik::Keymap::applyKeymapFont();

    // 默认创建窗口或全屏画面（针对 macOS Retina 大屏与外接显示器自适应）
    brls::Application::createWindow("Yiuf Player");

    // 注册全局全屏快捷键（F11 / Alt+Enter / macOS Cmd+F）
    muvik::DisplayMode::setupGlobalKeyBindings();

    // 应用本项目自有配色，覆盖 borealis 默认的 Switch 风格深色主题。
    // 必须在窗口创建之后（主题在此阶段初始化完成）。
    muvik::applyMuvikTheme();

    // 应用手柄按键布局偏好（Xbox / Steam 标准 vs Nintendo Switch 反转）
    muvik::InputSettings::applySettings();

    // 绑定 JS 插件扫码弹窗回调
    muvik::JSRuntimeHost::setQRCodeHandler([](const std::string& title, const std::string& url, const std::string& hint) {
        brls::sync([title, url, hint]() {
            muvik::QRCodeView::showDialog(title, url, hint);
        });
    });
    muvik::JSRuntimeHost::setNotifyHandler([](const std::string& msg) {
        brls::sync([msg]() {
            brls::Application::notify(msg);
        });
    });

    // 掌机硬件环境或显式指定时，自动在后台静默拉起局域网 Web 门户服务
    if (muvik::AppPaths::isTrimUI() || std::getenv("MUVIK_PORTAL")) {
        muvik::WebPortal::getInstance().start(8888);
    }
    //   MUVIK_WINDOW=1280x800   （Steam Deck 原生分辨率）
    //   MUVIK_WINDOW=640x360    （小屏）
    //
    // 必须在 createWindow 之后再调 glfwSetWindowSize：
    // 直接改 Application::windowWidth 会被窗口创建流程覆盖（实测无效，
    // 三种尺寸抓出来的帧都是 1280x720）。
    if (const char* spec = std::getenv("MUVIK_WINDOW")) {
        unsigned int w = 0, h = 0;
        if (std::sscanf(spec, "%ux%u", &w, &h) == 2 && w >= 320 && h >= 200) {
#if defined(ANDROID) || defined(__SDL2__)
            brls::Logger::info("忽略 MUVIK_WINDOW={}（尺寸由设备决定）", spec);
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
                brls::Logger::warning("无法获取窗口句柄，忽略 MUVIK_WINDOW");
            }
#endif
        } else {
            brls::Logger::warning("MUVIK_WINDOW 格式应为 宽x高，收到: {}", spec);
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

    // 禁用默认的全局退出拦截（Borealis 默认会把 BUTTON_START 绑定为全局退出，
    // 导致手柄上的设置/菜单键 ☰ 按下后程序直接关闭，像闪退一样）。
    brls::Application::setGlobalQuit(false);

    // 3. 进入主界面；若指定了文件则直接开始播放
    if (!startup_file.empty()) {
        std::vector<muvik::MediaItem> playlist;
        muvik::MediaItem item;
        item.path = startup_file;
        item.name = std::filesystem::path(startup_file).filename().string();
        std::string ext = std::filesystem::path(startup_file).extension().string();
        item.extension = ext;
        item.type = muvik::MediaScanner::isSupportedAudioFormat(ext)
                        ? muvik::MediaType::Audio
                        : muvik::MediaType::Video;
        playlist.push_back(item);

        // 若同目录下还有其他同类媒体，一并加入播放列表，支持自动连播。
        // 这让「用文件管理器打开某一集」也能顺带播完整季。
        {
            namespace fs = std::filesystem;
            fs::path dir = fs::path(startup_file).parent_path();
            std::error_code ec;
            if (!dir.empty()) {
                std::vector<muvik::MediaItem> siblings;
                try {
                    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
                    fs::directory_iterator end;
                    while (!ec && it != end) {
                        try {
                            std::error_code reg_ec;
                            if (it->is_regular_file(reg_ec)) {
                                std::string p2 = it->path().string();
                                if (p2 != startup_file) {
                                    std::string e2 = it->path().extension().string();
                                    if (muvik::MediaScanner::isSupportedVideoFormat(e2) && item.type == muvik::MediaType::Video) {
                                        muvik::MediaItem sib;
                                        sib.path = p2;
                                        sib.name = it->path().filename().string();
                                        sib.extension = e2;
                                        sib.type = muvik::MediaType::Video;
                                        siblings.push_back(sib);
                                    }
                                }
                            }
                        } catch (...) {}
                        it.increment(ec);
                    }
                } catch (...) {}
                std::sort(siblings.begin(), siblings.end(),
                          [](const muvik::MediaItem& a, const muvik::MediaItem& b) {
                              return a.name < b.name;
                          });
                playlist.insert(playlist.end(), siblings.begin(), siblings.end());
                if (!siblings.empty())
                    brls::Logger::info("同目录另有 {} 个视频，已加入播放列表", siblings.size());
            }
        }

        auto* browser = new muvik::FileBrowserActivity();

        brls::Application::pushActivity(browser);

        // 音频必须走全局 AudioPlayer，才能保持后台播放；
        // 若错误地交给 PlayerActivity（视频播放器），退出界面音乐就停了。
        if (item.type == muvik::MediaType::Audio) {
            brls::Logger::info("直接播放音频: {}", startup_file);
            muvik::AudioPlayer::getInstance().playPlaylist(playlist, 0);
        } else {
            brls::Logger::info("直接播放视频: {}", startup_file);
            brls::Application::pushActivity(
                new muvik::PlayerActivity(playlist, 0));
        }
    } else if (std::getenv("MUVIK_SEED_META")) {
        // 测试用：预置一条刮削元数据，验证视频库是否正确显示
        {
            muvik::MediaMetadata m;
            m.path = std::getenv("MUVIK_SEED_META");
            m.title = "星际穿越";
            m.overview = "一支探险队穿越虫洞寻找人类新家园。";
            m.rating = 8.7;
            m.release_date = "2014-11-07";
            m.runtime_minutes = 169;
            m.scraper_id = "demo";
            m.updated_at = 1700000000;
            muvik::MetadataStore::getInstance().put(m);
            muvik::MetadataStore::getInstance().save();
            brls::Logger::info("已预置元数据: {}", m.title);
        }
        brls::Application::pushActivity(new muvik::FileBrowserActivity());
    } else if (std::getenv("MUVIK_OPEN_BROWSER")) {
        // 直接进入指定或第一个含视频源插件的浏览界面，验证远程目录渲染
        brls::Application::pushActivity(new muvik::FileBrowserActivity());
        const char* target_plugin = std::getenv("MUVIK_PLUGIN_ID");
        auto plugins = muvik::PluginManager::getInstance().getPlugins();
        for (const auto& p : plugins) {
            if (target_plugin && p.id != target_plugin) continue;
            if (p.has_video_source) {
                brls::Logger::info("打开插件浏览: {} ({})", p.name, p.id);
                auto* act = new muvik::PluginBrowserActivity(p.path, p.name);
                if (const char* sub = std::getenv("MUVIK_BROWSE_PATH")) {
                    act->setInitialPath(sub);
                }
                brls::Application::pushActivity(act);
                break;
            }
        }
    } else if (std::getenv("MUVIK_OPEN_PLUGINS")) {
        // 直接打开插件管理页，便于自动化验证其渲染
        brls::Application::pushActivity(new muvik::FileBrowserActivity());
        brls::Application::pushActivity(new muvik::PluginManagerActivity());
    } else {
        brls::Application::pushActivity(new muvik::FileBrowserActivity());
    }

    // 4. 运行主循环
    //    每帧驱动后台音频引擎，使其在离开播放器界面后仍能持续解码与推进进度。
    //    若设置了 MUVIK_FRAMEGRAB，还会在主循环中抓取一帧画面后退出，
    //    用于在无法使用系统截图的环境下验证 UI 渲染。
    int frame_index = 0;
    // 帧率限制：从设置或环境变量读取。
    // target_fps == 0 表示自动跟随显示器刷新率（SteamOS 下由 Gamescope / VSync 自然同步，
    // 原生支持 Steam Deck LCD 60Hz、OLED 90Hz 以及外接高刷显示器 120Hz/144Hz）。
    const int configured_fps = muvik::DisplayMode::getTargetFps();
    int target_fps = configured_fps;
#if defined(__APPLE__)
    // macOS 上 GLFW 的 swapInterval 偶有失效，设 120 FPS 软兜底防止空转
    if (target_fps <= 0) target_fps = 120;
#endif
    const bool should_throttle = (target_fps > 0);
    const auto frame_budget = should_throttle
                                ? std::chrono::microseconds(1000000 / target_fps)
                                : std::chrono::microseconds(0);
    auto frame_start = std::chrono::steady_clock::now();

    // 自检：打印当前输入类型与按键图标，用于验证键盘/手柄区隔
    const bool print_keyicon = std::getenv("MUVIK_PRINT_KEYICON") != nullptr;
    // 自检：模拟一次键盘操作，验证输入类型能正确切到 KEYBOARD
    if (std::getenv("MUVIK_FAKE_KEYBOARD")) {
        brls::Application::reportInputSource(true);
        brls::Logger::info("已模拟键盘输入");
    }

    // 功能级冒烟测试（仅 MUVIK_SMOKE 时运行）
    muvik::runFunctionalSmoke();

    while (brls::Application::mainLoop()) {
        muvik::AudioPlayer::getInstance().tick();

        if (muvik::FrameGrabber::enabled()) {
            if (muvik::FrameGrabber::tick(frame_index)) {
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
        if (!muvik::FrameGrabber::enabled() && should_throttle) {
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            if (elapsed < frame_budget) {
                std::this_thread::sleep_for(frame_budget - elapsed);
            }
            frame_start = std::chrono::steady_clock::now();
        }
    }

    // 退出前落盘进度并关闭音频引擎与局域网后台
    muvik::WebPortal::getInstance().stop();
    muvik::AudioPlayer::getInstance().shutdown();

    return EXIT_SUCCESS;
}
