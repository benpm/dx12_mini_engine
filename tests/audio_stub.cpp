// No-op audio stub for the lua_scripting_tests target, which doesn't link
// the engine_lib (and therefore doesn't drag in miniaudio). The Lua bindings
// call this symbol; supplying a stub here lets the standalone test exe link
// without compiling miniaudio into it.

extern "C" int engine_audio_play_sound(void* /*audio*/, const char* /*path*/, float /*volume*/)
{
    return 0;
}

extern "C" int engine_app_queue_scene_load(void* /*app*/, const char* /*path*/)
{
    return 0;
}

extern "C" int engine_app_queue_scene_save(void* /*app*/, const char* /*path*/)
{
    return 0;
}

extern "C" int engine_save_slot_path(const char* /*slot*/, char* outBuf, int outBufSize)
{
    if (outBuf && outBufSize > 0) {
        outBuf[0] = '\0';
    }
    return 0;
}

extern "C" void engine_hud_clear(void*)
{
}

extern "C" void engine_hud_text(void*, float, float, const char*, unsigned int, float)
{
}

extern "C" void engine_hud_filled_rect(void*, float, float, float, float, unsigned int)
{
}

extern "C" void engine_hud_outline_rect(void*, float, float, float, float, unsigned int)
{
}

extern "C" void engine_particles_emit(void*, float, float, float, int, unsigned int, float)
{
}

extern "C" void engine_particles_clear(void*)
{
}

extern "C" int engine_particles_alive_count(void*)
{
    return 0;
}

extern "C" unsigned int engine_physics_create_box(
    void*, float, float, float, float, float, float, int, float
)
{
    return 0;
}

extern "C" unsigned int engine_physics_create_sphere(
    void*, float, float, float, float, int, float
)
{
    return 0;
}

extern "C" void engine_physics_destroy_body(void*, unsigned int) {}

extern "C" void engine_physics_get_body_position(
    void*, unsigned int, float* x, float* y, float* z
)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (z) *z = 0;
}

extern "C" void engine_physics_get_body_rotation(
    void*, unsigned int, float* x, float* y, float* z, float* w
)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (z) *z = 0;
    if (w) *w = 1;
}

extern "C" int engine_physics_raycast(
    void*, float, float, float, float, float, float, float, float* hx, float* hy, float* hz,
    float* hd
)
{
    if (hx) *hx = 0;
    if (hy) *hy = 0;
    if (hz) *hz = 0;
    if (hd) *hd = 0;
    return 0;
}
