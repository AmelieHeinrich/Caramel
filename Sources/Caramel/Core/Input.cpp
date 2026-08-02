/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Input.hpp"

std::array<bool, SDL_SCANCODE_COUNT> Input::s_KeysPrevious{};
std::array<bool, SDL_SCANCODE_COUNT> Input::s_KeysPressed{};
std::array<bool, SDL_SCANCODE_COUNT> Input::s_KeysReleased{};

std::array<bool, SDL_BUTTON_X2 + 1> Input::s_MouseButtonsPrevious{};
std::array<bool, SDL_BUTTON_X2 + 1> Input::s_MouseButtonsPressed{};
std::array<bool, SDL_BUTTON_X2 + 1> Input::s_MouseButtonsReleased{};

glm::vec2 Input::s_MousePosition{};
glm::vec2 Input::s_MouseDelta{};
glm::vec2 Input::s_MouseDeltaAccum{};
glm::vec2 Input::s_MouseScrollDelta{};
glm::vec2 Input::s_MouseScrollAccum{};

SDL_Gamepad* Input::s_Gamepad = nullptr;
SDL_JoystickID Input::s_GamepadInstanceID = 0;
std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> Input::s_GamepadButtonsPrevious{};
std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> Input::s_GamepadButtonsPressed{};
std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> Input::s_GamepadButtonsReleased{};
std::array<float32, SDL_GAMEPAD_AXIS_COUNT> Input::s_GamepadAxes{};

void Input::Initialize()
{
    float x, y;
    SDL_GetMouseState(&x, &y);
    s_MousePosition = { x, y };
}

void Input::Shutdown()
{
    if (s_Gamepad) {
        SDL_CloseGamepad(s_Gamepad);
        s_Gamepad = nullptr;
    }
}

void Input::ProcessEvent(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            s_MousePosition = { event.motion.x, event.motion.y };
            s_MouseDeltaAccum += glm::vec2{ event.motion.xrel, event.motion.yrel };
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            s_MouseScrollAccum += glm::vec2{ event.wheel.x, event.wheel.y };
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!s_Gamepad) {
                s_Gamepad = SDL_OpenGamepad(event.gdevice.which);
                s_GamepadInstanceID = event.gdevice.which;
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (s_Gamepad && event.gdevice.which == s_GamepadInstanceID) {
                SDL_CloseGamepad(s_Gamepad);
                s_Gamepad = nullptr;
                s_GamepadInstanceID = 0;
                s_GamepadButtonsPrevious.fill(false);
            }
            break;
        default:
            break;
    }
}

void Input::NewFrame()
{
    const bool* keys = SDL_GetKeyboardState(nullptr);
    for (int32 i = 0; i < SDL_SCANCODE_COUNT; i++) {
        bool down = keys[i];
        s_KeysPressed[i] = down && !s_KeysPrevious[i];
        s_KeysReleased[i] = !down && s_KeysPrevious[i];
        s_KeysPrevious[i] = down;
    }

    float mx, my;
    SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mx, &my);
    for (uint8 b = SDL_BUTTON_LEFT; b <= SDL_BUTTON_X2; b++) {
        bool down = (mouseState & SDL_BUTTON_MASK(b)) != 0;
        s_MouseButtonsPressed[b] = down && !s_MouseButtonsPrevious[b];
        s_MouseButtonsReleased[b] = !down && s_MouseButtonsPrevious[b];
        s_MouseButtonsPrevious[b] = down;
    }

    if (s_Gamepad) {
        for (int32 b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
            bool down = SDL_GetGamepadButton(s_Gamepad, (SDL_GamepadButton)b);
            s_GamepadButtonsPressed[b] = down && !s_GamepadButtonsPrevious[b];
            s_GamepadButtonsReleased[b] = !down && s_GamepadButtonsPrevious[b];
            s_GamepadButtonsPrevious[b] = down;
        }
        for (int32 a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
            s_GamepadAxes[a] = SDL_GetGamepadAxis(s_Gamepad, (SDL_GamepadAxis)a) / 32767.0f;
        }
    }

    s_MouseDelta = s_MouseDeltaAccum;
    s_MouseDeltaAccum = glm::vec2(0.0f);
    s_MouseScrollDelta = s_MouseScrollAccum;
    s_MouseScrollAccum = glm::vec2(0.0f);
}

bool Input::IsKeyDown(SDL_Scancode key)
{
    return SDL_GetKeyboardState(nullptr)[key];
}

bool Input::IsKeyPressed(SDL_Scancode key)
{
    return s_KeysPressed[key];
}

bool Input::IsKeyReleased(SDL_Scancode key)
{
    return s_KeysReleased[key];
}

glm::vec2 Input::GetMousePosition()
{
    return s_MousePosition;
}

glm::vec2 Input::GetMouseDelta()
{
    return s_MouseDelta;
}

glm::vec2 Input::GetMouseScrollDelta()
{
    return s_MouseScrollDelta;
}

bool Input::IsMouseButtonDown(uint8 button)
{
    float x, y;
    SDL_MouseButtonFlags state = SDL_GetMouseState(&x, &y);
    return (state & SDL_BUTTON_MASK(button)) != 0;
}

bool Input::IsMouseButtonPressed(uint8 button)
{
    return s_MouseButtonsPressed[button];
}

bool Input::IsMouseButtonReleased(uint8 button)
{
    return s_MouseButtonsReleased[button];
}

bool Input::IsGamepadConnected()
{
    return s_Gamepad != nullptr;
}

bool Input::IsGamepadButtonDown(SDL_GamepadButton button)
{
    return s_Gamepad && SDL_GetGamepadButton(s_Gamepad, button);
}

bool Input::IsGamepadButtonPressed(SDL_GamepadButton button)
{
    return s_GamepadButtonsPressed[button];
}

bool Input::IsGamepadButtonReleased(SDL_GamepadButton button)
{
    return s_GamepadButtonsReleased[button];
}

float32 Input::GetGamepadAxis(SDL_GamepadAxis axis)
{
    return s_Gamepad ? s_GamepadAxes[axis] : 0.0f;
}
