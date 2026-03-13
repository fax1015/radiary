#include "core/Scene.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace radiary {

namespace {

Color RandomBackgroundColor(std::mt19937& generator) {
    std::uniform_real_distribution<double> roll(0.0, 1.0);
    const double choice = roll(generator);
    if (choice < 0.8) {
        return {10, 10, 13, 255};
    }
    if (choice < 0.9) {
        return {245, 245, 245, 255};
    }

    std::uniform_int_distribution<int> channel(24, 236);
    return {
        static_cast<std::uint8_t>(channel(generator)),
        static_cast<std::uint8_t>(channel(generator)),
        static_cast<std::uint8_t>(channel(generator)),
        255
    };
}

TransformLayer MakeTransform(
    const std::string& name,
    const double weight,
    const double rotationDegrees,
    const double scaleX,
    const double scaleY,
    const double translateX,
    const double translateY,
    const double colorIndex,
    const std::initializer_list<std::pair<VariationType, double>>& variationWeights) {
    TransformLayer layer;
    layer.name = name;
    layer.weight = weight;
    layer.rotationDegrees = rotationDegrees;
    layer.scaleX = scaleX;
    layer.scaleY = scaleY;
    layer.translateX = translateX;
    layer.translateY = translateY;
    layer.colorIndex = colorIndex;
    for (const auto& [variation, amount] : variationWeights) {
        layer.variations[static_cast<std::size_t>(variation)] = amount;
    }
    if (std::all_of(layer.variations.begin(), layer.variations.end(), [](const double value) { return value == 0.0; })) {
        layer.variations[static_cast<std::size_t>(VariationType::Linear)] = 1.0;
    }
    return layer;
}

std::vector<GradientStop> DefaultGradient() {
    return {
        {0.0, {20, 20, 28, 255}},
        {0.2, {62, 48, 125, 255}},
        {0.48, {124, 110, 245, 255}},
        {0.74, {245, 130, 74, 255}},
        {1.0, {239, 236, 250, 255}}
    };
}

std::vector<GradientStop> ProceduralGradient() {
    return {
        {0.0, {8, 8, 11, 255}},
        {0.18, {20, 21, 26, 255}},
        {0.46, {74, 79, 90, 255}},
        {0.78, {174, 180, 191, 255}},
        {1.0, {246, 247, 250, 255}}
    };
}

PathSettings DefaultPath() {
    PathSettings path;
    path.name = "Path Layer 1";
    path.controlPoints = {
        {-5.6, 0.0, 0.0},
        {-1.9, 0.0, 0.0},
        {1.9, 0.0, 0.0},
        {5.6, 0.0, 0.0}
    };
    return path;
}

std::vector<Vec3> RandomPathPoints(std::mt19937& generator, const int pointCount) {
    std::uniform_real_distribution<double> x(-4.8, 4.8);
    std::uniform_real_distribution<double> y(-3.6, 3.6);
    std::uniform_real_distribution<double> z(-4.2, 4.2);

    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(std::max(2, pointCount)));
    for (int index = 0; index < std::max(2, pointCount); ++index) {
        const double t = (pointCount <= 1) ? 0.0 : static_cast<double>(index) / static_cast<double>(pointCount - 1);
        points.push_back({
            Lerp(-4.8, 4.8, t) + x(generator) * 0.36,
            y(generator),
            z(generator)
        });
    }
    return points;
}

double WrapHue(const double hue) {
    double wrapped = std::fmod(hue, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped;
}

Color HsvToRgb(const double hue, const double saturation, const double value) {
    const double clampedSaturation = Clamp(saturation, 0.0, 1.0);
    const double clampedValue = Clamp(value, 0.0, 1.0);
    const double chroma = clampedValue * clampedSaturation;
    const double sector = WrapHue(hue) / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));

    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    if (sector < 1.0) {
        red = chroma;
        green = x;
    } else if (sector < 2.0) {
        red = x;
        green = chroma;
    } else if (sector < 3.0) {
        green = chroma;
        blue = x;
    } else if (sector < 4.0) {
        green = x;
        blue = chroma;
    } else if (sector < 5.0) {
        red = x;
        blue = chroma;
    } else {
        red = chroma;
        blue = x;
    }

    const double match = clampedValue - chroma;
    return {
        static_cast<std::uint8_t>(std::round((red + match) * 255.0)),
        static_cast<std::uint8_t>(std::round((green + match) * 255.0)),
        static_cast<std::uint8_t>(std::round((blue + match) * 255.0)),
        255
    };
}

std::vector<GradientStop> RandomGradient(std::mt19937& generator) {
    std::uniform_real_distribution<double> hue(0.0, 360.0);
    std::uniform_real_distribution<double> offset(18.0, 58.0);
    std::uniform_real_distribution<double> complement(120.0, 190.0);
    std::uniform_real_distribution<double> saturation(0.55, 0.95);
    std::uniform_real_distribution<double> brightValue(0.75, 0.98);
    std::uniform_real_distribution<double> midValue(0.45, 0.70);
    std::bernoulli_distribution directionChoice(0.5);
    std::bernoulli_distribution darkStart(0.15);  // 15% chance of dark starting color

    const double baseHue = hue(generator);
    const double direction = directionChoice(generator) ? 1.0 : -1.0;
    const double accentHue = baseHue + direction * offset(generator);
    const double glowHue = baseHue + direction * complement(generator);

    const bool useDarkStart = darkStart(generator);
    const double startValue = useDarkStart ? 0.08 : midValue(generator);
    const double secondValue = useDarkStart ? 0.25 : brightValue(generator);

    return {
        {0.0, HsvToRgb(baseHue + 10.0, saturation(generator) * 0.5, startValue)},
        {0.16, HsvToRgb(baseHue - 6.0, saturation(generator) * 0.85, secondValue)},
        {0.46, HsvToRgb(accentHue, saturation(generator), brightValue(generator))},
        {0.78, HsvToRgb(glowHue, saturation(generator) * 0.9, brightValue(generator))},
        {1.0, HsvToRgb(glowHue + 12.0, saturation(generator) * 0.3, 0.98)}
    };
}

std::vector<GradientStop> RandomProceduralGradient(std::mt19937& generator) {
    std::uniform_real_distribution<double> hue(0.0, 360.0);
    std::uniform_real_distribution<double> saturation(0.45, 0.90);
    std::uniform_real_distribution<double> brightValue(0.70, 0.95);
    std::bernoulli_distribution darkStart(0.15);  // 15% chance of dark starting color

    const double baseHue = hue(generator);
    const bool useDarkStart = darkStart(generator);

    const Color deep = useDarkStart ?
        HsvToRgb(baseHue, saturation(generator) * 0.4, 0.08) :
        HsvToRgb(baseHue, saturation(generator) * 0.6, 0.45);
    const Color mid = HsvToRgb(baseHue + 20.0, saturation(generator) * 0.7, brightValue(generator) * 0.7);
    const Color bright = HsvToRgb(baseHue + 40.0, saturation(generator), brightValue(generator));
    const Color rim = HsvToRgb(baseHue + 60.0, saturation(generator) * 0.5, 0.95);

    return {
        {0.0, deep},
        {0.22, Lerp(deep, mid, 0.55)},
        {0.50, mid},
        {0.82, Lerp(mid, rim, 0.68)},
        {1.0, rim}
    };
}

std::vector<GradientStop> RandomSuperColorfulGradient(std::mt19937& generator) {
    std::uniform_real_distribution<double> hue(0.0, 360.0);
    std::uniform_real_distribution<double> saturation(0.72, 0.98);
    std::uniform_real_distribution<double> brightValue(0.78, 0.98);
    std::uniform_real_distribution<double> midValue(0.55, 0.82);
    std::uniform_real_distribution<double> jitter(-8.0, 8.0);
    std::uniform_int_distribution<int> schemeDist(0, 5);

    const double baseHue = hue(generator);
    const int scheme = schemeDist(generator);

    // Color harmony schemes producing 4-5 distinct hues that mesh well
    double h0 = baseHue;
    double h1 = 0.0;
    double h2 = 0.0;
    double h3 = 0.0;
    double h4 = 0.0;

    switch (scheme) {
    case 0:
        // Analogous warm spread (30-degree steps)
        h1 = h0 + 28.0 + jitter(generator);
        h2 = h0 + 56.0 + jitter(generator);
        h3 = h0 + 84.0 + jitter(generator);
        h4 = h0 + 112.0 + jitter(generator);
        break;
    case 1:
        // Triadic with fill (120-degree primary, fill between)
        h1 = h0 + 60.0 + jitter(generator);
        h2 = h0 + 120.0 + jitter(generator);
        h3 = h0 + 200.0 + jitter(generator);
        h4 = h0 + 280.0 + jitter(generator);
        break;
    case 2:
        // Split-complementary extended
        h1 = h0 + 150.0 + jitter(generator);
        h2 = h0 + 180.0 + jitter(generator);
        h3 = h0 + 210.0 + jitter(generator);
        h4 = h0 + 30.0 + jitter(generator);
        break;
    case 3:
        // Tetradic / rectangular
        h1 = h0 + 90.0 + jitter(generator);
        h2 = h0 + 180.0 + jitter(generator);
        h3 = h0 + 270.0 + jitter(generator);
        h4 = h0 + 45.0 + jitter(generator);
        break;
    case 4:
        // Rainbow sweep (wide hue range)
        h1 = h0 + 72.0 + jitter(generator);
        h2 = h0 + 144.0 + jitter(generator);
        h3 = h0 + 216.0 + jitter(generator);
        h4 = h0 + 288.0 + jitter(generator);
        break;
    case 5:
    default:
        // Complementary with analogous bridges
        h1 = h0 + 35.0 + jitter(generator);
        h2 = h0 + 165.0 + jitter(generator);
        h3 = h0 + 195.0 + jitter(generator);
        h4 = h0 + 330.0 + jitter(generator);
        break;
    }

    h0 = WrapHue(h0);
    h1 = WrapHue(h1);
    h2 = WrapHue(h2);
    h3 = WrapHue(h3);
    h4 = WrapHue(h4);

    // Build gradient with high-saturation, varied-brightness stops
    const double s0 = saturation(generator);
    const double s1 = saturation(generator);
    const double s2 = saturation(generator);
    const double s3 = saturation(generator);
    const double s4 = saturation(generator) * 0.6;

    const double v0 = midValue(generator);
    const double v1 = brightValue(generator);
    const double v2 = brightValue(generator);
    const double v3 = brightValue(generator);
    const double v4 = 0.95;

    return {
        {0.0, HsvToRgb(h0, s0, v0)},
        {0.22, HsvToRgb(h1, s1, v1)},
        {0.48, HsvToRgb(h2, s2, v2)},
        {0.75, HsvToRgb(h3, s3, v3)},
        {1.0, HsvToRgb(h4, s4, v4)}
    };
}

