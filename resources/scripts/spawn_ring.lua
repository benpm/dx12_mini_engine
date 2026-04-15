-- One-shot script: spawns a ring of entities around the origin
local count = 12
local radius = 5.0
local mesh_count = engine.get_mesh_count()

for i = 0, count - 1 do
    local angle = (i / count) * math.pi * 2
    local x = math.cos(angle) * radius
    local z = math.sin(angle) * radius
    local mesh_idx = i % mesh_count
    engine.spawn_entity(mesh_idx, 0, x, 0, z, 0.3)
end

engine.log("Spawned ring of " .. count .. " entities")
