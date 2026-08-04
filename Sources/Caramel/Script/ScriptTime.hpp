/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <SDL3/SDL_scancode.h>

class ScriptTime
{
public:
    static void Set(float32 delta, float32 elapsed, uint64 frameCount);

    static float32 GetDelta() { return s_Delta; }
    static float32 GetElapsed() { return s_Elapsed; }
    static uint64 GetFrameCount() { return s_FrameCount; }

private:
    static float32 s_Delta;
    static float32 s_Elapsed;
    static uint64 s_FrameCount;
};

// RegisterGlobalProperty needs a stable address per constant, so these live as real objects.
inline const int32 kScriptKeyW = SDL_SCANCODE_W;
inline const int32 kScriptKeyA = SDL_SCANCODE_A;
inline const int32 kScriptKeyS = SDL_SCANCODE_S;
inline const int32 kScriptKeyD = SDL_SCANCODE_D;
inline const int32 kScriptKeyQ = SDL_SCANCODE_Q;
inline const int32 kScriptKeyE = SDL_SCANCODE_E;
inline const int32 kScriptKeySpace = SDL_SCANCODE_SPACE;
inline const int32 kScriptKeyLeftShift = SDL_SCANCODE_LSHIFT;
inline const int32 kScriptKeyLeft = SDL_SCANCODE_LEFT;
inline const int32 kScriptKeyRight = SDL_SCANCODE_RIGHT;
inline const int32 kScriptKeyUp = SDL_SCANCODE_UP;
inline const int32 kScriptKeyDown = SDL_SCANCODE_DOWN;
