#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include "activity/plugin_manager_activity.hpp"
#include "view/media_cell.hpp"
#include "ui/design.hpp"
#include "ui/icons.hpp"
#include "utils/app_paths.hpp"
#include "plugin/plugin_installer.hpp"
#include "plugin/plugin_config_store.hpp"
#include "plugin/js_plugin.hpp"
#include "activity/plugin_browser_activity.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/dialog.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace muvik {

namespace {

// 统一的行构造：图标 + 标题 + 副标题 + 可选动作。
// 页面里所有条目都走这里，避免每处再写一遍 setText / setDetailText 的旧模式。
MediaCell* makeRow(ui::Icon icon, std::string title, std::string subtitle,
                   std::function<void()> on_click = nullptr, bool chevron = false) {
    auto* cell = new MediaCell(icon, std::move(title), std::move(subtitle));
    if (chevron) cell->setChevron(true);
    if (on_click) {
        cell->registerClickAction([cb = std::move(on_click)](brls::View*) {
            cb();
            return true;
        });
    }
    return cell;
}

} // namespace

brls::View* PluginManagerActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);
    list_->setPaddingBottom(28.0f);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setMarginRight(28.0f);
    styleScrollingFrame(scroller_);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle("插件管理");
    return frame_;
}

PluginManagerActivity::~PluginManagerActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

void PluginManagerActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 自检：自动弹出插件目录对话框，验证它能正常渲染
    if (std::getenv("MUVIK_OPEN_DIALOG")) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        alive_flags_.push_back(alive);
        std::thread([this, alive]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            brls::sync([this, alive]() {
                if (!alive->load()) return;
                this->revealPluginFolder();
            });
        }).detach();
    }

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction("重新扫描", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        PluginManager::getInstance().unloadAll();
        size_t n = PluginManager::getInstance().loadAll();
        brls::Application::notify("重新扫描完成，已加载 " + std::to_string(n) + " 个插件");
        render();
        return true;
    });

    render();
}

