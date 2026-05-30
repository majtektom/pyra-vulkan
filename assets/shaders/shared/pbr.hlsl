static const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float4 getLightPosition(uint i,
    float4 lp0, float4 lp1, float4 lp2, float4 lp3)
{
    if (i == 0) return lp0;
    if (i == 1) return lp1;
    if (i == 2) return lp2;
    return lp3;
}

float4 getLightDirection(uint i,
    float4 ld0, float4 ld1, float4 ld2, float4 ld3)
{
    if (i == 0) return ld0;
    if (i == 1) return ld1;
    if (i == 2) return ld2;
    return ld3;
}

float4 getLightColor(uint i,
    float4 lc0, float4 lc1, float4 lc2, float4 lc3)
{
    if (i == 0) return lc0;
    if (i == 1) return lc1;
    if (i == 2) return lc2;
    return lc3;
}

float4 getLightParams(uint i,
    float4 lr0, float4 lr1, float4 lr2, float4 lr3)
{
    if (i == 0) return lr0;
    if (i == 1) return lr1;
    if (i == 2) return lr2;
    return lr3;
}

float3 pbrDirectLight(float3 N, float3 V, float3 baseColor, float metallic, float roughness,
                      float NdotV, float3 F0, float3 L, float3 radiance)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    float3 kS = F;
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);

    return (kD * baseColor / PI + specular) * radiance * NdotL;
}

float3 evaluateLights(
    float3 N, float3 V, float3 baseColor, float metallic, float roughness,
    float NdotV, float3 F0, float3 worldPos,
    uint lightCount,
    float4 lp0, float4 ld0, float4 lc0, float4 lr0,
    float4 lp1, float4 ld1, float4 lc1, float4 lr1,
    float4 lp2, float4 ld2, float4 lc2, float4 lr2,
    float4 lp3, float4 ld3, float4 lc3, float4 lr3)
{
    float3 Lo = float3(0.0, 0.0, 0.0);
    uint count = min(lightCount, 4u);

    for (uint i = 0; i < count; i++)
    {
        float4 pos = getLightPosition(i, lp0, lp1, lp2, lp3);
        float4 dir = getLightDirection(i, ld0, ld1, ld2, ld3);
        float4 col = getLightColor(i, lc0, lc1, lc2, lc3);
        float4 par = getLightParams(i, lr0, lr1, lr2, lr3);
        uint ltype = uint(dir.w);

        if (ltype == 1)
        {
            float3 L = normalize(dir.xyz);
            float3 radiance = col.rgb * col.w;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 2)
        {
            float3 Lvec = pos.xyz - worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            float3 L = Lvec / dist;
            float3 radiance = col.rgb * col.w * attenuation;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 3)
        {
            float3 Lvec = pos.xyz - worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            float3 L = Lvec / dist;

            float3 spotDir = normalize(dir.xyz);
            float theta = dot(-L, spotDir);
            float inner = max(par.x, 0.01);
            float outer = max(par.y, 0.0);
            float spotIntensity = smoothstep(outer, inner, theta);

            float3 radiance = col.rgb * col.w * attenuation * spotIntensity;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
    }

    return Lo;
}
