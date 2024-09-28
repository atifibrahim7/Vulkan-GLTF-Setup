struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
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

cbuffer UboView : register(b0, space0)
{
    SHADER_VARS ubo;
}

Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space1);

static float4 specular = float4(1, 1, 1, 1);
static float4 ambient = float4(0.1f, 0.1f, 0.1f, 1);
static float Ns = 100;

float4 main(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);
    float3 B = cross(N, T) * input.tangent.w;
    float3x3 TBN = float3x3(T, B, N);

    float4 albedo = textures[0].Sample(samplers[0], input.uv);
    float4 metallicRoughness = textures[1].Sample(samplers[1], input.uv);
    float3 normalMap = textures[2].Sample(samplers[2], input.uv).xyz;
    float4 emissive = textures[3].Sample(samplers[3], input.uv);

    normalMap.y = 1.0 - normalMap.y;
    normalMap = (normalMap * 2.0) - 1.0;
    float3 worldNormal = normalize(mul(normalMap, TBN));

    float3 L = normalize(-ubo.sunDirection.xyz);
    float3 V = normalize(ubo.cameraPosition.xyz - input.worldPos);
    float3 H = normalize(L + V);

    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    float4 finalColor = ambient * albedo;
    float NdotL = max(dot(worldNormal, L), 0);
    finalColor += albedo * ubo.sunColor * NdotL;
    
    float NdotH = max(dot(worldNormal, H), 0);
    float4 specularTerm = specular * ubo.sunColor * pow(NdotH, Ns) * (1 - roughness);
    finalColor += lerp(specularTerm, albedo * specularTerm, metallic);

    finalColor += emissive;

    return finalColor;
}