#include <functional>
#include <string>
#include <vector>
#include "view/view_sheet.hpp"

#include <borealis/core/touch/tap_gesture.hpp>

#include "model/library_view_options.hpp"
#include "ui/design.hpp"
#include "utils/i18n.hpp"
#include "view/tool_button.hpp"

namespace muvik {

namespace {

constexpr float PANEL_W = 500.0f;
constexpr float PANEL_BOTTOM = 88.0f;
constexpr float PAD = 20.0f;
constexpr float ROW_H = 44.0f;
constexpr float LABEL_W = 56.0f;

// Box 本身不画背景（borealis 里只有 Rectangle 画），而这一层需要
// 「圆角 + 半透明」的底。重写 draw：先铺底，再交给基类画子项。
class CardBox : public brls::Box {
public:
    CardBox(float radius, NVGcolor color) : radius_(radius), color_(color) {}

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        if (width > 0.5f && height > 0.5f) {
            ui::fillRoundRect(vg, x, y, width, height, radius_, color_);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

private:
    float radius_;
    NVGcolor color_;
};

brls::Label* makeLabel(const std::string& text, float size, NVGcolor color) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    l->setVerticalAlign(brls::VerticalAlign::CENTER);
    l->setSingleLine(true);
    return l;
}

} // namespace

ViewSheet::ViewSheet(std::function<void()> on_change, std::function<void()> on_close)
    : on_change_(std::move(on_change)), on_close_(std::move(on_close)) {
    // 遮罩层：绝对定位铺满宿主（宿主是媒体库整页 library_root_）。
    // 不绝对定位的话，COLUMN 布局会给它分一个正常格子，把列表挤下去。
    const auto& pal = ui::palette();
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0.0f);
    this->setPositionLeft(0.0f);
    this->setPositionRight(0.0f);
    this->setPositionBottom(0.0f);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::CENTER);
    // 点击遮罩空白处关闭。panel 自己吃掉点击，否则点面板也会关闭。
    this->registerClickAction([this](brls::View*) {
        this->hide();
        return true;
    });
    this->registerAction(i18n::tr("common.close", "关闭"), brls::ControllerButton::BUTTON_B,
                         [this](brls::View*) {
                             this->hide();
                             return true;
                         });
    this->registerAction(i18n::tr("common.close", "关闭"), brls::ControllerButton::BUTTON_BACK,
                         [this](brls::View*) {
                             this->hide();
                             return true;
                         });
    this->registerAction(i18n::tr("common.close", "关闭"), brls::ControllerButton::BUTTON_START,
                         [this](brls::View*) {
                             this->hide();
                             return true;
                         });
    this->registerAction(i18n::tr("common.close", "关闭"), brls::ControllerButton::BUTTON_LB,
                         [this](brls::View*) {
                             this->hide();
                             return true;
                         });
    this->registerAction(i18n::tr("common.close", "关闭"), brls::ControllerButton::BUTTON_RB,
                         [this](brls::View*) {
                             this->hide();
                             return true;
                         });

    panel_ = new CardBox(ui::radius::lg, pal.surfaceRaised);
    panel_->setAxis(brls::Axis::COLUMN);
    panel_->setWidth(PANEL_W);
    panel_->setMarginBottom(PANEL_BOTTOM);
    panel_->setPadding(PAD);
    panel_->registerClickAction([](brls::View*) { return true; });
    this->addView(panel_);
}

