// NullBackend — fallback IPhysicsBackend used when the active backend fails
// to initialise (no Jolt + no PhysX, or both failed). Every call is a no-op
// so the engine still boots cleanly on hosts where physics isn't available.

#include "physics_backend.h"

namespace
{
    class NullBackend final : public IPhysicsBackend
    {
       public:
        bool isReady() const override { return true; }
        const char* name() const override { return "null"; }
        void step(float) override {}
        uint32_t bodyCount() const override { return 0; }
        BodyId createBoxBody(float, float, float, float, float, float, bool, float) override
        {
            return 0;
        }
        BodyId createSphereBody(float, float, float, float, bool, float) override { return 0; }
        BodyId createCapsuleBody(float, float, float, float, float, bool, float) override
        {
            return 0;
        }
        BodyId createConvexHullBody(
            const float*, uint32_t, uint32_t, float, float, float, bool, float, float
        ) override
        {
            return 0;
        }
        void destroyBody(BodyId) override {}
        void getBodyPosition(BodyId, float& px, float& py, float& pz) const override
        {
            px = py = pz = 0.0f;
        }
        void getBodyRotation(BodyId, float& qx, float& qy, float& qz, float& qw) const override
        {
            qx = qy = qz = 0.0f;
            qw = 1.0f;
        }
        void applyForce(BodyId, float, float, float) override {}
        void applyImpulse(BodyId, float, float, float) override {}
        void setBodyPosition(BodyId, float, float, float, bool) override {}
        void getLinearVelocity(BodyId, float& vx, float& vy, float& vz) const override
        {
            vx = vy = vz = 0.0f;
        }
        void setLinearVelocity(BodyId, float, float, float) override {}
        void getAngularVelocity(BodyId, float& wx, float& wy, float& wz) const override
        {
            wx = wy = wz = 0.0f;
        }
        void setAngularVelocity(BodyId, float, float, float) override {}
        bool raycast(float, float, float, float, float, float, float, float& hx, float& hy,
                     float& hz, float& hd) const override
        {
            hx = hy = hz = hd = 0.0f;
            return false;
        }
    };
}  // namespace

IPhysicsBackend* createNullBackend()
{
    return new NullBackend();
}
