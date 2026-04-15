-- Per-entity script: orbits the entity around the Y axis
-- Attach to any entity to make it orbit the origin
local script = {}
local speed = 1.5
local radius = 4.0
local base_y = 1.5

function script.on_create(id)
    engine.log("Orbit script attached to entity " .. id)
end

function script.on_update(id, dt)
    local t = engine.get_time() * speed
    local x = math.cos(t) * radius
    local z = math.sin(t) * radius
    engine.set_position(id, x, base_y, z)
end

function script.on_destroy(id)
    engine.log("Orbit script detached from entity " .. id)
end

return script
