/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <array>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

class Input
{
public:
    static void Initialize();
    static void Shutdown();

    static void ProcessEvent(const SDL_Event& event);
    static void NewFrame();

    // Must be called once, right after the window is created, before any relative-mouse-mode calls.
    static void SetWindow(SDL_Window* window);

    // Relative mode hides the OS cursor and reports raw, unbounded mouse motion as deltas (no
    // clamping at screen edges) -- used while dragging the camera view so a fast look-around swipe
    // doesn't hit the edge of the monitor and stop tracking further motion.
    static void SetRelativeMouseMode(bool enabled);

    static bool IsKeyDown(SDL_Scancode key);
    static bool IsKeyPressed(SDL_Scancode key);
    static bool IsKeyReleased(SDL_Scancode key);

    static glm::vec2 GetMousePosition();
    static glm::vec2 GetMouseDelta();
    static glm::vec2 GetMouseScrollDelta();
    static bool IsMouseButtonDown(uint8 button);
    static bool IsMouseButtonPressed(uint8 button);
    static bool IsMouseButtonReleased(uint8 button);

    static bool IsGamepadConnected();
    static bool IsGamepadButtonDown(SDL_GamepadButton button);
    static bool IsGamepadButtonPressed(SDL_GamepadButton button);
    static bool IsGamepadButtonReleased(SDL_GamepadButton button);
    static float32 GetGamepadAxis(SDL_GamepadAxis axis);
private:
    static std::array<bool, SDL_SCANCODE_COUNT> s_KeysPrevious;
    static std::array<bool, SDL_SCANCODE_COUNT> s_KeysPressed;
    static std::array<bool, SDL_SCANCODE_COUNT> s_KeysReleased;

    static std::array<bool, SDL_BUTTON_X2 + 1> s_MouseButtonsPrevious;
    static std::array<bool, SDL_BUTTON_X2 + 1> s_MouseButtonsPressed;
    static std::array<bool, SDL_BUTTON_X2 + 1> s_MouseButtonsReleased;

    static glm::vec2 s_MousePosition;
    static glm::vec2 s_MouseDelta;
    static glm::vec2 s_MouseDeltaAccum;
    static glm::vec2 s_MouseScrollDelta;
    static glm::vec2 s_MouseScrollAccum;

    static SDL_Window* s_Window;

    static SDL_Gamepad* s_Gamepad;
    static SDL_JoystickID s_GamepadInstanceID;
    static std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> s_GamepadButtonsPrevious;
    static std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> s_GamepadButtonsPressed;
    static std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> s_GamepadButtonsReleased;
    static std::array<float32, SDL_GAMEPAD_AXIS_COUNT> s_GamepadAxes;
};
