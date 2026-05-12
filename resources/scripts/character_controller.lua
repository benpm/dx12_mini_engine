-- character_controller.lua — per-entity script that turns the attached
-- entity into a WASD-driven physics character.
--
-- Prerequisites: the entity already has a RigidBody (attach a capsule via
-- engine.add_capsule_body + engine.attach_rigid_body, or use the one-call
-- engine.add_mesh_collider). on_update reads W/A/S/D each frame and
-- imperatively sets the body's horizontal velocity; vertical velocity is
-- preserved so gravity still pulls and jumps still arc.

local move_speed = 4.0  -- horizontal m/s while a direction button is held

function on_update(id, dt)
    local body = engine.get_rigid_body(id)
    if body == 0 then return end

    -- Sample current velocity so we can preserve the Y component.
    local _, vy, _ = engine.get_linear_velocity(body)

    local dx, dz = 0.0, 0.0
    if engine.is_button_down("MoveForward")  then dz = dz - 1.0 end
    if engine.is_button_down("MoveBackward") then dz = dz + 1.0 end
    if engine.is_button_down("MoveLeft")     then dx = dx - 1.0 end
    if engine.is_button_down("MoveRight")    then dx = dx + 1.0 end

    -- Normalise so diagonals aren't 1.4× faster.
    local len = math.sqrt(dx * dx + dz * dz)
    if len > 0.0 then
        dx = dx / len * move_speed
        dz = dz / len * move_speed
    end

    engine.set_linear_velocity(body, dx, vy, dz)
end
