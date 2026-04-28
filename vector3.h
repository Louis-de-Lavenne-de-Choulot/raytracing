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

				Vector3() : x(0), y(0), z(0) {}
                Vector3(double valx, double valy, double valz);

                double len();

                Vector3 normalize();

                double angleBetween(Vector3 *vec);

                double magnitude();

                double dot(const Vector3 *vec);

                Vector3 cross(const Vector3& other) const {
                        return Vector3(
                             y * other.z - z * other.y,
                            (x * other.z - z * other.x)*-1,
                            x * other.y - y * other.x
                        );
                    }

                Vector3 operator+(const Vector3 *vec) const;

                Vector3 operator+(const Vector3 &vec) const;

                Vector3 operator+(const double val) const;

                Vector3 &operator+=(const Vector3 &vec);

                Vector3 operator-(const Vector3 &vec) const;

                Vector3 operator-(const double val) const;

                Vector3 &operator-=(const Vector3 &vec);

                Vector3 operator*(const Vector3 &vec) const;

                Vector3 operator*(const double val) const;

                Vector3 &operator*=(const Vector3 &vec);
        };
};

#endif