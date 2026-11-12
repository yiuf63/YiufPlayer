/*
    Copyright 2019-2021 natinusala
    Copyright 2019 WerWolv
    Copyright 2019 p-sam

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/sidebar.hpp>

using namespace brls::literals;

namespace brls
{

const std::string sidebarItemXML = R"xml(
    <brls:Box
        width="auto"
        height="@style/brls/sidebar/item_height"
        alignItems="center"
        focusable="true" >

        <brls:Rectangle
            id="brls/sidebar/item_accent"
            width="@style/brls/sidebar/item_accent_rect_width"
            height="26"
            visibility="invisible"
            color="@theme/brls/accent"
            marginTop="0"
            marginBottom="0"
            marginLeft="@style/brls/sidebar/item_accent_margin_sides"
            marginRight="@style/brls/sidebar/item_accent_margin_sides" />

        <!-- Muvik 补丁：条目图标。
             上游 Sidebar 只有文字，窄侧栏（160）里四个汉字撑不满一行，
             右侧会空出一大片。加一个 18px 图标后侧栏才立得住。
             纹理统一走 drawIcon 的矢量路径不方便（XML 里只能放 Image），
             所以这里收 PNG：resources/icon/nav/*.png，36×36 供 2x 采样。 -->
        <brls:Image
            id="brls/sidebar/item_icon"
            width="18"
            height="18"
            marginRight="11"
            visibility="gone" />

        <brls:Label
            id="brls/sidebar/item_label"
            width="auto"
            height="auto"
            grow="1.0"
            singleLine="true"
            fontSize="@style/brls/sidebar/item_font_size"
            marginTop="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginBottom="@style/brls/sidebar/item_accent_margin_top_bottom"
            marginRight="@style/brls/sidebar/item_accent_margin_sides" />

    </brls:Box>
)xml";

SidebarItem::SidebarItem()
    : Box(Axis::ROW)
{
    this->inflateFromXMLString(sidebarItemXML);

    this->registerStringXMLAttribute("label", [this](std::string value)
        { this->setLabel(value); });

    this->setFocusSound(SOUND_FOCUS_SIDEBAR);

    this->registerAction(
        "hints/ok"_i18n, BUTTON_A, [](View* view)
        {
            Application::onControllerButtonPressed(BUTTON_NAV_RIGHT, false);
            return true; },
        false, false, SOUND_CLICK_SIDEBAR);

    this->addGestureRecognizer(new TapGestureRecognizer([this](TapGestureStatus status, Sound* soundToPlay)
        {
        if (this->active)
            return;

        this->playClickAnimation(status.state != GestureState::UNSURE);

        switch (status.state)
        {
            case GestureState::UNSURE:
                *soundToPlay = SOUND_FOCUS_SIDEBAR;
                break;
            case GestureState::FAILED:
            case GestureState::INTERRUPTED:
                *soundToPlay = SOUND_TOUCH_UNFOCUS;
                break;
            case GestureState::END:
                *soundToPlay = SOUND_CLICK_SIDEBAR;
                Application::giveFocus(this);
                break;
            default:
                break;
        } }));
}

void SidebarItem::setActive(bool active)
{
    if (active == this->active)
        return;

    Theme theme = Application::getTheme();

    if (active)
    {
        this->activeEvent.fire(this);

        this->accent->setVisibility(Visibility::VISIBLE);
        this->accent->setColor(theme["brls/accent"]);
        this->label->setTextColor(theme["brls/sidebar/active_item"]);
    }
    else
    {
        this->accent->setVisibility(Visibility::INVISIBLE);
        this->label->setTextColor(theme["brls/sidebar/inactive_item"]);
    }

    this->active = active;
}

void SidebarItem::onFocusGained()
{
    Box::onFocusGained();

    if (this->group)
        this->group->setActive(this);
}

void SidebarItem::onFocusLost()
{
    Box::onFocusLost();
}

void SidebarItem::setGroup(SidebarItemGroup* group)
{
    this->group = group;

    if (group)
        group->add(this);
}

GenericEvent* SidebarItem::getActiveEvent()
{
    return &this->activeEvent;
}

void SidebarItem::setLabel(std::string label)
{
    this->label->setText(label);
}

void SidebarItem::setIcon(std::string path)
{
    if (path.empty())
    {
        this->icon->setVisibility(Visibility::GONE);
        return;
    }

    this->icon->setImageFromFile(path);
    this->icon->setVisibility(Visibility::VISIBLE);
}

Sidebar::Sidebar()
{
    Style style = Application::getStyle();

    this->setScrollingBehavior(ScrollingBehavior::CENTERED);
    this->setBackground(ViewBackground::SIDEBAR);

    // Create content box
    this->contentBox = new Box(Axis::COLUMN);

    this->contentBox->setPadding(
        style["brls/sidebar/padding_top"],
        style["brls/sidebar/padding_right"],
        style["brls/sidebar/padding_bottom"],
        style["brls/sidebar/padding_left"]);

    this->setContentView(this->contentBox);
    this->setScrollingIndicatorVisible(false);
}

void Sidebar::addItem(std::string label, GenericEvent::Callback focusCallback)
{
    SidebarItem* item = new SidebarItem();
    item->setGroup(&this->group);
    item->setLabel(label);
    item->getActiveEvent()->subscribe(focusCallback);

    this->contentBox->addView(item);
}

SidebarItem* Sidebar::getItem(int position)
{
    // Muvik 补丁：上游直接下标访问 std::vector，越界拿到的是野指针，
    // 随后在 dynamic_cast 里 SEGV。崩溃堆栈只显示
    //   dyn_cast_get_derived_info ← __dynamic_cast ← createContentView
    // 完全看不出是「索引越界」，极难定位。
    //
    // 调用方确实会探边界地查：TabFrame::addTab 用
    //   while (getItem(idx)) ++idx;
    // 数条目数量，越界是正常路径而不是错误。
    auto& children = this->contentBox->getChildren();
    if (position < 0 || position >= (int)children.size())
        return nullptr;

    return dynamic_cast<SidebarItem*>(children[position]);
}

void Sidebar::addSeparator()
{
    this->contentBox->addView(new SidebarSeparator());
}

void Sidebar::clearItems()
{
    this->contentBox->clearViews();
    group.clear();
}

View* Sidebar::create()
{
    return new Sidebar();
}

void SidebarItemGroup::add(SidebarItem* item)
{
    this->items.push_back(item);
}

void SidebarItemGroup::setActive(SidebarItem* active)
{
    for (SidebarItem* item : this->items)
    {
        if (item == active)
            item->setActive(true);
        else
            item->setActive(false);
    }
}

void SidebarItemGroup::clear()
{
    this->items.clear();
}

SidebarSeparator::SidebarSeparator()
{
    Style style = Application::getStyle();

    this->setHeight(style["brls/sidebar/separator_height"]);
}

void SidebarSeparator::draw(NVGcontext* vg, float x, float y, float width, float height, Style style, FrameContext* ctx)
{
    float midY = y + height / 2;

    nvgBeginPath(vg);
    nvgFillColor(vg, a(ctx->theme["brls/sidebar/separator"]));
    nvgRect(vg, x, midY, width, 1);
    nvgFill(vg);
}

} // namespace brls
