cbuffer cbPerObject : register(b0)
{
    float3 gOffset;
    float  gScale;
    float3 pad;  // 패딩
};

struct VS_INPUT
{
    float3 Pos   : POSITION;
    float4 Color : COLOR;
};

struct PS_INPUT
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    // 위치 + 오프셋 + 스케일 적용
    float3 scaledPos = input.Pos * gScale;
    output.Pos = float4(scaledPos + gOffset, 1.0f);
    output.Color = input.Color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
    return input.Color;
}