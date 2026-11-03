#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include "activity/plugin_manager_activity.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include "utils/app_paths.hpp"
#include "plugin/plugin_installer.hpp"
#include "plugin/plugin_config_store.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/dialog.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace deck_player {

brls::View* PluginManagerActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
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
    if (std::getenv("DECKPLAYER_OPEN_DIALOG")) {
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

    // ---- 说明 ----
    brls::Header* h1 = new brls::Header();
    h1->setTitle("插件目录");
    list_->addView(h1);

    brls::DetailCell* dir_cell = new brls::DetailCell();
    dir_cell->setText("[目录] " + AppPaths::pluginsDir());
    dir_cell->setDetailText("将 .so / .dylib 插件放入此目录后重新扫描");
    dir_cell->registerClickAction([this](brls::View*) {
        revealPluginFolder();
        return true;
    });
    list_->addView(dir_cell);

    brls::DetailCell* install_cell = new brls::DetailCell();
    install_cell->setText("📦 从 ZIP 包安装插件");
    install_cell->setDetailText("输入 .zip 路径，自动解压并校验");
    install_cell->registerClickAction([this](brls::View*) {
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
        return true;
    });
    list_->addView(install_cell);

    brls::DetailCell* rescan_cell = new brls::DetailCell();
    rescan_cell->setText("重新扫描插件");
    rescan_cell->setDetailText("热重载：卸载全部插件后重新加载");
    rescan_cell->registerClickAction([this](brls::View*) {
        PluginManager::getInstance().unloadAll();
        size_t n = PluginManager::getInstance().loadAll();
        brls::Application::notify("已加载 " + std::to_string(n) + " 个插件");
        render();
        return true;
    });
    list_->addView(rescan_cell);

    // ---- 已加载插件 ----
    auto plugins = PluginManager::getInstance().getPlugins();
    brls::Header* h2 = new brls::Header();
    h2->setTitle("已加载插件 (" + std::to_string(plugins.size()) + ")");
    list_->addView(h2);

    if (plugins.empty()) {
        brls::Label* empty = new brls::Label();
        empty->setText("尚未加载任何插件。\n\n插件可提供两类能力：\n"
                       "  · 视频源：接入 Emby / Jellyfin / 自建站等\n"
                       "  · 刮削源：为本地媒体补齐封面与简介\n\n"
                       "接口为纯 C ABI，插件可独立于主程序编译。");
        empty->setMarginTop(20);
        empty->setMarginLeft(20);
        list_->addView(empty);
        return;
    }

    for (const auto& p : plugins) {
        brls::DetailCell* cell = new brls::DetailCell();
        cell->setText("🧩 " + p.name + "  v" + p.version);

        std::string caps;
        if (p.has_video_source) caps += "视频源";
        if (p.has_scraper) {
            if (!caps.empty()) caps += " + ";
            caps += "刮削器";
        }
        if (caps.empty()) caps = "无可用扩展点";
        cell->setDetailText(caps + (p.author.empty() ? "" : "  ·  " + p.author));

        std::string name = p.name;
        std::string path = p.path;
        bool is_source = p.has_video_source;
        std::string stem = fs::path(p.path).stem().string();
        cell->registerClickAction([this, name, path, is_source, stem](brls::View*) {
            brls::Box* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setWidth(520);

            brls::Header* h = new brls::Header();
            h->setTitle(name);
            box->addView(h);

            brls::Label* info = new brls::Label();
            info->setText(fs::path(path).filename().string());
            info->setMarginLeft(20);
            info->setMarginBottom(12);
            box->addView(info);

            brls::Dialog* d = new brls::Dialog(box);

            if (is_source) {
                // 服务器地址
                brls::DetailCell* url_cell = new brls::DetailCell();
                std::string cur_url = PluginConfigStore::getInstance().get(stem, "url");
                url_cell->setText("🌐 服务器地址");
                url_cell->setDetailText(cur_url.empty() ? "未设置（点击填写）" : cur_url);
                std::string s_stem = stem;
                url_cell->registerClickAction([this, d, s_stem](brls::View*) {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, s_stem](std::string v) {
                            PluginConfigStore::getInstance().set(s_stem, "url", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除服务器地址"
                                                                : "服务器地址已保存");
                            render();
                        },
                        "服务器地址", "例如 http://192.168.1.10:5244/dav", 512);
                    return true;
                });
                box->addView(url_cell);

                // 用户名
                brls::DetailCell* user_cell = new brls::DetailCell();
                std::string cur_user = PluginConfigStore::getInstance().get(stem, "user");
                user_cell->setText("👤 用户名");
                user_cell->setDetailText(cur_user.empty() ? "未设置（可选）" : cur_user);
                user_cell->registerClickAction([this, d, s_stem](brls::View*) {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, s_stem](std::string v) {
                            PluginConfigStore::getInstance().set(s_stem, "user", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除用户名" : "用户名已保存");
                            render();
                        },
                        "用户名", "留空表示无鉴权", 128);
                    return true;
                });
                box->addView(user_cell);

                // 密码
                brls::DetailCell* pass_cell = new brls::DetailCell();
                std::string cur_pass = PluginConfigStore::getInstance().get(stem, "pass");
                pass_cell->setText("🔑 密码");
                pass_cell->setDetailText(cur_pass.empty() ? "未设置（可选）"
                                                          : std::string("已设置"));
                pass_cell->registerClickAction([this, d, s_stem](brls::View*) {
                    d->close();
                    brls::Application::getImeManager()->openForText(
                        [this, s_stem](std::string v) {
                            PluginConfigStore::getInstance().set(s_stem, "pass", v);
                            PluginConfigStore::getInstance().save();
                            PluginManager::getInstance().unloadAll();
                            PluginManager::getInstance().loadAll();
                            brls::Application::notify(v.empty() ? "已清除密码" : "密码已保存");
                            render();
                        },
                        "密码", "留空表示无鉴权", 128);
                    return true;
                });
                box->addView(pass_cell);

                // 清除全部配置
                brls::DetailCell* clear_cell = new brls::DetailCell();
                clear_cell->setText("清除该插件的配置");
                clear_cell->registerClickAction([this, d, s_stem](brls::View*) {
                    d->close();
                    PluginConfigStore::getInstance().clear(s_stem);
                    PluginConfigStore::getInstance().save();
                    PluginManager::getInstance().unloadAll();
                    PluginManager::getInstance().loadAll();
                    brls::Application::notify("配置已清除");
                    render();
                    return true;
                });
                box->addView(clear_cell);
            }

            d->addButton("关闭", []() {});
            d->open();
            return true;
        });
        list_->addView(cell);
    }

    // ---- 已安装目录（含解压后未被加载的） ----
    auto dirs = PluginInstaller::listInstalledDirs();
    if (!dirs.empty()) {
        brls::Header* hd = new brls::Header();
        hd->setTitle("已安装插件包");
        list_->addView(hd);
        for (const auto& dir : dirs) {
            brls::DetailCell* cell = new brls::DetailCell();
            cell->setText("📦 " + fs::path(dir).filename().string());
            cell->setDetailText("点击可卸载");
            std::string d = dir;
            cell->registerClickAction([this, d](brls::View*) {
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
                return true;
            });
            list_->addView(cell);
        }
    }

    // ---- 被拒绝的插件提示 ----
    brls::Header* h3 = new brls::Header();
    h3->setTitle("兼容性说明");
    list_->addView(h3);

    brls::Label* note = new brls::Label();
    note->setText("插件必须导出统一入口符号 deck_player_plugin_entry_v1，\n"
                  "且声明的 ABI 版本需与主程序一致，否则会被安全跳过。\n"
                  "这是为了防止跨编译器/标准库版本导致的内存布局错位崩溃。");
    note->setMarginLeft(20);
    note->setMarginTop(10);
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

} // namespace deck_player
