-- physics_stress.lua — 1000-entity convex-hull stress / showcase.
--
-- Spawns one static floor body + a 10x10x10 grid of dynamic entities. Each
-- entity gets its own mesh instance (cycling through the loaded mesh table)
-- AND its own convex-hull collider built from that mesh's CPU-cached vertex
-- positions. The hull is sub-sampled to a small point count so Jolt builds
-- cheaply and the broadphase has tight bounds.

local mesh_count = engine.get_mesh_count()
if mesh_count == 0 then
    engine.log_warn("physics_stress: no meshes loaded — aborting")
    return
end

-- Default scene entities (teapots, primitives, gizmo arrows) are intentionally
-- left in place — destroying GizmoArrow entities here invalidates persistent
-- handles in GizmoState and segfaults the next gizmo.update().
-- Camera is framed so the 1000-body grid dominates regardless.

-- Build a list of mesh indices that are safe for hull-bodied stress.
-- Skipped because they wreck the simulation:
--   * teapot / teapot companion — too wide (~3m hull) → bodies overlap
--   * plane                     — flat → degenerate hull, zero inertia
--   * torus                     — non-convex topology → convex hull is the
--                                 enclosing prism, and at 2.7m radius it
--                                 overlaps neighbouring bodies at our step
local names = engine.get_mesh_names()
local safe_meshes = {}
local function is_excluded(name, idx)
    local lname = string.lower(name or "")
    if idx < 2 then return true end                       -- teapot + companion
    if string.find(lname, "plane") then return true end
    if string.find(lname, "torus") then return true end
    if string.find(lname, "teapot") then return true end
    return false
end
for i, name in ipairs(names) do
    local idx = i - 1
    if not is_excluded(name, idx) then
        table.insert(safe_meshes, idx)
    end
end
if #safe_meshes == 0 then
    engine.log_warn("physics_stress: no safe meshes after filter — aborting")
    return
end
engine.log(
    "physics_stress: using " .. #safe_meshes .. " safe meshes (filtered " ..
    (#names - #safe_meshes) .. ")"
)

-- One big static floor at y=-2 so the bottom layer of the grid lands on it.
engine.add_box_body(0, -2.0, 0, 100.0, 0.5, 100.0, false, 0.0)

-- 10x10x10 grid centred at the origin in X/Z, starting just above the floor.
local total = 0
local hull_failures = 0
local side = 10
-- step > 2 * (mesh_extent * scale) keeps hulls non-overlapping at spawn so
-- the broadphase doesn't get hammered with stacked overlaps on frame 0.
-- Spacing chosen so that even the largest remaining mesh (sphere/icosphere
-- at ±1m mesh-local, scaled by `scale`) leaves a >0.5 m gap between
-- adjacent hulls at spawn — keeps the broadphase quiet on frame 0.
-- After ~3 seconds of fall+settle the pile compresses considerably so the
-- final scene reads as a dense rubble heap rather than a sparse cloud.
local x_step = 2.2
local z_step = 2.2
local y_step = 1.6
local y_start = 4.0
local scale = 0.45
local mass = 1.0
local hull_points = 24
local hull_tolerance = 0.05

for iy = 0, side - 1 do
    for ix = 0, side - 1 do
        for iz = 0, side - 1 do
            local x = (ix - (side - 1) * 0.5) * x_step
            local z = (iz - (side - 1) * 0.5) * z_step
            local y = y_start + iy * y_step

            local mesh_idx = safe_meshes[(total % #safe_meshes) + 1]
            local mat_idx = total % 3   -- cycle Diffuse/Metal/Mirror
            local entity = engine.spawn_entity(mesh_idx, mat_idx, x, y, z, scale)

            -- One-call: builds a convex hull from this entity's MeshRef
            -- positions, scaled by the entity's transform, attached to it.
            local body = engine.add_mesh_collider(entity, true, mass, hull_points, hull_tolerance)
            if body == 0 then
                -- Mesh either had no cached positions or degenerate hull —
                -- fall back to a box collider so the entity still simulates.
                hull_failures = hull_failures + 1
                body = engine.add_box_body(x, y, z, 0.3, 0.3, 0.3, true, mass)
                engine.attach_rigid_body(entity, body)
            end
            total = total + 1
        end
    end
end

engine.log(
    "physics_stress: spawned " .. total .. " entities (" ..
    hull_failures .. " hull build failures, fell back to box)"
)
