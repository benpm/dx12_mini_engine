// PhysXBackend — IPhysicsBackend implementation backed by NVIDIA PhysX.
//
// This file is the plug-in slot: it always compiles, but the PhysX-using body
// is gated behind ENGINE_PHYSICS_BACKEND_PHYSX (set by CMake when the user
// supplies PhysX). Without the macro the factory returns nullptr and the
// engine falls back to Jolt at runtime.
//
// To enable: configure with -DENGINE_PHYSICS_BACKEND=PhysX and a PhysX SDK
// (PHYSX_ROOT pointing at a built SDK). The minimum required interfaces are
// PxPhysics, PxScene, PxRigidStatic, PxRigidDynamic, PxBoxGeometry,
// PxSphereGeometry, and PxRaycastBuffer. A reference implementation is
// intentionally not pre-included — picking the PhysX licensing/version is a
// project-level decision left to whoever swaps the backend.

#include "physics_backend.h"

#if defined(ENGINE_PHYSICS_BACKEND_PHYSX)
    // The user's PhysX SDK headers + a real PxPhysics-backed implementation
    // go here. Stays out of the upstream tree by default.
    #error "ENGINE_PHYSICS_BACKEND_PHYSX is defined but PhysX backend body has not been provided"
#endif

IPhysicsBackend* createPhysXBackend()
{
    // PhysX backend not compiled in — caller falls back to Jolt or null.
    return nullptr;
}
