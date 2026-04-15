-- Per-entity script: pulses emissive glow on the entity's material
local script = {}
local mat_idx = -1

function script.on_create(id)
    mat_idx = engine.get_material_index(id)
    if mat_idx < 0 then
        engine.log_warn("pulse_emissive: entity " .. id .. " has no material")
    end
end

function script.on_update(id, dt)
    if mat_idx < 0 then return end
    local t = engine.get_time()
    local strength = (math.sin(t * 3.0) * 0.5 + 0.5) * 5.0
    engine.set_material_emissive(mat_idx, 1.0, 0.3, 0.0, strength)
end

function script.on_destroy(id)
    if mat_idx >= 0 then
        engine.set_material_emissive(mat_idx, 0, 0, 0, 0)
    end
end

return script
