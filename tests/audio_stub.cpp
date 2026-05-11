// No-op audio stub for the lua_scripting_tests target, which doesn't link
// the engine_lib (and therefore doesn't drag in miniaudio). The Lua bindings
// call this symbol; supplying a stub here lets the standalone test exe link
// without compiling miniaudio into it.

extern "C" int engine_audio_play_sound(void* /*audio*/, const char* /*path*/, float /*volume*/)
{
    return 0;
}
