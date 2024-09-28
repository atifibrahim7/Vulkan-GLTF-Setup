struct OBJ_ATTRIBUTES
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct SHADER_VARS
{
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4 sunDirection;
    float4 sunColor;
    float4 cameraPosition;
    float4x4 worldMatrix;
};

cbuffer UboView : register(b0)
{
    SHADER_VARS ubo;
}

struct VOut
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float3 worldPos : POSITION;
};

VOut main(OBJ_ATTRIBUTES input)
{
    VOut output;
    
    float4 worldPosition = mul(ubo.worldMatrix, float4(input.Position, 1.0f));
    float4 viewPosition = mul(ubo.viewMatrix, worldPosition);
    output.position = mul(ubo.projectionMatrix, viewPosition);
    
    output.worldPos = worldPosition.xyz;
    output.normal = normalize(mul((float3x3) ubo.worldMatrix, input.Normal));
    output.uv = input.UV;
    
    // Transform tangent to world space while preserving W
    float3 worldTangent = mul((float3x3) ubo.worldMatrix, input.Tangent.xyz);
    output.tangent = float4(normalize(worldTangent), input.Tangent.w);
    
    return output;
}