std::vector<GradientStop> RandomSuperColorfulProceduralGradient(std::mt19937& generator) {
    std::uniform_real_distribution<double> hue(0.0, 360.0);
    std::uniform_real_distribution<double> saturation(0.65, 0.95);
    std::uniform_real_distribution<double> brightValue(0.72, 0.95);
    std::uniform_real_distribution<double> jitter(-10.0, 10.0);
    std::uniform_int_distribution<int> schemeDist(0, 3);

    const double baseHue = hue(generator);
    const int scheme = schemeDist(generator);

    double h0 = baseHue;
    double h1 = 0.0;
    double h2 = 0.0;
    double h3 = 0.0;

    switch (scheme) {
    case 0:
        h1 = h0 + 90.0 + jitter(generator);
        h2 = h0 + 180.0 + jitter(generator);
        h3 = h0 + 270.0 + jitter(generator);
        break;
    case 1:
        h1 = h0 + 40.0 + jitter(generator);
        h2 = h0 + 160.0 + jitter(generator);
        h3 = h0 + 200.0 + jitter(generator);
        break;
    case 2:
        h1 = h0 + 60.0 + jitter(generator);
        h2 = h0 + 120.0 + jitter(generator);
        h3 = h0 + 240.0 + jitter(generator);
        break;
    case 3:
    default:
        h1 = h0 + 72.0 + jitter(generator);
        h2 = h0 + 144.0 + jitter(generator);
        h3 = h0 + 288.0 + jitter(generator);
        break;
    }

    h0 = WrapHue(h0);
    h1 = WrapHue(h1);
    h2 = WrapHue(h2);
    h3 = WrapHue(h3);

    const Color deep = HsvToRgb(h0, saturation(generator) * 0.8, 0.35);
    const Color mid = HsvToRgb(h1, saturation(generator), brightValue(generator) * 0.8);
    const Color bright = HsvToRgb(h2, saturation(generator), brightValue(generator));
    const Color rim = HsvToRgb(h3, saturation(generator) * 0.7, 0.92);

    return {
        {0.0, deep},
        {0.22, Lerp(deep, mid, 0.55)},
        {0.50, mid},
        {0.78, bright},
        {1.0, rim}
    };
}

std::vector<Vec3> ProceduralControlPoints(const double phase, const Vec3& offset, const double scale) {
    return {
        {-5.4 * scale + offset.x, -0.6 * scale + offset.y, -2.4 * scale + std::sin(phase + 0.2) * 1.2 + offset.z},
        {-3.7 * scale + offset.x, 3.0 * scale + offset.y, 2.2 * scale + std::cos(phase + 0.7) * 1.0 + offset.z},
        {-1.5 * scale + offset.x, 0.8 * scale + offset.y, -3.7 * scale + std::sin(phase + 1.4) * 1.4 + offset.z},
        {0.4 * scale + offset.x, 3.6 * scale + offset.y, 3.4 * scale + std::cos(phase + 2.1) * 1.3 + offset.z},
        {2.2 * scale + offset.x, 0.0 * scale + offset.y, -3.1 * scale + std::sin(phase + 2.8) * 1.2 + offset.z},
        {4.6 * scale + offset.x, -3.3 * scale + offset.y, 2.8 * scale + std::cos(phase + 3.3) * 1.1 + offset.z},
        {5.9 * scale + offset.x, -0.8 * scale + offset.y, -1.4 * scale + std::sin(phase + 4.0) * 1.0 + offset.z}
    };
}

void ApplyProceduralFinish(
    PathSettings& path,
    const SegmentMode mode,
    const int sides,
    const double size,
    const double phase) {
    path.closed = false;
    path.thickness = 0.28;
    path.taper = 0.22;
    path.twist = 2.6 + std::sin(phase) * 0.55;
    path.repeatCount = 1;
    path.sampleCount = 144;
    path.segment.mode = mode;
    path.segment.segments = 14;
    path.segment.sides = sides;
    path.segment.breakSides = true;
    path.segment.chamfer = false;
    path.segment.chamferSize = 3.0;
    path.segment.caps = true;
    path.segment.size = size;
    path.segment.sizeX = size * 0.92;
    path.segment.sizeY = size * 0.74;
    path.segment.sizeZ = size * 1.28;
    path.segment.orientToPath = true;
    path.segment.orientReferenceAxis = PathAxis::Z;
    path.segment.rotateX = -8.0 + std::sin(phase * 1.3) * 14.0;
    path.segment.rotateY = 10.0 + std::cos(phase * 1.1) * 18.0;
    path.segment.rotateZ = std::sin(phase * 0.8) * 24.0;
    path.segment.twistZ = 56.0 + std::cos(phase) * 18.0;
    path.segment.debugNormals = false;
    path.segment.tessellate = TessellationMode::Triangles;
    path.segment.randomness = 3.8;

    path.fractalDisplacement.fractalType = FractalType::Turbulent;
    path.fractalDisplacement.space = FractalSpace::Local;
    path.fractalDisplacement.amplitude = 58.0;
    path.fractalDisplacement.frequency = 180.0 + std::sin(phase * 0.7) * 42.0;
    path.fractalDisplacement.evolution = phase * 1.5;
    path.fractalDisplacement.offsetX = std::sin(phase) * 0.8;
    path.fractalDisplacement.offsetY = std::cos(phase * 0.8) * 0.7;
    path.fractalDisplacement.offsetZ = std::sin(phase * 1.2) * 0.9;
    path.fractalDisplacement.complexity = 4;
    path.fractalDisplacement.octScale = 2.65;
    path.fractalDisplacement.octMult = 0.72;
    path.fractalDisplacement.smoothenNormals = 0.12;
    path.fractalDisplacement.seamlessLoop = false;

    path.material.renderMode = PathRenderMode::SolidWire;
    path.material.materialType = MaterialType::Metallic;
    path.material.primaryColor = {116, 118, 124, 255};
    path.material.accentColor = {248, 248, 251, 255};
    path.material.wireColor = {182, 186, 194, 255};
    path.material.pointSize = 4.0;
}

PathSettings MakeProceduralPath(
    const std::string& name,
    const SegmentMode mode,
    const int sides,
    const double size,
    const double phase,
    const Vec3& offset,
    const double scale) {
    PathSettings path = DefaultPath();
    path.name = name;
    path.controlPoints = ProceduralControlPoints(phase, offset, scale);
    ApplyProceduralFinish(path, mode, sides, size, phase);
    return path;
}

Scene MakeProceduralScene(const std::string& name, std::vector<PathSettings> paths) {
    Scene scene;
    scene.name = name;
    scene.mode = SceneMode::Path;
    scene.animatePath = false;
    scene.transforms = {
        MakeTransform("Anchor", 1.0, 0.0, 0.52, 0.52, 0.0, 0.0, 0.18, {
            {VariationType::Linear, 0.7},
            {VariationType::Sinusoidal, 0.3}
        }),
        MakeTransform("Shard", 0.82, 38.0, 0.48, 0.34, 0.7, -0.2, 0.56, {
            {VariationType::Swirl, 0.6},
            {VariationType::Curl, 0.4}
        }),
        MakeTransform("Echo", 0.64, -57.0, 0.28, 0.76, -0.62, 0.58, 0.84, {
            {VariationType::Julia, 0.5},
            {VariationType::Heart, 0.2},
            {VariationType::Bubble, 0.3}
        })
    };
    scene.paths = std::move(paths);
    scene.gradientStops = ProceduralGradient();
    scene.selectedPath = 0;
    return scene;
}

