#include "color.h"

Color::Color(double valr, double valg, double valb, double vala)
{
    r = valr;
    g = valg;
    b = valb;
    a = vala;
}

Color Color::operator+(const Color &c)
{
    return Color(r + c.r, g + c.g, b + c.b, a + c.a);
}

Color Color::operator+(const double val)
{
    return Color(r + val, g + val, b + val, a + val);
}

Color &Color::operator+=(const Color &c)
{
    this->r += c.r;
    this->g += c.g;
    this->b += c.b;
    this->a += c.a;
    return *this;
}

Color Color::operator-(const Color &c)
{
    return Color(r - c.r, g - c.g, b - c.b, a - c.a);
}

Color &Color::operator-=(const Color &c)
{
    this->r -= c.r;
    this->g -= c.g;
    this->b -= c.b;
    this->a -= c.a;
    return *this;
}

Color Color::operator*(const Color &c)
{
    return Color(r * c.r, g * c.g, b * c.b, a);
}

Color Color::operator*(const double val)
{
    return Color(r * val, g * val, b * val, a);
}

Color &Color::operator*=(const Color &c)
{
    this->r *= c.r;
    this->g *= c.g;
    this->b *= c.b;
    this->a *= c.a;
    return *this;
}