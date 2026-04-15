-- One-shot action: destroys all MeshRef entities
local entities = engine.get_entities_with("MeshRef")
local count = #entities

for i = 1, count do
    engine.destroy_entity(entities[i])
end

engine.log("Deleted " .. count .. " entities")
