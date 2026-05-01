#include "vector3.h"
#include "quaternion.h"
#include <cmath>
#include <numbers>

namespace HonHengine
{
    Vector3::Vector3(double valx, double valy, double valz)
    {
        x = valx;
        y = valy;
        z = valz;
    }

    double Vector3::len() const
    {
        return sqrt(x * x + y * y + z * z);
    }

    Vector3 Vector3::normalize() const
    {
        double l = len();
        return Vector3(this->x / l, this->y / l, this->z / l);
    }

    double Vector3::angleBetween(Vector3 *vec)
    {
        return acos(this->dot(vec) / (this->magnitude() * vec->magnitude())) * 180 / std::numbers::pi;
    }

    double Vector3::magnitude() const
    {
        return sqrt((*this).dot(this));
    }

    double Vector3::dot(const Vector3 *vec) const
    {
        return this->x * vec->x + this->y * vec->y + this->z * vec->z;
    }

    Vector3 Vector3::operator+(const Vector3 *vec) const
    {
        return Vector3(x + vec->x, y + vec->y, z + vec->z);
    }
    Vector3 Vector3::operator+(const Vector3 &vec) const
    {
        return Vector3(x + vec.x, y + vec.y, z + vec.z);
    }

    Vector3 Vector3::operator+(const double val) const
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

    Vector3 Vector3::operator-(const Vector3 &vec) const
    {
        return Vector3(x - vec.x, y - vec.y, z - vec.z);
    }

    Vector3 Vector3::operator-(const double val) const
    {
        return Vector3(x - val, y - val, z - val);
    }

    Vector3 &Vector3::operator-=(const Vector3 &vec)
    {
        this->x -= vec.x;
        this->y -= vec.y;
        this->z -= vec.z;
        return *this;
    }

    Vector3 Vector3::operator*(const Vector3 &vec) const
    {
        return Vector3(x * vec.x, y * vec.y, z * vec.z);
    }

    Vector3 Vector3::operator*(const double val) const
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
};