void ViewSheet::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx) {
    if (width > 0.5f && height > 0.5f) {
        // 深色：55% 黑压暗；浅色：暖白「蒙纱」。原型用的是「压暗 + 虚化」，
        // nanoVG 没有 backdrop blur，压暗是能保留的那一半 —— 有它，
        // 这一屏的注意力才会落在菜单上。色值跟随主题色板。
        const auto& pal = ui::palette();
        ui::fillRoundRect(vg, x, y, width, height, 0.0f,
                          ui::rgba(pal.scrim, pal.scrimAlpha));
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void ViewSheet::show(brls::Box* parent) {
    if (open_) return;
    open_ = true;
    if (this->hasParent()) {
        this->getParent()->removeView(this, false);
    }
    parent->addView(this);
    // 打开后方向键立刻可用：焦点给第一枚胶囊，否则方向键还在
    // 操作遮罩背后的列表，用户看着菜单、移的却是卡片。
    rebuild();   // 重新构建以同步外部可能变化的选项
    if (!chips_.empty() && chips_.front().btn) {
        brls::Application::giveFocus(chips_.front().btn);
    } else if (panel_) {
        brls::Application::giveFocus(panel_->getDefaultFocus());
    }
}

brls::View* ViewSheet::getDefaultFocus() {
    if (!chips_.empty() && chips_.front().btn) {
        return chips_.front().btn;
    }
    if (panel_) return panel_->getDefaultFocus();
    return this;
}

brls::View* ViewSheet::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    // 模态浮层焦点锁定：绝不允许焦点逃逸到 ViewSheet 遮罩外部的工具栏、侧边栏或媒体列表！
    (void)direction;
    (void)currentView;
    return nullptr;
}

brls::View* ViewSheet::getParentNavigationDecision(brls::View* from, brls::View* newFocus, brls::FocusDirection direction) {
    (void)from;
    (void)direction;
    // 阻止任何祖先试图将焦点切离 ViewSheet 模态层
    if (open_) return nullptr;
    return newFocus;
}

void ViewSheet::hide() {
    if (!open_) return;
    open_ = false;
    // free=false：hide() 往往是从本视图自己的点击回调里调进来的，
    // removeView 默认会 delete this —— 回调还没返回对象就没了。
    // 宿主持有本指针，detach 后下次 show() 复用。
    if (brls::Box* parent = this->getParent()) parent->removeView(this, false);
    if (on_close_) on_close_();
}

void ViewSheet::notifyChanged() {
    LibraryViewOptions::get().save();
    if (on_change_) on_change_();
}

void ViewSheet::syncChips() {
    for (auto& c : chips_) {
        if (c.btn) c.btn->setActive(c.is_active());
    }
    if (summary_) summary_->setText(LibraryViewOptions::get().summary());
}

brls::Box* ViewSheet::makeRow(
    const std::string& label,
    const std::vector<std::pair<std::string, std::function<bool()>>>& opts,
    std::function<void(size_t)> on_pick) {
    const auto& pal = ui::palette();

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setHeight(ROW_H);
    row->setWidthPercentage(100.0f);

    auto* name = makeLabel(label, ui::font::caption, pal.textMute);
    name->setWidth(LABEL_W);
    row->addView(name);

    for (size_t i = 0; i < opts.size(); ++i) {
        const bool on = opts[i].second();
        auto* chip = new ToolButton(opts[i].first, ui::Icon::None, 13.0f);
        chip->setCompact(true);
        chip->setActive(on);
        if (i + 1 < opts.size()) chip->setMarginRight(6.0f);

        const size_t idx = i;
        chip->registerClickAction([on_pick, idx](brls::View*) {
            on_pick(idx);
            return true;
        });
        row->addView(chip);

        // 记下判定函数而不是快照：选项变化后原地刷新亮灭，
        // 不重建视图 —— 重建会把正在处理这次点击的按钮本身删掉。
        chips_.push_back({chip, opts[i].second});
    }
    return row;
}

void ViewSheet::rebuild() {
    if (!panel_) return;
    chips_.clear();
    summary_ = nullptr;
    panel_->clearViews();

    auto& o = LibraryViewOptions::get();
    auto* p = &o;   // lambda 捕获指针：捕获局部引用在函数返回后是悬垂的
    const auto& pal = ui::palette();

    // ── 标题 + 当前摘要 ──
    {
        auto* head = new brls::Box();
        head->setAxis(brls::Axis::ROW);
        head->setAlignItems(brls::AlignItems::CENTER);
        head->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        head->setWidthPercentage(100.0f);
        head->setMarginBottom(2.0f);

        head->addView(makeLabel(i18n::tr("view_sheet.title", "视图与排序"), ui::font::title, pal.text));

        auto* closeBtn = new ToolButton(i18n::tr("common.close", "关闭"), ui::Icon::Close, 13.0f);
        closeBtn->setCompact(true);
        closeBtn->registerClickAction([this](brls::View*) {
            hide();
            return true;
        });
        head->addView(closeBtn);
        panel_->addView(head);

        summary_ = makeLabel(p->summary(), ui::font::caption, pal.textMute);
        summary_->setWidthPercentage(100.0f);
        summary_->setMarginBottom(8.0f);
        panel_->addView(summary_);
    }

    // ── 分组：结构是两层还是一层 ──
    panel_->addView(makeRow(i18n::tr("view_sheet.group", "分组"),
        {{i18n::tr("view_sheet.group_folder", "按文件夹"), [p]() { return p->group == LibraryGroup::Folder; }},
         {i18n::tr("view_sheet.group_flat", "全部平铺"), [p]() { return p->group == LibraryGroup::Flat; }}},
        [this, p](size_t i) {
            p->group = (i == 0) ? LibraryGroup::Folder : LibraryGroup::Flat;
            syncChips();
            notifyChanged();
        }));

    // ── 内容 ──
    panel_->addView(makeRow(i18n::tr("view_sheet.content", "内容"),
        {{i18n::tr("view_sheet.content_all", "视频与音乐"), [p]() { return p->content == LibraryContent::All; }},
         {i18n::tr("view_sheet.content_video", "仅视频"), [p]() { return p->content == LibraryContent::VideoOnly; }},
         {i18n::tr("view_sheet.content_audio", "仅音乐"), [p]() { return p->content == LibraryContent::AudioOnly; }}},
        [this, p](size_t i) {
            p->content = (i == 0) ? LibraryContent::All
                      : (i == 1) ? LibraryContent::VideoOnly
                                 : LibraryContent::AudioOnly;
            syncChips();
            notifyChanged();
        }));

    // ── 排序 ──
    panel_->addView(makeRow(i18n::tr("view_sheet.sort", "排序"),
        {{i18n::tr("view_sheet.sort_name", "名称"), [p]() { return p->sort == LibrarySort::Name; }},
         {i18n::tr("view_sheet.sort_recent", "最近添加"), [p]() { return p->sort == LibrarySort::Recent; }},
         {i18n::tr("view_sheet.sort_size", "大小"), [p]() { return p->sort == LibrarySort::Size; }},
         {i18n::tr("view_sheet.sort_progress", "进度"), [p]() { return p->sort == LibrarySort::Progress; }}},
        [this, p](size_t i) {
            p->sort = (i == 0) ? LibrarySort::Name
                   : (i == 1) ? LibrarySort::Recent
                   : (i == 2) ? LibrarySort::Size
                              : LibrarySort::Progress;
            syncChips();
            notifyChanged();
        }));

    // ── 筛选 ──
    panel_->addView(makeRow(i18n::tr("view_sheet.filter", "筛选"),
        {{i18n::tr("view_sheet.filter_all", "不限"), [p]() { return p->filter == LibraryFilter::All; }},
         {i18n::tr("view_sheet.filter_unfinished", "未看完"), [p]() { return p->filter == LibraryFilter::Unfinished; }},
         {i18n::tr("view_sheet.filter_finished", "已看完"), [p]() { return p->filter == LibraryFilter::Finished; }}},
        [this, p](size_t i) {
            p->filter = (i == 0) ? LibraryFilter::All
                      : (i == 1) ? LibraryFilter::Unfinished
                                 : LibraryFilter::Finished;
            syncChips();
            notifyChanged();
        }));

    // ── 分隔线 ──
    {
        auto* line = new CardBox(0.0f, ui::rgba(pal.outline, 0.7f));
        line->setWidthPercentage(100.0f);
        line->setHeight(1.0f);
        line->setMarginTop(6.0f);
        line->setMarginBottom(10.0f);
        panel_->addView(line);
    }

    // ── 底部：升降序 + 恢复默认 ──
    {
        auto* foot = new brls::Box();
        foot->setAxis(brls::Axis::ROW);
        foot->setAlignItems(brls::AlignItems::CENTER);
        foot->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        foot->setWidthPercentage(100.0f);

        auto* dirGroup = new brls::Box();
        dirGroup->setAxis(brls::Axis::ROW);
        dirGroup->setAlignItems(brls::AlignItems::CENTER);

        auto* asc = new ToolButton(i18n::tr("view_sheet.order_asc", "升序"), ui::Icon::None, 13.0f);
        asc->setCompact(true);
        asc->setMarginRight(6.0f);
        asc->setActive(!p->descending);
        asc->registerClickAction([this](brls::View*) {
            LibraryViewOptions::get().descending = false;
            syncChips();
            notifyChanged();
            return true;
        });
        dirGroup->addView(asc);
        chips_.push_back({asc, []() { return !LibraryViewOptions::get().descending; }});

        auto* desc = new ToolButton(i18n::tr("view_sheet.order_desc", "降序"), ui::Icon::None, 13.0f);
        desc->setCompact(true);
        desc->setActive(p->descending);
        desc->registerClickAction([this](brls::View*) {
            LibraryViewOptions::get().descending = true;
            syncChips();
            notifyChanged();
            return true;
        });
        dirGroup->addView(desc);
        chips_.push_back({desc, []() { return LibraryViewOptions::get().descending; }});

        foot->addView(dirGroup);

        auto* reset = new ToolButton(i18n::tr("view_sheet.reset", "恢复默认"), ui::Icon::None, 13.0f);
        reset->setCompact(true);
        reset->registerClickAction([this](brls::View*) {
            LibraryViewOptions::get().reset();
            syncChips();
            notifyChanged();
            return true;
        });
        foot->addView(reset);

        panel_->addView(foot);
    }
}

} // namespace muvik
