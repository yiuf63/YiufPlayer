#include <algorithm>
#include <string>
#include <vector>
#include "activity/folder_picker_activity.hpp"
#include "utils/scanner.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/dialog.hpp>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace deck_player {

FolderPickerActivity::FolderPickerActivity(OnPick on_pick)
    : on_pick_(std::move(on_pick)) {}

FolderPickerActivity::~FolderPickerActivity() = default;

brls::View* FolderPickerActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle("选择文件夹");
    return frame_;
}

void FolderPickerActivity::onContentAvailable() {
    Activity::onContentAvailable();

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (!history_.empty()) {
            navigateUp();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });

    render();
}

std::string FolderPickerActivity::currentTitle() const {
    if (current_path_.empty()) return "选择存储位置";
    std::string name = fs::path(current_path_).filename().string();
    if (name.empty()) name = current_path_;
    return name;
}

void FolderPickerActivity::navigateTo(const std::string& path) {
    if (!current_path_.empty()) history_.push_back(current_path_);
    current_path_ = path;
    render();
}

void FolderPickerActivity::navigateUp() {
    if (history_.empty()) {
        if (!current_path_.empty()) {
            current_path_.clear();
            render();
        }
        return;
    }
    current_path_ = history_.back();
    history_.pop_back();
    render();
}

void FolderPickerActivity::render() {
    if (!list_ || !frame_) return;
    frame_->setTitle(this->currentTitle());
    list_->clearViews();

    if (current_path_.empty()) {
        // 顶层：列出可用的存储位置
        brls::Header* header = new brls::Header();
        header->setTitle("选择要浏览的存储位置");
        list_->addView(header);

        auto sources = MediaScanner::getInstance().getAvailableSources();
        for (const auto& src : sources) {
            brls::DetailCell* cell = new brls::DetailCell();
            cell->setText(src.name);
            cell->setDetailText(src.path);
            std::string target = src.path;
            cell->registerClickAction([this, target](brls::View*) {
                navigateTo(target);
                return true;
            });
            list_->addView(cell);
        }

        // 手动输入路径
        brls::DetailCell* manual = new brls::DetailCell();
        manual->setText("✏️ 手动输入路径…");
        manual->setDetailText("适用于网络挂载点或未自动探测的目录");
        manual->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText(
                [this](std::string text) {
                    std::error_code ec;
                    if (text.empty()) return;
                    if (fs::exists(text, ec) && fs::is_directory(text, ec)) {
                        navigateTo(text);
                    } else {
                        brls::Application::notify("路径不存在或不是文件夹");
                    }
                },
                "输入文件夹绝对路径", "路径", 512);
            return true;
        });
        list_->addView(manual);
        return;
    }

    // 进入某目录
    brls::DetailCell* up = new brls::DetailCell();
    up->setText("⬆️ .. [返回上一级]");
    up->registerClickAction([this](brls::View*) {
        navigateUp();
        return true;
    });
    list_->addView(up);

    // 「选择当前文件夹」
    brls::DetailCell* pick_here = new brls::DetailCell();
    pick_here->setText("✅ 添加当前文件夹");
    pick_here->setDetailText("将整个 " + currentTitle() + " 文件夹加入媒体库");
    std::string here = current_path_;
    pick_here->registerClickAction([this, here](brls::View*) {
        if (on_pick_) on_pick_(here);
        brls::Application::popActivity();
        return true;
    });
    list_->addView(pick_here);

    brls::Header* sub = new brls::Header();
    sub->setTitle("子文件夹");
    list_->addView(sub);

    std::error_code ec;
    std::vector<std::string> dirs;
    fs::directory_iterator it(fs::path(current_path_),
        fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code e2;
            if (!entry.is_directory(e2)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            dirs.push_back(entry.path().string());
        }
    }
    std::sort(dirs.begin(), dirs.end());

    if (dirs.empty()) {
        brls::Label* empty = new brls::Label();
        empty->setText("该文件夹下没有子文件夹，可直接点击【添加当前文件夹】");
        empty->setMarginTop(20);
        empty->setMarginLeft(20);
        list_->addView(empty);
    } else {
        for (const auto& d : dirs) {
            brls::DetailCell* cell = new brls::DetailCell();
            std::string name = fs::path(d).filename().string();
            cell->setText("[文件夹] " + name);
            cell->registerClickAction([this, d](brls::View*) {
                navigateTo(d);
                return true;
            });
            list_->addView(cell);
        }
    }
}

} // namespace deck_player
