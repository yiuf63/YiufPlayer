#include <borealis/views/edit_text_dialog.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/application.hpp>

#ifdef __PSV__
#define EDIT_TEXT_DIALOG_POP_ANIMATION TransitionAnimation::NONE
#define EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT false
#include <SDL2/SDL.h>
#include "borealis/platforms/sdl/sdl_video.hpp"
#else
#define EDIT_TEXT_DIALOG_POP_ANIMATION TransitionAnimation::FADE
#define EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT true
#endif

namespace brls
{

    const std::string editTextDialogXML = R"xml(
    <brls:Box
        id="brls/container"
        width="auto"
        height="auto"
        axis="column"
        justifyContent="flexStart"
        alignItems="center"
        focusable="true"
        hideHighlight="true"
        backgroundColor="@theme/brls/backdrop">

        <brls:Label
            id="brls/dialog/header"
            fontSize="24"
            marginTop="24"
            marginBottom="16"
            textColor="#FFFFFF"/>

        <brls:Box
            id="brls/dialog/applet"
            width="720"
            cornerRadius="8"
            alignItems="center"
            axis="column"
            backgroundColor="@theme/brls/background">

            <brls:Label
                id="brls/dialog/label"
                grow="1"
                width="680"
                cursor="-1"
                minHeight="32"
                margin="14"
                autoAnimate="false"
                verticalAlign="top"/>

            <brls:Label
                id="brls/dialog/count"
                width="680"
                horizontalAlign="right"
                fontSize="16"
                textColor="@theme/brls/text_disabled"
                marginRight="20"
                marginBottom="4"/>

            <brls:Box
                id="brls/dialog/keyboard"
                width="688"
                axis="column"
                alignItems="center"
                marginBottom="8"/>

            <brls:Hints
                allowAButtonTouch="true"
                forceShown="true"
                addBaseAction="false"
                marginBottom="12"
                marginRight="10"
                width="auto"
                height="auto"/>
        </brls:Box>

    </brls:Box>
    )xml";

    EditTextDialog::EditTextDialog()
    {
        this->inflateFromXMLString(editTextDialogXML);

        this->buildKeyboard();

        // 提交完成 (手柄 START 键 / ☰)
        this->registerAction(
            "hints/ok"_i18n, BUTTON_START, [this](...)
            {
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->summitEvent.fire();
                    });
                return true; }, true);

        // 取消退出 (手柄 B 键)
        this->registerAction(
            "hints/back"_i18n, BUTTON_B, [this](...)
            {
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->cancelEvent.fire();
                    });
                return true; });

        // 快捷退格 (手柄 X 键)
        this->registerAction("hints/delete"_i18n, BUTTON_X, [this](...)
            {
                this->backspaceEvent.fire();
                return true; }, true, true);

        // 快捷空格 (手柄 Y 键)
        this->registerAction("空格", BUTTON_Y, [this](...)
            {
                this->insertTextEvent.fire(" ");
                return true; }, true, true);

        // 光标左移 (手柄 LB 键)
        this->registerAction("光标←", BUTTON_LB, [this](...)
            {
                this->cursorMoveEvent.fire(-1);
                return true; }, true, true);

        // 光标右移 (手柄 RB 键)
        this->registerAction("光标→", BUTTON_RB, [this](...)
            {
                this->cursorMoveEvent.fire(1);
                return true; }, true, true);

        // register paste action
        int pasteKey = BRLS_KBD_MODIFIER_CTRL;
#ifdef __APPLE__
        pasteKey = BRLS_KBD_MODIFIER_META;
#endif
        this->registerAction({ BRLS_KBD_KEY_V, pasteKey }, [this](...)
        {
            this->clipboardEvent.fire(Application::getPlatform()->pasteFromClipboard());
            return true;
        });

        // register delete action
        this->registerAction({ BRLS_KBD_KEY_DELETE }, [this](...)
        {
            // This is to ensure that the delete operation of the PSV ime will not be ignored
            this->backspaceEvent.fire();
            return true;
        }, true);
        this->registerAction({ BRLS_KBD_KEY_BACKSPACE }, [this](...)
        {
            this->backspaceEvent.fire();
            return true;
        }, true);

        this->registerAction("hints/delete"_i18n, BUTTON_BACK, [this](...)
            {
                this->backspaceEvent.fire();
                return true; }, true, true);

