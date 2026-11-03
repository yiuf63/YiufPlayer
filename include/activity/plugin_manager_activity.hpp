#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <vector>
#include <memory>
#include <atomic>

#include <borealis.hpp>
#include "plugin/plugin_manager.hpp"
#include <functional>

namespace deck_player {

// 插件管理面板：展示已加载插件的状态，提供重新扫描与打开目录的入口。
// 手柄与触控均可操作。
class PluginManagerActivity : public brls::Activity {
public:
    PluginManagerActivity() = default;
    ~PluginManagerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void render();
    void revealPluginFolder();

    brls::AppletFrame* frame_ = nullptr;
    brls::ScrollingFrame* scroller_ = nullptr;
    brls::Box* list_ = nullptr;
    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace deck_player
