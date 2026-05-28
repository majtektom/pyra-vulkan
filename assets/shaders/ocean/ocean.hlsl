cbuffer GlobalUniformBufferObject : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
};

[[vk::push_constant]]
cbuffer OceanPushConstants
{
    float4 cameraPosition;
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveSpeed;
    float waveFrequency;
    float foamThreshold;
    float pad0;
    float4 lightDir;
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float height : TEXCOORD2;
    [[vk::location(3)]] float2 texCoord : TEXCOORD3;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float height : TEXCOORD2;
    [[vk::location(3)]] float2 texCoord : TEXCOORD3;
};

float3 gerstnerWave(float3 p, float2 dir, float steepness, float waveLen, float amp, float speed, inout float3 tangent, inout float3 binormal)
{
    float freq = 6.283185307 / waveLen;
    float phase = freq * dot(dir, p.xz) + time * speed;
    float c = cos(phase);
    float s = sin(phase);

    tangent += float3(-dir.x * dir.x * steepness * s, dir.x * steepness * c, -dir.x * dir.y * steepness * s);
    binormal += float3(-dir.x * dir.y * steepness * s, dir.y * steepness * c, -dir.y * dir.y * steepness * s);

    return float3(dir.x * steepness * c, amp * s, dir.y * steepness * c);
}

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 p = input.position;
    float3 tangent = float3(1, 0, 0);
    float3 binormal = float3(0, 0, 1);
    float3 displaced = float3(0, 0, 0);

    displaced += gerstnerWave(p, float2(0.7, 0.7), 0.3, 40.0 / waveFrequency, 1.5 * waveAmplitude, 0.6 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(-0.3, 0.9), 0.2, 25.0 / waveFrequency, 1.0 * waveAmplitude, 0.8 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(0.5, -0.5), 0.15, 15.0 / waveFrequency, 0.6 * waveAmplitude, 1.2 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(-0.8, 0.2), 0.1, 10.0 / waveFrequency, 0.3 * waveAmplitude, 1.5 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(0.2, 0.85), 0.05, 6.0 / waveFrequency, 0.15 * waveAmplitude, 2.0 * waveSpeed, tangent, binormal);

    p.y = waterLevel;
    float3 worldPos = p + displaced;
    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));
    output.worldPos = worldPos;

    float3 n = normalize(cross(binormal, tangent));
    output.normal = n;

    output.height = displaced.y;
    output.texCoord = input.texCoord;

    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float3 N = normalize(input.normal);
    float3 L = normalize(lightDir.xyz);
    float3 V = normalize(cameraPosition.xyz - input.worldPos);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float fresnel = pow(1.0 - NdotV, 4.0);
    float alpha = lerp(0.75, 0.95, fresnel);

    float3 deepColor   = float3(0.01, 0.05, 0.15);
    float3 shallowColor = float3(0.04, 0.25, 0.45);
    float3 waterBody = lerp(deepColor, shallowColor, alpha);

    float3 skyReflect = lerp(float3(0.25, 0.45, 0.75), float3(0.55, 0.68, 0.88), fresnel);

    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 512.0) * 0.9 + pow(NdotH, 64.0) * 0.3;
    float3 sunSpec = float3(1.0, 0.9, 0.7) * spec * fresnel;

    float3 subSurf = deepColor * 0.3 * NdotL * (1.0 - fresnel);

    float foam = smoothstep(foamThreshold, foamThreshold + 0.4, input.height);
    float3 foamColor = float3(0.92, 0.94, 0.96);

    float3 result = lerp(waterBody, skyReflect, fresnel * 0.7);
    result += sunSpec * 0.8;
    result += subSurf;
    result = lerp(result, foamColor, foam * 0.4);

    return float4(result, alpha);
}
