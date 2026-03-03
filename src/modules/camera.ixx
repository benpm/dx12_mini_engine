module;

#include <DirectXMath.h>

export module camera;

export import common;

export class Camera
{
   public:
    float fov = 45.0_deg;
    float aspectRatio = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    virtual XMMATRIX proj() const;
    virtual XMMATRIX view() const = 0;
};

export class OrbitCamera : public Camera
{
   public:
    float yaw = 0.0_deg;
    float pitch = 0.0_deg;
    float radius = 5.0f;

    XMMATRIX view() const override;
};
