cbuffer RenderParams : register(b0)
{
    uint Width;
    uint Height;
    uint PreviewIterations;
    uint TransformCount;
    float Yaw;
    float Pitch;
    float Distance;
    float PanX;
    float PanY;
    float Zoom2D;
    float CameraAspect;
    float FlameRotateX;
    float FlameRotateY;
    float FlameRotateZ;
    float FlameDepthAmount;
    uint SymmetryMode;
    uint SymmetryOrder;
    float FlameCurveExposure;
    float FlameCurveContrast;
    float FlameCurveHighlights;
    float FlameCurveGamma;
    float BackgroundR;
    float BackgroundG;
    float BackgroundB;
    float BackgroundA;
    uint GridVisible;
    uint TotalThreadCount;
    float WorldScale;
    float TotalWeight;
    uint TransparentBackground;
    uint RandomSeedOffset;
    uint PreserveOrbitState;
    float FarDepth;
    float Padding[1];
};

struct TransformGpu
{
    float Weight;
    float CumulativeWeight;
    float RotationRadians;
    float ScaleX;
    float ScaleY;
    float TranslateX;
    float TranslateY;
    float ShearX;
    float ShearY;
    float ColorIndex;
    float UseCustomColor;
    float4 CustomColor;
    float Variations[60];
};

StructuredBuffer<TransformGpu> Transforms : register(t0);
StructuredBuffer<uint4> Palette : register(t1);
RWByteAddressBuffer FlameAccum : register(u0);
struct OrbitState
{
    float3 SamplePoint;
    float ColorIndex;
    uint RngState;
    uint BurnInRemaining;
    uint Initialized;
    uint Padding;
};
RWStructuredBuffer<OrbitState> OrbitStates : register(u1);
RWTexture2D<float4> FlameOutput : register(u2);
RWTexture2D<float> FlameDepthOutput : register(u3);

static const float kPi = 3.14159265358979323846;
static const uint kBurnInIterations = 24u;
static const uint kAccumulationScale = 4u;
static const float kDepthNear = 0.15f;
static const float kOrbitResetRadius = 1000000.0f;
static const float kProjectionReferenceFrameHeight = 720.0f;

