/*
    Copyright 2019-2021 natinusala
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

#pragma once

#include <borealis/core/bind.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/sidebar.hpp>
#include <functional>

namespace brls
{

typedef std::function<View*(void)> TabViewCreator;

// An applet frame containing a sidebar on the left with multiple tabs which content is showing on the right.
// Only one tab is kept in memory at all times : when switching, the current tab is freed before the the new one is instantiated.
class TabFrame : public Box
{
  public:
    TabFrame();

    void handleXMLElement(tinyxml2::XMLElement* element) override;

    void addTab(std::string label, TabViewCreator creator);
    void focusTab(int position);

    // 以编程方式切换到第 position 个标签（0 起）。
    // focusTab() 只是把焦点移到侧边栏项，并不保证触发切换 ——
    // addTab 的回调要求目标项「获得焦点」才会真正加载内容。
    // 本方法先移动焦点，再显式触发一次，用于自动化测试与外部调用。
    void selectTab(int position);

    // DeckPlayer 补丁：默认焦点应落在**内容区**，而不是侧边栏。
    //
    // 不覆写时继承 Box/View 的默认实现，返回的是「第一个可聚焦子视图」
    // —— 也就是侧边栏的第一项。后果是应用一启动、或切标签之后，
    // 焦点都停在 SidebarItem 上，而它的 A 键动作是「向右导航」
    // （sidebar.cpp:72），于是用户按 A 只会把焦点移进列表，打不开任何条目。
    //
    // 实测：自检里 currentFocus=brls::SidebarItem，按 A 后活动栈深度 1->1。
    View* getDefaultFocus() override;

    // 当前激活标签在侧边栏中的序号；无激活标签时返回 -1。
    int getActiveIndex() const;
    void clearTabs();
    void addSeparator();

    static View* create();

  private:
    BRLS_BIND(Sidebar, sidebar, "brls/tab_frame/sidebar");

    View* activeTab = nullptr;
    int activeIndex = -1;
};

} // namespace brls
