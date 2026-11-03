#pragma once

#include <functional>
#include <string>
#include <vector>
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>
#include "model/media_item.hpp"

namespace deck_player {

// 目录选择器：手柄 / 触控均可操作的文件夹浏览界面。
// 用于「添加媒体文件夹」，不假定存在系统文件管理器。
class FolderPickerActivity : public brls::Activity {
public:
    using OnPick = std::function<void(const std::string& path)>;

    explicit FolderPickerActivity(OnPick on_pick);
    ~FolderPickerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void render();
    void navigateTo(const std::string& path);
    void navigateUp();
    std::string currentTitle() const;

    OnPick on_pick_;
    std::string current_path_;
    std::vector<std::string> history_;

    brls::AppletFrame* frame_ = nullptr;
    brls::ScrollingFrame* scroller_ = nullptr;
    brls::Box* list_ = nullptr;
};

} // namespace deck_player