uint NextRandom(inout uint state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float RandomFloat(inout uint state)
{
    return (NextRandom(state) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float SafeSignedDenominator(float value)
{
    if (abs(value) >= 1.0e-9)
        return value;
    return (value < 0.0) ? -1.0e-9 : 1.0e-9;
}

bool IsFinitePoint(float2 sample)
{
    return isfinite(sample.x) && isfinite(sample.y);
}

bool IsFinitePoint3(float3 sample)
{
    return isfinite(sample.x) && isfinite(sample.y) && isfinite(sample.z);
}

bool IsOrbitOutOfRange(float2 sample)
{
    return abs(sample.x) > kOrbitResetRadius || abs(sample.y) > kOrbitResetRadius;
}

bool IsOrbitOutOfRange3(float3 sample)
{
    return abs(sample.x) > kOrbitResetRadius || abs(sample.y) > kOrbitResetRadius || abs(sample.z) > kOrbitResetRadius;
}

void ComputeCameraFrame(out float frameMinX, out float frameMinY, out float frameWidth, out float frameHeight, out float frameScale)
{
    frameWidth = (float)Width;
    frameHeight = frameWidth / max(0.001, CameraAspect);
    if (frameHeight > (float)Height)
    {
        frameHeight = (float)Height;
        frameWidth = frameHeight * max(0.001, CameraAspect);
    }
    frameMinX = ((float)Width - frameWidth) * 0.5;
    frameMinY = ((float)Height - frameHeight) * 0.5;
    frameScale = max(1.0, frameHeight) / kProjectionReferenceFrameHeight;
}

float ReconstructionKernel(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return 1.0;
    return (abs(dx) + abs(dy) == 1) ? 0.16 : 0.06;
}

float LocalDensityKernel(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return 1.0;
    return (abs(dx) + abs(dy) == 1) ? 0.58 : 0.32;
}

void LoadAccumulation(int2 pixel, out float densityAccum, out float3 colorAccum, out float depthAccum)
{
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)Width || pixel.y >= (int)Height)
    {
        densityAccum = 0.0;
        colorAccum = float3(0.0, 0.0, 0.0);
        depthAccum = 0.0;
        return;
    }

    uint pixelOffset = ((uint(pixel.y) * Width) + uint(pixel.x)) * 20u;
    densityAccum = (float)FlameAccum.Load(pixelOffset + 0u);
    colorAccum = float3(
        (float)FlameAccum.Load(pixelOffset + 4u),
        (float)FlameAccum.Load(pixelOffset + 8u),
        (float)FlameAccum.Load(pixelOffset + 12u));
    depthAccum = (float)FlameAccum.Load(pixelOffset + 16u);
}

void AccumulateSampleToPixel(int px, int py, uint sampleWeight, uint3 sampleColor, uint sampleDepth)
{
    if (sampleWeight == 0u || px < 0 || py < 0 || px >= (int)Width || py >= (int)Height)
        return;

    uint pixelOffset = ((uint(py) * Width) + uint(px)) * 20u;
    uint ignored = 0u;
    FlameAccum.InterlockedAdd(pixelOffset + 0u, sampleWeight, ignored);
    FlameAccum.InterlockedAdd(pixelOffset + 4u, sampleColor.x * sampleWeight, ignored);
    FlameAccum.InterlockedAdd(pixelOffset + 8u, sampleColor.y * sampleWeight, ignored);
    FlameAccum.InterlockedAdd(pixelOffset + 12u, sampleColor.z * sampleWeight, ignored);
    FlameAccum.InterlockedAdd(pixelOffset + 16u, sampleDepth, ignored);
}

float EdgeFavoringScale(float localDensity)
{
    return clamp(1.32 / pow(1.0 + max(0.0, localDensity), 0.38), 0.18, 1.32);
}

float LowDensitySplatBlend(float localDensity)
{
    return saturate((1.45 - log(1.0 + max(0.0, localDensity))) / 1.45) * 0.58;
}

float WideSplatKernel(float distance)
{
    const float supportRadius = 1.35;
    if (distance >= supportRadius)
        return 0.0;
    return pow(1.0 - distance / supportRadius, 1.75);
}

float3 RotateSymmetryPoint(float3 samplePoint, float radians)
{
    float cosine = cos(radians);
    float sine = sin(radians);
    return float3(
        samplePoint.x * cosine - samplePoint.y * sine,
        samplePoint.x * sine + samplePoint.y * cosine,
        samplePoint.z);
}

void EmitProjectedFlameSample(
    float3 samplePoint,
    uint3 sampleColor,
    float yawCos,
    float yawSin,
    float pitchCos,
    float pitchSin,
    float flameCosX,
    float flameSinX,
    float flameCosY,
    float flameSinY,
    float flameCosZ,
    float flameSinZ,
    float frameMinX,
    float frameMinY,
    float frameWidth,
    float frameHeight,
    float frameScale)
{
    float radius = sqrt(samplePoint.x * samplePoint.x + samplePoint.y * samplePoint.y);
    float angle = atan2(samplePoint.y, samplePoint.x);
    float depth = samplePoint.z * WorldScale * 0.72 * max(0.0, FlameDepthAmount);
    float lateralOffset = depth * (0.18 + radius * 0.06);
    float3 world = float3(
        samplePoint.x * WorldScale + cos(angle + samplePoint.z * 0.3) * lateralOffset,
        samplePoint.y * WorldScale + sin(angle - samplePoint.z * 0.25) * lateralOffset * 0.82,
        depth);
    world = float3(world.x, world.y * flameCosX - world.z * flameSinX, world.y * flameSinX + world.z * flameCosX);
    world = float3(world.x * flameCosY + world.z * flameSinY, world.y, -world.x * flameSinY + world.z * flameCosY);
    world = float3(world.x * flameCosZ - world.y * flameSinZ, world.x * flameSinZ + world.y * flameCosZ, world.z);

    float3 rotated = float3(world.x * yawCos + world.z * yawSin, world.y, -world.x * yawSin + world.z * yawCos);
    rotated = float3(rotated.x, rotated.y * pitchCos - rotated.z * pitchSin, rotated.y * pitchSin + rotated.z * pitchCos);
    if (!IsFinitePoint3(rotated))
        return;
    rotated.z += Distance;
    if (rotated.z <= 0.15)
        return;

    float perspective = 240.0 * frameScale * Zoom2D / rotated.z;
    if (!isfinite(perspective))
        return;
    float screenX = frameMinX + frameWidth * 0.5 + PanX * frameScale + rotated.x * perspective;
    float screenY = frameMinY + frameHeight * 0.5 + PanY * frameScale - rotated.y * perspective;
    if (screenX < -1.0 || screenY < -1.0 || screenX > (float)Width || screenY > (float)Height)
        return;

    float sampleWeightReal = clamp(pow(perspective / 30.0, 2.0), 0.35, 6.0);
    float normalizedDepth = saturate((rotated.z - kDepthNear) / max(1.0e-5, FarDepth - kDepthNear));
    int baseX = (int)floor(screenX);
    int baseY = (int)floor(screenY);
    float fracX = screenX - (float)baseX;
    float fracY = screenY - (float)baseY;
    float bilinearWeights[4] = {
        (1.0 - fracX) * (1.0 - fracY),
        fracX * (1.0 - fracY),
        (1.0 - fracX) * fracY,
        fracX * fracY
    };
    int2 offsets[4] = {
        int2(0, 0),
        int2(1, 0),
        int2(0, 1),
        int2(1, 1)
    };

    float sharpenedWeights[4] = {0.0, 0.0, 0.0, 0.0};
    float sharpenedWeightSum = 0.0;
    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        sharpenedWeights[index] = pow(max(0.0, bilinearWeights[index]), 2.2);
        sharpenedWeightSum += sharpenedWeights[index];
    }
    if (sharpenedWeightSum > 1.0e-6)
    {
        [unroll]
        for (uint index = 0u; index < 4u; ++index)
        {
            sharpenedWeights[index] /= sharpenedWeightSum;
        }
    }
    else
    {
        sharpenedWeights[0] = 1.0;
    }

    int centerX = (int)floor(screenX + 0.5);
    int centerY = (int)floor(screenY + 0.5);
    int2 adaptiveOffsets[9] = {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1, 0), int2(0, 0), int2(1, 0),
        int2(-1, 1), int2(0, 1), int2(1, 1)
    };

    float localDensityAccum = 0.0;
    float localDensityKernelSum = 0.0;
    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        float neighborDensityAccum = 0.0;
        float3 ignoredColorAccum = float3(0.0, 0.0, 0.0);
        float ignoredDepthAccum = 0.0;
        int2 offset = adaptiveOffsets[index];
        float kernel = LocalDensityKernel(offset.x, offset.y);
        LoadAccumulation(
            int2(centerX + offset.x, centerY + offset.y),
            neighborDensityAccum,
            ignoredColorAccum,
            ignoredDepthAccum);
        localDensityAccum += neighborDensityAccum * kernel;
        localDensityKernelSum += kernel;
    }

    float localDensity = 0.0;
    if (localDensityKernelSum > 1.0e-6)
        localDensity = (localDensityAccum / localDensityKernelSum) / (float)kAccumulationScale;

    float wideWeights[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    float wideWeightSum = 0.0;
    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        int2 offset = adaptiveOffsets[index];
        int px = centerX + offset.x;
        int py = centerY + offset.y;
        float pixelCenterX = (float)px + 0.5;
        float pixelCenterY = (float)py + 0.5;
        float wx = WideSplatKernel(abs(screenX - pixelCenterX));
        float wy = WideSplatKernel(abs(screenY - pixelCenterY));
        wideWeights[index] = wx * wy;
        wideWeightSum += wideWeights[index];
    }
    if (wideWeightSum > 1.0e-6)
    {
        [unroll]
        for (uint index = 0u; index < 9u; ++index)
        {
            wideWeights[index] /= wideWeightSum;
        }
    }
    else
    {
        wideWeights[4] = 1.0;
    }

    float narrowWeights[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    [unroll]
    for (uint index = 0u; index < 4u; ++index)
    {
        int px = baseX + offsets[index].x;
        int py = baseY + offsets[index].y;
        int gridX = px - (centerX - 1);
        int gridY = py - (centerY - 1);
        if (gridX < 0 || gridX >= 3 || gridY < 0 || gridY >= 3)
            continue;
        narrowWeights[gridY * 3 + gridX] += sharpenedWeights[index];
    }

    float wideBlend = LowDensitySplatBlend(localDensity);
    float finalWeights[9] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    float finalWeightSum = 0.0;
    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        finalWeights[index] = lerp(narrowWeights[index], wideWeights[index], wideBlend);
        finalWeightSum += finalWeights[index];
    }
    if (finalWeightSum > 1.0e-6)
    {
        [unroll]
        for (uint index = 0u; index < 9u; ++index)
        {
            finalWeights[index] /= finalWeightSum;
        }
    }
    else
    {
        finalWeights[4] = 1.0;
    }

    float densityScale = EdgeFavoringScale(localDensity);
    uint sampleWeight = max(1u, (uint)round(sampleWeightReal * densityScale * (float)kAccumulationScale));

    float visibleWeightSum = 0.0;
    uint bestIndex = 4u;
    float bestWeight = -1.0;
    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        int2 offset = adaptiveOffsets[index];
        int px = centerX + offset.x;
        int py = centerY + offset.y;
        if (px < 0 || py < 0 || px >= (int)Width || py >= (int)Height)
            continue;
        visibleWeightSum += finalWeights[index];
        if (finalWeights[index] > bestWeight)
        {
            bestWeight = finalWeights[index];
            bestIndex = index;
        }
    }

    if (visibleWeightSum <= 1.0e-6)
        return;

    uint distributedWeight = 0u;
    uint weights[9] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        int2 offset = adaptiveOffsets[index];
        int px = centerX + offset.x;
        int py = centerY + offset.y;
        if (px < 0 || py < 0 || px >= (int)Width || py >= (int)Height)
            continue;
        float normalizedWeight = finalWeights[index] / visibleWeightSum;
        weights[index] = (uint)floor((float)sampleWeight * normalizedWeight);
        distributedWeight += weights[index];
    }
    weights[bestIndex] += sampleWeight - distributedWeight;

    [unroll]
    for (uint index = 0u; index < 9u; ++index)
    {
        uint localWeight = weights[index];
        uint localDepth = (uint)round(normalizedDepth * 65535.0) * localWeight;
        int2 offset = adaptiveOffsets[index];
        AccumulateSampleToPixel(
            centerX + offset.x,
            centerY + offset.y,
            localWeight,
            sampleColor,
            localDepth);
    }
}

