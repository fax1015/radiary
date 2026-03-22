cbuffer DofParams : register(b0)
{
    uint Width;
    uint Height;
    uint UseGrid;
    uint UseFlame;
    uint UsePath;
    float FocusDepth;
    float FocusRange;
    float BlurStrength;
    float4 Padding;
};

Texture2D<float4> GridTexture : register(t0);
Texture2D<float4> FlameTexture : register(t1);
Texture2D<float> FlameDepthTexture : register(t2);
Texture2D<float4> PathTexture : register(t3);
Texture2D<float> PathDepthTexture : register(t4);

RWTexture2D<float4> OutputTexture : register(u0);

static const int2 kOffsets[8] = {
    int2(1, 0),
    int2(-1, 0),
    int2(0, 1),
    int2(0, -1),
    int2(1, 1),
    int2(-1, 1),
    int2(1, -1),
    int2(-1, -1)
};

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
void MainCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float4 centerColor = ComposeColor(pixel);
    float depth = ComposeDepth(pixel);
    float blurAmount = saturate((abs(depth - FocusDepth) - FocusRange) / max(0.02, 1.0 - FocusRange)) * saturate(BlurStrength);
    if (blurAmount <= 0.001)
    {
        OutputTexture[pixel] = centerColor;
        return;
    }

    int maxRadius = clamp((int)round(saturate(BlurStrength) * 12.0), 1, 12);
    float radius = max(1.0, blurAmount * maxRadius);
    float4 accum = centerColor * 2.0;
    float weightSum = 2.0;

    [unroll]
    for (uint index = 0; index < 8u; ++index)
    {
        int2 samplePixel = pixel + int2(round((float)kOffsets[index].x * radius), round((float)kOffsets[index].y * radius));
        samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
        samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);
        accum += ComposeColor(samplePixel);
        weightSum += 1.0;
    }

    OutputTexture[pixel] = accum / max(weightSum, 1.0e-5);
}
