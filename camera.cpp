#include "camera.h"
#include "basetype.h"

Camera::Camera(Vector3* position, Vector3* rotation){
    this->position = position;
    this->rotation = rotation;
    type = CAMERA;
}