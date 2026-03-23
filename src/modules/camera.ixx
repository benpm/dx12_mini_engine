module;

#include <cmath>

export module camera;

import common;
export import math;

export class Camera
{
   public:
    float fov = 55.0_deg;
    float aspectRatio = 1.0f;
    // Near clipping plane
    float nearPlane = 0.1f;
    // Far clipping plane
    float farPlane = 100.0f;

    virtual mat4 proj() const;
    virtual mat4 view() const = 0;
};

export class OrbitCamera : public Camera
{
   public:
    float yaw = -0.5f;
    float pitch = 0.5f;
    float radius = 30.0f;

    mat4 view() const override;
};

module :private;

mat4 Camera::proj() const
{
    return perspective(this->fov, this->aspectRatio, this->nearPlane, this->farPlane);
}

mat4 OrbitCamera::view() const
{
    vec3 eye(
        this->radius * std::cos(this->pitch) * std::cos(this->yaw),
        this->radius * std::sin(this->pitch),
        this->radius * std::cos(this->pitch) * std::sin(this->yaw)
    );
    return lookAt(eye, vec3(0, 0, 0), vec3(0, 1, 0));
}
