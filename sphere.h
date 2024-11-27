#pragma once
#ifndef SPHEREOBJECT
#define SPHEREOBJECT
#include "baseobject.h"
#include "basetype.h"
#include <utility>
struct Sphere : BaseObject{
    double radius;
    Sphere(Vector3* position, Vector3* rotation, Material* material, double radius);
    std::pair<double, double> IntersectRaySphere(Vector3* rayOrigin, Vector3* rayDirection, double dotDD);
};
#endif