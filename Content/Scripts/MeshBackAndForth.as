// Mesh-scoped counterpart to BackAndForth. A script's `self` type decides what it can be attached
// to: BackAndForth declares `EntityInstance self` and therefore always moves a whole instance, even
// when attached under a mesh. This one declares `Mesh self`, so it only offsets its own mesh.
class MeshBackAndForth
{
    Mesh self;

    [Range(0, 20)] [Tooltip("Units per second")]
    float backAndForthSpeed = 2.0f;

    [Range(0, 50)]
    float distance = 5.0f;

    vec3 axis = vec3(1.0f, 0.0f, 0.0f);

    // The mesh offset starts at zero, so unlike the instance version there is no placement to
    // capture -- the sweep is centred on wherever the mesh already sits inside the entity.
    private float time = 0.0f;

    void OnStart()
    {
        time = 0.0f;
    }

    void OnUpdate(float dt)
    {
        time += dt * backAndForthSpeed;
        self.position = normalize(axis) * (sin(time) * distance);
    }
}