PathSettings RandomProceduralPath(std::mt19937& generator, const int index, const std::uint32_t seed) {
    std::uniform_real_distribution<double> phase(0.0, kPi * 2.0);
    std::uniform_real_distribution<double> offsetX(-1.4, 1.4);
    std::uniform_real_distribution<double> offsetY(-1.0, 1.0);
    std::uniform_real_distribution<double> offsetZ(-1.9, 1.9);
    std::uniform_real_distribution<double> scaleDist(0.58, 0.9);
    std::uniform_real_distribution<double> sizeDist(68.0, 112.0);
    std::uniform_real_distribution<double> boost(0.0, 1.0);
    std::uniform_real_distribution<double> twistZ(36.0, 96.0);
    std::uniform_real_distribution<double> rotate(-28.0, 28.0);
    std::uniform_real_distribution<double> hueDist(0.0, 360.0);
    std::uniform_real_distribution<double> satDist(0.35, 0.85);
    std::uniform_real_distribution<double> brightVal(0.70, 0.95);
    std::uniform_real_distribution<double> midVal(0.50, 0.75);
    std::bernoulli_distribution darkColor(0.12);  // 12% chance of darker primary

    const double localPhase = phase(generator) + static_cast<double>(index) * 0.65 + static_cast<double>(seed % 17) * 0.04;
    const SegmentMode mode = static_cast<SegmentMode>((seed + static_cast<std::uint32_t>(index)) % 3);
    const int sides = 3 + static_cast<int>((seed + static_cast<std::uint32_t>(index) * 3U) % 4U);
    PathSettings path = MakeProceduralPath(
        "Path Layer " + std::to_string(index + 1),
        mode,
        sides,
        sizeDist(generator),
        localPhase,
        {offsetX(generator), offsetY(generator), offsetZ(generator)},
        scaleDist(generator));

    path.controlPoints = ProceduralControlPoints(localPhase, {offsetX(generator), offsetY(generator), offsetZ(generator)}, scaleDist(generator));
    path.segment.segments = 8 + static_cast<int>((seed + static_cast<std::uint32_t>(index) * 11U) % 10U);
    path.segment.breakSides = true;
    path.segment.chamfer = false;
    path.segment.sizeX *= 0.72 + boost(generator) * 0.20;
    path.segment.sizeY *= 0.56 + boost(generator) * 0.18;
    path.segment.sizeZ *= 0.84 + boost(generator) * 0.42;
    path.segment.rotateX += rotate(generator);
    path.segment.rotateY += rotate(generator);
    path.segment.rotateZ += rotate(generator);
    path.segment.twistZ = twistZ(generator);
    path.segment.randomness = 2.8 + boost(generator) * 2.9;

    // New procedural parameters
    const double profileRoll = boost(generator);
    if (profileRoll < 0.5) {
        path.segment.thicknessProfile = ThicknessProfile::Linear;
    } else if (profileRoll < 0.7) {
        path.segment.thicknessProfile = ThicknessProfile::Pulse;
        path.segment.thicknessPulseFrequency = 1.0 + boost(generator) * 4.0;
        path.segment.thicknessPulseDepth = 0.2 + boost(generator) * 0.5;
    } else if (profileRoll < 0.85) {
        path.segment.thicknessProfile = ThicknessProfile::Bezier;
    } else {
        path.segment.thicknessProfile = ThicknessProfile::Blobby;
        path.segment.thicknessBlobCenter = 0.3 + boost(generator) * 0.4;
        path.segment.thicknessBlobWidth = 0.15 + boost(generator) * 0.25;
    }
    path.segment.junctionSize = boost(generator) * 0.6;
    path.segment.junctionBlend = boost(generator) * 0.3;
    path.segment.tubeWarp = boost(generator) * 0.8;
    path.segment.tubeWarpFrequency = 0.5 + boost(generator) * 3.0;
    path.segment.tendrilCount = static_cast<int>(boost(generator) * 8);
    if (path.segment.tendrilCount > 0) {
        path.segment.tendrilLength = 0.5 + boost(generator) * 2.0;
        path.segment.tendrilThickness = 0.1 + boost(generator) * 0.4;
        path.segment.tendrilWarp = boost(generator) * 0.8;
    }
    const double layoutRoll = boost(generator);
    if (layoutRoll < 0.6) {
        path.layout = PathLayout::UserDefined;
    } else if (layoutRoll < 0.75) {
        path.layout = PathLayout::RadialCluster;
    } else if (layoutRoll < 0.9) {
        path.layout = PathLayout::Network;
    } else {
        path.layout = PathLayout::TendrilBall;
    }
    if (path.layout != PathLayout::UserDefined) {
        path.layoutRadius = 2.0 + boost(generator) * 4.0;
        path.layoutNodes = 4 + static_cast<int>(boost(generator) * 12);
        path.layoutRandomness = boost(generator) * 0.6;
    }

    path.fractalDisplacement.amplitude = 36.0 + boost(generator) * 90.0;
    path.fractalDisplacement.frequency = 120.0 + boost(generator) * 300.0;
    path.fractalDisplacement.evolution = boost(generator) * 8.0;
    path.fractalDisplacement.complexity = 3 + static_cast<int>((seed + static_cast<std::uint32_t>(index)) % 3U);
    path.fractalDisplacement.octScale = 2.2 + boost(generator) * 1.2;
    path.fractalDisplacement.octMult = 0.56 + boost(generator) * 0.28;
    path.fractalDisplacement.smoothenNormals = 0.05 + boost(generator) * 0.28;
    path.thickness = 0.2 + boost(generator) * 0.18;
    path.taper = 0.14 + boost(generator) * 0.24;
    path.twist = 1.6 + boost(generator) * 2.8;
    path.material.renderMode = boost(generator) > 0.78 ? PathRenderMode::Wireframe : PathRenderMode::SolidWire;
    path.material.materialType = MaterialType::Metallic;

    // Randomize colors with bright colors preferred
    const double baseHue = hueDist(generator);
    const bool useDark = darkColor(generator);
    const double primaryVal = useDark ? midVal(generator) * 0.5 : brightVal(generator);
    path.material.primaryColor = HsvToRgb(baseHue, satDist(generator), primaryVal);
    path.material.accentColor = HsvToRgb(fmod(baseHue + 30.0 + boost(generator) * 60.0, 360.0), satDist(generator) * 0.85, brightVal(generator));
    path.material.wireColor = HsvToRgb(fmod(baseHue + 180.0 + boost(generator) * 60.0, 360.0), satDist(generator) * 0.6, brightVal(generator));
    return path;
}

}  // namespace

std::vector<Color> BuildGradientPalette(const std::vector<GradientStop>& stops, const std::size_t count) {
    std::vector<Color> palette;
    palette.reserve(count);
    if (stops.empty() || count == 0) {
        return palette;
    }

    std::vector<GradientStop> orderedStops = stops;
    std::sort(orderedStops.begin(), orderedStops.end(), [](const GradientStop& left, const GradientStop& right) {
        return left.position < right.position;
    });

    for (std::size_t index = 0; index < count; ++index) {
        const double t = (count == 1) ? 0.0 : static_cast<double>(index) / static_cast<double>(count - 1);
        const auto upper = std::lower_bound(
            orderedStops.begin(),
            orderedStops.end(),
            t,
            [](const GradientStop& stop, const double value) { return stop.position < value; });

        if (upper == orderedStops.begin()) {
            palette.push_back(upper->color);
            continue;
        }
        if (upper == orderedStops.end()) {
            palette.push_back(orderedStops.back().color);
            continue;
        }

        const GradientStop& next = *upper;
        const GradientStop& previous = *(upper - 1);
        const double span = std::max(1.0e-9, next.position - previous.position);
        const double localAlpha = Clamp((t - previous.position) / span, 0.0, 1.0);
        palette.push_back(Lerp(previous.color, next.color, localAlpha));
    }

    return palette;
}

Scene CreateDefaultScene() {
    return MakeProceduralScene(
        "Chrome Reliquary",
        {
            MakeProceduralPath("Core Talon", SegmentMode::RepeatSphere, 3, 126.0, 0.0, {0.0, 0.0, 0.0}, 1.0),
            MakeProceduralPath("Outer Fang", SegmentMode::RepeatNGon, 4, 102.0, 1.15, {0.2, -0.45, 1.4}, 0.9),
            MakeProceduralPath("Spine Rail", SegmentMode::ExtrudeNGon, 3, 88.0, 2.2, {-0.2, 0.35, -1.7}, 0.82)
        });
}

Scene CreatePresetScene(const std::string& presetName) {
    if (presetName == "Helix Bloom") {
        Scene scene = MakeProceduralScene(
            presetName,
            {
                MakeProceduralPath("Helix Spine", SegmentMode::ExtrudeNGon, 3, 108.0, 0.6, {0.0, 0.0, 0.0}, 0.96),
                MakeProceduralPath("Orbit Blade", SegmentMode::RepeatNGon, 4, 92.0, 1.8, {0.3, -0.25, 1.2}, 0.86)
            });
        scene.paths[0].segment.segments = 18;
        scene.paths[0].segment.sizeZ = 148.0;
        scene.paths[0].segment.twistZ = 84.0;
        scene.paths[0].fractalDisplacement.amplitude = 48.0;
        scene.paths[0].fractalDisplacement.frequency = 156.0;
        scene.paths[1].segment.segments = 11;
        scene.paths[1].segment.randomness = 4.4;
        scene.paths[1].fractalDisplacement.amplitude = 62.0;
        return scene;
    }

    if (presetName == "Glass Attractor") {
        Scene scene = CreateDefaultScene();
        scene.name = presetName;
        scene.mode = SceneMode::Flame;
        scene.transforms = {
            MakeTransform("Core", 1.0, 12.0, 0.48, 0.62, 0.0, 0.0, 0.2, {
                {VariationType::Spherical, 0.35},
                {VariationType::Sinusoidal, 0.3},
                {VariationType::Linear, 0.35}
            }),
            MakeTransform("Arc", 0.82, 47.0, 0.56, 0.26, 0.8, 0.25, 0.58, {
                {VariationType::Horseshoe, 0.5},
                {VariationType::Polar, 0.25},
                {VariationType::Spiral, 0.25}
            }),
            MakeTransform("Echo", 0.7, -63.0, 0.3, 0.74, -0.7, -0.6, 0.9, {
                {VariationType::Heart, 0.35},
                {VariationType::Julia, 0.45},
                {VariationType::Bubble, 0.2}
            })
        };
        scene.gradientStops = {
            {0.0, {15, 17, 24, 255}},
            {0.35, {68, 102, 204, 255}},
            {0.65, {137, 176, 255, 255}},
            {1.0, {245, 242, 250, 255}}
        };
        return scene;
    }

    if (presetName == "Procedural Forge") {
        Scene scene = MakeProceduralScene(
            presetName,
            {
                MakeProceduralPath("Core Talon", SegmentMode::RepeatSphere, 3, 128.0, 0.0, {0.0, 0.1, 0.0}, 1.0),
                MakeProceduralPath("Orbit Fang", SegmentMode::RepeatNGon, 4, 104.0, 1.35, {0.3, -0.5, 1.5}, 0.92),
                MakeProceduralPath("Vein Spine", SegmentMode::ExtrudeNGon, 3, 92.0, 2.15, {-0.2, 0.4, -1.8}, 0.84)
            });
        scene.paths[1].segment.segments = 11;
        scene.paths[1].segment.randomness = 4.4;
        scene.paths[1].fractalDisplacement.amplitude = 66.0;
        scene.paths[1].fractalDisplacement.frequency = 230.0;
        scene.paths[1].fractalDisplacement.smoothenNormals = 0.08;
        scene.paths[2].segment.segments = 16;
        scene.paths[2].segment.sizeZ = 162.0;
        scene.paths[2].segment.twistZ = 72.0;
        scene.paths[2].fractalDisplacement.amplitude = 42.0;
        scene.paths[2].fractalDisplacement.frequency = 148.0;
        scene.selectedPath = 0;
        return scene;
    }

    if (presetName == "Blade Halo") {
        Scene scene = MakeProceduralScene(
            presetName,
            {
                MakeProceduralPath("Halo Crown", SegmentMode::RepeatSphere, 4, 118.0, 0.8, {0.0, 0.8, 0.0}, 0.94),
                MakeProceduralPath("Lower Thorn", SegmentMode::RepeatNGon, 3, 96.0, 2.0, {0.0, -1.2, -1.2}, 0.88),
                MakeProceduralPath("Riser", SegmentMode::ExtrudeNGon, 3, 82.0, 3.1, {-0.4, 0.2, 1.5}, 0.72)
            });
        scene.paths[0].closed = true;
        scene.paths[0].segment.segments = 16;
        scene.paths[0].segment.sizeY = 58.0;
        scene.paths[1].segment.randomness = 4.8;
        scene.paths[1].fractalDisplacement.amplitude = 84.0;
        scene.paths[2].segment.sizeZ = 170.0;
        return scene;
    }

    if (presetName == "Sigil Bloom") {
        Scene scene = MakeProceduralScene(
            presetName,
            {
                MakeProceduralPath("Sigil Arc", SegmentMode::ExtrudeNGon, 3, 86.0, 0.15, {-0.7, 0.0, -1.5}, 0.82),
                MakeProceduralPath("Bloom Shard", SegmentMode::RepeatSphere, 5, 110.0, 1.55, {0.6, 0.5, 1.1}, 0.9),
                MakeProceduralPath("Needle Ring", SegmentMode::RepeatNGon, 3, 90.0, 2.75, {0.0, -0.4, 0.0}, 0.78)
            });
        scene.paths[0].segment.segments = 17;
        scene.paths[0].segment.twistZ = 92.0;
        scene.paths[1].segment.randomness = 5.0;
        scene.paths[1].fractalDisplacement.frequency = 248.0;
        scene.paths[2].closed = true;
        scene.paths[2].segment.segments = 12;
        scene.paths[2].segment.sizeX = 124.0;
        scene.paths[2].segment.sizeY = 50.0;
        return scene;
    }

    if (presetName == "Cathedral Spines") {
        Scene scene = MakeProceduralScene(
            presetName,
            {
                MakeProceduralPath("Nave", SegmentMode::ExtrudeNGon, 4, 94.0, 0.4, {0.0, 0.0, 0.0}, 1.02),
                MakeProceduralPath("Transept", SegmentMode::ExtrudeNGon, 3, 76.0, 1.75, {0.0, 0.9, 1.8}, 0.76),
                MakeProceduralPath("Flying Buttress", SegmentMode::RepeatNGon, 4, 88.0, 3.1, {0.4, -0.8, -1.4}, 0.82)
            });
        scene.paths[0].segment.sizeZ = 188.0;
        scene.paths[0].segment.twistZ = 68.0;
        scene.paths[1].segment.sizeZ = 164.0;
        scene.paths[1].fractalDisplacement.amplitude = 72.0;
        scene.paths[2].segment.randomness = 5.1;
        return scene;
    }

    return CreateDefaultScene();
}

