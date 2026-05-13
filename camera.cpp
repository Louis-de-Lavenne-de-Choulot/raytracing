#include "camera.h"
#include "basetype.h"

HonHengine::Camera::Camera(Vector3 position, Quaternion rotation, double fov)
{
    this->transform = Transform(Vector3(1, 1, 1), position, rotation);
    this->fov = fov;
    type = CAMERA;
}
