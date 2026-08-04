class AnimatedRoughness
{
    Mesh self;

    [Range(0, 10)] [Tooltip("Cycles per second")]
    float speed = 0.5f;

    [Range(0, 1)]
    float minRoughness = 0.02f;

    [Range(0, 1)]
    float maxRoughness = 1.0f;

    [Range(0, 1)]
    float metallic = 1.0f;

    private float time = 0.0f;

    void OnUpdate(float dt)
    {
        time += dt * speed;
        float t = 0.5f + 0.5f * sin(time * 6.2831853f);

        Material mat = self.GetMaterial();
        mat.SetRoughness(minRoughness + (maxRoughness - minRoughness) * t);
        mat.SetMetallic(metallic);
    }
}
