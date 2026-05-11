-- physics_demo.lua — spawns a static floor body + a tower of dynamic
-- boxes attached to mesh entities. After this runs you should see the
-- entities fall and stack on the floor as the physics step ticks.

local mesh_count = engine.get_mesh_count()
if mesh_count == 0 then
    engine.log_warn("physics_demo: no meshes available — spawn aborted")
    return
end

-- Big static floor body at y=-2 so spawned boxes land on it. The
-- visual doesn't need a mesh; the engine's existing grid handles that.
engine.add_box_body(0, -2.0, 0, 50.0, 0.5, 50.0, false, 0.0)

-- Stack 8 dynamic cubes vertically. Each one gets a mesh entity at the
-- spawn position; attach_rigid_body binds the entity's Transform to the
-- body so the entity falls when physics advances.
local count = 8
for i = 0, count - 1 do
    local mesh_idx = i % mesh_count
    local x = (math.random() - 0.5) * 0.4
    local y = 2.0 + i * 1.2
    local z = (math.random() - 0.5) * 0.4
    local entity = engine.spawn_entity(mesh_idx, 0, x, y, z, 0.5)
    local body = engine.add_box_body(x, y, z, 0.5, 0.5, 0.5, true, 1.0)
    engine.attach_rigid_body(entity, body)
end

engine.log("physics_demo: spawned 1 floor + " .. count .. " dynamic boxes")
