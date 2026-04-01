#pragma once

// Profiling wrapper - include ONLY from .cpp files, never from .ixx module files.
// CPU zones use Tracy's ZoneScoped; GPU zones use TracyD3D12.

#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
    #include <tracy/TracyD3D12.hpp>

    #define PROFILE_FRAME_MARK FrameMark
    #define PROFILE_ZONE() ZoneScoped
    #define PROFILE_ZONE_NAMED(name) ZoneScopedN(name)

    #define PROFILE_CONCAT_INNER(a, b) a##b
    #define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)

    // Null-safe GPU macros: pass active=(ctx!=nullptr) so D3D12ZoneScope ctor
    // returns before touching any internal pointers if context init failed.
    // Tracy v0.13.1 defines TRACY_CALLSTACK as 0 by default, but D3D12 named-zone
    // macros still route through callstack overloads when that macro exists.
    // Use explicit S variant with depth=1 to avoid a depth=0 runtime assert.
    #define PROFILE_GPU_ZONE(ctx, cl, name)                                                       \
        TracyD3D12NamedZoneS(                                                                     \
            (ctx), PROFILE_CONCAT(tracy_d3d12_zone_, __LINE__), (cl), (name), 1, (ctx) != nullptr \
        )
    #define PROFILE_GPU_NEW_FRAME(ctx)  \
        do {                            \
            if (ctx) {                  \
                TracyD3D12NewFrame(ctx) \
            }                           \
        } while (0)
    #define PROFILE_GPU_COLLECT(ctx)   \
        do {                           \
            if (ctx) {                 \
                TracyD3D12Collect(ctx) \
            }                          \
        } while (0)
#else
    #define PROFILE_FRAME_MARK
    #define PROFILE_ZONE()
    #define PROFILE_ZONE_NAMED(name)
    #define PROFILE_GPU_ZONE(ctx, cl, name)
    #define PROFILE_GPU_NEW_FRAME(ctx)
    #define PROFILE_GPU_COLLECT(ctx)
#endif
