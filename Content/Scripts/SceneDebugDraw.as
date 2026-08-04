class SceneDebugDraw
{
    [Color]
    vec4 gridColor = vec4(0.25f, 0.25f, 0.3f, 1.0f);

    [Range(1, 200)]
    int gridLines = 20;

    [Range(0.1, 10.0)]
    float gridSpacing = 1.0f;

    bool drawAxes = true;

    void OnUpdate(float dt)
    {
        DebugStyle style;
        style.color = gridColor;
        style.thickness = 1.0f;

        float half = float(gridLines) * gridSpacing * 0.5f;
        for (int i = 0; i <= gridLines; i++)
        {
            float o = -half + float(i) * gridSpacing;
            Debug::Line(vec3(o, 0.0f, -half), vec3(o, 0.0f, half), style);
            Debug::Line(vec3(-half, 0.0f, o), vec3(half, 0.0f, o), style);
        }

        if (drawAxes)
            Debug::Axes(translate(vec3(0.0f)), 2.0f);
    }
}
