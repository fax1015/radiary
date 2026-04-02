#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.h"

namespace radiary {

enum class SceneMode {
    Flame,
    Path,
    Hybrid
};

enum class SegmentMode : std::uint8_t {
    ExtrudeNGon,
    RepeatNGon,
    RepeatSphere
};

enum class PathAxis : std::uint8_t {
    X,
    Y,
    Z
};

enum class TessellationMode : std::uint8_t {
    Triangles,
    Lines
};

enum class ThicknessProfile : std::uint8_t {
    Linear,
    Pulse,
    Bezier,
    Blobby
};

enum class PathLayout : std::uint8_t {
    UserDefined,
    RadialCluster,
    Network,
    TendrilBall
};

enum class FractalType : std::uint8_t {
    Regular,
    Turbulent
};

enum class FractalSpace : std::uint8_t {
    World,
    Local
};

enum class PathRenderMode : std::uint8_t {
    Solid,
    SolidWire,
    Wireframe,
    Points
};

enum class MaterialType : std::uint8_t {
    Metallic,
    Flat,
    Matte,
    Glossy
};

enum class KeyframeEasing : std::uint8_t {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Hold,
    Custom
};

enum class KeyframeOwnerType : std::uint8_t {
    Transform,
    Path,
    Scene,
    Effect
};

enum class SymmetryMode : std::uint8_t {
    None,
    Bilateral,
    Rotational,
    BilateralRotational
};

enum class EffectStackStage : std::uint8_t {
    Denoiser,
    DepthOfField,
    Curves,
    Sharpen,
    HueShift,
    PostProcess,
    ChromaticAberration,
    ColorTemperature,
    Saturation,
    ToneMapping,
    FilmGrain,
    Vignette
};

constexpr std::size_t kEffectStackStageCount = 12;
using EffectStackOrder = std::vector<EffectStackStage>;
inline constexpr std::array<EffectStackStage, kEffectStackStageCount> kAllEffectStages = {
    EffectStackStage::Denoiser,
    EffectStackStage::DepthOfField,
    EffectStackStage::Curves,
    EffectStackStage::Sharpen,
    EffectStackStage::HueShift,
    EffectStackStage::PostProcess,
    EffectStackStage::ChromaticAberration,
    EffectStackStage::ColorTemperature,
    EffectStackStage::Saturation,
    EffectStackStage::ToneMapping,
    EffectStackStage::FilmGrain,
    EffectStackStage::Vignette
};

enum class VariationType : std::uint8_t {
    Linear,
    Sinusoidal,
    Spherical,
    Swirl,
    Horseshoe,
    Polar,
    Heart,
    Disc,
    Spiral,
    Julia,
    Bubble,
    Eyefish,
    Cylinder,
    Blur,
    Ngon,
    Curl,
    Arch,
    Tangent,
    Rays,
    Cross,
    Bent,
    Waves,
    Fan,
    Rings,
    Popcorn,
    Bipolar,
    Wedge,
    Split,
    Fisheye,
    Handkerchief,
    Ex,
    Blade,
    Flower,
    Cosine,
    Fold,
    Checkers,
    Hyperbolic,
    Diamond,
    Exponential,
    Power,
    Sec,
    Csc,
    Cot,
    Sech,
    Perspective,
    Blob,
    PDJ,
    Fan2,
    Rings2,
    TwinTrian,
    Mobius,
    Supershape,
    Conic,
    Astroid,
    Lissajous,
    Vortex,
    Kaleidoscope,
    Droste,
    GoldenSpiral,
    Interference,
    Count
};

constexpr std::size_t kVariationCount = static_cast<std::size_t>(VariationType::Count);

struct CameraState {
    double yaw = 0.35;
    double pitch = -0.55;
    double distance = 8.0;
    double panX = 0.0;
    double panY = 0.0;
    double zoom2D = 1.0;
    double frameWidth = 16.0;
    double frameHeight = 9.0;
};

struct GradientStop {
    double position = 0.0;
    Color color {};
};

struct FlameRenderSettings {
    double rotationXDegrees = 0.0;
    double rotationYDegrees = 0.0;
    double rotationZDegrees = 0.0;
    double depthAmount = 1.0;
    SymmetryMode symmetry = SymmetryMode::None;
    int symmetryOrder = 2;
    double curveExposure = 1.0;
    double curveContrast = 1.0;
    double curveHighlights = 1.0;
    double curveGamma = 1.0;
};

struct DepthOfFieldSettings {
    bool enabled = false;
    double focusDepth = 0.45;
    double focusRange = 0.10;
    double blurStrength = 0.45;
};

struct DenoiserSettings {
    bool enabled = false;
    double strength = 0.5;
};

struct PostProcessSettings {
    bool enabled = false;
    double bloomIntensity = 0.35;
    double bloomRadius = 0.8;
    double bloomThreshold = 0.6;
    bool curvesEnabled = false;
    double curveBlackPoint = 0.0;
    double curveWhitePoint = 1.0;
    double curveGamma = 1.0;
    bool curveUseCustom = false;
    std::vector<Vec2> curveControlPoints;
    bool sharpenEnabled = false;
    double sharpenAmount = 0.0;
    bool hueShiftEnabled = false;
    double hueShiftDegrees = 0.0;
    double hueShiftSaturation = 1.0;
    bool chromaticAberrationEnabled = false;
    double chromaticAberration = 0.0;
    bool vignetteEnabled = false;
    double vignetteIntensity = 0.0;
    double vignetteRoundness = 0.5;
    bool toneMappingEnabled = false;
    bool acesToneMap = false;
    bool filmGrainEnabled = false;
    double filmGrain = 0.0;
    double filmGrainScale = 1.0;
    bool colorTemperatureEnabled = false;
    double colorTemperature = 6500.0;
    bool saturationEnabled = false;
    double saturationBoost = 0.0;
    double saturationVibrance = 0.0;
};

struct TransformLayer {
    std::string name;
    bool visible = true;
    double weight = 1.0;
    double rotationDegrees = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double translateX = 0.0;
    double translateY = 0.0;
    double shearX = 0.0;
    double shearY = 0.0;
    double colorIndex = 0.5;
    bool useCustomColor = false;
    Color customColor {255, 196, 128, 255};
    std::array<double, kVariationCount> variations {};
};

struct SegmentSettings {
    SegmentMode mode = SegmentMode::ExtrudeNGon;
    int segments = 16;
    int sides = 3;
    bool breakSides = false;
    bool chamfer = true;
    double chamferSize = 10.0;
    bool caps = true;
    double size = 100.0;
    double sizeX = 100.0;
    double sizeY = 100.0;
    double sizeZ = 100.0;
    bool orientToPath = true;
    PathAxis orientReferenceAxis = PathAxis::Z;
    double rotateX = 0.0;
    double rotateY = 0.0;
    double rotateZ = 0.0;
    double twistZ = 0.0;
    bool debugNormals = false;
    TessellationMode tessellate = TessellationMode::Triangles;
    double randomness = 0.0;
    // Metalheart additions (Phase 1-5)
    ThicknessProfile thicknessProfile = ThicknessProfile::Linear;
    double thicknessPulseFrequency = 1.0;
    double thicknessPulseDepth = 0.0;
    double thicknessBlobCenter = 0.5;
    double thicknessBlobWidth = 0.3;
    // Junction settings (Phase 2)
    double junctionSize = 0.0;
    double junctionBlend = 0.5;
    // Organic displacement (Phase 3)
    double tubeWarp = 0.0;
    double tubeWarpFrequency = 1.0;
    // Tendril settings (Phase 4)
    int tendrilCount = 0;
    double tendrilLength = 1.0;
    double tendrilThickness = 0.5;
    double tendrilWarp = 0.3;
};

struct FractalDisplacementSettings {
    FractalType fractalType = FractalType::Regular;
    FractalSpace space = FractalSpace::World;
    double amplitude = 0.0;
    double frequency = 200.0;
    double evolution = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;
    int complexity = 2;
    double octScale = 2.0;
    double octMult = 0.5;
    double smoothenNormals = 1.0;
    bool seamlessLoop = false;
};

struct MaterialSettings {
    PathRenderMode renderMode = PathRenderMode::SolidWire;
    MaterialType materialType = MaterialType::Metallic;
    Color primaryColor {176, 184, 196, 255};
    Color accentColor {244, 247, 252, 255};
    Color wireColor {214, 220, 228, 255};
    double pointSize = 4.0;
};

struct PathSettings {
    std::string name = "Path Layer 1";
    bool visible = true;
    std::vector<Vec3> controlPoints;
    bool closed = false;
    double thickness = 0.22;
    double taper = 0.15;
    double twist = 0.9;
    int repeatCount = 3;
    int sampleCount = 96;
    SegmentSettings segment {};
    FractalDisplacementSettings fractalDisplacement {};
    MaterialSettings material {};
    // Metalheart additions
    PathLayout layout = PathLayout::UserDefined;
    double layoutRadius = 3.0;
    int layoutNodes = 8;
    double layoutRandomness = 0.2;
};

struct ScenePose {
    SceneMode mode = SceneMode::Hybrid;
    CameraState camera {};
    FlameRenderSettings flameRender {};
    DepthOfFieldSettings depthOfField {};
    DenoiserSettings denoiser {};
    PostProcessSettings postProcess {};
    std::vector<EffectStackStage> effectStack;
    std::vector<TransformLayer> transforms;
    std::vector<PathSettings> paths;
    std::vector<GradientStop> gradientStops;
    Color backgroundColor {10, 10, 13, 255};
    bool gridVisible = true;
};

struct SceneKeyframe {
    int frame = 0;
    Color markerColor {84, 138, 228, 255};
    KeyframeOwnerType ownerType = KeyframeOwnerType::Transform;
    int ownerIndex = 0;
    KeyframeEasing easing = KeyframeEasing::Linear;
    double easeX1 = 0.0;
    double easeY1 = 0.0;
    double easeX2 = 1.0;
    double easeY2 = 1.0;
    ScenePose pose {};
};

struct Scene {
    std::string name;
    SceneMode mode = SceneMode::Hybrid;
    CameraState camera {};
    FlameRenderSettings flameRender {};
    DepthOfFieldSettings depthOfField {};
    DenoiserSettings denoiser {};
    PostProcessSettings postProcess {};
    std::vector<EffectStackStage> effectStack;
    std::vector<TransformLayer> transforms;
    std::vector<PathSettings> paths;
    std::vector<GradientStop> gradientStops;
    Color backgroundColor {10, 10, 13, 255};
    int selectedTransform = 0;
    int selectedPath = 0;
    std::uint32_t previewIterations = 480000;
    double timelineSeconds = 0.0;
    double timelineFrame = 0.0;
    double timelineFrameRate = 24.0;
    int timelineStartFrame = 0;
    int timelineEndFrame = 120;
    bool animatePath = true;
    bool gridVisible = true;
    std::vector<SceneKeyframe> keyframes;
};

std::vector<Color> BuildGradientPalette(const std::vector<GradientStop>& stops, std::size_t count);
bool HasActivePostProcess(const PostProcessSettings& settings);
bool IsEffectStageEnabled(const Scene& scene, EffectStackStage stage);
void NormalizeEffectStackOrder(EffectStackOrder& order);
const char* EffectStageDisplayName(EffectStackStage stage);
void EnableEffectStage(Scene& scene, EffectStackStage stage, bool enabled);
Scene CreateDefaultScene();
Scene CreatePresetScene(const std::string& presetName);
Scene CreateRandomScene(std::uint32_t seed);
ScenePose CaptureScenePose(const Scene& scene);
void ApplyScenePose(Scene& scene, const ScenePose& pose);
Scene EvaluateSceneAtFrame(const Scene& scene, double frame);
void ApplyKeyframeEasingPreset(SceneKeyframe& keyframe, KeyframeEasing easing);
void SortKeyframes(Scene& scene);
int FindKeyframeIndex(const Scene& scene, int frame);
int FindKeyframeIndex(const Scene& scene, int frame, KeyframeOwnerType ownerType, int ownerIndex);
double TimelineSecondsForFrame(const Scene& scene, double frame);
std::string ToString(SceneMode mode);
SceneMode SceneModeFromString(const std::string& value);
std::string ToString(EffectStackStage stage);
EffectStackStage EffectStackStageFromString(const std::string& value);
std::string ToString(SegmentMode mode);
SegmentMode SegmentModeFromString(const std::string& value);
std::string ToString(PathAxis axis);
PathAxis PathAxisFromString(const std::string& value);
std::string ToString(TessellationMode mode);
TessellationMode TessellationModeFromString(const std::string& value);
std::string ToString(FractalType type);
FractalType FractalTypeFromString(const std::string& value);
std::string ToString(FractalSpace space);
FractalSpace FractalSpaceFromString(const std::string& value);
std::string ToString(PathRenderMode mode);
PathRenderMode PathRenderModeFromString(const std::string& value);
std::string ToString(MaterialType type);
MaterialType MaterialTypeFromString(const std::string& value);
std::string ToString(KeyframeEasing easing);
KeyframeEasing KeyframeEasingFromString(const std::string& value);
std::string ToString(SymmetryMode mode);
SymmetryMode SymmetryModeFromString(const std::string& value);
std::string ToString(VariationType variation);
VariationType VariationTypeFromString(const std::string& value);
std::string ToString(ThicknessProfile profile);
ThicknessProfile ThicknessProfileFromString(const std::string& value);
std::string ToString(PathLayout layout);
PathLayout PathLayoutFromString(const std::string& value);

}  // namespace radiary
