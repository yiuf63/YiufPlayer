/*
    Copyright 2021 natinusala
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
#include <borealis/core/logger.hpp>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#include <borealis/platforms/glfw/glfw_input.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <unistd.h>

namespace brls
{

#define GLFW_GAMEPAD_BUTTON_NONE SIZE_MAX
#define GLFW_GAMEPAD_BUTTON_MAX 15
#define GLFW_GAMEPAD_AXIS_MAX 6

// LT and RT do not exist here because they are axes
static const size_t GLFW_BUTTONS_MAPPING[GLFW_GAMEPAD_BUTTON_MAX] = {
    BUTTON_A, // GLFW_GAMEPAD_BUTTON_A
    BUTTON_B, // GLFW_GAMEPAD_BUTTON_B
    BUTTON_X, // GLFW_GAMEPAD_BUTTON_X
    BUTTON_Y, // GLFW_GAMEPAD_BUTTON_Y
    BUTTON_LB, // GLFW_GAMEPAD_BUTTON_LEFT_BUMPER
    BUTTON_RB, // GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER
    BUTTON_BACK, // GLFW_GAMEPAD_BUTTON_BACK
    BUTTON_START, // GLFW_GAMEPAD_BUTTON_START
    BUTTON_GUIDE, // GLFW_GAMEPAD_BUTTON_GUIDE
    BUTTON_LSB, // GLFW_GAMEPAD_BUTTON_LEFT_THUMB
    BUTTON_RSB, // GLFW_GAMEPAD_BUTTON_RIGHT_THUMB
    BUTTON_UP, // GLFW_GAMEPAD_BUTTON_DPAD_UP
    BUTTON_RIGHT, // GLFW_GAMEPAD_BUTTON_DPAD_RIGHT
    BUTTON_DOWN, // GLFW_GAMEPAD_BUTTON_DPAD_DOWN
    BUTTON_LEFT, // GLFW_GAMEPAD_BUTTON_DPAD_LEFT
};

static const size_t GLFW_AXIS_MAPPING[GLFW_GAMEPAD_AXIS_MAX] = {
    LEFT_X,
    LEFT_Y,
    RIGHT_X,
    RIGHT_Y,
    LEFT_Z,
    RIGHT_Z,
};

static short controllersCount = 0;

static void glfwJoystickCallback(int jid, int event)
{
    if (event == GLFW_CONNECTED)
    {
        // 动态接入新手柄时，若 Steam/外部环境注入了 SDL_GAMECONTROLLERCONFIG 则重新加载映射
        if (const char* env_mapping = std::getenv("SDL_GAMECONTROLLERCONFIG"))
        {
            glfwUpdateGamepadMappings(env_mapping);
        }

        Logger::info("glfw: joystick {} connected", jid);

        if (glfwJoystickIsGamepad(jid))
        {
            Logger::info("glfw: joystick {} is gamepad: \"{}\"", jid, glfwGetGamepadName(jid));
        }

        controllersCount++;
    }
    else if (event == GLFW_DISCONNECTED)
    {
        Logger::info("glfw: joystick {} disconnected", jid);

        controllersCount--;
    }
    Application::setActiveEvent(true);
}

#define GLFW_STICKY 4
static RawTouchState touchState = { 0, 0, { 0, 0 } };
static int touchStateStatus = GLFW_RELEASE;
static bool touchUpdate = false;

static inline int getTouchState()
{
    if (touchStateStatus == GLFW_STICKY)
    {
        touchStateStatus = GLFW_RELEASE;
        // Try to ignore the touch end event as this can lead to a smaller calculation of scrolling acceleration
        int res = touchUpdate ? GLFW_PRESS : GLFW_RELEASE;
        touchUpdate = false;
        return res;
    }
    else
    {
        touchUpdate = false;
        return touchStateStatus;
    }
}

static void glfwTouchCallback(GLFWwindow* window, int touch, int action, double xpos, double ypos)
{
#if defined(BOREALIS_USE_D3D11)
    float scaleFactor = 1.0f / Application::windowScale;
#else
    double scaleFactor = Application::getPlatform()->getVideoContext()->getScaleFactor() / Application::windowScale;
#endif
    touchState.fingerId   = 0;
    touchState.pressed    = action != GLFW_RELEASE;
    touchState.position.x = (float)(xpos * scaleFactor);
    touchState.position.y = (float)(ypos * scaleFactor);
    touchStateStatus      = touchState.pressed ? GLFW_PRESS : GLFW_STICKY;
    touchUpdate          |= touchState.pressed;
    Application::setActiveEvent(true);
}

void GLFWInputManager::keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* self = (GLFWInputManager*)Application::getPlatform()->getInputManager();
    KeyState state {};
    state.key            = (BrlsKeyboardScancode)key;
    state.mods           = mods;
    state.pressed        = action != GLFW_RELEASE;
    const char* key_name = glfwGetKeyName(key, scancode);
    if (key_name != NULL)
        Logger::debug("Key: {} / Code: {} / Action: {}", key_name, key, action);
    else
        Logger::debug("Key: NULL / Code: {} / Action: {}", key, action);
    self->getKeyboardKeyStateChanged()->fire(state);
    Application::setActiveEvent(true);
}

void GLFWInputManager::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto* self = (GLFWInputManager*)Application::getPlatform()->getInputManager();
#if defined(_WIN32) || defined(__linux__)
    self->scrollOffset.x += xoffset * 30;
    self->scrollOffset.y += yoffset * 30;
#else
    self->scrollOffset.x += xoffset * 10;
    self->scrollOffset.y += yoffset * 10;
#endif
    self->getMouseScrollOffsetChanged()->fire(Point(xoffset, yoffset));
    Application::setActiveEvent(true);
}

void GLFWInputManager::cursorCallback(GLFWwindow* window, double x, double y)
{
    auto* self = (GLFWInputManager*)Application::getPlatform()->getInputManager();
    if (self->pointerLocked)
    {
        int width, height;
        glfwGetWindowSize(self->window, &width, &height);

        int hWidth  = width / 2;
        int hHeight = height / 2;

        Point localPointerOffset;

        localPointerOffset.x = (x - hWidth); // / Application::windowScale;
        localPointerOffset.y = (y - hHeight); // / Application::windowScale;

        self->pointerOffsetBuffer.x += (x - hWidth); // / Application::windowScale;
        self->pointerOffsetBuffer.y += (y - hHeight); // / Application::windowScale;

        self->getMouseCusorOffsetChanged()->fire(localPointerOffset);
        glfwSetCursorPos(self->window, hWidth, hHeight);
    }
}

GLFWInputManager::GLFWInputManager(GLFWwindow* window)
    : window(window)
{
    if (access(DesktopPlatform::GAMEPAD_DB.c_str(), F_OK) == -1)
    {
        brls::Logger::warning("Cannot find custom gamepad db, (Searched at: {})",
            DesktopPlatform::GAMEPAD_DB);
    }
    else
    {
        const std::string mappings = loadFileContents(DesktopPlatform::GAMEPAD_DB);
        brls::Logger::info("Load custom gamepad db: {}", DesktopPlatform::GAMEPAD_DB);
        glfwUpdateGamepadMappings(mappings.c_str());
    }

    // 加载 Steam / 外部环境注入的 SDL_GAMECONTROLLERCONFIG 映射
    if (const char* env_mapping = std::getenv("SDL_GAMECONTROLLERCONFIG"))
    {
        brls::Logger::info("glfw: load SDL_GAMECONTROLLERCONFIG from environment");
        glfwUpdateGamepadMappings(env_mapping);
    }

    glfwSetJoystickCallback(glfwJoystickCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetKeyCallback(window, keyboardCallback);
    if (glfwTouchInputSupported())
    {
        glfwSetInputMode(window, GLFW_TOUCH, GLFW_TRUE);
        glfwSetTouchCallback(window, glfwTouchCallback);
    }

    controllersCount = 0;
    for (int i = 0; i <= GLFW_JOYSTICK_LAST; i++)
    {
        if (glfwJoystickPresent(i))
        {
            controllersCount++;
            const char* guid = glfwGetJoystickGUID(i);
            if (guid)
                Logger::info("glfw: joystick {} GUID {}", i, guid);
            if (glfwJoystickIsGamepad(i))
            {
                Logger::info("glfw: joystick {} connected", i);
                Logger::info("glfw: joystick {} is gamepad: \"{}\"", i, glfwGetGamepadName(i));
            }
        }
    }

    Application::getRunLoopEvent()->subscribe([this]()
        {
#if defined(_WIN32) || defined(__linux__)
        // smooth scroll
        if(fabs(scrollOffset.y) < 1) scrollOffset.y = 0;
        else scrollOffset.y *= 0.8;
        if(fabs(scrollOffset.x) < 1) scrollOffset.x = 0;
        else scrollOffset.x *= 0.8;
#else
        scrollOffset.x  = 0;
        scrollOffset.y  = 0;
#endif
        pointerOffset.x = 0;
        pointerOffset.y = 0; });
}

short GLFWInputManager::getControllersConnectedCount()
{
    short count = 0;
    for (int i = 0; i <= GLFW_JOYSTICK_LAST; i++)
    {
        if (glfwJoystickPresent(i))
            count++;
    }
    return count;
}

void GLFWInputManager::updateUnifiedControllerState(ControllerState* state)
{
    for (size_t i = 0; i < _BUTTON_MAX; i++)
        state->buttons[i] = false;

    for (size_t i = 0; i < _AXES_MAX; i++)
        state->axes[i] = 0;

    if (!glfwGetWindowAttrib(window, GLFW_FOCUSED))
        return;

    // Muvik：记录是否存在真实手柄输入（在任何键盘合并之前）。
    // 遍历 GLFW 全部 16 个插槽（0..GLFW_JOYSTICK_LAST），支持外接手柄（Steam Deck 上常落在 slot 5+）
    bool hasAnyPadInput = false;
    for (int i = 0; i <= GLFW_JOYSTICK_LAST; i++)
    {
        if (!glfwJoystickPresent(i))
            continue;

        ControllerState localState {};
        updateControllerState(&localState, i);

        for (size_t j = 0; j < _BUTTON_MAX; j++)
        {
            if (localState.buttons[j]) hasAnyPadInput = true;
            state->buttons[j] |= localState.buttons[j];
        }
        for (size_t j = 0; j < _AXES_MAX; j++)
        {
            // 摇杆有明显位移也算手柄输入（死区 0.2）
            if (localState.axes[j] > 0.2f || localState.axes[j] < -0.2f)
                hasAnyPadInput = true;
        }

        for (size_t axis = 0; axis < _AXES_MAX; axis++)
        {
            state->axes[axis] += localState.axes[axis];

            if (state->axes[axis] < -1)
                state->axes[axis] = -1;
            else if (state->axes[axis] > 1)
                state->axes[axis] = 1;
        }
    }

    // Add keyboard keys on top of gamepad buttons
    if (Application::isSwapInputKeys()) {
        state->buttons[BUTTON_B] |= glfwGetKey(this->window, GLFW_KEY_KP_ENTER) != 0;
        state->buttons[BUTTON_B] |= glfwGetKey(this->window, GLFW_KEY_ENTER) != 0;
        state->buttons[BUTTON_A] |= glfwGetKey(this->window, GLFW_KEY_ESCAPE) != 0;
        state->buttons[BUTTON_A] |= glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    } else {
        state->buttons[BUTTON_A] |= glfwGetKey(this->window, GLFW_KEY_KP_ENTER) != 0;
        state->buttons[BUTTON_A] |= glfwGetKey(this->window, GLFW_KEY_ENTER) != 0;
        state->buttons[BUTTON_B] |= glfwGetKey(this->window, GLFW_KEY_ESCAPE) != 0;
        state->buttons[BUTTON_B] |= glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    }
    state->buttons[BUTTON_X] |= (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);

    state->buttons[BUTTON_UP] |= glfwGetKey(this->window, GLFW_KEY_UP) != 0;
    state->buttons[BUTTON_RIGHT] |= glfwGetKey(this->window, GLFW_KEY_RIGHT) != 0;
    state->buttons[BUTTON_DOWN] |= glfwGetKey(this->window, GLFW_KEY_DOWN) != 0;
    state->buttons[BUTTON_LEFT] |= glfwGetKey(this->window, GLFW_KEY_LEFT) != 0;

    state->buttons[BUTTON_NAV_UP] |= state->buttons[BUTTON_UP];
    state->buttons[BUTTON_NAV_RIGHT] |= state->buttons[BUTTON_RIGHT];
    state->buttons[BUTTON_NAV_DOWN] |= state->buttons[BUTTON_DOWN];
    state->buttons[BUTTON_NAV_LEFT] |= state->buttons[BUTTON_LEFT];

    // Muvik 补丁：判断这次输入是否有「真实手柄」参与。
    //
    // 上游把键盘按键直接 OR 进同一个 buttons 数组，之后无法区分
    // 到底是键盘还是手柄在操作，界面只能一律显示手柄图标。
    //
    // 这里把「纯键盘」与「含手柄」两种情况分开标记：
    //   - 只要有任何实体手柄按下 → GAMEPAD
    //   - 只有键盘操作           → KEYBOARD
    // 这样提示栏就能显示与实际输入一致的文案。
    // 用 static 记录上次来源，避免每帧都对 UI 触发类型切换事件。
    const bool keyboardUsed =
        glfwGetKey(window, GLFW_KEY_ENTER)    != 0 ||
        glfwGetKey(window, GLFW_KEY_KP_ENTER) != 0 ||
        glfwGetKey(window, GLFW_KEY_ESCAPE)   != 0 ||
        glfwGetKey(window, GLFW_KEY_UP)       != 0 ||
        glfwGetKey(window, GLFW_KEY_DOWN)     != 0 ||
        glfwGetKey(window, GLFW_KEY_LEFT)     != 0 ||
        glfwGetKey(window, GLFW_KEY_RIGHT)    != 0;
    const bool gamepadUsed  = hasAnyPadInput;
    const bool anyInput     = keyboardUsed || gamepadUsed;

    static int lastSource = -1;   // 0=键盘 1=手柄 -1=未知
    if (anyInput)
    {
        Application::setActiveEvent(true);
        int src = gamepadUsed ? 1 : 0;
        if (src != lastSource)
        {
            lastSource = src;
            Application::reportInputSource(src == 0);
        }
    } else {
        lastSource = -1;
    }
}

void GLFWInputManager::updateControllerState(ControllerState* state, int controller)
{
    // 优先尝试读取已注册 SDL 映射的标准 Gamepad 状态
    GLFWgamepadstate glfwState = {};
    if (glfwGetGamepadState(controller, &glfwState))
    {
        for (size_t i = 0; i < GLFW_GAMEPAD_BUTTON_MAX; i++)
        {
            // Translate GLFW gamepad to borealis controller
            size_t brlsButton          = GLFW_BUTTONS_MAPPING[i];
            state->buttons[brlsButton] = (bool)glfwState.buttons[i];
        }

        state->buttons[BUTTON_LT] = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.1f;
        state->buttons[BUTTON_RT] = glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.1f;

        state->buttons[BUTTON_NAV_UP]    = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -0.5f || glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] < -0.5f || state->buttons[BUTTON_UP];
        state->buttons[BUTTON_NAV_RIGHT] = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_X] > 0.5f || glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] > 0.5f || state->buttons[BUTTON_RIGHT];
        state->buttons[BUTTON_NAV_DOWN]  = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] > 0.5f || glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] > 0.5f || state->buttons[BUTTON_DOWN];
        state->buttons[BUTTON_NAV_LEFT]  = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -0.5f || glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] < -0.5f || state->buttons[BUTTON_LEFT];

        for (size_t i = 0; i < GLFW_GAMEPAD_AXIS_MAX; i++)
        {
            state->axes[GLFW_AXIS_MAPPING[i]] = glfwState.axes[i];
        }
        return;
    }

    // 回退机制：若此设备未注册 SDL Gamepad 映射（例如特定型号八位堂、D-Input 模式、第三方未映射摇杆），
    // 直接读取底层原始按键、方向十字键 (Hats) 和模拟轴 (Axes)，避免手柄完全无法控制
    int buttonCount = 0;
    const unsigned char* rawButtons = glfwGetJoystickButtons(controller, &buttonCount);
    if (rawButtons && buttonCount > 0)
    {
        if (buttonCount > 0) state->buttons[BUTTON_A] = (rawButtons[0] == GLFW_PRESS);
        if (buttonCount > 1) state->buttons[BUTTON_B] = (rawButtons[1] == GLFW_PRESS);
        if (buttonCount > 2) state->buttons[BUTTON_X] = (rawButtons[2] == GLFW_PRESS);
        if (buttonCount > 3) state->buttons[BUTTON_Y] = (rawButtons[3] == GLFW_PRESS);
        if (buttonCount > 4) state->buttons[BUTTON_LB] = (rawButtons[4] == GLFW_PRESS);
        if (buttonCount > 5) state->buttons[BUTTON_RB] = (rawButtons[5] == GLFW_PRESS);
        if (buttonCount > 6) state->buttons[BUTTON_BACK] = (rawButtons[6] == GLFW_PRESS);
        if (buttonCount > 7) state->buttons[BUTTON_START] = (rawButtons[7] == GLFW_PRESS);
        if (buttonCount > 8) state->buttons[BUTTON_GUIDE] = (rawButtons[8] == GLFW_PRESS);
        if (buttonCount > 9) state->buttons[BUTTON_LSB] = (rawButtons[9] == GLFW_PRESS);
        if (buttonCount > 10) state->buttons[BUTTON_RSB] = (rawButtons[10] == GLFW_PRESS);
        if (buttonCount > 11) state->buttons[BUTTON_UP] |= (rawButtons[11] == GLFW_PRESS);
        if (buttonCount > 12) state->buttons[BUTTON_DOWN] |= (rawButtons[12] == GLFW_PRESS);
        if (buttonCount > 13) state->buttons[BUTTON_LEFT] |= (rawButtons[13] == GLFW_PRESS);
        if (buttonCount > 14) state->buttons[BUTTON_RIGHT] |= (rawButtons[14] == GLFW_PRESS);
    }

    int hatCount = 0;
    const unsigned char* rawHats = glfwGetJoystickHats(controller, &hatCount);
    if (rawHats && hatCount > 0)
    {
        unsigned char hat = rawHats[0];
        if (hat & GLFW_HAT_UP) state->buttons[BUTTON_UP] = true;
        if (hat & GLFW_HAT_RIGHT) state->buttons[BUTTON_RIGHT] = true;
        if (hat & GLFW_HAT_DOWN) state->buttons[BUTTON_DOWN] = true;
        if (hat & GLFW_HAT_LEFT) state->buttons[BUTTON_LEFT] = true;
    }

    int axisCount = 0;
    const float* rawAxes = glfwGetJoystickAxes(controller, &axisCount);
    if (rawAxes && axisCount >= 2)
    {
        state->axes[LEFT_X] = rawAxes[0];
        state->axes[LEFT_Y] = rawAxes[1];
        if (axisCount >= 4)
        {
            state->axes[RIGHT_X] = rawAxes[2];
            state->axes[RIGHT_Y] = rawAxes[3];
        }
        if (axisCount >= 6)
        {
            state->axes[LEFT_Z] = rawAxes[4];
            state->axes[RIGHT_Z] = rawAxes[5];
            state->buttons[BUTTON_LT] = rawAxes[4] > 0.1f;
            state->buttons[BUTTON_RT] = rawAxes[5] > 0.1f;
        }
        if (axisCount >= 8)
        {
            // 部分手柄十字键映射在 Axis 6 / Axis 7
            if (rawAxes[6] < -0.5f) state->buttons[BUTTON_LEFT] = true;
            if (rawAxes[6] > 0.5f) state->buttons[BUTTON_RIGHT] = true;
            if (rawAxes[7] < -0.5f) state->buttons[BUTTON_UP] = true;
            if (rawAxes[7] > 0.5f) state->buttons[BUTTON_DOWN] = true;
        }
    }

    state->buttons[BUTTON_NAV_UP]    = state->axes[LEFT_Y] < -0.5f || state->axes[RIGHT_Y] < -0.5f || state->buttons[BUTTON_UP];
    state->buttons[BUTTON_NAV_RIGHT] = state->axes[LEFT_X] > 0.5f || state->axes[RIGHT_X] > 0.5f || state->buttons[BUTTON_RIGHT];
    state->buttons[BUTTON_NAV_DOWN]  = state->axes[LEFT_Y] > 0.5f || state->axes[RIGHT_Y] > 0.5f || state->buttons[BUTTON_DOWN];
    state->buttons[BUTTON_NAV_LEFT]  = state->axes[LEFT_X] < -0.5f || state->axes[RIGHT_X] < -0.5f || state->buttons[BUTTON_LEFT];
}

bool GLFWInputManager::getKeyboardKeyState(BrlsKeyboardScancode key)
{
    return glfwGetKey(this->window, key);
}

void GLFWInputManager::updateTouchStates(std::vector<RawTouchState>* states)
{
    if (getTouchState()) {
        touchState.pressed = true;
        states->push_back(touchState);
    }
}

void GLFWInputManager::updateMouseStates(RawMouseState* state)
{
    double x, y;
    glfwGetCursorPos(this->window, &x, &y);

    state->leftButton   = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    state->middleButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    state->rightButton  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

#if defined(BOREALIS_USE_METAL) || defined(BOREALIS_USE_D3D11)
    // 使用 metal, d3d11 的 cocoa 窗口鼠标事件不需要进行 dpi 缩放。
    state->position.x = x / Application::windowScale;
    state->position.y = y / Application::windowScale;
#else
    double scaleFactor = brls::Application::getPlatform()->getVideoContext()->getScaleFactor();
    state->position.x  = x * scaleFactor / Application::windowScale;
    state->position.y  = y * scaleFactor / Application::windowScale;
#endif
    state->offset = pointerOffset;
    state->scroll = scrollOffset;
}

void GLFWInputManager::setPointerLock(bool lock)
{
    pointerLocked = lock;

    int state = lock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    glfwSetInputMode(window, GLFW_CURSOR, state);
}

void GLFWInputManager::runloopStart()
{
    pointerOffset         = pointerOffsetBuffer;
    pointerOffsetBuffer.x = 0;
    pointerOffsetBuffer.y = 0;
}

void GLFWInputManager::sendRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
}

};
