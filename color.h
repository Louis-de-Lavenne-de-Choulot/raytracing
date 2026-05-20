#pragma once
#ifndef COLOR
#define COLOR
namespace HonHengine
{
    struct Color
    {
        double r;
        double g;
        double b;
        double a;
        Color() = default;
        Color(double valr, double valg, double valb, double vala = 1);

        Color operator+(const Color &c);
        Color operator+(const double val);

        Color &operator+=(const Color &c);

        Color operator-(const Color &c);

        Color &operator-=(const Color &c);

        Color operator*(const Color &c);
        Color operator*(const double val);

        Color &operator*=(const Color &c);

        // ── Lerp ─────────────────────────────────────────────────────────────
 // Linear interpolation between two colours.
 // t = 0.0 → returns a,  t = 1.0 → returns b,  t = 0.5 → midpoint.
 // Used by the near-plane clipper to interpolate vertex colours at the
 // exact point where a triangle edge crosses the near plane.
        static Color lerp(const Color& a, const Color& b, double t);

    };
};
#endif