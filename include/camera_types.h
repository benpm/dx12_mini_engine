#pragma once

#include "math_types.h"

#include <cmath>

class Camera
{
   public:
    float fov = 0.959931f;  // 55 degrees in radians
    float aspectRatio = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    virtual mat4 proj() const { return perspective(fov, aspectRatio, nearPlane, farPlane); }
    virtual mat4 view() const = 0;
    virtual ~Camera() = default;
};

class OrbitCamera : public Camera
{
   public:
    float yaw = -0.5f;
    float pitch = 0.5f;
    float radius = 30.0f;

    mat4 view() const override
    {
        vec3 eye(
            radius * std::cos(pitch) * std::cos(yaw), radius * std::sin(pitch),
            radius * std::cos(pitch) * std::sin(yaw)
        );
        return lookAt(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    }
};
