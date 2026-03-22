cbuffer HybridParams : register(b0)
{
    float FlameDepthBias;
    float FlameDepthSoftness;
    uint UseFlameOcclusion;
    float Padding;
};

Texture2D<float> FlameDepthTexture : register(t0);

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};

PSInput MainVS(VSInput input)
{
    PSInput output;
    output.Position = float4(input.Position, 1.0);
    output.Color = input.Color;
    return output;
}

float4 MainPS(PSInput input) : SV_Target0
{
    float alpha = input.Color.a;
    if (UseFlameOcclusion != 0u)
    {
        int2 pixel = int2(input.Position.xy);
        float flameDepth = FlameDepthTexture.Load(int3(pixel, 0));
        if (flameDepth < 0.999)
        {
            float depthDelta = input.Position.z - (flameDepth + FlameDepthBias);
            float occlusion = saturate(depthDelta / max(FlameDepthSoftness, 1.0e-5));
            alpha *= lerp(1.0, 0.14, occlusion);
        }
    }
    return float4(input.Color.rgb, alpha);
}
