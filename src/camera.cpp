module;

#include <DirectXMath.h>
#include <cmath>

module camera;

XMMATRIX Camera::proj() const
{
    return XMMatrixPerspectiveFovLH(this->fov, this->aspectRatio, this->nearPlane, this->farPlane);
}

XMMATRIX OrbitCamera::view() const
{
    return XMMatrixLookAtLH(
        XMVectorSet(
            this->radius * cos(this->pitch) * cos(this->yaw), this->radius * sin(this->pitch),
            this->radius * cos(this->pitch) * sin(this->yaw), 0.0f
        ),
        XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );
}
