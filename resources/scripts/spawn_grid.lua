-- One-shot action: spawns a grid of entities
local rows = 5
local cols = 5
local spacing = 2.0
local mesh_count = engine.get_mesh_count()
local mat_count = engine.get_material_count()

local offset_x = -(cols - 1) * spacing / 2
local offset_z = -(rows - 1) * spacing / 2

for r = 0, rows - 1 do
    for c = 0, cols - 1 do
        local x = offset_x + c * spacing
        local z = offset_z + r * spacing
        local mesh_idx = (r * cols + c) % math.max(mesh_count, 1)
        local mat_idx = (r + c) % math.max(mat_count, 1)
        engine.spawn_entity(mesh_idx, mat_idx, x, 0, z, 0.4)
    end
end

engine.log("Spawned " .. (rows * cols) .. " entities in a grid")
