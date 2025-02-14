#pragma once
#ifndef VECTOR3
#define VECTOR3
namespace PEngine
{
        struct Vector3
        {
                double x;
                double y;
                double z;

                Vector3(double valx, double valy, double valz);

                double len();

                Vector3 normalize();

                double angleBetween(Vector3 *vec);

                double magnitude();

                double dot(const Vector3 *vec);

                Vector3 operator+(const Vector3 *vec);

                Vector3 operator+(const Vector3 &vec);

                Vector3 operator+(const double val);

                Vector3 &operator+=(const Vector3 &vec);

                Vector3 operator-(const Vector3 &vec);

                Vector3 operator-(const double val);

                Vector3 &operator-=(const Vector3 &vec);

                Vector3 operator*(const Vector3 &vec);

                Vector3 operator*(const double val);

                Vector3 &operator*=(const Vector3 &vec);
        };
};

#endif