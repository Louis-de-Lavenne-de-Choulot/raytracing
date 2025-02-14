#pragma once
#ifndef OBJECTTYPE
#define OBJECTTYPE
namespace PEngine
{
    enum ObjectType
    {
        NONE = 0,
        AMBIENT_LIGHT,
        DIRECTIONAL_LIGHT,
        POINT_LIGHT,
        CAMERA,
        SPHERE,
        RECTANGLE,
        PLANE,
        TRIANGLE,
        CUBE,
        CYLINDER,
        CONE,
    };
};
#endif