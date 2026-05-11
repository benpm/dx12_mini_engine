#pragma once

#include "math_types.h"

#include <cstdint>
#include <string>
#include <vector>

// Skeleton + AnimationClip — engine-side mirrors of glTF skin/animation data.
//
// This is the data-side foundation for skeletal animation. GPU skinning (a
// skinned vertex-shader variant + joint matrix structured buffer) is staged
// for a follow-up; right now the parsed data is what the Animator system
// advances each frame, ready to be consumed by that future render path.

struct SkeletonJoint
{
    int parent = -1;          // Index into Skeleton::joints (-1 = root)
    std::string name;
    mat4 inverseBindMatrix{}; // glTF inverseBindMatrices entry for this joint
    // Local transform components — the animation system writes into these each
    // frame, then the runtime recomposes joint local matrices = T * R * S.
    vec3 localTranslation{ 0.0f, 0.0f, 0.0f };
    vec4 localRotation{ 0.0f, 0.0f, 0.0f, 1.0f };  // quat (x, y, z, w)
    vec3 localScale{ 1.0f, 1.0f, 1.0f };
};

struct Skeleton
{
    std::string name;
    std::vector<SkeletonJoint> joints;
};

// One channel = one (joint, path) keyframe stream. path is "translation",
// "rotation" (quat), or "scale". Values stride matches the path's component
// count: 3 for T/S, 4 for R.
struct AnimationChannel
{
    enum class Path : uint8_t { Translation, Rotation, Scale };
    int jointIndex = -1;
    Path path = Path::Translation;
    std::vector<float> timestamps;  // seconds, sorted ascending
    std::vector<float> values;      // stride 3 or 4 (4 for Rotation)
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;          // max of all channel last-timestamps
    std::vector<AnimationChannel> channels;
};
