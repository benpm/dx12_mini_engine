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