void EmitSymmetrySamples(
    float3 samplePoint,
    uint3 sampleColor,
    float yawCos,
    float yawSin,
    float pitchCos,
    float pitchSin,
    float flameCosX,
    float flameSinX,
    float flameCosY,
    float flameSinY,
    float flameCosZ,
    float flameSinZ,
    float frameMinX,
    float frameMinY,
    float frameWidth,
    float frameHeight,
    float frameScale)
{
    const bool includeBilateral = SymmetryMode == 1u || SymmetryMode == 3u;
    const bool includeRotational = SymmetryMode == 2u || SymmetryMode == 3u;
    const uint order = max(SymmetryOrder, 2u);
    if (!includeRotational)
    {
        EmitProjectedFlameSample(
            samplePoint,
            sampleColor,
            yawCos,
            yawSin,
            pitchCos,
            pitchSin,
            flameCosX,
            flameSinX,
            flameCosY,
            flameSinY,
            flameCosZ,
            flameSinZ,
            frameMinX,
            frameMinY,
            frameWidth,
            frameHeight,
            frameScale);
        if (includeBilateral)
        {
            float3 mirrored = float3(-samplePoint.x, samplePoint.y, samplePoint.z);
            if (abs(mirrored.x - samplePoint.x) > 1.0e-6 || abs(mirrored.y - samplePoint.y) > 1.0e-6)
            {
                EmitProjectedFlameSample(
                    mirrored,
                    sampleColor,
                    yawCos,
                    yawSin,
                    pitchCos,
                    pitchSin,
                    flameCosX,
                    flameSinX,
                    flameCosY,
                    flameSinY,
                    flameCosZ,
                    flameSinZ,
                    frameMinX,
                    frameMinY,
                    frameWidth,
                    frameHeight,
                    frameScale);
            }
        }
        return;
    }

    [loop]
    for (uint index = 0u; index < order; ++index)
    {
        float3 rotatedPoint = index == 0u
            ? samplePoint
            : RotateSymmetryPoint(samplePoint, (2.0 * kPi * (float)index) / (float)order);
        EmitProjectedFlameSample(
            rotatedPoint,
            sampleColor,
            yawCos,
            yawSin,
            pitchCos,
            pitchSin,
            flameCosX,
            flameSinX,
            flameCosY,
            flameSinY,
            flameCosZ,
            flameSinZ,
            frameMinX,
            frameMinY,
            frameWidth,
            frameHeight,
            frameScale);

        if (includeBilateral)
        {
            float3 mirrored = float3(-rotatedPoint.x, rotatedPoint.y, rotatedPoint.z);
            if (abs(mirrored.x - rotatedPoint.x) > 1.0e-6 || abs(mirrored.y - rotatedPoint.y) > 1.0e-6)
            {
                EmitProjectedFlameSample(
                    mirrored,
                    sampleColor,
                    yawCos,
                    yawSin,
                    pitchCos,
                    pitchSin,
                    flameCosX,
                    flameSinX,
                    flameCosY,
                    flameSinY,
                    flameCosZ,
                    flameSinZ,
                    frameMinX,
                    frameMinY,
                    frameWidth,
                    frameHeight,
                    frameScale);
            }
        }
    }
}



