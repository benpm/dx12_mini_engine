#pragma once

// C-style API for audio + scene save/load, so non-module TUs (e.g.
// lua_scripting_impl.cpp) can hit subsystems without needing to import the
// audio/save_game modules. The Lua impl receives opaque pointers via registry
// slots populated by the engine on init.

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declared opaque handles.
typedef struct AudioSystemHandle AudioSystemHandle;
typedef struct ApplicationHandle ApplicationHandle;

// Returns 1 on success, 0 on failure.
int engine_audio_play_sound(void* audioSystemPtr, const char* path, float volume);

// Queue a deferred scene-file load/save on the Application. Path is treated
// as an absolute or working-dir-relative path. Returns 1 on success.
int engine_app_queue_scene_load(void* appPtr, const char* path);
int engine_app_queue_scene_save(void* appPtr, const char* path);

// Resolve a save-slot name (e.g. "slot1") to a full path under
// %LOCALAPPDATA%\dx12_mini_engine\saves\<name>.json. Writes the result into
// outBuf (UTF-8). Returns 1 on success. Caller's outBuf must be at least 260 bytes.
int engine_save_slot_path(const char* slotName, char* outBuf, int outBufSize);

// Particle ops. particlesPtr is the address of a ParticleSystem instance.
void engine_particles_emit(
    void* particlesPtr, float x, float y, float z, int count, unsigned int rgba, float life
);
void engine_particles_clear(void* particlesPtr);
int engine_particles_alive_count(void* particlesPtr);

// HUD ops. hudPtr is the address of a Hud instance.
void engine_hud_clear(void* hudPtr);
void engine_hud_text(void* hudPtr, float x, float y, const char* text, unsigned int color, float scale);
void engine_hud_filled_rect(
    void* hudPtr, float x, float y, float w, float h, unsigned int color
);
void engine_hud_outline_rect(
    void* hudPtr, float x, float y, float w, float h, unsigned int color
);

#ifdef __cplusplus
}
#endif
