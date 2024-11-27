#pragma once
#ifndef OBJECTTYPE
#define OBJECTTYPE
enum ObjectType {
    NONE = 0,
    AMBIENT_LIGHT,
    DIRECTIONAL_LIGHT,
    POINT_LIGHT,
    CAMERA,
    SPHERE,
    PLANE,
    TRIANGLE,
    CUBE,
    CYLINDER,
    CONE,
};
#endif