float2 ApplyVariation(uint variation, float2 samplePoint)
{
    float radiusSquared = max(1.0e-9, dot(samplePoint, samplePoint));
    float radius = sqrt(radiusSquared);
    float angle = (samplePoint.x == 0.0 && samplePoint.y == 0.0) ? 0.0 : atan2(samplePoint.y, samplePoint.x);

    switch (variation)
    {
    case 0:
        return samplePoint;
    case 1:
        return float2(sin(samplePoint.x), sin(samplePoint.y));
    case 2:
        return float2(samplePoint.x / radiusSquared, samplePoint.y / radiusSquared);
    case 3:
    {
        float sine = sin(radiusSquared);
        float cosine = cos(radiusSquared);
        return float2(samplePoint.x * sine - samplePoint.y * cosine, samplePoint.x * cosine + samplePoint.y * sine);
    }
    case 4:
        return float2(((samplePoint.x - samplePoint.y) * (samplePoint.x + samplePoint.y)) / radius, (2.0 * samplePoint.x * samplePoint.y) / radius);
    case 5:
        return float2(angle / kPi, radius - 1.0);
    case 6:
    {
        float value = angle * radius;
        return float2(radius * sin(value), -radius * cos(value));
    }
    case 7:
    {
        float factor = angle / kPi;
        return float2(factor * sin(kPi * radius), factor * cos(kPi * radius));
    }
    case 8:
        return float2((cos(angle) + sin(radius)) / radius, (sin(angle) - cos(radius)) / radius);
    case 9:
    {
        float omega = (sin(angle * 12.9898) > 0.0) ? 0.0 : kPi;
        float root = sqrt(radius);
        float theta = angle * 0.5 + omega;
        return float2(root * cos(theta), root * sin(theta));
    }
    case 10:
    {
        float factor = 4.0 / (radiusSquared + 4.0);
        return float2(samplePoint.x * factor, samplePoint.y * factor);
    }
    case 11:
    {
        float factor = 2.0 / (radius + 1.0);
        return float2(samplePoint.x * factor, samplePoint.y * factor);
    }
    case 12:
        return float2(sin(samplePoint.x), samplePoint.y);
    case 13:
    {
        float a = angle * kPi;
        float r = radius * (sin(radiusSquared * 3.7) * 0.5 + 0.5);
        return float2(r * cos(a), r * sin(a));
    }
    case 14:
    {
        float power = 3.0;
        float sides = 5.0;
        float corners = 0.5;
        float circle = 1.0;
        float t = angle - sides * floor(angle / sides);
        float k = (t > sides * 0.5) ? (sides - t) : t;
        k = (corners * (1.0 / cos(k) - 1.0) + circle) / max(0.001, pow(radius, power));
        return float2(samplePoint.x * k, samplePoint.y * k);
    }
    case 15:
    {
        float c1 = 0.5;
        float c2 = 0.0;
        float re = 1.0 + c1 * samplePoint.x + c2 * (samplePoint.x * samplePoint.x - samplePoint.y * samplePoint.y);
        float im = c1 * samplePoint.y + 2.0 * c2 * samplePoint.x * samplePoint.y;
        float r2 = 1.0 / max(1.0e-9, re * re + im * im);
        return float2((samplePoint.x * re + samplePoint.y * im) * r2, (samplePoint.y * re - samplePoint.x * im) * r2);
    }
    case 16:
        return float2(sin(angle), sin(angle) * sin(angle) / max(0.001, cos(angle)));
    case 17:
        return float2(sin(samplePoint.x) / max(0.001, cos(samplePoint.y)), tan(samplePoint.y));
    case 18:
    {
        float factor = tan(radius) / max(1.0e-9, radiusSquared);
        return float2(factor * cos(samplePoint.x), factor * sin(samplePoint.y));
    }
    case 19:
    {
        float s = abs(samplePoint.x * samplePoint.x - samplePoint.y * samplePoint.y);
        float factor = sqrt(1.0 / max(1.0e-9, s * s));
        return float2(samplePoint.x * factor, samplePoint.y * factor);
    }

    case 20:
    {
        float nx = (samplePoint.x < 0.0) ? samplePoint.x * 2.0 : samplePoint.x;
        float ny = (samplePoint.y < 0.0) ? samplePoint.y * 0.5 : samplePoint.y;
        return float2(nx, ny);
    }
    case 21:
        return float2(samplePoint.x + sin(samplePoint.y * 2.0), samplePoint.y + sin(samplePoint.x * 2.0));
    case 22:
    {
        float fan = kPi * 0.5;
        float t = angle + fan * floor(angle / fan);
        float r = radius * fan / t;
        return float2(r * cos(t), r * sin(t));
    }
    case 23:
    {
        float rings = 0.5;
        float r = radius - rings * floor(radius / rings);
        float factor = r / max(1.0e-9, radius);
        return float2(samplePoint.x * factor, samplePoint.y * factor);
    }
    case 24:
    {
        float popcorn = 0.3;
        return float2(
            samplePoint.x + popcorn * sin(tan(3.0 * samplePoint.y)),
            samplePoint.y + popcorn * sin(tan(3.0 * samplePoint.x)));
    }
    case 25:
    {
        float bipolar = 0.5;
        float r = sqrt(samplePoint.x * samplePoint.x + samplePoint.y * samplePoint.y);
        float t = atan2(samplePoint.y, samplePoint.x) + bipolar;
        return float2(r * cos(t), r * sin(t));
    }
    case 26:
    {
        float wedge = 0.5;
        float a = angle + wedge * floor(angle / wedge);
        return float2(radius * cos(a), radius * sin(a));
    }
    case 27:
    {
        float split = 0.5;
        return float2(
            samplePoint.x + split * sin(samplePoint.y),
            samplePoint.y + split * sin(samplePoint.x));
    }
    case 28:
    {
        float r = 2.0 / (radius + 1.0);
        return float2(r * samplePoint.y, r * samplePoint.x);
    }
    case 29:
    {
        float theta = angle * radius;
        return float2(radius * sin(angle + theta), radius * cos(angle - theta));
    }
    case 30:
    {
        float d = sqrt(samplePoint.x * samplePoint.x + samplePoint.y * samplePoint.y);
        float ex = d * d * d;
        return float2(ex * cos(angle * d), ex * sin(angle * d));
    }
    case 31:
    {
        float blade = angle * radius;
        return float2(
            samplePoint.x * cos(blade) + sin(blade),
            samplePoint.y * cos(blade) - sin(blade));
    }
    case 32:
    {
        float flower = 0.5;
        float r = radius * (flower + sin(angle * 3.0));
        return float2(r * cos(angle), r * sin(angle));
    }
    case 33:
        return float2(
            cos(kPi * samplePoint.x) * cosh(samplePoint.y),
            -sin(kPi * samplePoint.x) * sinh(samplePoint.y));
    case 34:
        return float2(
            abs(samplePoint.x) - abs(samplePoint.y),
            abs(samplePoint.x) + abs(samplePoint.y) - 1.0);
    case 35:
    {
        float checkers = 0.5;
        float xParity = fmod(floor(samplePoint.x), 2.0);
        float yParity = fmod(floor(samplePoint.y), 2.0);
        return float2(
            samplePoint.x + checkers * (xParity == 0.0 ? 1.0 : -1.0),
            samplePoint.y + checkers * (yParity == 0.0 ? 1.0 : -1.0));
    }

    case 36:
        return float2(samplePoint.x / radiusSquared, samplePoint.y);
    case 37:
        return float2((samplePoint.x / radius) * cos(radius), (samplePoint.y / radius) * sin(radius));
    case 38:
    {
        float factor = exp(samplePoint.x - 1.0);
        float theta = kPi * samplePoint.y;
        return float2(factor * cos(theta), factor * sin(theta));
    }
    case 39:
    {
        float exponent = samplePoint.x / radius;
        float factor = pow(radius, exponent);
        return float2((samplePoint.y / radius) * factor, (samplePoint.x / radius) * factor);
    }
    case 40:
    {
        float denominator = SafeSignedDenominator(cos(2.0 * samplePoint.x) + cosh(2.0 * samplePoint.y));
        float factor = 2.0 / denominator;
        return float2(
            factor * cos(samplePoint.x) * cosh(samplePoint.y),
            factor * sin(samplePoint.x) * sinh(samplePoint.y));
    }
    case 41:
    {
        float denominator = SafeSignedDenominator(cosh(2.0 * samplePoint.y) - cos(2.0 * samplePoint.x));
        float factor = 2.0 / denominator;
        return float2(
            factor * sin(samplePoint.x) * cosh(samplePoint.y),
            -factor * cos(samplePoint.x) * sinh(samplePoint.y));
    }
    case 42:
    {
        float factor = 1.0 / SafeSignedDenominator(cosh(2.0 * samplePoint.y) - cos(2.0 * samplePoint.x));
        return float2(
            factor * sin(2.0 * samplePoint.x),
            -factor * sinh(2.0 * samplePoint.y));
    }
    case 43:
    {
        float denominator = SafeSignedDenominator(cos(2.0 * samplePoint.y) + cosh(2.0 * samplePoint.x));
        float factor = 2.0 / denominator;
        return float2(
            factor * cos(samplePoint.y) * cosh(samplePoint.x),
            -factor * sin(samplePoint.y) * sinh(samplePoint.x));
    }
    case 44:
    {
        float p_angle = kPi / 4.0;
        float dist = 2.0;
        float denominator = SafeSignedDenominator(dist - samplePoint.y * sin(p_angle));
        return float2(
            dist * samplePoint.x / denominator,
            dist * samplePoint.y * cos(p_angle) / denominator);
    }
    case 45:
    {
        float p_high = 1.2;
        float p_low = 0.5;
        float p_waves = 3.0;
        float factor = radius * (p_low + 0.5 * (p_high - p_low) * (1.0 + sin(p_waves * angle)));
        return float2(
            factor * cos(angle),
            factor * sin(angle));
    }
    case 46:
    {
        float a = 0.1;
        float b = 1.9;
        float c = -0.8;
        float d = -1.2;
        return float2(
            sin(a * samplePoint.y) - cos(b * samplePoint.x),
            sin(c * samplePoint.x) - cos(d * samplePoint.y));
    }
    case 47:
    {
        float p_x = kPi * 0.1;
        float p_y = 0.5;
        float dx = kPi * (p_x * p_x + 1.0e-9);
        float dx2 = dx * 0.5;
        float t = angle + p_y - dx * floor((angle + p_y) / dx);
        float a_val = t > dx2 ? angle - dx2 : angle + dx2;
        return float2(
            radius * sin(a_val),
            radius * cos(a_val));
    }
    case 48:
    {
        float p_val = 0.5;
        float p_val2 = p_val * p_val + 1.0e-9;
        float t = radius - 2.0 * p_val2 * floor((radius + p_val2) / (2.0 * p_val2)) + radius * (1.0 - p_val2);
        return float2(
            t * sin(angle),
            t * cos(angle));
    }
    case 49:
    {
        float r = radius * 0.8;
        float sinr = sin(r);
        float cosr = cos(r);
        float diff = log10(max(1.0e-9, sinr * sinr)) + cosr;
        return float2(
            samplePoint.x + 0.8 * samplePoint.x * diff,
            samplePoint.y + 0.8 * samplePoint.x * (diff - sinr * kPi));
    }
    case 50:
    {
        float re_a = 1.0, im_a = 0.0;
        float re_b = 0.5, im_b = 0.5;
        float re_c = -0.5, im_c = 0.5;
        float re_d = 1.0, im_d = 0.0;
        float re_num = (re_a * samplePoint.x - im_a * samplePoint.y + re_b);
        float im_num = (re_a * samplePoint.y + im_a * samplePoint.x + im_b);
        float re_den = (re_c * samplePoint.x - im_c * samplePoint.y + re_d);
        float im_den = (re_c * samplePoint.y + im_c * samplePoint.x + im_d);
        float denom = SafeSignedDenominator(re_den * re_den + im_den * im_den);
        return float2(
            (re_num * re_den + im_num * im_den) / denom,
            (im_num * re_den - re_num * im_den) / denom);
    }
    case 51:
    {
        float m = 6.0;
        float n1 = 1.0, n2 = 1.0, n3 = 1.0;
        float a_ss = 1.0, b_ss = 1.0;
        float mAngle = m * angle / 4.0;
        float t1 = pow(abs(cos(mAngle) / a_ss), n2);
        float t2 = pow(abs(sin(mAngle) / b_ss), n3);
        float rSuper = pow(max(1.0e-9, t1 + t2), -1.0 / n1);
        return float2(rSuper * cos(angle), rSuper * sin(angle));
    }
    case 52:
    {
        float eccentricity = 0.8;
        float r_conic = eccentricity / SafeSignedDenominator(1.0 + eccentricity * cos(angle));
        return float2(r_conic * cos(angle), r_conic * sin(angle));
    }
    case 53:
    {
        float n_ast = 3.0;
        float cosA = cos(angle);
        float sinA = sin(angle);
        float sx = (cosA >= 0.0) ? 1.0 : -1.0;
        float sy = (sinA >= 0.0) ? 1.0 : -1.0;
        return float2(
            sx * pow(max(1.0e-9, abs(cosA)), 2.0 / n_ast),
            sy * pow(max(1.0e-9, abs(sinA)), 2.0 / n_ast));
    }
    case 54:
    {
        float freqA = 3.0, freqB = 2.0;
        float phaseA = 0.0, phaseB = kPi / 2.0;
        float t = angle;
        return float2(
            sin(freqA * t + phaseA) * radius,
            sin(freqB * t + phaseB) * radius);
    }
    case 55:
    {
        float vStrength = 2.0;
        float vDecay = 0.5;
        float twist = vStrength * exp(-radius * vDecay);
        float newAngle = angle + twist;
        return float2(radius * cos(newAngle), radius * sin(newAngle));
    }
    case 56:
    {
        float segments = 8.0;
        float segAngle = 2.0 * kPi / segments;
        float a_k = fmod(angle + kPi, segAngle);
        if (a_k > segAngle * 0.5) a_k = segAngle - a_k;
        a_k -= segAngle * 0.5;
        return float2(radius * cos(a_k), radius * sin(a_k));
    }
    case 57:
    {
        float logR = log(max(1.0e-9, radius));
        float dFactor = 1.2;
        float newAngle = angle + logR * dFactor;
        float newRadius = exp(logR - floor(logR));
        return float2(newRadius * cos(newAngle), newRadius * sin(newAngle));
    }
    case 58:
    {
        float phi = (1.0 + sqrt(5.0)) / 2.0;
        float logPhi = log(phi);
        float spiralR = exp(logPhi * angle / (2.0 * kPi));
        float blend = radius / (radius + 1.0);
        float r2 = radius * (1.0 - blend) + spiralR * blend;
        return float2(r2 * cos(angle), r2 * sin(angle));
    }
    case 59:
    {
        float freq1 = 5.0, freq2 = 7.0;
        float wave1 = sin(freq1 * samplePoint.x) * cos(freq2 * samplePoint.y);
        float wave2 = cos(freq1 * samplePoint.y) * sin(freq2 * samplePoint.x);
        float interference = (wave1 + wave2) * 0.5;
        return float2(
            samplePoint.x + interference * 0.3,
            samplePoint.y + interference * 0.3);
    }
    default:
        return samplePoint;
    }
}

