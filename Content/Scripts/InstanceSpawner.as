[RunOnce]
class InstanceSpawner
{
    Entity self;

    [Tooltip("Path to a .cmdl under Content/Cache")]
    string modelPath = "Content/Cache/Sphere.cmdl";

    [Range(1, 200000)]
    int count = 100;

    [Range(0, 2000)]
    float radius = 20.0f;

    [Range(0, 200)]
    [Tooltip("Max random vertical jitter, applied on top of the spiral layout")]
    float heightVariation = 0.0f;

    [Tooltip("Randomize each instance's rotation on all three axes")]
    bool randomRotation = false;

    [Range(0, 1)]
    [Tooltip("Random uniform scale jitter, as a fraction of the base scale (0 = no variation)")]
    float scaleVariation = 0.0f;

    [Range(1, 100000)]
    [Tooltip("Change this to get a different random layout with the same count")]
    int seed = 1;

    // Cheap deterministic hash, shader-style (sin+fraction) rather than a stateful RNG, so a given
    // (index, seed) pair always produces the same value -- reordering or skipping indices can't
    // desync the sequence the way a stateful generator would.
    float Hash(float n)
    {
        return fraction(sin(n * 12.9898f) * 43758.5453f);
    }

    void ApplyRandomVariation(EntityInstance inst, float seedBase)
    {
        if (randomRotation)
        {
            inst.rotation = vec3(
                Hash(seedBase + 1.5f) * 360.0f,
                Hash(seedBase + 2.5f) * 360.0f,
                Hash(seedBase + 3.5f) * 360.0f);
        }

        if (scaleVariation > 0.0f)
        {
            float s = 1.0f + (Hash(seedBase + 4.5f) * 2.0f - 1.0f) * scaleVariation;
            inst.scale = vec3(s, s, s);
        }
    }

    void OnStart()
    {
        Entity root = Scene::SpawnModel(modelPath, "SpawnedInstances");
        if (!root.IsValid())
        {
            PrintError("InstanceSpawner: could not spawn " + modelPath);
            return;
        }

        Scene::Reparent(root, self);

        // Golden-angle (phyllotaxis) spiral: fills a disc far more evenly at high counts than a
        // fixed-winds sweep, which leaves visible arms once count gets into the tens of thousands.
        const float goldenAngle = 2.399963f;

        for (int i = 0; i < count; i++)
        {
            float fi = float(i);
            float t = fi / float(count);
            float angle = fi * goldenAngle;
            float r = radius * sqrt(t);
            float seedBase = fi + float(seed) * 1000.0f;

            float y = (Hash(seedBase + 0.5f) * 2.0f - 1.0f) * heightVariation;
            vec3 p = vec3(cos(angle) * r, y, sin(angle) * r);

            // SpawnModel already created one instance at the origin, same as dropping a model in.
            if (i == 0)
            {
                EntityInstance inst = root.GetInstance(0);
                inst.position = p;
                ApplyRandomVariation(inst, seedBase);
            }
            else
            {
                EntityInstance inst = root.AddInstance(p);
                ApplyRandomVariation(inst, seedBase);
            }
        }

        Print("Spawned " + count + " instances of " + modelPath);
    }
}
