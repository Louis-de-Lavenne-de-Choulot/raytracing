#pragma once
#include "vertice.h"
#include "triangle.h"
#include "material.h"

#include <vector>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace PEngine
{
    struct VTeapot
    {
        std::vector<Vertice>  vertices;
        std::vector<Triangle> triangles;

        struct Profile
        {
            float y;
            float r;
        };

        VTeapot(
            Material* mat,
            int radialSegments = 256,   // HIGH RES
            int heightSegments = 128    // HIGH RES
        )
        {
            // Reserve to avoid reallocations at high poly counts
            vertices.reserve(200000);
            triangles.reserve(400000);

            // -------------------------------
            // BODY PROFILE (radius over Y)
            // -------------------------------
            std::vector<Profile> profile =
            {
                { -1.00f, 0.00f },
                { -0.95f, 0.35f },
                { -0.60f, 0.80f },
                { -0.20f, 1.00f },
                {  0.30f, 0.85f },
                {  0.65f, 0.55f },
                {  0.80f, 0.30f },
                {  0.95f, 0.10f },
                {  1.05f, 0.00f }
            };

            // -------------------------------
            // LATHE BODY (SMOOTH)
            // -------------------------------
            for (int y = 0; y < heightSegments; y++)
            {
                float t0 = float(y) / heightSegments;
                float t1 = float(y + 1) / heightSegments;

                Profile p0 = sampleProfile(profile, t0);
                Profile p1 = sampleProfile(profile, t1);

                for (int i = 0; i < radialSegments; i++)
                {
                    float a0 = (float)i / radialSegments * 2.0f * std::numbers::pi;
                    float a1 = (float)(i + 1) / radialSegments * 2.0f * std::numbers::pi;

                    int base = (int)vertices.size();

                    vertices.emplace_back(Vector3(
                        std::cos(a0) * p0.r, p0.y, std::sin(a0) * p0.r));
                    vertices.emplace_back(Vector3(
                        std::cos(a1) * p0.r, p0.y, std::sin(a1) * p0.r));
                    vertices.emplace_back(Vector3(
                        std::cos(a1) * p1.r, p1.y, std::sin(a1) * p1.r));
                    vertices.emplace_back(Vector3(
                        std::cos(a0) * p1.r, p1.y, std::sin(a0) * p1.r));

                    triangles.emplace_back(base, base + 1, base + 2, mat);
                    triangles.emplace_back(base, base + 2, base + 3, mat);
                }
            }

            // -------------------------------
            // SPOUT (SMOOTH TUBE)
            // -------------------------------
            generateTube(
                Vector3(1.0f, -0.1f, 0.0f),
                Vector3(1.6f, 0.5f, 0.0f),
                0.12f,
                64,   // tubeSegments
                64,   // tubeSides
                mat
            );

            // -------------------------------
            // HANDLE (SMOOTH TORUS SEGMENT)
            // -------------------------------
            generateHandle(
                Vector3(-1.0f, 0.1f, 0.0f),
                0.7f,
                0.12f,
                64,   // arc segments
                64,   // ring sides
                mat
            );
        }

        // ======================================================
        // Helpers
        // ======================================================

        Profile sampleProfile(const std::vector<Profile>& profile, float t)
        {
            t = std::clamp(t, 0.0f, 0.9999f);
            float fIndex = t * (profile.size() - 1);
            int i0 = int(fIndex);
            int i1 = i0 + 1;
            float lt = fIndex - i0;

            return {
                std::lerp(profile[i0].y, profile[i1].y, lt),
                std::lerp(profile[i0].r, profile[i1].r, lt)
            };
        }

        // -------------------------------
        // Straight Tube
        // -------------------------------
        void generateTube(
            const Vector3& start,
            const Vector3& end,
            float radius,
            int segments,
            int sides,
            Material* mat
        )
        {
            Vector3 dir = end - start;

            for (int i = 0; i < segments; i++)
            {
                float t0 = float(i) / segments;
                float t1 = float(i + 1) / segments;

                Vector3 c0 = start + dir * t0;
                Vector3 c1 = start + dir * t1;

                for (int a = 0; a < sides; a++)
                {
                    float ang0 = a * 2.f * std::numbers::pi / sides;
                    float ang1 = (a + 1) * 2.f * std::numbers::pi / sides;

                    int base = (int)vertices.size();

                    vertices.emplace_back(c0 + Vector3(
                        std::cos(ang0) * radius,
                        std::sin(ang0) * radius,
                        0));

                    vertices.emplace_back(c0 + Vector3(
                        std::cos(ang1) * radius,
                        std::sin(ang1) * radius,
                        0));

                    vertices.emplace_back(c1 + Vector3(
                        std::cos(ang1) * radius,
                        std::sin(ang1) * radius,
                        0));

                    vertices.emplace_back(c1 + Vector3(
                        std::cos(ang0) * radius,
                        std::sin(ang0) * radius,
                        0));

                    triangles.emplace_back(base, base + 1, base + 2, mat);
                    triangles.emplace_back(base, base + 2, base + 3, mat);
                }
            }
        }

        // -------------------------------
        // Handle (Torus Section)
        // -------------------------------
        void generateHandle(
            const Vector3& center,
            float majorR,
            float minorR,
            int arcSegments,
            int sides,
            Material* mat
        )
        {
            for (int i = 0; i < arcSegments; i++)
            {
                float a0 = i * std::numbers::pi / arcSegments;
                float a1 = (i + 1) * std::numbers::pi / arcSegments;

                for (int j = 0; j < sides; j++)
                {
                    float b0 = j * 2.f * std::numbers::pi / sides;
                    float b1 = (j + 1) * 2.f * std::numbers::pi / sides;

                    Vector3 p00 = torusPoint(a0, b0, majorR, minorR);
                    Vector3 p10 = torusPoint(a1, b0, majorR, minorR);
                    Vector3 p11 = torusPoint(a1, b1, majorR, minorR);
                    Vector3 p01 = torusPoint(a0, b1, majorR, minorR);

                    int base = (int)vertices.size();

                    vertices.emplace_back(center + p00);
                    vertices.emplace_back(center + p10);
                    vertices.emplace_back(center + p11);
                    vertices.emplace_back(center + p01);

                    triangles.emplace_back(base, base + 1, base + 2, mat);
                    triangles.emplace_back(base, base + 2, base + 3, mat);
                }
            }
        }

        Vector3 torusPoint(float a, float b, float R, float r)
        {
            return Vector3(
                (R + r * std::cos(b)) * std::cos(a),
                r * std::sin(b),
                (R + r * std::cos(b)) * std::sin(a)
            );
        }
    };
}
