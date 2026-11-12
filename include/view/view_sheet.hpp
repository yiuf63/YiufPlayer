#pragma once

// 「视图与排序」呼出菜单 —— 媒体库页内的浮层，不是独立 Activity。
//
// 为什么不用 Activity：borealis 的 Activity 栈是「只画栈顶」，
// 推入菜单后背后的媒体库根本不在画面里，用户要的「背景淡出、
// 菜单是唯一焦点」就无从谈起。浮层则是一张绝对定位的遮罩，
// 盖在媒体库整页上：列表还在下面，只是被压暗了。
//
// 视觉跟着原型：贴底居中、宽 460、四组选项各一行、底部升序降序 + 恢复默认；
// 背景压暗（nanoVG 没有 backdrop blur，用 55% 黑替代原型的虚化）。
// 焦点进菜单：打开时给第一枚胶囊，方向键走不到遮罩背后的卡片；
// B 或点遮罩关闭，焦点交还给列表。

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <borealis.hpp>

#include "ui/icons.hpp"

namespace muvik {

class ToolButton;

class ViewSheet : public brls::Box {
public:
    // on_change：任一选项变化后触发（列表要跟着重排）。
    // on_close：菜单关闭后触发（宿主把焦点接回列表）。
    ViewSheet(std::function<void()> on_change, std::function<void()> on_close);

    using brls::Box::show;
    using brls::Box::hide;
    void show(brls::Box* parent);
    void hide();
    bool isOpen() const { return open_; }

    // 画压暗底。Box 不自带背景，不重写这一层就是透明的 ——
    // 菜单浮在原亮度列表上，读不出「这一屏只有菜单要读」。
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    void rebuild();
    void syncChips();
    void notifyChanged();

    // 一行：左侧分组名，右侧一排胶囊。
    // opts 每项是「文字 + 当前是否选中」的判定函数，判定在选项变化后被
    // 重新调用（不是快照），所以胶囊的亮灭能原地刷新。
    brls::Box* makeRow(
        const std::string& label,
        const std::vector<std::pair<std::string, std::function<bool()>>>& opts,
        std::function<void(size_t)> on_pick);

    std::function<void()> on_change_;
    std::function<void()> on_close_;
    brls::Box* panel_ = nullptr;
    brls::Label* summary_ = nullptr;
    bool open_ = false;

    // 每枚胶囊配一个「当前是否该亮着」的判定。
    // 选项变化后原地更新，不重建视图 —— 重建会把正在处理这次点击的
    // 按钮本身删掉，之后回调返回时就在访问已释放内存。
    struct ChipRef {
        ToolButton* btn;
        std::function<bool()> is_active;
    };
    std::vector<ChipRef> chips_;
};

} // namespace muvik
