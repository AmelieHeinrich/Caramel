class BackAndForth
{
    EntityInstance self;

    [Range(0, 20)] [Tooltip("Units per second")]
    float backAndForthSpeed = 2.0f;

    [Range(0, 50)]
    float distance = 5.0f;

    vec3 axis = vec3(1.0f, 0.0f, 0.0f);

    bool drawGizmo = true;

    [Color]
    vec4 gizmoColor = vec4(1.0f, 0.8f, 0.1f, 1.0f);

    // Hidden properties survive a hot reload, so the sweep centre is captured once from wherever the
    // instance was placed and does not drift every time the file is edited.
    [Hidden] vec3 origin = vec3(0.0f);
    [Hidden] bool originCaptured = false;

    private float time = 0.0f;

    void OnStart()
    {
        if (!originCaptured)
        {
            origin = self.position;
            originCaptured = true;
        }
        time = 0.0f;
    }

    void OnUpdate(float dt)
    {
        time += dt * backAndForthSpeed;

        vec3 dir = normalize(axis);
        self.position = origin + dir * (sin(time) * distance);

        if (drawGizmo)
        {
            DebugStyle style;
            style.color = gizmoColor;
            style.depthTest = false;
            Debug::Line(origin - dir * distance, origin + dir * distance, style);
            Debug::Sphere(self.position, 0.15f, style);
        }
    }
}
