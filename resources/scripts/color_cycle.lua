-- Per-entity script: cycles albedo color over time
local script = {}

function script.on_create(id)
    engine.log("Color cycle attached to entity " .. id)
end

function script.on_update(id, dt)
    local t = engine.get_time()
    local r = math.sin(t * 2.0) * 0.5 + 0.5
    local g = math.sin(t * 2.0 + 2.094) * 0.5 + 0.5
    local b = math.sin(t * 2.0 + 4.189) * 0.5 + 0.5
    engine.set_albedo_override(id, r, g, b, 1.0)
end

function script.on_destroy(id)
    engine.log("Color cycle detached from entity " .. id)
    engine.set_albedo_override(id, 0, 0, 0, 0)
end

return script
