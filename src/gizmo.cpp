module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <wrl.h>
#include <cmath>
#include <cstdint>
#include <vector>

module gizmo;

using namespace DirectX;

static constexpr float PI = 3.14159265358979323846f;
static constexpr uint32_t kSegments = 12;
static constexpr float kShaftRadius = 0.02f;
static constexpr float kShaftLength = 0.8f;
static constexpr float kHeadRadius = 0.06f;
static constexpr float kHeadLength = 0.2f;

// Generate arrow mesh along +Y axis
static void generateArrowMesh(std::vector<VertexPBR>& verts, std::vector<uint32_t>& idx)
{
    verts.clear();
    idx.clear();

    auto pushVert = [&](vec3 p, vec3 n) {
        VertexPBR v;
        v.position = p;
        v.normal = n;
        v.uv = { 0, 0 };
        verts.push_back(v);
    };

    // --- Shaft (cylinder along Y from 0 to shaftLength) ---
    uint32_t shaftBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i <= kSegments; ++i) {
        float a = static_cast<float>(i % kSegments) * 2.0f * PI / static_cast<float>(kSegments);
        float cx = std::cos(a);
        float cz = std::sin(a);
        vec3 n = normalize(vec3(cx, 0, cz));
        pushVert(vec3(cx * kShaftRadius, 0, cz * kShaftRadius), n);
        pushVert(vec3(cx * kShaftRadius, kShaftLength, cz * kShaftRadius), n);
    }
    for (uint32_t i = 0; i < kSegments; ++i) {
        uint32_t b = shaftBase + i * 2;
        idx.push_back(b);
        idx.push_back(b + 2);
        idx.push_back(b + 1);
        idx.push_back(b + 1);
        idx.push_back(b + 2);
        idx.push_back(b + 3);
    }

    // --- Shaft bottom cap ---
    uint32_t botCenter = static_cast<uint32_t>(verts.size());
    pushVert(vec3(0, 0, 0), vec3(0, -1, 0));
    for (uint32_t i = 0; i < kSegments; ++i) {
        float a = static_cast<float>(i) * 2.0f * PI / static_cast<float>(kSegments);
        pushVert(vec3(std::cos(a) * kShaftRadius, 0, std::sin(a) * kShaftRadius), vec3(0, -1, 0));
    }
    for (uint32_t i = 0; i < kSegments; ++i) {
        idx.push_back(botCenter);
        idx.push_back(botCenter + 1 + (i + 1) % kSegments);
        idx.push_back(botCenter + 1 + i);
    }

    // --- Cone head (from shaftLength to shaftLength + headLength) ---
    uint32_t coneBase = static_cast<uint32_t>(verts.size());
    float tipY = kShaftLength + kHeadLength;
    // Cone slope normal: normalize(headRadius, headLength, 0) rotated around Y
    float slopeLen = std::sqrt(kHeadRadius * kHeadRadius + kHeadLength * kHeadLength);
    float ny = kHeadRadius / slopeLen;
    float nr = kHeadLength / slopeLen;
    for (uint32_t i = 0; i <= kSegments; ++i) {
        float a = static_cast<float>(i % kSegments) * 2.0f * PI / static_cast<float>(kSegments);
        float cx = std::cos(a);
        float cz = std::sin(a);
        vec3 n = normalize(vec3(cx * nr, ny, cz * nr));
        pushVert(vec3(cx * kHeadRadius, kShaftLength, cz * kHeadRadius), n);
        pushVert(vec3(0, tipY, 0), n);
    }
    for (uint32_t i = 0; i < kSegments; ++i) {
        uint32_t b = coneBase + i * 2;
        idx.push_back(b);
        idx.push_back(b + 2);
        idx.push_back(b + 1);
    }

    // --- Cone base cap ---
    uint32_t coneCapCenter = static_cast<uint32_t>(verts.size());
    pushVert(vec3(0, kShaftLength, 0), vec3(0, -1, 0));
    for (uint32_t i = 0; i < kSegments; ++i) {
        float a = static_cast<float>(i) * 2.0f * PI / static_cast<float>(kSegments);
        pushVert(
            vec3(std::cos(a) * kHeadRadius, kShaftLength, std::sin(a) * kHeadRadius), vec3(0, -1, 0)
        );
    }
    for (uint32_t i = 0; i < kSegments; ++i) {
        idx.push_back(coneCapCenter);
        idx.push_back(coneCapCenter + 1 + (i + 1) % kSegments);
        idx.push_back(coneCapCenter + 1 + i);
    }
}

// Project a world-space point to screen-space pixels
static vec2 worldToScreen(const vec3& worldPos, const mat4& viewProj, const D3D12_VIEWPORT& vp)
{
    XMVECTOR p = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    XMVECTOR clip = XMVector4Transform(p, viewProj.load());
    float w = XMVectorGetW(clip);
    if (std::abs(w) < 1e-6f) {
        return { 0, 0 };
    }
    float ndcX = XMVectorGetX(clip) / w;
    float ndcY = XMVectorGetY(clip) / w;
    return { (ndcX * 0.5f + 0.5f) * vp.Width + vp.TopLeftX,
             (-ndcY * 0.5f + 0.5f) * vp.Height + vp.TopLeftY };
}

