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

#include <tinyxml2.h>
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/util.hpp>
#include <borealis/views/tab_frame.hpp>

using namespace brls::literals;

const std::string tabFrameContentXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        axis="row">

        <brls:Sidebar
            id="brls/tab_frame/sidebar"
            width="@style/brls/tab_frame/sidebar_width"
            height="auto" />

        <!-- Content will be injected here with grow="1.0" -->

    </brls:Box>
)xml";

namespace brls
{

TabFrame::TabFrame()
{
    this->inflateFromXMLString(tabFrameContentXML);
}

void TabFrame::addTab(std::string label, TabViewCreator creator)
{
    this->sidebar->addItem(label, [this, creator](brls::View* view) {
        // Only trigger when the sidebar item gains focus
        if (!view->isFocused())
            return;

        // Remove the existing tab if it exists
        if (this->activeTab)
        {
            this->removeView(this->activeTab); // will call willDisappear and delete
            this->activeTab = nullptr;
        }

        // Add the new tab
        View* newContent = creator();

        if (!newContent)
            return;

        newContent->setGrow(1.0f);
        this->addView(newContent); // addView calls willAppear

        this->activeTab = newContent;

        // 记录当前标签序号（供 selectTab/getActiveIndex 使用）
        int idx = 0;
        while (SidebarItem* it = this->sidebar->getItem(idx))
        {
            if (it == view) { this->activeIndex = idx; break; }
            if (++idx > 64) break;
        }

        newContent->registerAction(
            "hints/back"_i18n, BUTTON_B, [this](View* view) {
                if (Application::getInputType() == InputType::TOUCH)
                    this->dismiss();
                else
                    Application::giveFocus(this->sidebar);
                return true;
            },
            false, false, SOUND_BACK);
    });
}

void TabFrame::addTab(std::string label, std::string icon, TabViewCreator creator)
{
    this->addTab(label, creator);

    if (icon.empty())
        return;

    // Sidebar::addItem 不返回新建的条目，按「最后一个」取回来。
    int idx = 0;
    while (this->sidebar->getItem(idx))
        ++idx;
    if (SidebarItem* last = this->sidebar->getItem(idx - 1))
        last->setIcon(icon);
}

void TabFrame::setTabLabel(int position, const std::string& label)
{
    if (SidebarItem* item = this->sidebar->getItem(position))
        item->setLabel(label);
}

void TabFrame::focusTab(int position)
{
    Application::giveFocus(this->sidebar->getItem(position));
}

void TabFrame::selectTab(int position)
{
    SidebarItem* item = this->sidebar->getItem(position);
    if (!item)
        return;

    // addTab 的回调在侧边栏项「获得焦点」时真正加载内容，
    // 所以这里需要先把焦点移过去。
    Application::giveFocus(item);

    // Muvik 补丁：内容加载后把焦点进一步送进内容区。
    //
    // 上游只把焦点停在 SidebarItem 上，而 SidebarItem 的 A 键动作是
    // 「向右导航」（sidebar.cpp:72），于是用户按 A 只会把焦点移进列表，
    // 必须再按一次才真正选中条目——表现为「点了视频没反应」。
    //
    // 这里在选中标签后把焦点交给内容区的默认焦点，
    // 使「切到视频库 → 按 A」一步即可打开视频。
    // 侧边栏本身仍可通过 LEFT 键返回。
    //
    // **必须延后一帧**：activeTab 是在 SidebarItem 的「获得焦点」回调里
    // 才赋值的（见上面 addTab 的实现，tab_frame.cpp:75）。
    // giveFocus(item) 只是把事件投递出去，回调此刻还没执行，
    // 因此同步读 this->activeTab 拿到的是**上一个**标签页（首次为 nullptr），
    // getDefaultFocus() 自然拿不到新内容区的焦点。
    //
    // 实测症状：自检里 currentFocus 仍是 brls::SidebarItem，
    // 按 A 只会触发该 item 的「向右导航」，永远打不开视频。
    brls::sync([this]() {
        if (!this->activeTab)
            return;
        View* target = this->activeTab->getDefaultFocus();
        if (target && target->isFocusable())
            Application::giveFocus(target);
    });
}

View* TabFrame::getDefaultFocus()
{
    // 首次构建时 activeTab 为空：addTab 只登记了工厂，
    // 真正的内容要等 SidebarItem **获得焦点**才创建（见上面的回调）。
    // 于是「要焦点进内容区」和「内容要焦点才创建」互相等待，
    // giveFocus 最终落到侧边栏，用户按 A 只会触发「向右导航」。
    //
    // 这里主动把第一个标签加载出来，打破这个循环。
    if (!this->activeTab && this->sidebar) {
        if (SidebarItem* first = this->sidebar->getItem(this->activeIndex >= 0 ? this->activeIndex : 0)) {
            Application::giveFocus(first);
        }
    }

    if (this->activeTab) {
        if (View* target = this->activeTab->getDefaultFocus())
            return target;
    }
    return this->sidebar ? this->sidebar : Box::getDefaultFocus();
}

int TabFrame::getActiveIndex() const
{
    return activeIndex;
}

void TabFrame::clearTabs()
{
    this->sidebar->clearItems();
}

void TabFrame::addSeparator()
{
    this->sidebar->addSeparator();
}

void TabFrame::handleXMLElement(tinyxml2::XMLElement* element)
{
    std::string name = element->Name();

    if (name == "brls:Tab")
    {
        const tinyxml2::XMLAttribute* labelAttribute = element->FindAttribute("label");

        if (!labelAttribute)
            fatal("\"label\" attribute missing from \"" + name + "\" tab");

        std::string label = View::getStringXMLAttributeValue(labelAttribute->Value());

        tinyxml2::XMLElement* viewElement = element->FirstChildElement();

        if (viewElement)
        {
            this->addTab(label, [viewElement] {
                return View::createFromXMLElement(viewElement);
            });

            if (viewElement->NextSiblingElement())
                fatal("\"brls:Tab\" can only contain one child element");
        }
        else
        {
            this->addTab(label, [] { return nullptr; });
        }
    }
    else if (name == "brls:Separator")
    {
        this->addSeparator();
    }
    else
    {
        fatal("Unknown child element \"" + name + "\" for \"brls:Tab\"");
    }
}

View* TabFrame::create()
{
    return new TabFrame();
}

} // namespace brls
