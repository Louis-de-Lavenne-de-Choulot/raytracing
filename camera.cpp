#include "camera.h"
#include "basetype.h"

Camera::Camera(Vector3* position, Quaternion* rotation){
    this->transform = new Transform(new Vector3(1, 1, 1), position, rotation);
    type = CAMERA;
}