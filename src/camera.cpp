module;

#include <cmath>

module camera;

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
