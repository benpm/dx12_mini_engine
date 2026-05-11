module;

#include <algorithm>
#include <cstdint>

module particles;

#include "audio_capi.h"

extern "C" void engine_particles_emit(
    void* p, float x, float y, float z, int count, unsigned int rgba, float life
)
{
    if (!p || count <= 0) {
        return;
    }
    // rgba packed as 0xAABBGGRR (matches ImGui's IM_COL32 layout for symmetry).
    float r = ((rgba >> 0) & 0xFF) / 255.0f;
    float g = ((rgba >> 8) & 0xFF) / 255.0f;
    float b = ((rgba >> 16) & 0xFF) / 255.0f;
    float a = ((rgba >> 24) & 0xFF) / 255.0f;
    static_cast<ParticleSystem*>(p)->emit({ x, y, z }, static_cast<uint32_t>(count),
                                          { r, g, b, a == 0.0f ? 1.0f : a },
                                          life <= 0.0f ? 1.5f : life);
}

extern "C" void engine_particles_clear(void* p)
{
    if (auto* sys = static_cast<ParticleSystem*>(p)) {
        sys->clear();
    }
}

extern "C" int engine_particles_alive_count(void* p)
{
    if (!p) {
        return 0;
    }
    return static_cast<int>(static_cast<ParticleSystem*>(p)->aliveCount());
}

float ParticleSystem::rand01()
{
    // Tiny xorshift32; deterministic-enough for VFX, fast, no allocation.
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (seed & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void ParticleSystem::emit(
    const vec3& pos, uint32_t count, const vec4& color, float life, float spread
)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (particles.size() >= maxParticles) {
            break;
        }
        Particle p;
        p.position = pos;
        float vx = (rand01() - 0.5f) * 2.0f * spread;
        float vy = rand01() * spread + 0.5f;
        float vz = (rand01() - 0.5f) * 2.0f * spread;
        p.velocity = { vx, vy, vz };
        p.color = color;
        p.size = 0.05f + rand01() * 0.10f;
        p.life = life * (0.5f + rand01());
        p.maxLife = p.life;
        particles.push_back(p);
    }
}

void ParticleSystem::update(float dt)
{
    // Forward Euler. Apply gravity, decay life. Compact in place — particles
    // that died are swapped to the back and popped.
    const vec3 gravity{ 0.0f, -3.0f, 0.0f };  // vec3 isn't literal so can't be constexpr
    for (size_t i = 0; i < particles.size();) {
        auto& p = particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        p.velocity.x += gravity.x * dt;
        p.velocity.y += gravity.y * dt;
        p.velocity.z += gravity.z * dt;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
        // Fade alpha with remaining life.
        p.color.w = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        ++i;
    }
}

void ParticleSystem::clear()
{
    particles.clear();
}

uint32_t ParticleSystem::snapshot(
    vec3* outPositions, vec4* outColors, float* outSizes, uint32_t outCapacity
) const
{
    uint32_t n = std::min<uint32_t>(outCapacity, static_cast<uint32_t>(particles.size()));
    for (uint32_t i = 0; i < n; ++i) {
        outPositions[i] = particles[i].position;
        outColors[i] = particles[i].color;
        outSizes[i] = particles[i].size;
    }
    return n;
}
