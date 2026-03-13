#include "engine/flame/Variation.h"

#include <cmath>

namespace radiary {

namespace {

double SafeSignedDenominator(const double value) {
    if (std::abs(value) >= 1.0e-9) {
        return value;
    }
    return std::copysign(1.0e-9, value == 0.0 ? 1.0 : value);
}

}  // namespace

Vec2 ApplyVariation(const VariationType variation, const Vec2& point) {
    const double radiusSquared = SafeRadiusSquared(point);
    const double radius = std::sqrt(radiusSquared);
    const double angle = SafeAtan2(point.y, point.x);

    switch (variation) {
    case VariationType::Linear:
        return point;
    case VariationType::Sinusoidal:
        return {std::sin(point.x), std::sin(point.y)};
    case VariationType::Spherical:
        return {point.x / radiusSquared, point.y / radiusSquared};
    case VariationType::Swirl: {
        const double sine = std::sin(radiusSquared);
        const double cosine = std::cos(radiusSquared);
        return {
            point.x * sine - point.y * cosine,
            point.x * cosine + point.y * sine
        };
    }
    case VariationType::Horseshoe:
        return {
            ((point.x - point.y) * (point.x + point.y)) / radius,
            (2.0 * point.x * point.y) / radius
        };
    case VariationType::Polar:
        return {angle / kPi, radius - 1.0};
    case VariationType::Heart: {
        const double value = angle * radius;
        return {radius * std::sin(value), -radius * std::cos(value)};
    }
    case VariationType::Disc: {
        const double factor = angle / kPi;
        return {factor * std::sin(kPi * radius), factor * std::cos(kPi * radius)};
    }
    case VariationType::Spiral:
        return {
            (std::cos(angle) + std::sin(radius)) / radius,
            (std::sin(angle) - std::cos(radius)) / radius
        };
    case VariationType::Julia: {
        const double omega = (std::sin(angle * 12.9898) > 0.0) ? 0.0 : kPi;
        const double root = std::sqrt(radius);
        const double theta = angle * 0.5 + omega;
        return {root * std::cos(theta), root * std::sin(theta)};
    }
    case VariationType::Bubble: {
        const double factor = 4.0 / (radiusSquared + 4.0);
        return {point.x * factor, point.y * factor};
    }
    case VariationType::Eyefish: {
        const double factor = 2.0 / (radius + 1.0);
        return {point.x * factor, point.y * factor};
    }
    case VariationType::Cylinder:
        return {std::sin(point.x), point.y};
    case VariationType::Blur: {
        const double a = angle * kPi;
        const double r = radius * (std::sin(radiusSquared * 3.7) * 0.5 + 0.5);
        return {r * std::cos(a), r * std::sin(a)};
    }
    case VariationType::Ngon: {
        const int power = 3;
        const double sides = 5.0;
        const double corners = 0.5;
        const double circle = 1.0;
        const double t = angle - sides * std::floor(angle / sides);
        double k = (t > sides * 0.5) ? (sides - t) : t;
        k = (corners * (1.0 / std::cos(k) - 1.0) + circle) / std::max(0.001, std::pow(radius, static_cast<double>(power)));
        return {point.x * k, point.y * k};
    }
    case VariationType::Curl: {
        const double c1 = 0.5;
        const double c2 = 0.0;
        const double re = 1.0 + c1 * point.x + c2 * (point.x * point.x - point.y * point.y);
        const double im = c1 * point.y + 2.0 * c2 * point.x * point.y;
        const double r2 = 1.0 / std::max(1.0e-9, re * re + im * im);
        return {(point.x * re + point.y * im) * r2, (point.y * re - point.x * im) * r2};
    }
    case VariationType::Arch:
        return {std::sin(angle), std::sin(angle) * std::sin(angle) / std::max(0.001, std::cos(angle))};
    case VariationType::Tangent:
        return {std::sin(point.x) / std::max(0.001, std::cos(point.y)), std::tan(point.y)};
    case VariationType::Rays: {
        const double factor = std::tan(radius) / std::max(1.0e-9, radiusSquared);
        return {factor * std::cos(point.x), factor * std::sin(point.y)};
    }
    case VariationType::Cross: {
        const double s = std::abs(point.x * point.x - point.y * point.y);
        const double factor = std::sqrt(1.0 / std::max(1.0e-9, s * s));
        return {point.x * factor, point.y * factor};
    }
    case VariationType::Bent: {
        const double nx = (point.x < 0.0) ? point.x * 2.0 : point.x;
        const double ny = (point.y < 0.0) ? point.y * 0.5 : point.y;
        return {nx, ny};
    }
    case VariationType::Waves: {
        const double x = point.x + std::sin(point.y * 2.0);
        const double y = point.y + std::sin(point.x * 2.0);
        return {x, y};
    }
    case VariationType::Fan: {
        const double fan = kPi * 0.5;
        const double t = angle + fan * std::floor(angle / fan);
        const double r = radius * fan / t;
        return {r * std::cos(t), r * std::sin(t)};
    }
    case VariationType::Rings: {
        const double rings = 0.5;
        const double r = radius - rings * std::floor(radius / rings);
        const double factor = r / std::max(1.0e-9, radius);
        return {point.x * factor, point.y * factor};
    }
    case VariationType::Popcorn: {
        const double popcorn = 0.3;
        return {
            point.x + popcorn * std::sin(std::tan(3.0 * point.y)),
            point.y + popcorn * std::sin(std::tan(3.0 * point.x))
        };
    }
    case VariationType::Bipolar: {
        const double bipolar = 0.5;
        const double r = std::sqrt(point.x * point.x + point.y * point.y);
        const double t = std::atan2(point.y, point.x) + bipolar;
        return {r * std::cos(t), r * std::sin(t)};
    }
    case VariationType::Wedge: {
        const double wedge = 0.5;
        const double a = angle + wedge * std::floor(angle / wedge);
        return {radius * std::cos(a), radius * std::sin(a)};
    }
    case VariationType::Split: {
        const double split = 0.5;
        return {
            point.x + split * std::sin(point.y),
            point.y + split * std::sin(point.x)
        };
    }
    case VariationType::Fisheye: {
        const double r = 2.0 / (radius + 1.0);
        return {r * point.y, r * point.x};
    }
    case VariationType::Handkerchief: {
        const double theta = angle * radius;
        return {radius * std::sin(angle + theta), radius * std::cos(angle - theta)};
    }
    case VariationType::Ex: {
        const double d = std::sqrt(point.x * point.x + point.y * point.y);
        const double ex = d * d * d;
        return {
            ex * std::cos(angle * d),
            ex * std::sin(angle * d)
        };
    }
    case VariationType::Blade: {
        const double blade = angle * radius;
        return {
            point.x * std::cos(blade) + std::sin(blade),
            point.y * std::cos(blade) - std::sin(blade)
        };
    }
    case VariationType::Flower: {
        const double flower = 0.5;
        const double r = radius * (flower + std::sin(angle * 3.0));
        return {r * std::cos(angle), r * std::sin(angle)};
    }
    case VariationType::Cosine: {
        return {
            std::cos(kPi * point.x) * std::cosh(point.y),
            -std::sin(kPi * point.x) * std::sinh(point.y)
        };
    }
    case VariationType::Fold: {
        return {
            std::abs(point.x) - std::abs(point.y),
            std::abs(point.x) + std::abs(point.y) - 1.0
        };
    }
    case VariationType::Checkers: {
        const double checkers = 0.5;
        return {
            point.x + checkers * (static_cast<int>(std::floor(point.x)) % 2 == 0 ? 1.0 : -1.0),
            point.y + checkers * (static_cast<int>(std::floor(point.y)) % 2 == 0 ? 1.0 : -1.0)
        };
    }
    case VariationType::Hyperbolic:
        return {point.x / radiusSquared, point.y};
    case VariationType::Diamond:
        return {(point.x / radius) * std::cos(radius), (point.y / radius) * std::sin(radius)};
    case VariationType::Exponential: {
        const double factor = std::exp(point.x - 1.0);
        const double theta = kPi * point.y;
        return {factor * std::cos(theta), factor * std::sin(theta)};
    }
    case VariationType::Power: {
        const double exponent = point.x / radius;
        const double factor = std::pow(radius, exponent);
        return {(point.y / radius) * factor, (point.x / radius) * factor};
    }
    case VariationType::Sec: {
        const double denominator = SafeSignedDenominator(std::cos(2.0 * point.x) + std::cosh(2.0 * point.y));
        const double factor = 2.0 / denominator;
        return {
            factor * std::cos(point.x) * std::cosh(point.y),
            factor * std::sin(point.x) * std::sinh(point.y)
        };
    }
    case VariationType::Csc: {
        const double denominator = SafeSignedDenominator(std::cosh(2.0 * point.y) - std::cos(2.0 * point.x));
        const double factor = 2.0 / denominator;
        return {
            factor * std::sin(point.x) * std::cosh(point.y),
            -factor * std::cos(point.x) * std::sinh(point.y)
        };
    }
    case VariationType::Cot: {
        const double factor = 1.0 / SafeSignedDenominator(std::cosh(2.0 * point.y) - std::cos(2.0 * point.x));
        return {
            factor * std::sin(2.0 * point.x),
            -factor * std::sinh(2.0 * point.y)
        };
    }
    case VariationType::Sech: {
        const double denominator = SafeSignedDenominator(std::cos(2.0 * point.y) + std::cosh(2.0 * point.x));
        const double factor = 2.0 / denominator;
        return {
            factor * std::cos(point.y) * std::cosh(point.x),
            -factor * std::sin(point.y) * std::sinh(point.x)
        };
    }
    case VariationType::Perspective: {
        const double p_angle = kPi / 4.0;
        const double dist = 2.0;
        const double denominator = SafeSignedDenominator(dist - point.y * std::sin(p_angle));
        return {
            dist * point.x / denominator,
            dist * point.y * std::cos(p_angle) / denominator
        };
    }
    case VariationType::Blob: {
        const double p_high = 1.2;
        const double p_low = 0.5;
        const double p_waves = 3.0;
        const double factor = radius * (p_low + 0.5 * (p_high - p_low) * (1.0 + std::sin(p_waves * angle)));
        return {
            factor * std::cos(angle),
            factor * std::sin(angle)
        };
    }
    case VariationType::PDJ: {
        const double a = 0.1;
        const double b = 1.9;
        const double c = -0.8;
        const double d = -1.2;
        return {
            std::sin(a * point.y) - std::cos(b * point.x),
            std::sin(c * point.x) - std::cos(d * point.y)
        };
    }
    case VariationType::Fan2: {
        const double p_x = kPi * 0.1;
        const double p_y = 0.5;
        const double dx = kPi * (p_x * p_x + 1.0e-9);
        const double dx2 = dx * 0.5;
        const double t = angle + p_y - dx * std::floor((angle + p_y) / dx);
        const double a = t > dx2 ? angle - dx2 : angle + dx2;
        return {
            radius * std::sin(a),
            radius * std::cos(a)
        };
    }
    case VariationType::Rings2: {
        const double p_val = 0.5;
        const double p_val2 = p_val * p_val + 1.0e-9;
        const double t = radius - 2.0 * p_val2 * std::floor((radius + p_val2) / (2.0 * p_val2)) + radius * (1.0 - p_val2);
        return {
            t * std::sin(angle),
            t * std::cos(angle)
        };
    }
    case VariationType::TwinTrian: {
        const double r = radius * 0.8;
        const double sinr = std::sin(r);
        const double cosr = std::cos(r);
        const double diff = std::log10(std::max(1.0e-9, sinr * sinr)) + cosr;
        return {
            point.x + 0.8 * point.x * diff,
            point.y + 0.8 * point.x * (diff - sinr * kPi)
        };
    }
    case VariationType::Count:
    default:
        return point;
    }
}

}  // namespace radiary