float2 ApplyAffine(TransformGpu layer, float2 samplePoint)
{
    float cosine = cos(layer.RotationRadians);
    float sine = sin(layer.RotationRadians);
    float2 scaled = float2(samplePoint.x * layer.ScaleX, samplePoint.y * layer.ScaleY);
    float2 sheared = float2(scaled.x + layer.ShearX * scaled.y, scaled.y + layer.ShearY * scaled.x);
    return float2(sheared.x * cosine - sheared.y * sine + layer.TranslateX, sheared.x * sine + sheared.y * cosine + layer.TranslateY);
}

uint ChooseTransform(float randomValue)
{
    [loop]
    for (uint index = 0; index < TransformCount; ++index)
        if (randomValue <= Transforms[index].CumulativeWeight)
            return index;
    return max(TransformCount, 1) - 1;
}

[numthreads(128, 1, 1)]
void AccumulateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint threadIndex = dispatchThreadId.x;
    if (threadIndex >= TotalThreadCount || TransformCount == 0)
        return;

    OrbitState state = OrbitStates[threadIndex];
    uint rng = state.RngState;
    float3 samplePoint = state.SamplePoint;
    float colorIndex = state.ColorIndex;
    uint burnInRemaining = state.BurnInRemaining;
    if (PreserveOrbitState == 0u || state.Initialized == 0u)
    {
        rng = (threadIndex * 747796405u + 2891336453u) ^ (RandomSeedOffset * 1664525u + 1013904223u);
        rng = max(rng, 1u);
        samplePoint = float3(RandomFloat(rng) * 2.0 - 1.0, RandomFloat(rng) * 2.0 - 1.0, (RandomFloat(rng) * 2.0 - 1.0) * 0.35);
        colorIndex = 0.5;
        burnInRemaining = kBurnInIterations;
    }
    uint iterationsPerThread = max(1u, (PreviewIterations + TotalThreadCount - 1u) / TotalThreadCount);
    float yawCos = cos(Yaw);
    float yawSin = sin(Yaw);
    float pitchCos = cos(Pitch);
    float pitchSin = sin(Pitch);
    float flameCosX = cos(FlameRotateX);
    float flameSinX = sin(FlameRotateX);
    float flameCosY = cos(FlameRotateY);
    float flameSinY = sin(FlameRotateY);
    float flameCosZ = cos(FlameRotateZ);
    float flameSinZ = sin(FlameRotateZ);
    float frameMinX;
    float frameMinY;
    float frameWidth;
    float frameHeight;
    float frameScale;
    ComputeCameraFrame(frameMinX, frameMinY, frameWidth, frameHeight, frameScale);

    [loop]
    for (uint iteration = 0; iteration < iterationsPerThread + kBurnInIterations; ++iteration)
    {
        float pick = RandomFloat(rng) * TotalWeight;
        TransformGpu layer = Transforms[ChooseTransform(pick)];
        float2 affine = ApplyAffine(layer, samplePoint.xy);
        if (!IsFinitePoint(affine) || IsOrbitOutOfRange(affine))
        {
            samplePoint = float3(RandomFloat(rng) * 2.0 - 1.0, RandomFloat(rng) * 2.0 - 1.0, (RandomFloat(rng) * 2.0 - 1.0) * 0.35);
            colorIndex = 0.5;
            burnInRemaining = kBurnInIterations;
            continue;
        }

        float2 varied = float2(0.0, 0.0);
        float totalVariationWeight = 0.0;
        bool unstableOrbit = false;
        for (uint variation = 0; variation < 60u; ++variation)
        {
            float amount = layer.Variations[variation];
            if (amount == 0.0)
                continue;
            float2 variationPoint = ApplyVariation(variation, affine);
            if (!IsFinitePoint(variationPoint) || IsOrbitOutOfRange(variationPoint))
            {
                unstableOrbit = true;
                break;
            }
            totalVariationWeight += amount;
            varied += variationPoint * amount;
        }
        if (unstableOrbit)
        {
            samplePoint = float3(RandomFloat(rng) * 2.0 - 1.0, RandomFloat(rng) * 2.0 - 1.0, (RandomFloat(rng) * 2.0 - 1.0) * 0.35);
            colorIndex = 0.5;
            burnInRemaining = kBurnInIterations;
            continue;
        }
        if (totalVariationWeight <= 1.0e-9)
            varied = affine;
        else
            varied *= (1.0 / totalVariationWeight);
        if (!IsFinitePoint(varied) || IsOrbitOutOfRange(varied))
        {
            samplePoint = float3(RandomFloat(rng) * 2.0 - 1.0, RandomFloat(rng) * 2.0 - 1.0, (RandomFloat(rng) * 2.0 - 1.0) * 0.35);
            colorIndex = 0.5;
            burnInRemaining = kBurnInIterations;
            continue;
        }

        float radius = sqrt(varied.x * varied.x + varied.y * varied.y);
        float angle = atan2(varied.y, varied.x);
        float depthDrive =
            sin(samplePoint.z * 1.35 + angle * (1.8 + abs(layer.ShearX) * 0.35) + layer.RotationRadians * 0.7) * (0.24 + radius * 0.18)
            + cos((varied.x * 1.9 - varied.y * 1.6) * (1.0 + abs(layer.ShearY) * 0.25) + samplePoint.z * 0.9) * (0.12 + radius * 0.08)
            + (layer.ColorIndex - 0.5) * 0.28
            + (layer.TranslateX - layer.TranslateY) * 0.05;
        float nextDepth = samplePoint.z * 0.74 + depthDrive;
        float swirlAngle = nextDepth * (0.52 + radius * 0.18) + layer.RotationRadians * 0.22;
        float swirlCos = cos(swirlAngle);
        float swirlSin = sin(swirlAngle);
        samplePoint = float3(
            varied.x * swirlCos - varied.y * swirlSin,
            varied.x * swirlSin + varied.y * swirlCos,
            nextDepth);
        if (!IsFinitePoint3(samplePoint) || IsOrbitOutOfRange3(samplePoint))
        {
            samplePoint = float3(RandomFloat(rng) * 2.0 - 1.0, RandomFloat(rng) * 2.0 - 1.0, (RandomFloat(rng) * 2.0 - 1.0) * 0.35);
            colorIndex = 0.5;
            burnInRemaining = kBurnInIterations;
            continue;
        }
        colorIndex = lerp(colorIndex, layer.ColorIndex, 0.16);

        if (burnInRemaining > 0u)
        {
            burnInRemaining -= 1u;
            continue;
        }

        uint4 sampleColor;
        if (layer.UseCustomColor > 0.5)
        {
            sampleColor = uint4(
                (uint)round(saturate(layer.CustomColor.x) * 255.0),
                (uint)round(saturate(layer.CustomColor.y) * 255.0),
                (uint)round(saturate(layer.CustomColor.z) * 255.0),
                (uint)round(saturate(layer.CustomColor.w) * 255.0));
        }
        else
        {
            uint paletteIndex = min((uint)255, (uint)floor(clamp(colorIndex, 0.0, 1.0) * 255.0));
            sampleColor = Palette[paletteIndex];
        }
        EmitSymmetrySamples(
            samplePoint,
            sampleColor.xyz,
            yawCos,
            yawSin,
            pitchCos,
            pitchSin,
            flameCosX,
            flameSinX,
            flameCosY,
            flameSinY,
            flameCosZ,
            flameSinZ,
            frameMinX,
            frameMinY,
            frameWidth,
            frameHeight,
            frameScale);
    }

    if (PreserveOrbitState != 0u)
    {
        state.SamplePoint = samplePoint;
        state.ColorIndex = colorIndex;
        state.RngState = max(rng, 1u);
        state.BurnInRemaining = burnInRemaining;
        state.Initialized = 1u;
        OrbitStates[threadIndex] = state;
    }
}



