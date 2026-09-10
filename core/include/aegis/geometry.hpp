#pragma once
#include <cmath>
#include <vector>
#include "rules.hpp"
namespace aegis
{
struct Vec
{
    double x = 0, y = 0;
    Vec operator+(Vec b) const
    {
        return {x + b.x, y + b.y};
    }
    Vec operator-(Vec b) const
    {
        return {x - b.x, y - b.y};
    }
    Vec operator*(double s) const
    {
        return {x * s, y * s};
    }
};
inline double dot(Vec a, Vec b)
{
    return a.x * b.x + a.y * b.y;
}
inline double length(Vec a)
{
    return std::sqrt(dot(a, a));
}
inline Vec normalized(Vec a)
{
    double l = length(a);
    return l > 1e-9 ? a * (1 / l) : Vec{};
}
struct Circle
{
    Vec position;
    double radius;
};
inline bool segmentHits(Vec a, Vec b, Circle c, double padding = 0)
{
    const Vec ab = b - a;
    const double t = dot(ab, ab) > 1e-12 ? bounded(dot(c.position - a, ab) / dot(ab, ab), 0, 1) : 0;
    return length(a + ab * t - c.position) < c.radius + padding;
}
inline bool lineOfSight(Vec a, Vec b, const std::vector<Circle>& cover)
{
    for (const auto& c : cover)
        if (segmentHits(a, b, c))
            return false;
    return true;
}
inline bool navigable(Vec p, const std::vector<Circle>& cover)
{
    if (std::abs(p.x) > 19.5 || std::abs(p.y) > 14.5)
        return false;
    for (auto c : cover)
        if (length(p - c.position) < c.radius + 0.4)
            return false;
    return true;
}
// Portable geometry sampler, deliberately NOT called Unreal EQS.
inline Vec coverPoint(Vec self, Vec threat, const std::vector<Circle>& cover, bool retreat)
{
    Vec best = self;
    double bestScore = -1e9;
    for (const auto& c : cover)
        for (int i = 0; i < 12; ++i)
        {
            const double angle = i * 6.283185307179586 / 12;
            Vec p = c.position + Vec{std::cos(angle), std::sin(angle)} * (c.radius + 0.7);
            if (!navigable(p, cover))
                continue;
            double score = (!lineOfSight(p, threat, cover) ? 8.0 : 0.0) - length(p - self) * 0.45;
            score += retreat ? length(p - threat) * 0.15 : -std::abs(length(p - threat) - 8) * 0.3;
            if (score > bestScore)
            {
                bestScore = score;
                best = p;
            }
        }
    return best;
}
} // namespace aegis
