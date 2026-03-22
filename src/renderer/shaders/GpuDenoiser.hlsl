cbuffer DenoiserParams : register(b0)
{
    uint Width;
    uint Height;
    uint UseGrid;
    uint UseFlame;
    uint UsePath;
    float Strength;
    float SigmaSpatial;
    float SigmaColor;
    float SigmaDepth;
    float3 Padding;
};

Texture2D<float4> GridTexture : register(t0);
Texture2D<float4> FlameTexture : register(t1);
Texture2D<float> FlameDepthTexture : register(t2);
Texture2D<float4> PathTexture : register(t3);
Texture2D<float> PathDepthTexture : register(t4);
Texture2D<float4> InputTexture : register(t5);
Texture2D<float> InputDepthTexture : register(t6);

RWTexture2D<float4> OutputTexture : register(u0);
RWTexture2D<float> OutputDepthTexture : register(u1);

float4 AlphaOver(float4 baseColor, float4 overlayColor)
{
    return float4(
        overlayColor.rgb + baseColor.rgb * (1.0 - overlayColor.a),
        overlayColor.a + baseColor.a * (1.0 - overlayColor.a));
}

float4 ComposeColor(int2 pixel)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    if (UseGrid != 0u)
    {
        color = GridTexture.Load(int3(pixel, 0));
    }
    if (UseFlame != 0u)
    {
        float4 flame = FlameTexture.Load(int3(pixel, 0));
        color = (UseGrid != 0u) ? AlphaOver(color, flame) : flame;
    }
    if (UsePath != 0u)
    {
        color = AlphaOver(color, PathTexture.Load(int3(pixel, 0)));
    }
    return color;
}

float ComposeDepth(int2 pixel)
{
    if (UsePath != 0u)
    {
        float4 path = PathTexture.Load(int3(pixel, 0));
        if (path.a > 0.001)
        {
            return PathDepthTexture.Load(int3(pixel, 0));
        }
    }

    if (UseFlame != 0u)
    {
        float flameDepth = FlameDepthTexture.Load(int3(pixel, 0));
        if (flameDepth < 0.999)
        {
            return flameDepth;
        }
    }

    return 1.0;
}

[numthreads(8, 8, 1)]
void ComposeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    OutputTexture[pixel] = ComposeColor(pixel);
    OutputDepthTexture[pixel] = ComposeDepth(pixel);
}

[numthreads(8, 8, 1)]
void FilterCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float4 centerColor = InputTexture.Load(int3(pixel, 0));
    float centerDepth = InputDepthTexture.Load(int3(pixel, 0));

    if (Strength <= 0.0)
    {
        OutputTexture[pixel] = centerColor;
        return;
    }

    int radius = clamp((int)ceil(SigmaSpatial * 2.0), 1, 8);
    int blurRadius = clamp((int)ceil(SigmaSpatial), 1, 4);
    float invSpatialVar = 1.0 / (2.0 * SigmaSpatial * SigmaSpatial);
    float invColorVar = 1.0 / (2.0 * SigmaColor * SigmaColor);
    float invDepthVar = 1.0 / (2.0 * SigmaDepth * SigmaDepth);

    float4 meanColor = float4(0.0, 0.0, 0.0, 0.0);
    float4 m2Color = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            int2 samplePixel = pixel + int2(x, y);
            samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
            samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);

            float4 sampleColor = InputTexture.Load(int3(samplePixel, 0));
            float spatialDistSq = (float)(x * x + y * y);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);
            float weight = spatialWeight * (sampleColor.a > 0.01 ? 1.0 : 0.001);

            meanColor += sampleColor * weight;
            m2Color += sampleColor * sampleColor * weight;
            weightSum += weight;
        }
    }

    meanColor /= max(weightSum, 1.0e-6);
    m2Color /= max(weightSum, 1.0e-6);

    float4 variance = max(float4(0.0, 0.0, 0.0, 0.0), m2Color - meanColor * meanColor);
    float4 stdDev = sqrt(variance);
    float thresholdMult = lerp(4.0, 0.5, saturate(Strength));
    float4 minColor = meanColor - stdDev * thresholdMult;
    float4 maxColor = meanColor + stdDev * thresholdMult;
    float4 filteredColor = clamp(centerColor, minColor, maxColor);

    float4 accumColor = float4(0.0, 0.0, 0.0, 0.0);
    float blurWeightSum = 0.0;

    for (int by = -blurRadius; by <= blurRadius; ++by)
    {
        for (int bx = -blurRadius; bx <= blurRadius; ++bx)
        {
            int2 samplePixel = pixel + int2(bx, by);
            samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
            samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);

            float4 sampleColor = InputTexture.Load(int3(samplePixel, 0));
            sampleColor = clamp(sampleColor, minColor, maxColor);
            float sampleDepth = InputDepthTexture.Load(int3(samplePixel, 0));

            float spatialDistSq = (float)(bx * bx + by * by);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);

            float depthDiff = sampleDepth - centerDepth;
            float depthWeight = exp(-(depthDiff * depthDiff) * invDepthVar);

            float3 colorDiff = sampleColor.rgb - filteredColor.rgb;
            float colorDistSq = dot(colorDiff, colorDiff);
            float colorWeight = exp(-colorDistSq * invColorVar);

            float weight = spatialWeight * depthWeight * colorWeight;
            accumColor += sampleColor * weight;
            blurWeightSum += weight;
        }
    }

    if (blurWeightSum > 1.0e-6)
    {
        filteredColor = lerp(filteredColor, accumColor / blurWeightSum, saturate(Strength * 1.5));
    }

    OutputTexture[pixel] = filteredColor;
}
