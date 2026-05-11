#pragma once

// C-style API for audio playback, so non-module TUs (e.g. lua_scripting_impl.cpp)
// can play sounds without needing to import the audio module. The Lua impl
// receives an opaque AudioSystem pointer via a registry slot.

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declared opaque handle (AudioSystem* in C++).
typedef struct AudioSystemHandle AudioSystemHandle;

// Returns 1 on success, 0 on failure.
int engine_audio_play_sound(void* audioSystemPtr, const char* path, float volume);

#ifdef __cplusplus
}
#endif
