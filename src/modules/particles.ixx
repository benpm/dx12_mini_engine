module;

#include <cstdint>
#include <vector>

export module particles;

export import math;

// ParticleSystem — CPU-driven particle simulation. Particles are pushed into
// the existing BillboardRenderer pipeline for rendering, so no extra PSO is
// needed for the first pass. Full GPU compute particles (append/consume
// buffers, ExecuteIndirect) are staged for a follow-up.
//
// A typical use looks like:
//   ParticleSystem ps;
//   ps.emit({0, 1, 0}, 50);   // 50 particles burst at world (0,1,0)
//   each frame: ps.update(dt);
//                 BillboardRenderer.updateInstances(...) is fed ps.billboards
//
// Lua bindings expose engine.spawn_particles(x, y, z, count) so scripts can
// trigger effects.
export struct Particle
{
    vec3 position;
    vec3 velocity;
    vec4 color;        // RGBA, alpha drives fade
    float size;
    float life;        // seconds remaining
    float maxLife;     // initial life value
};

export class ParticleSystem
{
   public:
    static constexpr uint32_t maxParticles = 4096;

    void emit(const vec3& pos, uint32_t count, const vec4& color = { 1.0f, 0.7f, 0.2f, 1.0f },
              float life = 1.5f, float spread = 2.0f);
    void update(float dt);
    void clear();

    // For renderers that consume BillboardInstance directly. Fills outPositions,
    // outColors, outSizes (interleaved-free for cheap upload). Returns the
    // number of live particles written (clamped to outCapacity).
    uint32_t snapshot(vec3* outPositions, vec4* outColors, float* outSizes,
                      uint32_t outCapacity) const;

    uint32_t aliveCount() const { return static_cast<uint32_t>(particles.size()); }

   private:
    std::vector<Particle> particles;
    uint32_t seed = 1234567u;
    float rand01();
};