Scene CreateRandomScene(const std::uint32_t seed) {
    std::mt19937 generator(seed);
    std::uniform_real_distribution<double> rotation(-180.0, 180.0);
    std::uniform_real_distribution<double> scale(0.14, 1.18);
    std::uniform_real_distribution<double> translate(-1.95, 1.95);
    std::uniform_real_distribution<double> weight(0.16, 1.75);
    std::uniform_real_distribution<double> amount(0.12, 1.65);
    std::uniform_real_distribution<double> colorJitter(-0.12, 0.12);
    std::uniform_real_distribution<double> shear(-1.25, 1.25);
    std::uniform_real_distribution<double> skewScale(0.18, 1.32);
    std::uniform_real_distribution<double> flameRotate(-110.0, 110.0);
    std::uniform_real_distribution<double> flameDepth(0.6, 2.4);
    std::uniform_int_distribution<int> archetypeDist(0, 31);
    std::bernoulli_distribution superColorful(0.15);
    std::bernoulli_distribution addLinear(0.15);  // Reduced from 0.55 - Linear creates straight lines
    std::bernoulli_distribution invertShear(0.5);
    std::bernoulli_distribution addWaves(0.3);
    std::bernoulli_distribution addBent(0.25);
    std::bernoulli_distribution addFlower(0.2);
    std::bernoulli_distribution addCosine(0.2);
    std::bernoulli_distribution addHyperbolic(0.2);
    std::bernoulli_distribution addExponential(0.16);
    std::bernoulli_distribution addSech(0.12);
    std::bernoulli_distribution enableDof(0.10);
    std::uniform_real_distribution<double> dofFocusDepth(0.24, 0.76);
    std::uniform_real_distribution<double> dofFocusRange(0.04, 0.18);
    std::uniform_real_distribution<double> dofBlurStrength(0.28, 0.82);

    Scene scene;
    scene.name = "Mutagen " + std::to_string(seed % 10000);
    const std::uint32_t modeRoll = seed % 20;
    scene.mode = modeRoll < 13 ? SceneMode::Flame : (modeRoll < 18 ? SceneMode::Hybrid : SceneMode::Path);
    scene.animatePath = false;
    scene.backgroundColor = RandomBackgroundColor(generator);
    scene.depthOfField.enabled = enableDof(generator);
    if (scene.depthOfField.enabled) {
        scene.depthOfField.focusDepth = dofFocusDepth(generator);
        scene.depthOfField.focusRange = dofFocusRange(generator);
        scene.depthOfField.blurStrength = dofBlurStrength(generator);
    }
    const bool isSuperColorful = superColorful(generator);
    if (isSuperColorful) {
        scene.gradientStops = scene.mode == SceneMode::Flame ? RandomSuperColorfulGradient(generator) : RandomSuperColorfulProceduralGradient(generator);
    } else {
        scene.gradientStops = scene.mode == SceneMode::Flame ? RandomGradient(generator) : RandomProceduralGradient(generator);
    }
    if (scene.mode != SceneMode::Path) {
        scene.flameRender.rotationXDegrees = flameRotate(generator);
        scene.flameRender.rotationYDegrees = flameRotate(generator);
        scene.flameRender.rotationZDegrees = flameRotate(generator);
        scene.flameRender.depthAmount = flameDepth(generator);
    }
    const int transformCount = 2 + static_cast<int>(seed % 11);
    for (int index = 0; index < transformCount; ++index) {
        TransformLayer layer;
        layer.name = "Layer " + std::to_string(index + 1);
        layer.weight = weight(generator);
        layer.rotationDegrees = rotation(generator);
        const double baseScale = scale(generator);
        layer.scaleX = baseScale * skewScale(generator);
        layer.scaleY = baseScale * skewScale(generator);
        layer.translateX = translate(generator);
        layer.translateY = translate(generator);
        layer.shearX = shear(generator);
        layer.shearY = invertShear(generator) ? -layer.shearX * (0.35 + skewScale(generator) * 0.4) : shear(generator);
        layer.colorIndex = Clamp((static_cast<double>(index) + 0.5) / static_cast<double>(transformCount) + colorJitter(generator), 0.08, 0.92);

        if (addLinear(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Linear)] = 0.08 + amount(generator) * 0.15;  // Reduced weight
        }

        switch (archetypeDist(generator)) {
        case 0:
            layer.variations[static_cast<std::size_t>(VariationType::Swirl)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Curl)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Spiral)] = amount(generator) * 0.78;
            break;
        case 1:
            layer.variations[static_cast<std::size_t>(VariationType::Spherical)] = amount(generator) * 0.92;
            layer.variations[static_cast<std::size_t>(VariationType::Bubble)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Eyefish)] = amount(generator) * 0.74;
            break;
        case 2:
            layer.variations[static_cast<std::size_t>(VariationType::Heart)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Disc)] = amount(generator) * 0.86;
            layer.variations[static_cast<std::size_t>(VariationType::Polar)] = amount(generator) * 0.62;
            break;
        case 3:
            layer.variations[static_cast<std::size_t>(VariationType::Julia)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Rays)] = amount(generator) * 0.82;
            layer.variations[static_cast<std::size_t>(VariationType::Ngon)] = amount(generator) * 0.74;
            break;
        case 4:
            layer.variations[static_cast<std::size_t>(VariationType::Horseshoe)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Cross)] = amount(generator) * 0.76;
            layer.variations[static_cast<std::size_t>(VariationType::Tangent)] = amount(generator) * 0.58;
            break;
        case 5:
            layer.variations[static_cast<std::size_t>(VariationType::Sinusoidal)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Cylinder)] = amount(generator) * 0.72;
            layer.variations[static_cast<std::size_t>(VariationType::Arch)] = amount(generator) * 0.88;
            break;
        case 6:
            layer.variations[static_cast<std::size_t>(VariationType::Blur)] = amount(generator) * 0.64;
            layer.variations[static_cast<std::size_t>(VariationType::Swirl)] = amount(generator) * 0.82;
            layer.variations[static_cast<std::size_t>(VariationType::Julia)] = amount(generator) * 0.74;
            layer.variations[static_cast<std::size_t>(VariationType::Disc)] = amount(generator) * 0.56;
            break;
        case 7:
            // Organic flowing patterns
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Popcorn)] = amount(generator) * 0.85;
            layer.variations[static_cast<std::size_t>(VariationType::Split)] = amount(generator) * 0.6;
            break;
        case 8:
            // Angular geometric
            layer.variations[static_cast<std::size_t>(VariationType::Bent)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Fan)] = amount(generator) * 0.9;
            layer.variations[static_cast<std::size_t>(VariationType::Wedge)] = amount(generator) * 0.7;
            break;
        case 9:
            // Concentric rings
            layer.variations[static_cast<std::size_t>(VariationType::Rings)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Spherical)] = amount(generator) * 0.8;
            layer.variations[static_cast<std::size_t>(VariationType::Bubble)] = amount(generator) * 0.5;
            break;
        case 10:
            // Complex fractal
            layer.variations[static_cast<std::size_t>(VariationType::Bipolar)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Julia)] = amount(generator) * 0.9;
            layer.variations[static_cast<std::size_t>(VariationType::Curl)] = amount(generator) * 0.7;
            break;
        case 11:
            // Spikey chaos
            layer.variations[static_cast<std::size_t>(VariationType::Rays)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Cross)] = amount(generator) * 0.85;
            layer.variations[static_cast<std::size_t>(VariationType::Tangent)] = amount(generator) * 0.65;
            layer.variations[static_cast<std::size_t>(VariationType::Wedge)] = amount(generator) * 0.5;
            break;
        case 12:
            // Soft ethereal
            layer.variations[static_cast<std::size_t>(VariationType::Sinusoidal)] = amount(generator) * 0.8;
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator) * 0.6;
            layer.variations[static_cast<std::size_t>(VariationType::Eyefish)] = amount(generator) * 0.9;
            break;
        case 13:
            // Mixed chaos - random selection
            layer.variations[static_cast<std::size_t>(VariationType::Swirl)] = amount(generator) * 0.5;
            layer.variations[static_cast<std::size_t>(VariationType::Bent)] = amount(generator) * 0.4;
            layer.variations[static_cast<std::size_t>(VariationType::Popcorn)] = amount(generator) * 0.3;
            layer.variations[static_cast<std::size_t>(VariationType::Rings)] = amount(generator) * 0.35;
            break;
        case 14:
            // Flower power
            layer.variations[static_cast<std::size_t>(VariationType::Flower)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Julia)] = amount(generator) * 0.7;
            layer.variations[static_cast<std::size_t>(VariationType::Cosine)] = amount(generator) * 0.5;
            break;
        case 15:
            // Blade runner
            layer.variations[static_cast<std::size_t>(VariationType::Blade)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Ex)] = amount(generator) * 0.8;
            layer.variations[static_cast<std::size_t>(VariationType::Handkerchief)] = amount(generator) * 0.6;
            break;
        case 16:
            // Fisheye distortion
            layer.variations[static_cast<std::size_t>(VariationType::Fisheye)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Bubble)] = amount(generator) * 0.9;
            layer.variations[static_cast<std::size_t>(VariationType::Eyefish)] = amount(generator) * 0.7;
            break;
        case 17:
            // Geometric fold
            layer.variations[static_cast<std::size_t>(VariationType::Fold)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Checkers)] = amount(generator) * 0.6;
            layer.variations[static_cast<std::size_t>(VariationType::Cross)] = amount(generator) * 0.5;
            break;
        case 18:
            // Complex hybrid
            layer.variations[static_cast<std::size_t>(VariationType::Cosine)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Flower)] = amount(generator) * 0.7;
            layer.variations[static_cast<std::size_t>(VariationType::Blade)] = amount(generator) * 0.5;
            layer.variations[static_cast<std::size_t>(VariationType::Ex)] = amount(generator) * 0.4;
            break;
        case 19:
            layer.variations[static_cast<std::size_t>(VariationType::Hyperbolic)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Diamond)] = amount(generator) * 0.78;
            layer.variations[static_cast<std::size_t>(VariationType::Spiral)] = amount(generator) * 0.52;
            break;
        case 20:
            layer.variations[static_cast<std::size_t>(VariationType::Exponential)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Power)] = amount(generator) * 0.72;
            layer.variations[static_cast<std::size_t>(VariationType::Cosine)] = amount(generator) * 0.44;
            break;
        case 21:
            layer.variations[static_cast<std::size_t>(VariationType::Sec)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Csc)] = amount(generator) * 0.74;
            layer.variations[static_cast<std::size_t>(VariationType::Cot)] = amount(generator) * 0.58;
            break;
        case 22:
            layer.variations[static_cast<std::size_t>(VariationType::Sech)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Exponential)] = amount(generator) * 0.66;
            layer.variations[static_cast<std::size_t>(VariationType::Bubble)] = amount(generator) * 0.48;
            break;
        case 23:
            // Maximum chaos
            layer.variations[static_cast<std::size_t>(VariationType::Handkerchief)] = amount(generator) * 0.6;
            layer.variations[static_cast<std::size_t>(VariationType::Fisheye)] = amount(generator) * 0.5;
            layer.variations[static_cast<std::size_t>(VariationType::Fold)] = amount(generator) * 0.4;
            layer.variations[static_cast<std::size_t>(VariationType::Checkers)] = amount(generator) * 0.3;
            break;
        case 24:
            // Nebula swirl - organic flowing shapes
            layer.variations[static_cast<std::size_t>(VariationType::Swirl)] = amount(generator) * 0.85;
            layer.variations[static_cast<std::size_t>(VariationType::Sinusoidal)] = amount(generator) * 0.6;
            layer.variations[static_cast<std::size_t>(VariationType::Eyefish)] = amount(generator) * 0.45;
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator) * 0.35;
            break;
        case 25:
            // Crystal lattice - geometric with depth
            layer.variations[static_cast<std::size_t>(VariationType::Ngon)] = amount(generator) * 0.92;
            layer.variations[static_cast<std::size_t>(VariationType::Checkers)] = amount(generator) * 0.55;
            layer.variations[static_cast<std::size_t>(VariationType::Fold)] = amount(generator) * 0.48;
            layer.variations[static_cast<std::size_t>(VariationType::Rings)] = amount(generator) * 0.3;
            break;
        case 26:
            // Aurora - smooth flowing gradients
            layer.variations[static_cast<std::size_t>(VariationType::Polar)] = amount(generator) * 0.78;
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator) * 0.65;
            layer.variations[static_cast<std::size_t>(VariationType::Curl)] = amount(generator) * 0.52;
            layer.variations[static_cast<std::size_t>(VariationType::Bubble)] = amount(generator) * 0.28;
            break;
        case 27:
            // Solar flare - explosive radial energy
            layer.variations[static_cast<std::size_t>(VariationType::Rays)] = amount(generator);
            layer.variations[static_cast<std::size_t>(VariationType::Exponential)] = amount(generator) * 0.68;
            layer.variations[static_cast<std::size_t>(VariationType::Tangent)] = amount(generator) * 0.45;
            layer.variations[static_cast<std::size_t>(VariationType::Spiral)] = amount(generator) * 0.38;
            break;
        case 28:
            // Silk threads - delicate interweaving
            layer.variations[static_cast<std::size_t>(VariationType::Fan)] = amount(generator) * 0.82;
            layer.variations[static_cast<std::size_t>(VariationType::Popcorn)] = amount(generator) * 0.55;
            layer.variations[static_cast<std::size_t>(VariationType::Split)] = amount(generator) * 0.42;
            layer.variations[static_cast<std::size_t>(VariationType::Sinusoidal)] = amount(generator) * 0.3;
            break;
        case 29:
            // Deep ocean - organic with subtle ripples
            layer.variations[static_cast<std::size_t>(VariationType::Spherical)] = amount(generator) * 0.75;
            layer.variations[static_cast<std::size_t>(VariationType::Bipolar)] = amount(generator) * 0.62;
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator) * 0.48;
            layer.variations[static_cast<std::size_t>(VariationType::Sech)] = amount(generator) * 0.32;
            break;
        case 30:
            // Plasma storm - chaotic high-energy
            layer.variations[static_cast<std::size_t>(VariationType::Julia)] = amount(generator) * 0.88;
            layer.variations[static_cast<std::size_t>(VariationType::Blade)] = amount(generator) * 0.65;
            layer.variations[static_cast<std::size_t>(VariationType::Cross)] = amount(generator) * 0.52;
            layer.variations[static_cast<std::size_t>(VariationType::Power)] = amount(generator) * 0.38;
            break;
        case 31:
            // Cosmic web - interconnected structures
            layer.variations[static_cast<std::size_t>(VariationType::Wedge)] = amount(generator) * 0.72;
            layer.variations[static_cast<std::size_t>(VariationType::Disc)] = amount(generator) * 0.58;
            layer.variations[static_cast<std::size_t>(VariationType::Heart)] = amount(generator) * 0.45;
            layer.variations[static_cast<std::size_t>(VariationType::Flower)] = amount(generator) * 0.35;
            break;
        case 32:
            // Blobby fields - organic soft shapes
            layer.variations[static_cast<std::size_t>(VariationType::Blob)] = amount(generator) * 0.85;
            layer.variations[static_cast<std::size_t>(VariationType::TwinTrian)] = amount(generator) * 0.65;
            layer.variations[static_cast<std::size_t>(VariationType::PDJ)] = amount(generator) * 0.45;
            layer.variations[static_cast<std::size_t>(VariationType::Perspective)] = amount(generator) * 0.35;
            break;
        case 33:
        default:
            // Geometric rings - structured circular patterns
            layer.variations[static_cast<std::size_t>(VariationType::Rings2)] = amount(generator) * 0.78;
            layer.variations[static_cast<std::size_t>(VariationType::Fan2)] = amount(generator) * 0.62;
            layer.variations[static_cast<std::size_t>(VariationType::PDJ)] = amount(generator) * 0.52;
            layer.variations[static_cast<std::size_t>(VariationType::TwinTrian)] = amount(generator) * 0.42;
            break;
        }

        // Additional random variations for more complexity
        if (addWaves(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Waves)] = amount(generator) * 0.5;
        }
        if (addBent(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Bent)] = amount(generator) * 0.4;
        }
        if (addFlower(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Flower)] = amount(generator) * 0.35;
        }
        if (addCosine(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Cosine)] = amount(generator) * 0.3;
        }
        if (addHyperbolic(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Hyperbolic)] = amount(generator) * 0.34;
            layer.variations[static_cast<std::size_t>(VariationType::Diamond)] = amount(generator) * 0.24;
        }
        if (addExponential(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Exponential)] = amount(generator) * 0.28;
            layer.variations[static_cast<std::size_t>(VariationType::Power)] = amount(generator) * 0.2;
        }
        if (addSech(generator)) {
            layer.variations[static_cast<std::size_t>(VariationType::Sech)] = amount(generator) * 0.22;
            layer.variations[static_cast<std::size_t>(VariationType::Sec)] = amount(generator) * 0.16;
            layer.variations[static_cast<std::size_t>(VariationType::Blob)] = amount(generator) * 0.15;
        }

        for (std::size_t variation = 0; variation < kVariationCount; ++variation) {
            if (layer.variations[variation] == 0.0 && amount(generator) > 1.46) {
                layer.variations[variation] = amount(generator) * 0.28;
            }
        }
        scene.transforms.push_back(layer);
    }
    const int pathCount = 1 + static_cast<int>(seed % 3);
    scene.paths.clear();
    scene.paths.reserve(static_cast<std::size_t>(pathCount));
    for (int index = 0; index < pathCount; ++index) {
        scene.paths.push_back(RandomProceduralPath(generator, index, seed));
    }
    scene.selectedPath = 0;
    scene.previewIterations = 260000 + (seed % 7) * 140000;
    return scene;
}