#if defined(__PSV__) and !defined(BOREALIS_USE_GXM)
        // After turning off the on-screen keyboard, tap on the input area to reopen
        this->addGestureRecognizer(new brls::TapGestureRecognizer([](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END) {
                auto* videoContext = (SDLVideoContext*)Application::getPlatform()->getVideoContext();
                if (!SDL_IsScreenKeyboardShown(videoContext->getSDLWindow()))
                {
                    SDL_StopTextInput();
                    SDL_StartTextInput();
                }
                Application::setInputType(InputType::GAMEPAD);
            }
        }));
        // Force to set to gamepad mode to avoid missing the `Enter` keyboard event
        Application::setInputType(InputType::GAMEPAD);
#endif

        this->init = true;
    }

    EditTextDialog::~EditTextDialog() = default;

    void EditTextDialog::buildKeyboard()
    {
        if (!this->keyboardBox) return;
        this->keyboardBox->clearViews();

        auto makeKey = [](const std::string& text, float width, std::function<void()> cb) -> Box* {
            auto* key = new Box();
            key->setFocusable(true);
            key->setWidth(width);
            key->setHeight(36.0f);
            key->setMarginLeft(2.0f);
            key->setMarginRight(2.0f);
            key->setCornerRadius(6.0f);
            key->setJustifyContent(JustifyContent::CENTER);
            key->setAlignItems(AlignItems::CENTER);
            key->setBackgroundColor(Application::getTheme().getColor("brls/sidebar/separator"));

            auto* lbl = new Label();
            lbl->setText(text);
            lbl->setFontSize(13.0f);
            lbl->setTextColor(Application::getTheme().getColor("brls/text"));
            lbl->setHorizontalAlign(HorizontalAlign::CENTER);
            lbl->setVerticalAlign(VerticalAlign::CENTER);
            key->addView(lbl);

            key->registerClickAction([cb](View*) {
                if (cb) cb();
                return true;
            });
            return key;
        };

        std::vector<std::vector<std::string>> layout;
        if (keyboardMode == 0) {
            layout = {
                { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", ".", "/" },
                { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", ":", "_" },
                { "a", "s", "d", "f", "g", "h", "j", "k", "l", "@", "?", "&" },
                { "Shift", "z", "x", "c", "v", "b", "n", "m", ",", "=", "退格" },
                { "符号", "空格", "清空", "完成" }
            };
        } else if (keyboardMode == 1) {
            layout = {
                { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", ".", "/" },
                { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", ":", "_" },
                { "A", "S", "D", "F", "G", "H", "J", "K", "L", "@", "?", "&" },
                { "小写", "Z", "X", "C", "V", "B", "N", "M", ",", "=", "退格" },
                { "符号", "空格", "清空", "完成" }
            };
        } else {
            layout = {
                { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "+", "~", "\\" },
                { "[", "]", "{", "}", "<", ">", ";", "'", "\"", "|", "?", "/" },
                { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "_" },
                { "字母", "http://", "https://", ".com", ".cn", "退格" },
                { "字母", "空格", "清空", "完成" }
            };
        }

        for (size_t r = 0; r < layout.size(); r++) {
            auto* rowBox = new Box();
            rowBox->setAxis(Axis::ROW);
            rowBox->setJustifyContent(JustifyContent::CENTER);
            rowBox->setAlignItems(AlignItems::CENTER);
            rowBox->setMarginBottom(4.0f);

            for (size_t c = 0; c < layout[r].size(); c++) {
                const std::string& keyStr = layout[r][c];
                float w = 48.0f;
                std::function<void()> onClick;

                if (keyStr == "Shift") {
                    w = 68.0f;
                    onClick = [this]() {
                        this->keyboardMode = 1;
                        this->buildKeyboard();
                    };
                } else if (keyStr == "小写") {
                    w = 68.0f;
                    onClick = [this]() {
                        this->keyboardMode = 0;
                        this->buildKeyboard();
                    };
                } else if (keyStr == "符号") {
                    w = 70.0f;
                    onClick = [this]() {
                        this->keyboardMode = 2;
                        this->buildKeyboard();
                    };
                } else if (keyStr == "字母") {
                    w = 70.0f;
                    onClick = [this]() {
                        this->keyboardMode = 0;
                        this->buildKeyboard();
                    };
                } else if (keyStr == "退格") {
                    w = 68.0f;
                    onClick = [this]() {
                        this->backspaceEvent.fire();
                    };
                } else if (keyStr == "空格") {
                    w = 260.0f;
                    onClick = [this]() {
                        this->insertTextEvent.fire(" ");
                    };
                } else if (keyStr == "清空") {
                    w = 70.0f;
                    onClick = [this]() {
                        this->clearEvent.fire();
                    };
                } else if (keyStr == "完成") {
                    w = 110.0f;
                    onClick = [this]() {
                        Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                            this->summitEvent.fire();
                        });
                    };
                } else if (keyStr == "http://") {
                    w = 88.0f;
                    onClick = [this]() { this->insertTextEvent.fire("http://"); };
                } else if (keyStr == "https://") {
                    w = 94.0f;
                    onClick = [this]() { this->insertTextEvent.fire("https://"); };
                } else if (keyStr == ".com") {
                    w = 62.0f;
                    onClick = [this]() { this->insertTextEvent.fire(".com"); };
                } else if (keyStr == ".cn") {
                    w = 54.0f;
                    onClick = [this]() { this->insertTextEvent.fire(".cn"); };
                } else {
                    if (layout[r].size() <= 12) w = 52.0f;
                    std::string s = keyStr;
                    onClick = [this, s]() {
                        this->insertTextEvent.fire(s);
                    };
                }

                Box* k = makeKey(keyStr, w, onClick);
                if (keyStr == "完成") {
                    k->setBackgroundColor(Application::getTheme().getColor("brls/button/primary_enabled_background"));
                }

                rowBox->addView(k);
            }

            if (r == 2) {
                rowBox->setDefaultFocusedIndex(4);
            }
            this->keyboardBox->addView(rowBox);
        }

        this->keyboardBox->setDefaultFocusedIndex(2);
    }

    void EditTextDialog::open()
    {
        Application::pushActivity(new Activity(this));
        if (this->getDefaultFocus()) {
            Application::giveFocus(this->getDefaultFocus());
        }
    }

    void EditTextDialog::setText(const std::string& value)
    {
        this->content = value;
        this->updateUI();
    }

    void EditTextDialog::setHeaderText(const std::string& value)
    {
        this->header->setText(value);
    }

    void EditTextDialog::setHintText(const std::string& value)
    {
        if (value.empty())
        {
            this->hint = "hints/input"_i18n;
        }
        else
        {
            this->hint = value;
        }
        this->updateUI();
    }

    void EditTextDialog::setCountText(const std::string& value)
    {
        this->count->setText(value);
    }

    bool EditTextDialog::isTranslucent()
    {
        return EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT;
    }

    void EditTextDialog::onLayout()
    {
        if (!init)
            return;
        this->layoutEvent.fire(Point { this->label->getX(),
            this->label->getY() + this->label->getHeight() });
    }

    Event<Point>* EditTextDialog::getLayoutEvent()
    {
        return &this->layoutEvent;
    }

    Event<>* EditTextDialog::getBackspaceEvent()
    {
        return &this->backspaceEvent;
    }

    Event<>* EditTextDialog::getCancelEvent()
    {
        return &this->cancelEvent;
    }

    Event<>* EditTextDialog::getSubmitEvent()
    {
        return &this->summitEvent;
    }

    Event<std::string>* EditTextDialog::getClipboardEvent()
    {
        return &this->clipboardEvent;
    }

    Event<std::string>* EditTextDialog::getInsertTextEvent()
    {
        return &this->insertTextEvent;
    }

    Event<>* EditTextDialog::getClearEvent()
    {
        return &this->clearEvent;
    }

    Event<int>* EditTextDialog::getCursorMoveEvent()
    {
        return &this->cursorMoveEvent;
    }

    void EditTextDialog::updateUI()
    {
        if (content.empty())
        {
            label->setTextColor(Application::getTheme().getColor("brls/text_disabled"));
            label->setText(hint);
            label->setCursor((int)CursorPosition::START);
        }
        else
        {
            label->setTextColor(Application::getTheme().getColor("brls/text"));
            label->setText(content);
        }
    }

    void EditTextDialog::setCursor(int cursor) {
        label->setCursor(cursor);
    }
}
