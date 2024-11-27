#include "vector3.h"
#include <cmath>
Vector3::Vector3(double valx, double valy, double valz)
{
    x = valx;
    y = valy;
    z = valz;
}

double Vector3::len()
{
    return sqrt(x * x + y * y + z * z);
}

Vector3 Vector3::normalize()
{
    double l = len();
    return Vector3(this->x / l, this->y / l, this->z / l);
}

double Vector3::dot(const Vector3* vec)
{
    return this->x * vec->x + this->y * vec->y + this->z * vec->z;
}

Vector3 Vector3::operator+(const Vector3 &vec)
{
    return Vector3(x + vec.x, y + vec.y, z + vec.z);
}

Vector3 Vector3::operator+(const double val)
{
    return Vector3(x + val, y + val, z + val);
}

Vector3 &Vector3::operator+=(const Vector3 &vec)
{
    this->x += vec.x;
    this->y += vec.y;
    this->z += vec.z;
    return *this;
}

Vector3 Vector3::operator-(const Vector3 &vec)
{
    return Vector3(x - vec.x, y - vec.y, z - vec.z);
}

Vector3 &Vector3::operator-=(const Vector3 &vec)
{
    this->x -= vec.x;
    this->y -= vec.y;
    this->z -= vec.z;
    return *this;
}

Vector3 Vector3::operator*(const Vector3 &vec)
{
    return Vector3(x * vec.x, y * vec.y, z * vec.z);
}

Vector3 Vector3::operator*(const double val)
{
    return Vector3(x * val, y * val, z * val);
}

Vector3 &Vector3::operator*=(const Vector3 &vec)
{
    this->x *= vec.x;
    this->y *= vec.y;
    this->z *= vec.z;
    return *this;
}
