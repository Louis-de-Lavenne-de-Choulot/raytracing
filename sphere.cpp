
#include "sphere.h"
#include "baseobject.h"
#include "basetype.h"
#include <tuple>
#include <cfloat>
#include <cmath>
#include <iostream>

Sphere::Sphere(Vector3 *position, Vector3 *rotation, Material *material, double rad) : BaseObject(position, rotation, material)
{
    radius = rad;
    type = SPHERE;
}

std::tuple<double, double> Sphere::IntersectRaySphere(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD)
{
    Vector3* rayCentertoOrigin = new Vector3(*rayOrigin - *position);
    double a = dotDD;
    double b = 2 * (*rayCentertoOrigin).dot(rayDirection);
    double c = (*rayCentertoOrigin).dot(rayCentertoOrigin) - radius * radius;
    double discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
    {
        return std::tuple(DBL_MAX, DBL_MAX);
    }
    double t0 = (-b + sqrt(discriminant)) / (2 * a);
    double t1 = (-b - sqrt(discriminant)) / (2 * a);
    return std::tuple(t0, t1);
}