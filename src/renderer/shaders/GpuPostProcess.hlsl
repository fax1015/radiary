cbuffer PostProcessParams : register(b0)
{
    uint Width;
    uint Height;
    float BloomIntensity;
    float BloomThreshold;
    float ChromaticAberration;
    float VignetteIntensity;
    float VignetteRoundness;
    uint AcesToneMap;
    float FilmGrain;
    float ColorTemperature;
    float SaturationBoost;
    uint RandomSeed;
    uint MipWidth;
    uint MipHeight;
    float2 Padding;
};

Texture2D<float4> InputTexture : register(t0);
Texture2D<float4> BloomTexture : register(t1);
RWTexture2D<float4> OutputTexture : register(u0);

static const float kPi = 3.14159265358979323846;

uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint seed)
{
    return float(PCGHash(seed) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ColorTemperatureToRGB(float kelvin)
{
    float temp = clamp(kelvin, 1000.0, 15000.0) / 100.0;
    float3 color;
    if (temp <= 66.0)
    {
        color.r = 1.0;
        color.g = saturate(0.39008157876901960784 * log(temp) - 0.63184144378862745098);
    }
    else
    {
        color.r = saturate(1.29293618606274509804 * pow(temp - 60.0, -0.1332047592));
        color.g = saturate(1.12989086089529411765 * pow(temp - 60.0, -0.0755148492));
    }
    if (temp >= 66.0)
        color.b = 1.0;
    else if (temp <= 19.0)
        color.b = 0.0;
    else
        color.b = saturate(0.54320678911019607843 * log(temp - 10.0) - 1.19625408914);
    return color;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void BloomDownCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= MipWidth || dispatchThreadId.y >= MipHeight)
        return;

    float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(MipWidth, MipHeight);
    int2 srcCoord = int2(uv * float2(Width, Height));

    float3 color = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 sampleCoord = srcCoord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(Width - 1, Height - 1));
            float3 sampleColor = InputTexture.Load(int3(sampleCoord, 0)).rgb;
            float lum = Luminance(sampleColor);
            float brightPass = max(0.0, lum - BloomThreshold) / max(lum, 0.001);
            float kernel = (dx == 0 && dy == 0) ? 4.0 : ((abs(dx) + abs(dy) == 1) ? 2.0 : 1.0);
            color += sampleColor * brightPass * kernel;
            totalWeight += kernel;
        }
    }

    color /= max(totalWeight, 1.0);
    OutputTexture[int2(dispatchThreadId.xy)] = float4(color, 1.0);
}

[numthreads(8, 8, 1)]
void BloomUpCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= MipWidth || dispatchThreadId.y >= MipHeight)
        return;

    float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(MipWidth, MipHeight);

    float3 color = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    uint srcWidth, srcHeight;
    InputTexture.GetDimensions(srcWidth, srcHeight);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float2 offset = float2(dx, dy) / float2(srcWidth, srcHeight);
            int2 sampleCoord = int2((uv + offset) * float2(srcWidth, srcHeight));
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(srcWidth - 1, srcHeight - 1));
            float kernel = (dx == 0 && dy == 0) ? 4.0 : ((abs(dx) + abs(dy) == 1) ? 2.0 : 1.0);
            color += InputTexture.Load(int3(sampleCoord, 0)).rgb * kernel;
            totalWeight += kernel;
        }
    }

    color /= max(totalWeight, 1.0);

    float3 existing = BloomTexture.Load(int3(dispatchThreadId.xy, 0)).rgb;
    OutputTexture[int2(dispatchThreadId.xy)] = float4(existing + color, 1.0);
}

[numthreads(8, 8, 1)]
void PostProcessCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float2 uv = (float2(pixel) + 0.5) / float2(Width, Height);
    float2 center = float2(0.5, 0.5);

    float3 color;
    if (ChromaticAberration > 0.001)
    {
        float2 dir = uv - center;
        float dist = length(dir);
        float2 offset = dir * ChromaticAberration * 0.02 * dist;

        int2 coordR = clamp(int2((uv + offset) * float2(Width, Height)), int2(0, 0), int2(Width - 1, Height - 1));
        int2 coordG = pixel;
        int2 coordB = clamp(int2((uv - offset) * float2(Width, Height)), int2(0, 0), int2(Width - 1, Height - 1));

        color.r = InputTexture.Load(int3(coordR, 0)).r;
        color.g = InputTexture.Load(int3(coordG, 0)).g;
        color.b = InputTexture.Load(int3(coordB, 0)).b;
    }
    else
    {
        color = InputTexture.Load(int3(pixel, 0)).rgb;
    }

    if (BloomIntensity > 0.001)
    {
        uint bloomW, bloomH;
        BloomTexture.GetDimensions(bloomW, bloomH);
        int2 bloomCoord = clamp(int2(uv * float2(bloomW, bloomH)), int2(0, 0), int2(bloomW - 1, bloomH - 1));
        float3 bloom = BloomTexture.Load(int3(bloomCoord, 0)).rgb;
        color += bloom * BloomIntensity;
    }

    if (abs(ColorTemperature - 6500.0) > 10.0)
    {
        float3 tempColor = ColorTemperatureToRGB(ColorTemperature);
        float3 neutral = ColorTemperatureToRGB(6500.0);
        color *= tempColor / max(neutral, float3(0.001, 0.001, 0.001));
    }

    if (abs(SaturationBoost) > 0.001)
    {
        float lum = Luminance(color);
        color = lerp(float3(lum, lum, lum), color, 1.0 + SaturationBoost);
        color = max(color, float3(0.0, 0.0, 0.0));
    }

    if (AcesToneMap != 0u)
    {
        color = ACESFilm(color);
    }

    if (FilmGrain > 0.001)
    {
        uint seed = (dispatchThreadId.y * Width + dispatchThreadId.x) ^ RandomSeed;
        float grain = (RandomFloat(seed) - 0.5) * FilmGrain * 0.15;
        float lum = Luminance(color);
        float grainScale = 1.0 - saturate(lum * 2.0);
        color += grain * grainScale;
    }

    if (VignetteIntensity > 0.001)
    {
        float2 vignUv = uv - center;
        float aspectRatio = float(Width) / float(Height);
        vignUv.x *= lerp(1.0, aspectRatio, VignetteRoundness);
        float vignDist = length(vignUv) * 1.4142;
        float vignette = 1.0 - smoothstep(0.4, 1.2, vignDist) * VignetteIntensity;
        color *= vignette;
    }

    color = saturate(color);
    float alpha = InputTexture.Load(int3(pixel, 0)).a;
    OutputTexture[pixel] = float4(color, alpha);
}
