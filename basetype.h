#pragma once
#ifndef OBJECTTYPE
#define OBJECTTYPE
namespace HonHengine
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
        TEAPOT,
        OBJ_MESH,
        GLTF_MESH
    };
};
#endif