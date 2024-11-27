#pragma once
#ifndef COLOR
#define COLOR
struct Color
{
    double r;
    double g;
    double b;
    double a;

    Color(double valr, double valg, double valb, double vala);

    Color operator+(const Color &c);
    Color operator+(const double val);

    Color &operator+=(const Color &c);

    Color operator-(const Color &c);

    Color &operator-=(const Color &c);

    Color operator*(const Color &c);
    Color operator*(const double val);

    Color &operator*=(const Color &c);
};
#endif