ScenePose CaptureScenePose(const Scene& scene) {
    ScenePose pose;
    pose.mode = scene.mode;
    pose.camera = scene.camera;
    pose.flameRender = scene.flameRender;
    pose.depthOfField = scene.depthOfField;
    pose.denoiser = scene.denoiser;
    pose.transforms = scene.transforms;
    pose.paths = scene.paths;
    pose.gradientStops = scene.gradientStops;
    pose.backgroundColor = scene.backgroundColor;
    pose.gridVisible = scene.gridVisible;
    return pose;
}

void ApplyScenePose(Scene& scene, const ScenePose& pose) {
    std::vector<bool> transformVisibility;
    transformVisibility.reserve(scene.transforms.size());
    for (const TransformLayer& layer : scene.transforms) {
        transformVisibility.push_back(layer.visible);
    }
    std::vector<bool> pathVisibility;
    pathVisibility.reserve(scene.paths.size());
    for (const PathSettings& path : scene.paths) {
        pathVisibility.push_back(path.visible);
    }

    scene.mode = pose.mode;
    scene.camera = pose.camera;
    scene.flameRender = pose.flameRender;
    scene.depthOfField = pose.depthOfField;
    scene.denoiser = pose.denoiser;
    scene.transforms = pose.transforms;
    scene.paths = pose.paths;
    scene.gradientStops = pose.gradientStops;
    scene.backgroundColor = pose.backgroundColor;
    scene.gridVisible = pose.gridVisible;
    for (std::size_t index = 0; index < std::min(scene.transforms.size(), transformVisibility.size()); ++index) {
        scene.transforms[index].visible = transformVisibility[index];
    }
    for (std::size_t index = 0; index < std::min(scene.paths.size(), pathVisibility.size()); ++index) {
        scene.paths[index].visible = pathVisibility[index];
    }
    scene.selectedTransform = std::clamp(scene.selectedTransform, 0, static_cast<int>(scene.transforms.size()) - 1);
    scene.selectedPath = std::clamp(scene.selectedPath, 0, static_cast<int>(scene.paths.size()) - 1);
}

