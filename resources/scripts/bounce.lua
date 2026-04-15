-- Per-entity script: bounces entity up and down with squash/stretch
local script = {}
local height = 3.0
local freq = 2.0
local base_x = 0
local base_z = 0

function script.on_create(id)
    base_x, _, base_z = engine.get_position(id)
    engine.log("Bounce attached to entity " .. id)
end

function script.on_update(id, dt)
    local t = engine.get_time()
    local y = math.abs(math.sin(t * freq)) * height
    engine.set_position(id, base_x, y, base_z)

    -- Squash at bottom, stretch at top
    local stretch = 0.8 + 0.4 * math.abs(math.sin(t * freq))
    engine.set_scale(id, stretch)
end

return script
