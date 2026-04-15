-- One-shot action: randomizes albedo of all entities with MeshRef
local entities = engine.get_entities_with("MeshRef")
local count = #entities

for i = 1, count do
    local id = entities[i]
    local r = math.random()
    local g = math.random()
    local b = math.random()
    engine.set_albedo_override(id, r, g, b, 1.0)
end

engine.log("Randomized colors of " .. count .. " entities")
