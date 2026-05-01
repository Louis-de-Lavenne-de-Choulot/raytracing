#include "color.h"
#include <algorithm>
namespace HonHengine
{
    Color::Color(double valr, double valg, double valb, double vala)
    {
        r = valr;
        g = valg;
        b = valb;
        a = vala;
    }

    Color Color::operator+(const Color& c)
    {
        return Color(std::min(r + c.r, 255.), std::min(g + c.g, 255.), std::min(b + c.b, 255.), std::min(a + c.a, 255.));
    }

    Color Color::operator+(const double val)
    {
        return Color(std::min(r + val, 255.), std::min(g + val, 255.), std::min(b + val, 255.), std::min(a + val, 255.));
    }

    Color& Color::operator+=(const Color& c)
    {
        this->r += c.r;
        this->g += c.g;
        this->b += c.b;
        this->a += c.a;
        return *this;
    }

    Color Color::operator-(const Color& c)
    {
        return Color(r - c.r, g - c.g, b - c.b, a - c.a);
    }

    Color& Color::operator-=(const Color& c)
    {
        this->r -= c.r;
        this->g -= c.g;
        this->b -= c.b;
        this->a -= c.a;
        return *this;
    }

    Color Color::operator*(const Color& c)
    {
        return Color(r * c.r, g * c.g, b * c.b, a);
    }

    Color Color::operator*(const double val)
    {
        return { r * val, g * val, b * val, a };
    }

    Color& Color::operator*=(const Color& c)
    {
        this->r *= c.r;
        this->g *= c.g;
        this->b *= c.b;
        this->a *= c.a;
        return *this;
    }

    // ── Color::lerp ───────────────────────────────────────────────────────────
    //
    // Computes the linear interpolation between colour `a` and colour `b`
    // by scalar `t` in [0, 1]:
    //
    //   result = a + t * (b - a)
    //          = (1 - t) * a  +  t * b        ← equivalent, numerically stable form
    //
    // This is applied independently to each channel (r, g, b, a).
    //
    // Why this formula?
    //   At t=0:  result = a + 0*(b-a) = a
    //   At t=1:  result = a + 1*(b-a) = b
    //   At t=0.5 (midpoint): result = 0.5*a + 0.5*b  (average)
    //
    // In the clipper, `t` is the parametric distance along the triangle edge
    // from the invalid vertex (behind the near plane) to the valid vertex
    // (in front of it).  So t=0 is the invalid end, t=1 is the valid end,
    // and the clipped point sits exactly at t - giving the correct blended
    // colour for Gouraud interpolation to continue across the new vertex.
    //
    // No clamping is needed here: since t is always in [0,1] and both input
    // colours have channels in [0,255], the result stays within [0,255].
    Color Color::lerp(const Color& a, const Color& b, double t)
    {
        return Color(
            a.r + t * (b.r - a.r),
            a.g + t * (b.g - a.g),
            a.b + t * (b.b - a.b),
            a.a + t * (b.a - a.a)
        );
    }
}