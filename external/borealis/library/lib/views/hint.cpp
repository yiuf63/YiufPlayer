/*
    Copyright 2021 XITRIX

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
#include <borealis/core/logger.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/util.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/hint.hpp>

using namespace brls::literals;

namespace brls
{

const std::string hintXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        axis="row"
        paddingTop="4"
        paddingBottom="4"
        paddingLeft="16"
        paddingRight="16"
        cornerRadius="6">
            <brls:Label
                id="icon"
                width="auto"
                height="auto"
                fontSize="25.5"/>

            <brls:Label
                id="hint"
                width="auto"
                height="auto"
                fontSize="21.5"
                marginLeft="8"/>

    </brls:Box>
)xml";

Hint::Hint(std::shared_ptr<Action> action, bool allowAButtonTouch)
    : Box(Axis::ROW)
      , action(action)
{
    this->inflateFromXMLString(hintXML);
    this->setFocusable(false);

    icon->setText(getKeyIcon(static_cast<ControllerButton>(action->getButton())));
    hint->setText(action->getHintText());

    if ((action->getButton() != BUTTON_A || allowAButtonTouch) && action->isAvailable() && !Application::isInputBlocks())
    {
        this->addGestureRecognizer(new TapGestureRecognizer(this, [this, action]()
        {
            action->getActionListener()(this);
        }));
    }

    if (!action->isAvailable() || Application::isInputBlocks())
    {
        Theme theme = Application::getTheme();
        icon->setTextColor(theme["brls/text_disabled"]);
        hint->setTextColor(theme["brls/text_disabled"]);
    }
}

std::string Hint::getKeyIcon(ControllerButton button, [[maybe_unused]] bool ignoreKeysSwap)
{
    // Muvik 修复：禁止在此处调用 InputManager::mapControllerState(button)。
    // Application::mainLoop 中已经根据 isSwapInputKeys() 将硬件输入正确映射到了逻辑按钮
    // （物理 A 按键产生 BUTTON_A，物理 X 产生 BUTTON_X）。
    // 若在 getKeyIcon 中再次做 mapControllerState，会导致界面提示倒错
    // （例如 BUTTON_A 确定显示为 [B]，BUTTON_X 选项显示为 [Y]），
    // 用户按照屏幕提示按键时会触发完全相反的操作。

    // Muvik 补丁：键盘输入时显示键盘字符，而不是手柄图标。
    // 上游无论何种输入都显示 Switch 手柄字形，用户用键盘操作时
    // 会看到 A/B/X/Y，与实际按的 Enter/Esc 完全对不上。
    if (Application::getInputType() == InputType::KEYBOARD)
    {
        switch (button)
        {
            case BUTTON_A:     return "Enter";
            case BUTTON_B:     return "Esc";
            case BUTTON_X:     return "X";
            case BUTTON_Y:     return "Y";
            case BUTTON_LB:    return "Q";
            case BUTTON_RB:    return "E";
            case BUTTON_LT:    return "1";
            case BUTTON_RT:    return "3";
            case BUTTON_LSB:   return "Z";
            case BUTTON_RSB:   return "C";
            case BUTTON_START: return "M";
            case BUTTON_BACK:  return "F";
            case BUTTON_LEFT:  return "←";
            case BUTTON_UP:    return "↑";
            case BUTTON_RIGHT: return "→";
            case BUTTON_DOWN:  return "↓";
            case BUTTON_NAV_LEFT:  return "←";
            case BUTTON_NAV_UP:    return "↑";
            case BUTTON_NAV_RIGHT: return "→";
            case BUTTON_NAV_DOWN:  return "↓";
            default:           return "?";
        }
    }

    switch (button)
    {
        case BUTTON_A:
            return "\uE0E0";
        case BUTTON_B:
            return "\uE0E1";
        case BUTTON_X:
            return "\uE0E2";
        case BUTTON_Y:
            return "\uE0E3";
        case BUTTON_LSB:
            return "\uE104";
        case BUTTON_RSB:
            return "\uE105";
        case BUTTON_LT:
            return "\uE0E6";
        case BUTTON_RT:
            return "\uE0E7";
        case BUTTON_LB:
            return "\uE0E4";
        case BUTTON_RB:
            return "\uE0E5";
        case BUTTON_START:
            return "\uE0EF";
        case BUTTON_BACK:
            return "\uE0F0";
        case BUTTON_LEFT:
            return "\uE0ED";
        case BUTTON_UP:
            return "\uE0EB";
        case BUTTON_RIGHT:
            return "\uE0EE";
        case BUTTON_DOWN:
            return "\uE0EC";
        default:
            return "\uE152";
    }
}

Hints::Hints()
{
    setAxis(Axis::ROW);
    setDirection(Direction::LEFT_TO_RIGHT);

    hintSubscription = Application::getGlobalHintsUpdateEvent()->subscribe([this]()
    {
        if (!AppletFrame::HIDE_BOTTOM_BAR || forceShown)
        {
            refillHints(Application::getCurrentFocus());
        }
    });

    this->registerBoolXMLAttribute("addBaseAction", [this](bool value)
    {
        this->setAddUnableAButtonAction(value);
    });

    this->registerBoolXMLAttribute("allowAButtonTouch", [this](bool value)
    {
        this->setAllowAButtonTouch(value);
    });

    this->registerBoolXMLAttribute("forceShown", [this](bool value)
    {
        this->forceShown = value;
    });
}

Hints::~Hints()
{
    Application::getGlobalHintsUpdateEvent()->unsubscribe(hintSubscription);
}

static int buttonToSortableVal(ControllerButton button)
{
    // From left to right:
    //  - first +
    //  - then all hints that are not B and A in original order
    //  - finally B and A

    switch (button)
    {
        case BUTTON_START:
            return 0;
        case BUTTON_B:
            return 2;
        case BUTTON_A:
            return 3;
        default:
            return 1;
    }
}

static bool actionsSortFunc(const std::shared_ptr<Action>& a, const std::shared_ptr<Action>& b)
{
    return buttonToSortableVal(static_cast<ControllerButton>(a->getButton())) < buttonToSortableVal(static_cast<ControllerButton>(b->getButton()));
}

void Hints::refillHints(View* focusView)
{
    if (!focusView)
        return;

    // todo: 做一个缓存，可以节约 Hint 组件生成
    clearViews();

    std::set<ControllerButton> addedButtons; // we only ever want one action per key
    std::vector<std::shared_ptr<Action> > actions;

    while (focusView != nullptr)
    {
        for (auto& action : focusView->getActions())
        {
            if (action->getType() != ACTION_GAMEPAD)
                continue; // only show gamepad actions in hints

            if (action->isHidden())
                continue;

            if (addedButtons.find(static_cast<ControllerButton>(action->getButton())) != addedButtons.end())
                continue;

            if (Application::isHintsLiteMode() && action->getButton() != BUTTON_A && action->getButton() != BUTTON_B)
            {
                continue;
            }

            addedButtons.insert(static_cast<ControllerButton>(action->getButton()));
            actions.push_back(action);
        }

        focusView = focusView->getParent();
    }

    const auto it = std::find_if(actions.begin(), actions.end(), [](const std::shared_ptr<Action>& action)
    {
        return *action == BUTTON_A;
    });

    if (addUnableAButtonAction && it == actions.end())
    {
        actions.push_back(std::make_shared<GamepadAction>(BUTTON_A, 0, "hints/ok"_i18n, false, false, false, Sound::SOUND_NONE, nullptr));
    }

    // Sort the actions
    std::stable_sort(actions.begin(), actions.end(), actionsSortFunc);

    for (auto action : actions)
    {
        Hint* hint = new Hint(action, allowAButtonTouch);
        addView(hint);
    }
}

View* Hints::create()
{
    return new Hints();
}

} // namespace brls