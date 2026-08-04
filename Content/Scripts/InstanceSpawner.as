[RunOnce]
class InstanceSpawner
{
    Entity self;

    [Tooltip("Path to a .cmdl under Content/Cache")]
    string modelPath = "Content/Cache/Sphere.cmdl";

    [Range(1, 512)]
    int count = 100;

    [Range(0, 200)]
    float radius = 20.0f;

    [Range(0, 20)]
    float heightVariation = 0.0f;

    void OnStart()
    {
        Entity root = Scene::SpawnModel(modelPath, "SpawnedInstances");
        if (!root.IsValid())
        {
            PrintError("InstanceSpawner: could not spawn " + modelPath);
            return;
        }

        Scene::Reparent(root, self);

        for (int i = 0; i < count; i++)
        {
            float t = float(i) / float(count);
            float angle = t * 6.2831853f * 8.0f;
            float r = radius * sqrt(t);
            vec3 p = vec3(cos(angle) * r, sin(angle * 3.0f) * heightVariation, sin(angle) * r);

            // SpawnModel already created one instance at the origin, same as dropping a model in.
            if (i == 0)
                root.GetInstance(0).position = p;
            else
                root.AddInstance(p);
        }

        Print("Spawned " + count + " instances of " + modelPath);
    }
}