void GizmoState::init(Scene& scene, gfx::IDevice& dev, CommandQueue& cmdQueue)
{
    std::vector<VertexPBR> verts;
    std::vector<uint32_t> indices;
    generateArrowMesh(verts, indices);

    // Create 3 emissive materials (R, G, B)
    vec4 colors[3] = {
        { 1, 0, 0, 1 },  // X = red
        { 0, 1, 0, 1 },  // Y = green
        { 0, 0, 1, 1 },  // Z = blue
    };
    for (int i = 0; i < 3; ++i) {
        Material mat;
        mat.albedo = colors[i];
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        mat.emissiveStrength = 5.0f;
        mat.emissive = colors[i];
        mat.reflective = false;
        materialIndices[i] = static_cast<int>(scene.materials.size());
        scene.materials.push_back(mat);
    }

    // Upload arrow mesh
    auto cmdList = cmdQueue.getCmdList();
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> temps;
    arrowMeshRef = scene.appendToMegaBuffers(
        dev, cmdQueue, cmdList, verts, indices, materialIndices[0], temps
    );
    uint64_t fv = cmdQueue.execCmdList(cmdList);
    scene.trackUploadBatch(fv, std::move(temps));

    // Create 3 arrow entities
    for (int i = 0; i < 3; ++i) {
        MeshRef mesh = arrowMeshRef;
        mesh.materialIndex = materialIndices[i];
        Transform tf;
        tf.world = scale(0.0f);  // hidden initially
        arrows[i] =
            scene.ecsWorld.entity().set(tf).set(mesh).set(GizmoArrow{ static_cast<GizmoAxis>(i) });
    }

    spdlog::info("Gizmo initialized ({} verts, {} indices)", verts.size(), indices.size());
}

void GizmoState::update(
    Scene& scene,
    flecs::entity& selectedEntity,
    const OrbitCamera& cam,
    const D3D12_VIEWPORT& viewport,
    vec2 mousePos,
    bool leftDown,
    bool leftWasDown,
    uint32_t pickedIndex
)
{
    bool leftJustPressed = leftDown && !leftWasDown;
    bool leftJustReleased = !leftDown && leftWasDown;

    // Position gizmo at selected entity
    if (selectedEntity.is_alive() && selectedEntity.has<Transform>()) {
        auto tf = selectedEntity.get<Transform>();
        vec3 entityPos = { tf.world._41, tf.world._42, tf.world._43 };

        // Camera position
        float camX = cam.radius * std::cos(cam.pitch) * std::cos(cam.yaw);
        float camY = cam.radius * std::sin(cam.pitch);
        float camZ = cam.radius * std::cos(cam.pitch) * std::sin(cam.yaw);
        vec3 camPos = { camX, camY, camZ };
        float dist = length(entityPos - camPos);
        float gizmoScale = dist * 0.1f;

        // Per-axis rotation: arrow mesh points along +Y
        mat4 axisRotations[3] = {
            rotateAxis(vec3(0, 0, 1), -PI * 0.5f),  // X: +Y → +X
            mat4{},                                 // Y: identity
            rotateAxis(vec3(1, 0, 0), PI * 0.5f),   // Z: +Y → +Z
        };

        for (int i = 0; i < 3; ++i) {
            mat4 world = scale(gizmoScale) * axisRotations[i] * translate(entityPos);
            arrows[i].set(Transform{ world });
        }

        // --- Drag logic ---
        if (leftJustPressed && !dragging) {
            // Check if the pick hit a gizmo arrow
            if (pickedIndex > 0) {
                uint32_t pickIdx = pickedIndex - 1;
                if (pickIdx < scene.drawIndexToEntity.size()) {
                    flecs::entity picked = scene.drawIndexToEntity[pickIdx];
                    if (isGizmoEntity(picked)) {
                        auto ga = picked.get<GizmoArrow>();
                        dragging = true;
                        dragAxis = ga.axis;
                        dragStartEntityPos = entityPos;
                        dragStartMouse = mousePos;

                        // Compute screen-space axis direction
                        mat4 vp = cam.view() * cam.proj();
                        vec3 axisDir = { 0, 0, 0 };
                        switch (dragAxis) {
                            case GizmoAxis::X:
                                axisDir.x = 1.0f;
                                break;
                            case GizmoAxis::Y:
                                axisDir.y = 1.0f;
                                break;
                            case GizmoAxis::Z:
                                axisDir.z = 1.0f;
                                break;
                        }
                        vec2 s0 = worldToScreen(entityPos, vp, viewport);
                        vec2 s1 = worldToScreen(entityPos + axisDir, vp, viewport);
                        vec2 dir = s1 - s0;
                        float len = length(dir);
                        if (len > 1.0f) {
                            dragScreenDir = dir / len;
                        } else {
                            dragging = false;  // axis nearly perpendicular to view
                        }
                    }
                }
            }
        }

        if (dragging && leftDown) {
            vec2 mouseDelta = mousePos - dragStartMouse;
            float projection = dot(mouseDelta, dragScreenDir);

            // Convert pixel displacement to world units
            float sensitivity = dist / viewport.Height;
            float worldDisp = projection * sensitivity;

            vec3 newPos = dragStartEntityPos;
            switch (dragAxis) {
                case GizmoAxis::X:
                    newPos.x += worldDisp;
                    break;
                case GizmoAxis::Y:
                    newPos.y += worldDisp;
                    break;
                case GizmoAxis::Z:
                    newPos.z += worldDisp;
                    break;
            }

            Transform etf = selectedEntity.get<Transform>();
            etf.world._41 = newPos.x;
            etf.world._42 = newPos.y;
            etf.world._43 = newPos.z;
            selectedEntity.set(etf);
        }

        if (leftJustReleased && dragging) {
            dragging = false;
        }
    } else {
        // No selection — hide arrows
        for (int i = 0; i < 3; ++i) {
            arrows[i].set(Transform{ scale(0.0f) });
        }
        if (dragging) {
            dragging = false;
        }
    }
}

bool GizmoState::isGizmoEntity(flecs::entity e) const
{
    for (int i = 0; i < 3; ++i) {
        if (arrows[i] == e) {
            return true;
        }
    }
    return false;
}
