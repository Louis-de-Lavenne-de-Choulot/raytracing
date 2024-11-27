#include "color.h"
#include<algorithm>
using namespace std;
Color::Color(double valr, double valg, double valb, double vala)
{
    r = valr;
    g = valg;
    b = valb;
    a = vala;
}

Color Color::operator+(const Color &c)
{
    return Color(std::min(r + c.r, 255.), std::min(g + c.g, 255.), std::min(b + c.b, 255.), std::min(a + c.a, 255.));
}

Color Color::operator+(const double val)
{
    return Color(std::min(r + val, 255.), std::min(g + val, 255.), std::min(b + val, 255.), std::min(a + val, 255.));
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