[numthreads(8, 8, 1)]
void ToneMapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixelCoord = int2(dispatchThreadId.xy);
    float densityAccum = 0.0;
    float3 colorAccum = float3(0.0, 0.0, 0.0);
    float depthAccum = 0.0;
    LoadAccumulation(pixelCoord, densityAccum, colorAccum, depthAccum);

    float reconstructedDensity = densityAccum;
    float3 reconstructedColor = colorAccum;
    float reconstructedDepth = depthAccum;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;

            float sampleDensity = 0.0;
            float3 sampleColor = float3(0.0, 0.0, 0.0);
            float sampleDepth = 0.0;
            LoadAccumulation(pixelCoord + int2(dx, dy), sampleDensity, sampleColor, sampleDepth);
            float kernel = ReconstructionKernel(dx, dy);
            reconstructedDensity += sampleDensity * kernel;
            reconstructedColor += sampleColor * kernel;
            reconstructedDepth += sampleDepth * kernel;
        }
    }

    float centerDensity = densityAccum / (float)kAccumulationScale;
    float reconstructionBlend = saturate((0.55 - centerDensity) / 0.55) * 0.18;
    densityAccum = lerp(densityAccum, reconstructedDensity, reconstructionBlend);
    colorAccum = lerp(colorAccum, reconstructedColor, reconstructionBlend);
    depthAccum = lerp(depthAccum, reconstructedDepth, reconstructionBlend);

    float3 background = TransparentBackground != 0u ? float3(0.0, 0.0, 0.0) : float3(BackgroundR, BackgroundG, BackgroundB);
    if (GridVisible != 0u)
    {
        bool majorX = (dispatchThreadId.x % 48u) == 0u;
        bool majorY = (dispatchThreadId.y % 48u) == 0u;
        bool axisX = dispatchThreadId.x == Width / 2u;
        bool axisY = dispatchThreadId.y == Height / 2u;
        if (majorX || majorY)
            background = float3(24.0 / 255.0, 24.0 / 255.0, 32.0 / 255.0) * 0.35 + background * 0.65;
        if (axisX || axisY)
            background = float3(36.0 / 255.0, 38.0 / 255.0, 48.0 / 255.0) * 0.5 + background * 0.5;
    }

    if (densityAccum <= 1.0e-4)
    {
        FlameOutput[int2(dispatchThreadId.xy)] = float4(background, TransparentBackground != 0u ? 0.0 : 1.0);
        FlameDepthOutput[int2(dispatchThreadId.xy)] = 1.0;
        return;
    }

    float density = densityAccum / (float)kAccumulationScale;
    float logDensity = log(1.0 + density);
    float exposure = clamp(FlameCurveExposure, 0.25, 3.0);
    float contrast = clamp(FlameCurveContrast, 0.45, 2.2);
    float highlights = clamp(FlameCurveHighlights, 0.0, 2.0);
    float gamma = clamp(FlameCurveGamma, 0.45, 1.8);
    float intensity = pow(clamp((logDensity * exposure) / 4.05, 0.0, 1.72 + highlights * 0.18), 1.04);
    float divisor = max((float)kAccumulationScale, densityAccum);
    float rawR = clamp((colorAccum.x / divisor) / 255.0, 0.0, 1.0);
    float rawG = clamp((colorAccum.y / divisor) / 255.0, 0.0, 1.0);
    float rawB = clamp((colorAccum.z / divisor) / 255.0, 0.0, 1.0);
    float maxChannel = max(max(rawR, rawG), max(rawB, 1.0e-6));
    float saturationBoost = 1.0 + (1.0 - maxChannel) * (0.10 + highlights * 0.04);
    float highlightLift = smoothstep(0.42, 1.08, intensity) * highlights;
    
    float alpha = clamp(intensity * (1.04 + highlightLift * 0.14), 0.0, 1.0);
    float bloom = 0.94 + intensity * 0.18 + highlightLift * 0.30;
    
    float rf = clamp(pow(rawR, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    float gf = clamp(pow(rawG, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    float bf = clamp(pow(rawB, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    float3 mapped = float3(pow(rf, 0.58 / gamma), pow(gf, 0.58 / gamma), pow(bf, 0.58 / gamma));
    mapped = clamp((mapped - 0.5) * contrast + 0.5, 0.0, 1.0);
    
    float3 output = background + (mapped - background) * alpha;
    float outputAlpha = TransparentBackground != 0u ? alpha : 1.0;
    FlameOutput[int2(dispatchThreadId.xy)] = float4(output, outputAlpha);
    FlameDepthOutput[int2(dispatchThreadId.xy)] = saturate((depthAccum / max(1.0, densityAccum)) / 65535.0);
}