void SortKeyframes(Scene& scene) {
    std::sort(scene.keyframes.begin(), scene.keyframes.end(), [](const SceneKeyframe& left, const SceneKeyframe& right) {
        return left.frame < right.frame;
    });
}

int FindKeyframeIndex(const Scene& scene, const int frame) {
    for (std::size_t index = scene.keyframes.size(); index-- > 0;) {
        if (scene.keyframes[index].frame == frame) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int FindKeyframeIndex(const Scene& scene, const int frame, const KeyframeOwnerType ownerType, const int ownerIndex) {
    for (std::size_t index = scene.keyframes.size(); index-- > 0;) {
        const SceneKeyframe& keyframe = scene.keyframes[index];
        if (keyframe.frame == frame && keyframe.ownerType == ownerType && keyframe.ownerIndex == ownerIndex) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

double TimelineSecondsForFrame(const Scene& scene, const double frame) {
    return frame / std::max(1.0, scene.timelineFrameRate);
}

namespace {

Vec3 LerpVec3(const Vec3& start, const Vec3& end, const double alpha) {
    return {
        Lerp(start.x, end.x, alpha),
        Lerp(start.y, end.y, alpha),
        Lerp(start.z, end.z, alpha)
    };
}

int LerpInt(const int start, const int end, const double alpha) {
    return static_cast<int>(std::round(Lerp(static_cast<double>(start), static_cast<double>(end), alpha)));
}

double CubicBezier1D(const double p0, const double p1, const double p2, const double p3, const double t) {
    const double inverse = 1.0 - t;
    return inverse * inverse * inverse * p0
        + 3.0 * inverse * inverse * t * p1
        + 3.0 * inverse * t * t * p2
        + t * t * t * p3;
}

double EvaluateKeyframeEasing(const SceneKeyframe& keyframe, const double alpha) {
    const double clamped = Clamp(alpha, 0.0, 1.0);
    if (keyframe.easing == KeyframeEasing::Hold) {
        return clamped >= 1.0 ? 1.0 : 0.0;
    }
    if (keyframe.easing == KeyframeEasing::Linear) {
        return clamped;
    }

    double lower = 0.0;
    double upper = 1.0;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const double middle = (lower + upper) * 0.5;
        const double x = CubicBezier1D(0.0, keyframe.easeX1, keyframe.easeX2, 1.0, middle);
        if (x < clamped) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    return CubicBezier1D(0.0, keyframe.easeY1, keyframe.easeY2, 1.0, (lower + upper) * 0.5);
}

bool CanInterpolate(const ScenePose& left, const ScenePose& right) {
    if (left.transforms.size() != right.transforms.size()
        || left.paths.size() != right.paths.size()
        || left.gradientStops.size() != right.gradientStops.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.paths.size(); ++index) {
        if (left.paths[index].controlPoints.size() != right.paths[index].controlPoints.size()) {
            return false;
        }
    }
    return true;
}

ScenePose InterpolatePose(const ScenePose& left, const ScenePose& right, const double alpha) {
    if (!CanInterpolate(left, right)) {
        return alpha < 0.5 ? left : right;
    }

    ScenePose pose;
    pose.mode = alpha < 0.5 ? left.mode : right.mode;
    pose.camera.yaw = Lerp(left.camera.yaw, right.camera.yaw, alpha);
    pose.camera.pitch = Lerp(left.camera.pitch, right.camera.pitch, alpha);
    pose.camera.distance = Lerp(left.camera.distance, right.camera.distance, alpha);
    pose.camera.panX = Lerp(left.camera.panX, right.camera.panX, alpha);
    pose.camera.panY = Lerp(left.camera.panY, right.camera.panY, alpha);
    pose.camera.zoom2D = Lerp(left.camera.zoom2D, right.camera.zoom2D, alpha);
    pose.camera.frameWidth = Lerp(left.camera.frameWidth, right.camera.frameWidth, alpha);
    pose.camera.frameHeight = Lerp(left.camera.frameHeight, right.camera.frameHeight, alpha);
    pose.flameRender.rotationXDegrees = Lerp(left.flameRender.rotationXDegrees, right.flameRender.rotationXDegrees, alpha);
    pose.flameRender.rotationYDegrees = Lerp(left.flameRender.rotationYDegrees, right.flameRender.rotationYDegrees, alpha);
    pose.flameRender.rotationZDegrees = Lerp(left.flameRender.rotationZDegrees, right.flameRender.rotationZDegrees, alpha);
    pose.flameRender.depthAmount = Lerp(left.flameRender.depthAmount, right.flameRender.depthAmount, alpha);
    pose.depthOfField.enabled = alpha < 0.5 ? left.depthOfField.enabled : right.depthOfField.enabled;
    pose.depthOfField.focusDepth = Lerp(left.depthOfField.focusDepth, right.depthOfField.focusDepth, alpha);
    pose.depthOfField.focusRange = Lerp(left.depthOfField.focusRange, right.depthOfField.focusRange, alpha);
    pose.depthOfField.blurStrength = Lerp(left.depthOfField.blurStrength, right.depthOfField.blurStrength, alpha);
    pose.denoiser.enabled = alpha < 0.5 ? left.denoiser.enabled : right.denoiser.enabled;
    pose.denoiser.strength = Lerp(left.denoiser.strength, right.denoiser.strength, alpha);
    pose.backgroundColor = Lerp(left.backgroundColor, right.backgroundColor, alpha);
    pose.gridVisible = alpha < 0.5 ? left.gridVisible : right.gridVisible;

    pose.gradientStops.resize(left.gradientStops.size());
    for (std::size_t index = 0; index < left.gradientStops.size(); ++index) {
        pose.gradientStops[index].position = Lerp(left.gradientStops[index].position, right.gradientStops[index].position, alpha);
        pose.gradientStops[index].color = Lerp(left.gradientStops[index].color, right.gradientStops[index].color, alpha);
    }

    pose.transforms.resize(left.transforms.size());
    for (std::size_t index = 0; index < left.transforms.size(); ++index) {
        const TransformLayer& a = left.transforms[index];
        const TransformLayer& b = right.transforms[index];
        TransformLayer layer = alpha < 0.5 ? a : b;
        layer.weight = Lerp(a.weight, b.weight, alpha);
        layer.rotationDegrees = Lerp(a.rotationDegrees, b.rotationDegrees, alpha);
        layer.scaleX = Lerp(a.scaleX, b.scaleX, alpha);
        layer.scaleY = Lerp(a.scaleY, b.scaleY, alpha);
        layer.translateX = Lerp(a.translateX, b.translateX, alpha);
        layer.translateY = Lerp(a.translateY, b.translateY, alpha);
        layer.shearX = Lerp(a.shearX, b.shearX, alpha);
        layer.shearY = Lerp(a.shearY, b.shearY, alpha);
        layer.colorIndex = Lerp(a.colorIndex, b.colorIndex, alpha);
        layer.useCustomColor = alpha < 0.5 ? a.useCustomColor : b.useCustomColor;
        layer.customColor = Lerp(a.customColor, b.customColor, alpha);
        for (std::size_t variation = 0; variation < kVariationCount; ++variation) {
            layer.variations[variation] = Lerp(a.variations[variation], b.variations[variation], alpha);
        }
        pose.transforms[index] = layer;
    }

    pose.paths.resize(left.paths.size());
    for (std::size_t index = 0; index < left.paths.size(); ++index) {
        const PathSettings& a = left.paths[index];
        const PathSettings& b = right.paths[index];
        PathSettings path = alpha < 0.5 ? a : b;
        path.closed = alpha < 0.5 ? a.closed : b.closed;
        path.thickness = Lerp(a.thickness, b.thickness, alpha);
        path.taper = Lerp(a.taper, b.taper, alpha);
        path.twist = Lerp(a.twist, b.twist, alpha);
        path.repeatCount = LerpInt(a.repeatCount, b.repeatCount, alpha);
        path.sampleCount = LerpInt(a.sampleCount, b.sampleCount, alpha);
        path.segment.mode = alpha < 0.5 ? a.segment.mode : b.segment.mode;
        path.segment.segments = LerpInt(a.segment.segments, b.segment.segments, alpha);
        path.segment.sides = LerpInt(a.segment.sides, b.segment.sides, alpha);
        path.segment.breakSides = alpha < 0.5 ? a.segment.breakSides : b.segment.breakSides;
        path.segment.chamfer = alpha < 0.5 ? a.segment.chamfer : b.segment.chamfer;
        path.segment.chamferSize = Lerp(a.segment.chamferSize, b.segment.chamferSize, alpha);
        path.segment.caps = alpha < 0.5 ? a.segment.caps : b.segment.caps;
        path.segment.size = Lerp(a.segment.size, b.segment.size, alpha);
        path.segment.sizeX = Lerp(a.segment.sizeX, b.segment.sizeX, alpha);
        path.segment.sizeY = Lerp(a.segment.sizeY, b.segment.sizeY, alpha);
        path.segment.sizeZ = Lerp(a.segment.sizeZ, b.segment.sizeZ, alpha);
        path.segment.orientToPath = alpha < 0.5 ? a.segment.orientToPath : b.segment.orientToPath;
        path.segment.orientReferenceAxis = alpha < 0.5 ? a.segment.orientReferenceAxis : b.segment.orientReferenceAxis;
        path.segment.rotateX = Lerp(a.segment.rotateX, b.segment.rotateX, alpha);
        path.segment.rotateY = Lerp(a.segment.rotateY, b.segment.rotateY, alpha);
        path.segment.rotateZ = Lerp(a.segment.rotateZ, b.segment.rotateZ, alpha);
        path.segment.twistZ = Lerp(a.segment.twistZ, b.segment.twistZ, alpha);
        path.segment.debugNormals = alpha < 0.5 ? a.segment.debugNormals : b.segment.debugNormals;
        path.segment.tessellate = alpha < 0.5 ? a.segment.tessellate : b.segment.tessellate;
        path.segment.randomness = Lerp(a.segment.randomness, b.segment.randomness, alpha);
        // Procedural additions
        path.segment.thicknessProfile = alpha < 0.5 ? a.segment.thicknessProfile : b.segment.thicknessProfile;
        path.segment.thicknessPulseFrequency = Lerp(a.segment.thicknessPulseFrequency, b.segment.thicknessPulseFrequency, alpha);
        path.segment.thicknessPulseDepth = Lerp(a.segment.thicknessPulseDepth, b.segment.thicknessPulseDepth, alpha);
        path.segment.thicknessBlobCenter = Lerp(a.segment.thicknessBlobCenter, b.segment.thicknessBlobCenter, alpha);
        path.segment.thicknessBlobWidth = Lerp(a.segment.thicknessBlobWidth, b.segment.thicknessBlobWidth, alpha);
        path.segment.junctionSize = Lerp(a.segment.junctionSize, b.segment.junctionSize, alpha);
        path.segment.junctionBlend = Lerp(a.segment.junctionBlend, b.segment.junctionBlend, alpha);
        path.segment.tubeWarp = Lerp(a.segment.tubeWarp, b.segment.tubeWarp, alpha);
        path.segment.tubeWarpFrequency = Lerp(a.segment.tubeWarpFrequency, b.segment.tubeWarpFrequency, alpha);
        path.segment.tendrilCount = LerpInt(a.segment.tendrilCount, b.segment.tendrilCount, alpha);
        path.segment.tendrilLength = Lerp(a.segment.tendrilLength, b.segment.tendrilLength, alpha);
        path.segment.tendrilThickness = Lerp(a.segment.tendrilThickness, b.segment.tendrilThickness, alpha);
        path.segment.tendrilWarp = Lerp(a.segment.tendrilWarp, b.segment.tendrilWarp, alpha);
        path.layout = alpha < 0.5 ? a.layout : b.layout;
        path.layoutRadius = Lerp(a.layoutRadius, b.layoutRadius, alpha);
        path.layoutNodes = LerpInt(a.layoutNodes, b.layoutNodes, alpha);
        path.layoutRandomness = Lerp(a.layoutRandomness, b.layoutRandomness, alpha);
        path.fractalDisplacement.fractalType = alpha < 0.5 ? a.fractalDisplacement.fractalType : b.fractalDisplacement.fractalType;
        path.fractalDisplacement.space = alpha < 0.5 ? a.fractalDisplacement.space : b.fractalDisplacement.space;
        path.fractalDisplacement.amplitude = Lerp(a.fractalDisplacement.amplitude, b.fractalDisplacement.amplitude, alpha);
        path.fractalDisplacement.frequency = Lerp(a.fractalDisplacement.frequency, b.fractalDisplacement.frequency, alpha);
        path.fractalDisplacement.evolution = Lerp(a.fractalDisplacement.evolution, b.fractalDisplacement.evolution, alpha);
        path.fractalDisplacement.offsetX = Lerp(a.fractalDisplacement.offsetX, b.fractalDisplacement.offsetX, alpha);
        path.fractalDisplacement.offsetY = Lerp(a.fractalDisplacement.offsetY, b.fractalDisplacement.offsetY, alpha);
        path.fractalDisplacement.offsetZ = Lerp(a.fractalDisplacement.offsetZ, b.fractalDisplacement.offsetZ, alpha);
        path.fractalDisplacement.complexity = LerpInt(a.fractalDisplacement.complexity, b.fractalDisplacement.complexity, alpha);
        path.fractalDisplacement.octScale = Lerp(a.fractalDisplacement.octScale, b.fractalDisplacement.octScale, alpha);
        path.fractalDisplacement.octMult = Lerp(a.fractalDisplacement.octMult, b.fractalDisplacement.octMult, alpha);
        path.fractalDisplacement.smoothenNormals = Lerp(a.fractalDisplacement.smoothenNormals, b.fractalDisplacement.smoothenNormals, alpha);
        path.fractalDisplacement.seamlessLoop = alpha < 0.5 ? a.fractalDisplacement.seamlessLoop : b.fractalDisplacement.seamlessLoop;
        path.material.renderMode = alpha < 0.5 ? a.material.renderMode : b.material.renderMode;
        path.material.materialType = alpha < 0.5 ? a.material.materialType : b.material.materialType;
        path.material.primaryColor = Lerp(a.material.primaryColor, b.material.primaryColor, alpha);
        path.material.accentColor = Lerp(a.material.accentColor, b.material.accentColor, alpha);
        path.material.wireColor = Lerp(a.material.wireColor, b.material.wireColor, alpha);
        path.material.pointSize = Lerp(a.material.pointSize, b.material.pointSize, alpha);
        path.controlPoints.resize(a.controlPoints.size());
        for (std::size_t pointIndex = 0; pointIndex < a.controlPoints.size(); ++pointIndex) {
            path.controlPoints[pointIndex] = LerpVec3(a.controlPoints[pointIndex], b.controlPoints[pointIndex], alpha);
        }
        pose.paths[index] = path;
    }

    return pose;
}

}  // namespace

void ApplyKeyframeEasingPreset(SceneKeyframe& keyframe, const KeyframeEasing easing) {
    keyframe.easing = easing;
    switch (easing) {
    case KeyframeEasing::EaseIn:
        keyframe.easeX1 = 0.42;
        keyframe.easeY1 = 0.00;
        keyframe.easeX2 = 1.00;
        keyframe.easeY2 = 1.00;
        break;
    case KeyframeEasing::EaseOut:
        keyframe.easeX1 = 0.00;
        keyframe.easeY1 = 0.00;
        keyframe.easeX2 = 0.58;
        keyframe.easeY2 = 1.00;
        break;
    case KeyframeEasing::EaseInOut:
        keyframe.easeX1 = 0.42;
        keyframe.easeY1 = 0.00;
        keyframe.easeX2 = 0.58;
        keyframe.easeY2 = 1.00;
        break;
    case KeyframeEasing::Hold:
        keyframe.easeX1 = 0.00;
        keyframe.easeY1 = 0.00;
        keyframe.easeX2 = 1.00;
        keyframe.easeY2 = 1.00;
        break;
    case KeyframeEasing::Custom:
        break;
    case KeyframeEasing::Linear:
    default:
        keyframe.easeX1 = 0.00;
        keyframe.easeY1 = 0.00;
        keyframe.easeX2 = 1.00;
        keyframe.easeY2 = 1.00;
        break;
    }
}

Scene EvaluateSceneAtFrame(const Scene& scene, const double frame) {
    Scene evaluated = scene;
    const double clampedFrame = Clamp(frame, static_cast<double>(scene.timelineStartFrame), static_cast<double>(std::max(scene.timelineStartFrame, scene.timelineEndFrame)));
    evaluated.timelineFrame = clampedFrame;
    evaluated.timelineSeconds = TimelineSecondsForFrame(scene, clampedFrame);
    if (scene.keyframes.empty()) {
        return evaluated;
    }

    int previousIndex = -1;
    int nextIndex = -1;
    for (std::size_t index = 0; index < scene.keyframes.size(); ++index) {
        if (scene.keyframes[index].frame <= clampedFrame) {
            previousIndex = static_cast<int>(index);
        }
        if (scene.keyframes[index].frame >= clampedFrame) {
            nextIndex = static_cast<int>(index);
            while (nextIndex + 1 < static_cast<int>(scene.keyframes.size()) && scene.keyframes[static_cast<std::size_t>(nextIndex + 1)].frame == scene.keyframes[index].frame) {
                ++nextIndex;
            }
            if (scene.keyframes[index].frame == clampedFrame) {
                previousIndex = nextIndex;
            }
            break;
        }
    }
    if (previousIndex < 0) {
        previousIndex = 0;
    }
    if (nextIndex < 0) {
        nextIndex = static_cast<int>(scene.keyframes.size()) - 1;
    }

    if (previousIndex == nextIndex) {
        ApplyScenePose(evaluated, scene.keyframes[static_cast<std::size_t>(previousIndex)].pose);
        evaluated.timelineFrame = clampedFrame;
        evaluated.timelineSeconds = TimelineSecondsForFrame(scene, clampedFrame);
        evaluated.keyframes = scene.keyframes;
        return evaluated;
    }

    const SceneKeyframe& previous = scene.keyframes[static_cast<std::size_t>(previousIndex)];
    const SceneKeyframe& next = scene.keyframes[static_cast<std::size_t>(nextIndex)];
    const double span = std::max(1.0, static_cast<double>(next.frame - previous.frame));
    const double alpha = EvaluateKeyframeEasing(previous, (clampedFrame - static_cast<double>(previous.frame)) / span);
    ApplyScenePose(evaluated, InterpolatePose(previous.pose, next.pose, alpha));
    evaluated.timelineFrame = clampedFrame;
    evaluated.timelineSeconds = TimelineSecondsForFrame(scene, clampedFrame);
    evaluated.keyframes = scene.keyframes;
    return evaluated;
}

namespace {

template <typename TEnum, std::size_t N>
std::string EnumToString(const TEnum value, const std::pair<TEnum, const char*> (&table)[N], const char* fallback) {
    for (const auto& [enumValue, name] : table) {
        if (enumValue == value) {
            return name;
        }
    }
    return fallback;
}

template <typename TEnum, std::size_t N>
TEnum EnumFromString(const std::string& value, const std::pair<TEnum, const char*> (&table)[N], const TEnum fallback) {
    for (const auto& [enumValue, name] : table) {
        if (value == name) {
            return enumValue;
        }
    }
    return fallback;
}

constexpr std::pair<SceneMode, const char*> kSceneModeNames[] = {
    {SceneMode::Flame, "flame"},
    {SceneMode::Path, "path"},
    {SceneMode::Hybrid, "hybrid"}
};

constexpr std::pair<SegmentMode, const char*> kSegmentModeNames[] = {
    {SegmentMode::ExtrudeNGon, "extrude_n_gon"},
    {SegmentMode::RepeatNGon, "repeat_n_gon"},
    {SegmentMode::RepeatSphere, "repeat_sphere"}
};

constexpr std::pair<PathAxis, const char*> kPathAxisNames[] = {
    {PathAxis::X, "x"},
    {PathAxis::Y, "y"},
    {PathAxis::Z, "z"}
};

constexpr std::pair<TessellationMode, const char*> kTessellationModeNames[] = {
    {TessellationMode::Triangles, "triangles"},
    {TessellationMode::Lines, "lines"}
};

constexpr std::pair<FractalType, const char*> kFractalTypeNames[] = {
    {FractalType::Regular, "regular"},
    {FractalType::Turbulent, "turbulent"}
};

constexpr std::pair<FractalSpace, const char*> kFractalSpaceNames[] = {
    {FractalSpace::World, "world"},
    {FractalSpace::Local, "local"}
};

constexpr std::pair<PathRenderMode, const char*> kPathRenderModeNames[] = {
    {PathRenderMode::Solid, "solid"},
    {PathRenderMode::SolidWire, "solid_wire"},
    {PathRenderMode::Wireframe, "wireframe"},
    {PathRenderMode::Points, "points"}
};

constexpr std::pair<MaterialType, const char*> kMaterialTypeNames[] = {
    {MaterialType::Metallic, "metallic"},
    {MaterialType::Flat, "flat"},
    {MaterialType::Matte, "matte"},
    {MaterialType::Glossy, "glossy"}
};

constexpr std::pair<KeyframeEasing, const char*> kKeyframeEasingNames[] = {
    {KeyframeEasing::Linear, "linear"},
    {KeyframeEasing::EaseIn, "ease_in"},
    {KeyframeEasing::EaseOut, "ease_out"},
    {KeyframeEasing::EaseInOut, "ease_in_out"},
    {KeyframeEasing::Hold, "hold"},
    {KeyframeEasing::Custom, "custom"}
};

constexpr std::pair<VariationType, const char*> kVariationTypeNames[] = {
    {VariationType::Linear, "linear"},
    {VariationType::Sinusoidal, "sinusoidal"},
    {VariationType::Spherical, "spherical"},
    {VariationType::Swirl, "swirl"},
    {VariationType::Horseshoe, "horseshoe"},
    {VariationType::Polar, "polar"},
    {VariationType::Heart, "heart"},
    {VariationType::Disc, "disc"},
    {VariationType::Spiral, "spiral"},
    {VariationType::Julia, "julia"},
    {VariationType::Bubble, "bubble"},
    {VariationType::Eyefish, "eyefish"},
    {VariationType::Cylinder, "cylinder"},
    {VariationType::Blur, "blur"},
    {VariationType::Ngon, "ngon"},
    {VariationType::Curl, "curl"},
    {VariationType::Arch, "arch"},
    {VariationType::Tangent, "tangent"},
    {VariationType::Rays, "rays"},
    {VariationType::Cross, "cross"},
    {VariationType::Bent, "bent"},
    {VariationType::Waves, "waves"},
    {VariationType::Fan, "fan"},
    {VariationType::Rings, "rings"},
    {VariationType::Popcorn, "popcorn"},
    {VariationType::Bipolar, "bipolar"},
    {VariationType::Wedge, "wedge"},
    {VariationType::Split, "split"},
    {VariationType::Fisheye, "fisheye"},
    {VariationType::Handkerchief, "handkerchief"},
    {VariationType::Ex, "ex"},
    {VariationType::Blade, "blade"},
    {VariationType::Flower, "flower"},
    {VariationType::Cosine, "cosine"},
    {VariationType::Fold, "fold"},
    {VariationType::Checkers, "checkers"},
    {VariationType::Hyperbolic, "hyperbolic"},
    {VariationType::Diamond, "diamond"},
    {VariationType::Exponential, "exponential"},
    {VariationType::Power, "power"},
    {VariationType::Sec, "sec"},
    {VariationType::Csc, "csc"},
    {VariationType::Cot, "cot"},
    {VariationType::Sech, "sech"},
    {VariationType::Perspective, "perspective"},
    {VariationType::Blob, "blob"},
    {VariationType::PDJ, "pdj"},
    {VariationType::Fan2, "fan2"},
    {VariationType::Rings2, "rings2"},
    {VariationType::TwinTrian, "twintrian"}
};

constexpr std::pair<ThicknessProfile, const char*> kThicknessProfileNames[] = {
    {ThicknessProfile::Linear, "linear"},
    {ThicknessProfile::Pulse, "pulse"},
    {ThicknessProfile::Bezier, "bezier"},
    {ThicknessProfile::Blobby, "blobby"}
};

constexpr std::pair<PathLayout, const char*> kPathLayoutNames[] = {
    {PathLayout::UserDefined, "user_defined"},
    {PathLayout::RadialCluster, "radial_cluster"},
    {PathLayout::Network, "network"},
    {PathLayout::TendrilBall, "tendril_ball"}
};

}  // namespace

std::string ToString(const SceneMode mode) { return EnumToString(mode, kSceneModeNames, "hybrid"); }
SceneMode SceneModeFromString(const std::string& value) { return EnumFromString(value, kSceneModeNames, SceneMode::Hybrid); }

std::string ToString(const SegmentMode mode) { return EnumToString(mode, kSegmentModeNames, "repeat_sphere"); }
SegmentMode SegmentModeFromString(const std::string& value) {
    if (value == "repeat_shape") { return SegmentMode::RepeatSphere; }
    return EnumFromString(value, kSegmentModeNames, SegmentMode::RepeatSphere);
}

std::string ToString(const PathAxis axis) { return EnumToString(axis, kPathAxisNames, "z"); }
PathAxis PathAxisFromString(const std::string& value) { return EnumFromString(value, kPathAxisNames, PathAxis::Z); }

std::string ToString(const TessellationMode mode) { return EnumToString(mode, kTessellationModeNames, "triangles"); }
TessellationMode TessellationModeFromString(const std::string& value) { return EnumFromString(value, kTessellationModeNames, TessellationMode::Triangles); }

std::string ToString(const FractalType type) { return EnumToString(type, kFractalTypeNames, "regular"); }
FractalType FractalTypeFromString(const std::string& value) { return EnumFromString(value, kFractalTypeNames, FractalType::Regular); }

std::string ToString(const FractalSpace space) { return EnumToString(space, kFractalSpaceNames, "world"); }
FractalSpace FractalSpaceFromString(const std::string& value) { return EnumFromString(value, kFractalSpaceNames, FractalSpace::World); }

std::string ToString(const PathRenderMode mode) { return EnumToString(mode, kPathRenderModeNames, "solid_wire"); }
PathRenderMode PathRenderModeFromString(const std::string& value) { return EnumFromString(value, kPathRenderModeNames, PathRenderMode::SolidWire); }

std::string ToString(const MaterialType type) { return EnumToString(type, kMaterialTypeNames, "metallic"); }
MaterialType MaterialTypeFromString(const std::string& value) { return EnumFromString(value, kMaterialTypeNames, MaterialType::Metallic); }

std::string ToString(const KeyframeEasing easing) { return EnumToString(easing, kKeyframeEasingNames, "linear"); }
KeyframeEasing KeyframeEasingFromString(const std::string& value) { return EnumFromString(value, kKeyframeEasingNames, KeyframeEasing::Linear); }

std::string ToString(const VariationType variation) { return EnumToString(variation, kVariationTypeNames, "linear"); }
VariationType VariationTypeFromString(const std::string& value) { return EnumFromString(value, kVariationTypeNames, VariationType::Linear); }

std::string ToString(const ThicknessProfile profile) { return EnumToString(profile, kThicknessProfileNames, "linear"); }
ThicknessProfile ThicknessProfileFromString(const std::string& value) { return EnumFromString(value, kThicknessProfileNames, ThicknessProfile::Linear); }

std::string ToString(const PathLayout layout) { return EnumToString(layout, kPathLayoutNames, "user_defined"); }
PathLayout PathLayoutFromString(const std::string& value) { return EnumFromString(value, kPathLayoutNames, PathLayout::UserDefined); }

}  // namespace radiary
