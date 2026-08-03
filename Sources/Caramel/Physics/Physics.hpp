/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 15:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

namespace Physics
{
    // Brings up Jolt's default allocator, Factory and type registry. Must run once before any
    // JPH::Shape is created or deserialized (collider cooking, StreamingModel loads, raycasts).
    void Initialize();

    // Tears Jolt back down. Call only after every JPH::RefConst<Shape> in the engine has been
    // released (StreamingManager/StreamingModel own the ones created from streamed colliders).
    void Shutdown();
}
