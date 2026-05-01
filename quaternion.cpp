#include "quaternion.h"
#include <cmath>
#include "eulerangle.h"
#include <numbers>

namespace HonHengine
{
    EulerAngles Quaternion::ToEulerAngle()
    {
        EulerAngles angles;

        // roll (x-axis rotation)
        double sinr_cosp = 2 * (this->w * this->x + this->y * this->z);
        double cosr_cosp = 1 - 2 * (this->x * this->x + this->y * this->y);
        angles.roll = std::atan2(sinr_cosp, cosr_cosp);

        // pitch (y-axis rotation)
        double sinp = std::sqrt(1 + 2 * (this->w * this->y - this->x * this->z));
        double cosp = std::sqrt(1 - 2 * (this->w * this->y - this->x * this->z));
        angles.pitch = 2 * std::atan2(sinp, cosp) - std::numbers::pi / 2;

        // yaw (z-axis rotation)
        double siny_cosp = 2 * (this->w * this->z + this->x * this->y);
        double cosy_cosp = 1 - 2 * (this->y * this->y + this->z * this->z);
        angles.yaw = std::atan2(siny_cosp, cosy_cosp);

        // convert to degree
        angles.roll *= 180 / std::numbers::pi;
        angles.pitch *= 180 / std::numbers::pi;
        angles.yaw *= 180 / std::numbers::pi;

        return angles;
    }
};