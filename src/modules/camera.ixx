export module camera;

export import common;

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
