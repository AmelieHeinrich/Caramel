// Stress rig for the light list: fills the scene with a mix of light types and keeps them moving, so
// clustered culling and ReSTIR DI have something with real temporal churn to be measured against. A
// static light field lets a cache hide every mistake either of those can make.
//
// Attach to an Empty -- the spawned lights are parented under it, and that parent is also how the
// script re-finds them after a hot reload.
//
// Note that the shading loop is currently naive (every light, every pixel), so frame time is O(count)
// per pixel. A few hundred lights is already enough to make that obvious; four figures will crawl
// until there is a culling pass in front of it. That is the point of this script.
class LightSpawner
{
    Entity self;

    [Range(0, 65536)]
    [Tooltip("Applied live -- the field grows and shrinks as you drag this")]
    int count = 64;

    [Range(1, 500)]
    [Tooltip("Radius of the ring the lights orbit on")]
    float radius = 25.0f;

    [Range(0, 100)]
    [Tooltip("Vertical spread of the field")]
    float height = 8.0f;

    [Tooltip("Which types go into the mix, round-robin over whatever is enabled")]
    bool usePoint = true;
    bool useSpot = true;
    bool useArea = false;

    [Range(0, 5000000)]
    [Tooltip("Lumens. Local lights lose intensity to 1/d^2, so this needs floodlight numbers to carry across a scene")]
    float lumens = 200000.0f;

    [Range(0, 100000)]
    [Tooltip("Nits, area lights only")]
    float nits = 5000.0f;

    [Range(1, 500)]
    float lightRange = 30.0f;

    bool animate = true;

    [Range(0, 5)]
    float orbitSpeed = 0.2f;

    [Range(0, 20)]
    [Tooltip("Vertical bob height")]
    float bobAmount = 3.0f;

    [Range(0, 5)]
    float bobSpeed = 0.8f;

    [Tooltip("Cycle each light's hue over time instead of holding its spawn colour")]
    bool cycleColor = false;

    [Range(0, 5)]
    float colorSpeed = 0.3f;

    [Range(0, 1)]
    [Tooltip("0 = white, 1 = fully saturated")]
    float saturation = 0.8f;

    [Range(1, 100000)]
    [Tooltip("Change for a different layout with the same count")]
    int seed = 1;

    private array<Light> lights;
    private float time = 0.0f;

    // Same shader-style hash the other spawners use: a given (index, seed) pair always produces the
    // same value, so reloading does not reshuffle the field.
    float Hash(float n)
    {
        return fraction(sin(n * 12.9898f) * 43758.5453f);
    }

    // Cheap sinusoidal hue ramp. Not a real HSV conversion -- three offset cosines is enough to tell
    // lights apart, which is the whole point of colouring them.
    vec3 Hue(float t)
    {
        float r = 0.5f + 0.5f * cos(6.28318f * t);
        float g = 0.5f + 0.5f * cos(6.28318f * (t + 0.3333f));
        float b = 0.5f + 0.5f * cos(6.28318f * (t + 0.6666f));
        return lerp(vec3(1.0f, 1.0f, 1.0f), vec3(r, g, b), saturation);
    }

    int TypeForIndex(int i)
    {
        array<int> pool;
        if (usePoint) pool.insertLast(LightType::Point);
        if (useSpot)  pool.insertLast(LightType::Spot);
        if (useArea)  pool.insertLast(LightType::Area);
        if (pool.length() == 0)
            return LightType::Point;
        return pool[i % int(pool.length())];
    }

    // Reclaims whatever a previous run left parented under us. Walking children is O(children); the
    // obvious alternative -- looking each light up by name -- is a linear scan of every node in the
    // scene per light, so at four-figure counts it is quadratic and hangs the editor.
    void Adopt()
    {
        uint childCount = self.GetChildCount();
        for (uint i = 0; i < childCount; i++)
        {
            Light light = self.GetChild(i).AsLight();
            if (light.IsValid())
                lights.insertLast(light);
        }
    }

    // Only ever touches the difference. Dragging count from 64 to 16000 spawns 15936 lights and
    // leaves the existing ones exactly where they are.
    void Sync()
    {
        while (int(lights.length()) > count)
        {
            lights[lights.length() - 1].Destroy();
            lights.removeLast();
        }

        while (int(lights.length()) < count)
        {
            int index = int(lights.length());
            Light light = Scene::SpawnLight(TypeForIndex(index), "SpawnedLight_" + index);
            if (!light.IsValid())
            {
                PrintError("LightSpawner: could not spawn light " + index);
                return;
            }

            Scene::Reparent(light.GetEntity(), self);
            lights.insertLast(light);
        }
    }

    void OnStart()
    {
        lights.resize(0);
        time = 0.0f;

        Adopt();
        Sync();

        Print("LightSpawner: " + lights.length() + " lights");
    }

    void OnUpdate(float dt)
    {
        // OnStart does not re-run when an inspector value changes, so the count is reconciled here.
        // That is what makes every knob on this script live.
        if (int(lights.length()) != count)
            Sync();

        if (animate)
            time += dt;

        for (uint i = 0; i < lights.length(); i++)
        {
            Light light = lights[i];
            if (!light.IsValid())
                continue;

            float fi = float(i);
            float phase = Hash(fi + float(seed) * 1000.0f);

            int type = TypeForIndex(int(i));
            light.type = type;
            light.range = lightRange;
            light.intensity = (type == LightType::Area) ? nits : lumens;
            light.color = cycleColor ? Hue(fraction(phase + time * colorSpeed)) : Hue(phase);

            if (type == LightType::Spot)
            {
                light.innerAngle = 20.0f;
                light.outerAngle = 35.0f;
                // Straight down, so the spots sweep across whatever is in the middle of the scene
                // instead of all staring the same way.
                light.rotation = vec3(-90.0f, 0.0f, 0.0f);
            }
            else if (type == LightType::Area)
            {
                light.shape = AreaShape::Rect;
                light.size = vec2(2.0f, 2.0f);
                light.rotation = vec3(-90.0f, 0.0f, 0.0f);
            }

            // Each light gets its own orbit radius and speed, so the field never settles into a
            // single rotating ring -- overlapping orbits are what make a cluster's light set churn
            // frame to frame, which is the case worth profiling.
            float r = radius * (0.3f + 0.7f * Hash(fi + 7.5f));
            float speed = orbitSpeed * (0.5f + Hash(fi + 3.5f));
            float angle = 6.28318f * phase + time * speed;
            float y = height * (Hash(fi + 11.5f) - 0.5f) + sin(time * bobSpeed + phase * 6.28318f) * bobAmount;

            light.position = vec3(cos(angle) * r, y, sin(angle) * r);
        }
    }
}
