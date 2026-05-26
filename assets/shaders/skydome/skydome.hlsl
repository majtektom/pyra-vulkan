cbuffer GlobalUniformBufferObject : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
};

[[vk::push_constant]]
cbuffer SkydomePushConstants
{
    float4 cameraPosition;
    float time;
    float domeScale;
    float cloudCoverage;
    float cloudSpeed0;
    float cloudSpeed1;
    float cloudScale0;
    float cloudScale1;
    float sunDirX;
    float sunDirY;
    float sunDirZ;
    float sunSize;
    float4 skyTopColor;
    float4 skyHorizonColor;
    float4 sunColor;
};

float hash21(float2 p)
{
    p = frac(p * float2(443.8975, 397.2973));
    p += dot(p, p.yx + 19.19);
    return frac(p.x * p.y);
}

float hash22(float2 p)
{
    float2 r = frac(p * float2(123.34, 345.45) + dot(p, p.yx + 21.42));
    return frac(r.x * r.y + dot(r, r.yx + 17.31));
}

float noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p)
{
    float2x2 rot = float2x2(0.8, 0.6, -0.6, 0.8);
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    float lacunarity = 2.3;
    for (int i = 0; i < 6; i++)
    {
        val += amp * noise(p * freq);
        freq *= lacunarity;
        amp *= 0.5;
        p = mul(p, rot) * 1.3;
    }
    return val;
}

float cloudFbm(float2 p)
{
    float2 q0 = p;
    float2 q1 = p + float2(fbm(p * 2.0 + 0.5) * 0.7, fbm(p * 2.0 + 2.7) * 0.7);
    return fbm(q1 * 1.1) * 0.65 + fbm(q0 * 0.5 + 1.2) * 0.35;
}

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
    [[vk::location(0)]] float2 tc : TEXCOORD0;
    [[vk::location(1)]] float heightFrac : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
};

struct FragmentInput
{
    [[vk::location(0)]] float2 tc : TEXCOORD0;
    [[vk::location(1)]] float heightFrac : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 localPos = float3(
        input.position.x * domeScale,
        input.position.y,
        input.position.z * domeScale);
    float3 worldPos = localPos + float3(cameraPosition.x, 0.0, cameraPosition.z);

    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));

    float distX = input.position.x / max(cloudScale0, 1.0);
    float distZ = input.position.z / max(cloudScale0, 1.0);
    output.tc = float2(distX, distZ);

    output.heightFrac = saturate((input.position.y + 200.0) / 1200.0);
    output.worldPos = worldPos;

    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float3 skyGradient = lerp(skyHorizonColor.rgb, skyTopColor.rgb,
                              smoothstep(0.0, 0.55, input.heightFrac));

    float2 tc0 = float2(input.tc.x + time * cloudSpeed0 * 0.015, input.tc.y);
    float2 tc1 = float2(input.tc.x + time * cloudSpeed1 * 0.015, input.tc.y);

    float noise0 = cloudFbm(tc0 * 2.8);
    float noise1 = cloudFbm(tc1 * 4.2);
    float cloudDensity = noise0 * 0.6 + noise1 * 0.4;

    float threshold = 0.82 - cloudCoverage * 0.77;
    float cloudFactor = smoothstep(threshold - 0.06, threshold + 0.06, cloudDensity);

    float detailNoise = fbm(tc0 * 8.0 + time * 0.005) * 0.15;
    cloudFactor = saturate(cloudFactor + detailNoise * cloudCoverage);

    float darkness = pow(saturate(cloudCoverage), 2.5);
    float3 cloudLight = float3(0.94, 0.92, 0.90);
    float3 cloudDark  = float3(0.16, 0.15, 0.14);
    float3 cloudBase = lerp(cloudLight, cloudDark, darkness);

    float cloudVar = (cloudDensity - 0.5) * 0.35 * darkness;
    cloudBase = saturate(cloudBase + cloudVar);

    float heightShade = 1.0 - smoothstep(0.25, 0.75, input.heightFrac) * 0.25 * darkness;
    cloudBase *= heightShade;

    float3 result = lerp(skyGradient, cloudBase, cloudFactor);

    float3 sunDir = normalize(float3(sunDirX, sunDirY, sunDirZ));
    float3 viewDir = normalize(float3(
        input.worldPos.x - cameraPosition.x,
        input.worldPos.y - cameraPosition.y,
        input.worldPos.z - cameraPosition.z));
    float sunDot = dot(viewDir, sunDir);
    float sunGlow = exp((sunDot - 1.0) * sunSize) * sunColor.a;
    float sunDisc = smoothstep(0.9993, 0.9999, sunDot);

    result += sunColor.rgb * (sunDisc + sunGlow * 0.12);

    float starVis = saturate(1.0 - sunColor.a * 5.0);
    if (starVis > 0.01)
    {
        float2 starP = input.tc * 4.0;
        float2 starCell = floor(starP * 60.0);
        float2 starUv = frac(starP * 60.0);

        float2 starPos = float2(hash21(starCell), hash21(starCell + 17.0));
        float dist = length(starUv - starPos);
        float starBright = hash21(starCell + 31.0);
        starBright = starBright * 0.5 + 0.5;

        float twinkle = 0.6 + 0.4 * sin(time * 2.5 + hash21(starCell + 7.0) * 200.0);
        starBright *= twinkle;

        float star = (1.0 - smoothstep(0.0, 0.018, dist)) * starBright;

        float horizonFade = smoothstep(0.0, 0.25, input.heightFrac);
        star *= horizonFade;

        star *= starVis * (1.0 - cloudFactor);

        float3 starColor = lerp(float3(0.7, 0.8, 1.0), float3(1.0, 0.85, 0.6), starBright * 0.3);
        result += starColor * star;
    }

    return float4(result, 1.0);
}
