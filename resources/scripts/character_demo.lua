-- character_demo.lua — one-shot startup script for the character_demo
-- scene. Sets up a small arena (floor + obstacles) and a player capsule
-- attached to character_controller.lua so WASD drives it via physics.

local mesh_count = engine.get_mesh_count()
if mesh_count == 0 then
    engine.log_warn("character_demo: no meshes loaded — aborting")
    return
end

-- Pick a few mesh indices by name so the layout is deterministic regardless
-- of the alphabetical glTF load order.
local function find_mesh(name)
    local names = engine.get_mesh_names()
    for i, n in ipairs(names) do
        if string.lower(n) == string.lower(name) then
            return i - 1   -- engine.spawn_entity uses 0-based mesh indices
        end
    end
    return 0  -- fallback: first mesh
end

local cube_idx = find_mesh("Cube")
local sphere_idx = find_mesh("Sphere")

-- Static floor body (~40 m × 1 m × 40 m at y=-1). No visual — the engine
-- grid renders the ground line; the body just stops dynamic objects.
engine.add_box_body(0, -1.0, 0, 20.0, 0.5, 20.0, false, 0.0)

-- Four box obstacle entities arranged in a square. Each gets a static box
-- body the player can collide with.
local obstacle_positions = {
    { 4.0, 0.5, 0.0 },
    { -4.0, 0.5, 0.0 },
    { 0.0, 0.5, 4.0 },
    { 0.0, 0.5, -4.0 },
}
for _, p in ipairs(obstacle_positions) do
    local e = engine.spawn_entity(cube_idx, 0, p[1], p[2], p[3], 1.0)
    local b = engine.add_box_body(p[1], p[2], p[3], 0.5, 0.5, 0.5, false, 0.0)
    engine.attach_rigid_body(e, b)
end

-- Player — sphere visual + capsule body. Sphere fits inside the capsule's
-- 0.6 m diameter so the visual stays inside the collider.
local player_entity = engine.spawn_entity(sphere_idx, 0, 0.0, 2.0, 0.0, 0.3)
local player_body = engine.add_capsule_body(
    0.0, 2.0, 0.0, 0.6, 0.3, true, 70.0
)
engine.attach_rigid_body(player_entity, player_body)
engine.attach_script(player_entity, "character_controller.lua")
engine.set_selected_entity(player_entity)

engine.log("character_demo: spawned player + 4 obstacles. WASD to move.")