void PluginManagerActivity::render() {
    if (!list_) return;
    list_->clearViews();

    // ---- 插件目录 ----
    list_->addView(new SectionHeader("插件目录与来源"));

    list_->addView(makeRow(ui::Icon::Folder, "本地插件目录", AppPaths::pluginsDir(),
                           [this]() { revealPluginFolder(); }, true));

    auto ws_paths = PluginManager::getInstance().getWorkshopSearchPaths();
    std::string ws_desc = ws_paths.empty()
        ? ("未检测到创意工坊目录（Steam AppID: " + PluginManager::getInstance().getSteamAppId() + "）")
        : ("已检测到创意工坊：" + ws_paths[0]);
    list_->addView(makeRow(ui::Icon::Plugin, "Steam 创意工坊", ws_desc,
                           []() {
        brls::Application::notify("Steam 创意工坊：订阅 MOD 后启动自动加载");
    }, true));

    auto dlc_paths = PluginManager::getInstance().getDlcSearchPaths();
    std::string dlc_desc = dlc_paths.empty() ? "检测同级 dlc/ 与 Steam DLC 目录" : ("已检测到 DLC 目录：" + dlc_paths[0]);
    list_->addView(makeRow(ui::Icon::Drive, "Steam DLC 扩展", dlc_desc,
                           []() {
        brls::Application::notify("Steam DLC：放入 dlc/ 或 Steam DLC 目录后自动识别");
    }, true));

    list_->addView(makeRow(ui::Icon::Plus, "从 ZIP 包安装插件",
                           "输入 .zip 路径，自动解压并校验", [this]() {
        brls::Application::getImeManager()->openForText(
            [this](std::string path) {
                if (path.empty()) return;
                PluginInstaller::Result r = PluginInstaller::installFromZip(path);
                if (r.ok) {
                    brls::Logger::info("安装成功: {}", r.message);
                    PluginManager::getInstance().unloadAll();
                    size_t n = PluginManager::getInstance().loadAll();
                    brls::Logger::info("热重载后加载 {} 个插件", n);
                    brls::Application::notify(r.message);
                    render();
                } else {
                    brls::Dialog* d = new brls::Dialog("安装失败\n\n" + r.message);
                    d->addButton("知道了", []() {});
                    d->open();
                }
            },
            "输入插件包 (.zip) 路径", "例如 ~/Downloads/myplugin.zip", 512);
    }));

    list_->addView(makeRow(ui::Icon::Search, "重新扫描插件",
                           "热重载：扫描本地、创意工坊与 DLC 插件", [this]() {
        PluginManager::getInstance().unloadAll();
        size_t n = PluginManager::getInstance().loadAll();
        brls::Application::notify("已加载 " + std::to_string(n) + " 个插件");
        render();
    }));

    // ---- 已加载插件 ----
    auto plugins = PluginManager::getInstance().getPlugins();
    list_->addView(new SectionHeader("已加载插件 · " +
                                     std::to_string(plugins.size())));

    if (plugins.empty()) {
        list_->addView(new EmptyState(
            ui::Icon::Plugin, "尚未加载任何插件",
            "支持三类插件来源：本地安装、Steam 创意工坊订阅与 DLC 扩展\n"
            "提供视频源（WebDAV / Emby / Jellyfin）与刮削器能力"));
    }

    for (const auto& p : plugins) {
        std::string caps;
        if (p.has_video_source) caps += "视频源";
        if (p.has_scraper) {
            if (!caps.empty()) caps += " + ";
            caps += "刮削器";
        }
        if (caps.empty()) caps = "无可用扩展点";
        caps += "  ·  v" + p.version;
        if (!p.author.empty()) caps += "  ·  " + p.author;

        std::string badge = p.source_badge.empty() ? "[本地]" : p.source_badge;
        std::string title = badge + " " + p.name;
        std::string name = p.name;
        std::string path = p.path;
        const bool is_source = p.has_video_source;
        std::string stem = fs::path(p.path).stem().string();

        list_->addView(makeRow(ui::Icon::Plugin, title, caps,
                               [this, p, name, path, is_source, stem]() {
            brls::Box* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setWidth(560);
            box->setPaddingBottom(12.0f);

            // 先建对话框再建行：行的回调需要拿到 d 才能正确关闭本面板
            brls::Dialog* d = new brls::Dialog(box);
            box->addView(new SectionHeader(name));

            // 如果该插件提供视频源，增加直接进入 / 浏览入口
            if (is_source) {
                box->addView(makeRow(ui::Icon::Folder, "浏览视频源", "进入插件目录浏览与播放频道",
                                     [path, name, d]() {
                    d->close();
                    brls::Application::pushActivity(new PluginBrowserActivity(path, name));
                }, true));
            }

            std::string src_text = "本地插件目录";
            if (p.source_type == PluginSourceType::Workshop) {
                src_text = "Steam 创意工坊 (Item #" + p.workshop_item_id + ")";
            } else if (p.source_type == PluginSourceType::DLC) {
                src_text = "Steam DLC 扩展内容";
            }
            box->addView(makeRow(ui::Icon::Info, "插件来源", src_text, nullptr, false));

            if (p.is_js_plugin) {
                // 现代 JS 插件配置：动态从 manifest 的 settings 呈现
                auto js_plug = PluginManager::getInstance().getJSPlugin(p.id);
                if (!js_plug) js_plug = PluginManager::getInstance().getJSPlugin(p.path);
                if (js_plug && !js_plug->getSettings().empty()) {
                    box->addView(new SectionHeader("插件参数配置"));
                    for (const auto& field : js_plug->getSettings()) {
                        std::string cur_val = PluginConfigStore::getInstance().get(p.id, field.key);
                        if (cur_val.empty() && !field.default_value.empty()) {
                            cur_val = field.default_value;
                        }
                        std::string display_val = cur_val.empty() ? "未设置（点击填写）" : cur_val;
                        if (field.type == "password" && !cur_val.empty()) {
                            display_val = "••••••••";
                        }
                        std::string f_key = field.key;
                        std::string f_label = field.label;
                        std::string pid = p.id;
                        box->addView(makeRow(field.type == "password" ? ui::Icon::Lock : ui::Icon::Sliders,
                                             field.label, display_val,
                                             [this, pid, f_key, f_label, d]() {
                            d->close();
                            brls::Application::getImeManager()->openForText(
                                [this, pid, f_key, f_label](std::string v) {
                                    PluginConfigStore::getInstance().set(pid, f_key, v);
                                    PluginConfigStore::getInstance().save();
                                    PluginManager::getInstance().unloadAll();
                                    PluginManager::getInstance().loadAll();
                                    brls::Application::notify(f_label + (v.empty() ? " 已清空" : " 已保存"));
                                    render();
                                },
                                f_label, "输入配置值", 512);
                        }, true));
                    }
                    box->addView(makeRow(ui::Icon::Close, "重置该插件的配置",
                                         "恢复默认配置", [this, p, d]() {
                        d->close();
                        PluginConfigStore::getInstance().clear(p.id);
                        PluginConfigStore::getInstance().save();
                        PluginManager::getInstance().unloadAll();
                        PluginManager::getInstance().loadAll();
                        brls::Application::notify("配置已重置");
                        render();
                    }));
                } else if (!is_source) {
                    brls::Label* info = new brls::Label();
                    info->setText("该插件无需额外配置。");
                    info->setFontSize(ui::font::detail);
                    info->setMarginLeft(24.0f);
                    info->setMarginTop(8.0f);
                    info->setMarginBottom(8.0f);
                    box->addView(info);
                }
            } else if (is_source) {
                // 服务器地址
                std::string cur_url = PluginConfigStore::getInstance().get(stem, "url");
                box->addView(makeRow(ui::Icon::Drive, "服务器地址",
                                     cur_url.empty() ? "未设置（点击填写）" : cur_url,
                                     [this, stem, d]() {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, stem](std::string v) {
                            PluginConfigStore::getInstance().set(stem, "url", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除服务器地址"
                                                                : "服务器地址已保存");
                            render();
                        },
                        "服务器地址", "例如 http://192.168.1.10:5244/dav", 512);
                }, true));

                // 用户名
                std::string cur_user = PluginConfigStore::getInstance().get(stem, "user");
                box->addView(makeRow(ui::Icon::Info, "用户名",
                                     cur_user.empty() ? "未设置（可选）" : cur_user,
                                     [this, stem, d]() {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, stem](std::string v) {
                            PluginConfigStore::getInstance().set(stem, "user", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除用户名" : "用户名已保存");
                            render();
                        },
                        "用户名", "留空表示无鉴权", 128);
                }, true));

                // 密码
                std::string cur_pass = PluginConfigStore::getInstance().get(stem, "pass");
                box->addView(makeRow(ui::Icon::Lock, "密码",
                                     cur_pass.empty() ? "未设置（可选）" : "已设置",
                                     [this, stem, d]() {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, stem](std::string v) {
                            PluginConfigStore::getInstance().set(stem, "pass", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除密码" : "密码已保存");
                            render();
                        },
                        "密码", "留空表示无鉴权", 128);
                }, true));

                // 清除全部配置
                box->addView(makeRow(ui::Icon::Close, "清除该插件的配置",
                                     "服务器地址 / 用户名 / 密码全部删除", [this, stem, d]() {
                    d->close();
                    PluginConfigStore::getInstance().clear(stem);
                    PluginConfigStore::getInstance().save();
                    PluginManager::getInstance().unloadAll();
                    PluginManager::getInstance().loadAll();
                    brls::Application::notify("配置已清除");
                    render();
                }));
            } else {
                brls::Label* info = new brls::Label();
                info->setText("该插件没有可配置项。");
                info->setFontSize(ui::font::detail);
                info->setMarginLeft(24.0f);
                info->setMarginTop(8.0f);
                info->setMarginBottom(8.0f);
                box->addView(info);
            }

            d->addButton("关闭", []() {});
            d->open();
        }, true));
    }

    // ---- 已安装目录（含解压后未被加载的） ----
    auto dirs = PluginInstaller::listInstalledDirs();
    if (!dirs.empty()) {
        list_->addView(new SectionHeader("已安装插件包"));
        for (const auto& dir : dirs) {
            std::string d = dir;
            list_->addView(makeRow(
                ui::Icon::Plugin, fs::path(dir).filename().string(), "点击可卸载",
                [this, d]() {
                    brls::Dialog* dlg = new brls::Dialog(
                        "卸载插件包：" + fs::path(d).filename().string() + " ?");
                    dlg->addButton("取消", []() {});
                    dlg->addButton("卸载", [this, d]() {
                        if (PluginInstaller::uninstall(d)) {
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify("已卸载");
                            render();
                        } else {
                            brls::Application::notify("卸载失败");
                        }
                    });
                    dlg->open();
                }, true));
        }
    }

    // ---- 兼容性说明 ----
    list_->addView(new SectionHeader("兼容性说明"));

    brls::Label* note = new brls::Label();
    note->setText("插件必须导出统一入口符号 muvik_plugin_entry_v1，\n"
                  "且声明的 ABI 版本需与主程序一致，否则会被安全跳过。\n"
                  "这是为了防止跨编译器/标准库版本导致的内存布局错位崩溃。");
    note->setFontSize(ui::font::detail);
    note->setTextColor(nvgRGBA(140, 146, 156, 255));
    note->setMarginLeft(24.0f);
    note->setMarginTop(4.0f);
    list_->addView(note);
}

void PluginManagerActivity::revealPluginFolder() {
    std::string dir = AppPaths::pluginsDir();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        AppPaths::ensureDir(dir);
    }
    // 掌机环境没有文件管理器，这里只提示路径，由用户通过其他方式访问
    brls::Dialog* d = new brls::Dialog(
        "插件目录：\n" + dir +
        "\n\n在桌面模式下可通过该路径管理插件文件。\n"
        "放入或删除文件后，按 Y 键重新扫描即可热重载。");
    d->addButton("知道了", []() {});
    d->open();
}

} // namespace muvik
