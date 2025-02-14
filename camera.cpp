#include "camera.h"
#include "basetype.h"

PEngine::Camera::Camera(Vector3 *position, Quaternion *rotation, double fov)
{
    this->transform = new Transform(new Vector3(1, 1, 1), position, rotation);
    this->fov = fov;
    type = CAMERA;
}