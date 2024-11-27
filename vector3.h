#pragma once
#ifndef VECTOR3
#define VECTOR3
struct Vector3 {
        double x;
        double y;
        double z;

        Vector3(double valx, double valy, double valz);

        double len();

        Vector3 normalize();

        double dot(const Vector3* vec);

        Vector3 operator+(const Vector3& vec);

        Vector3 operator+(const double val);

        Vector3& operator+=(const Vector3& vec);

        Vector3 operator-(const Vector3& vec);

        Vector3& operator-=(const Vector3& vec);

        Vector3 operator*(const Vector3& vec);

        Vector3 operator*(const double val);

        Vector3& operator*=(const Vector3& vec);
};

#endif