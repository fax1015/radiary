#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace radiary {

constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

inline Vec2 operator+(const Vec2& left, const Vec2& right) {
    return {left.x + right.x, left.y + right.y};
}

inline Vec2 operator-(const Vec2& left, const Vec2& right) {
    return {left.x - right.x, left.y - right.y};
}

inline Vec2 operator*(const Vec2& value, const double scalar) {
    return {value.x * scalar, value.y * scalar};
}

inline Vec3 operator+(const Vec3& left, const Vec3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

inline Vec3 operator-(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline Vec3 operator*(const Vec3& value, const double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

inline double Dot(const Vec2& left, const Vec2& right) {
    return left.x * right.x + left.y * right.y;
}

inline double Dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline double Length(const Vec2& value) {
    return std::sqrt(Dot(value, value));
}

inline double Clamp(const double value, const double minimum, const double maximum) {
    return std::clamp(value, minimum, maximum);
}

inline double Lerp(const double start, const double end, const double alpha) {
    return start + (end - start) * alpha;
}

inline Color Lerp(const Color& start, const Color& end, const double alpha) {
    return {
        static_cast<std::uint8_t>(Clamp(std::round(Lerp(start.r, end.r, alpha)), 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(std::round(Lerp(start.g, end.g, alpha)), 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(std::round(Lerp(start.b, end.b, alpha)), 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(std::round(Lerp(start.a, end.a, alpha)), 0.0, 255.0))
    };
}

inline double DegreesToRadians(const double value) {
    return value * kPi / 180.0;
}

inline double SafeAtan2(const double y, const double x) {
    return (x == 0.0 && y == 0.0) ? 0.0 : std::atan2(y, x);
}

inline double Smoothstep(const double edge0, const double edge1, const double x) {
    const double t = Clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double Length3(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline Vec3 Normalize(const Vec3& value) {
    const double length = Length3(value);
    return length > 1.0e-9 ? value * (1.0 / length) : Vec3{};
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double SafeRadiusSquared(const Vec2& value) {
    return std::max(1.0e-9, value.x * value.x + value.y * value.y);
}

inline std::uint32_t ToBgra(const Color& color) {
    return static_cast<std::uint32_t>(color.b)
        | (static_cast<std::uint32_t>(color.g) << 8U)
        | (static_cast<std::uint32_t>(color.r) << 16U)
        | (static_cast<std::uint32_t>(color.a) << 24U);
}

}  // namespace radiary
