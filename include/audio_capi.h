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

// Returns 1 if the named button (e.g. "MoveForward", "Exit") is pressed.
int engine_app_is_button_down(void* appPtr, const char* buttonName);

// Script attach/detach for the Lua side — luaPtr is a LuaScripting* registered
// at init time. Returns 1 on successful attach.
int engine_lua_attach_script(void* luaPtr, unsigned long long entityId, const char* path);
void engine_lua_detach_script(void* luaPtr, unsigned long long entityId);

// Resolve a save-slot name (e.g. "slot1") to a full path under
// %LOCALAPPDATA%\dx12_mini_engine\saves\<name>.json. Writes the result into
// outBuf (UTF-8). Returns 1 on success. Caller's outBuf must be at least 260 bytes.
int engine_save_slot_path(const char* slotName, char* outBuf, int outBufSize);

// Scene mesh-data introspection. scenePtr is the address of a Scene instance.
// On success: outData points at the mesh's tight-packed vec3 position stream
// (12 bytes/vert), outCount is the vertex count. Returns 1 on success, 0 if the
// mesh index is out of range or the position cache hasn't been populated.
// Pointer is owned by Scene and stays valid until clearScene().
int engine_scene_get_mesh_positions(
    void* scenePtr, int meshIdx, const float** outData, unsigned int* outCount
);

// Physics ops. physicsPtr is the address of a PhysicsWorld instance.
unsigned int engine_physics_create_box(
    void* physicsPtr, float px, float py, float pz, float hx, float hy, float hz, int dynamic,
    float mass
);
unsigned int engine_physics_create_sphere(
    void* physicsPtr, float px, float py, float pz, float radius, int dynamic, float mass
);
unsigned int engine_physics_create_capsule(
    void* physicsPtr, float px, float py, float pz, float halfHeight, float radius, int dynamic,
    float mass
);
// Convex-hull body creation. positions is a tightly-packed vec3 stream (12B/vert).
// The backend may further simplify; pass at most 256 points.
unsigned int engine_physics_create_convex_hull(
    void* physicsPtr, const float* positions, unsigned int count, unsigned int stride, float px,
    float py, float pz, int dynamic, float mass, float hullTolerance
);

void engine_physics_destroy_body(void* physicsPtr, unsigned int id);
void engine_physics_get_body_position(
    void* physicsPtr, unsigned int id, float* outX, float* outY, float* outZ
);
void engine_physics_get_body_rotation(
    void* physicsPtr, unsigned int id, float* outX, float* outY, float* outZ, float* outW
);
int engine_physics_raycast(
    void* physicsPtr, float ox, float oy, float oz, float dx, float dy, float dz,
    float maxDistance, float* hitX, float* hitY, float* hitZ, float* hitDistance
);
void engine_physics_apply_force(void* physicsPtr, unsigned int id, float fx, float fy, float fz);
void engine_physics_apply_impulse(void* physicsPtr, unsigned int id, float ix, float iy, float iz);
void engine_physics_set_body_position(
    void* physicsPtr, unsigned int id, float px, float py, float pz
);
void engine_physics_get_linear_velocity(
    void* physicsPtr, unsigned int id, float* outX, float* outY, float* outZ
);
void engine_physics_set_linear_velocity(
    void* physicsPtr, unsigned int id, float vx, float vy, float vz
);
void engine_physics_get_angular_velocity(
    void* physicsPtr, unsigned int id, float* outX, float* outY, float* outZ
);
void engine_physics_set_angular_velocity(
    void* physicsPtr, unsigned int id, float wx, float wy, float wz
);

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
