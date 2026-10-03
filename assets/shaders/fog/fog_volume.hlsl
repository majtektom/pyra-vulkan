#define FROXEL_Z 64

[[vk::push_constant]]
cbuffer FogPushConstants
{
    float4x4 invViewProj;
    float4 cameraPos;
    float4 fogColor;
    float4 lightDir;
    float fogDensity;
    float fogHeightFalloff;
    float seaLevel;
    float fogRadius;
};

// RWTexture3D<float4> kompiluje się do formatu Rgba32f — obraz po stronie C++ musi mieć R32G32B32A32_SFLOAT.
[[vk::binding(0, 0)]] RWTexture3D<float4> fogVolume;

struct LocalFogVolume
{
    float4 centerDensity;
    float4 halfExtentsEdgeFade;
};

[[vk::binding(1, 0)]] StructuredBuffer<LocalFogVolume> localFogVolumes;

float localFogDensity(float3 worldPos, uint volumeCount)
{
    float density = 0.0;
    [loop] for (uint i = 0; i < volumeCount; ++i)
    {
        LocalFogVolume volume = localFogVolumes[i];
        float3 normalizedOffset = abs(worldPos - volume.centerDensity.xyz)
            / max(volume.halfExtentsEdgeFade.xyz, 0.001);
        float boundaryDistance = length(normalizedOffset);
        float fadeWidth = max(volume.halfExtentsEdgeFade.w, 0.01);
        float weight = 1.0 - smoothstep(1.0 - fadeWidth, 1.0, boundaryDistance);
        density += volume.centerDensity.w * weight;
    }
    return density;
}

groupshared float densities[FROXEL_Z];
groupshared float3 inscatter[FROXEL_Z];
groupshared float distances[FROXEL_Z];

// Jedna grupa = jedna kolumna froxeli (piksel ekranu × FROXEL_Z plasterków wzdłuż promienia).
// Wynik w plasterku z: (światło rozproszone od kamery do plasterka, transmitancja do plasterka).
[numthreads(1, 1, FROXEL_Z)]
void main(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
    uint width, height, slices;
    fogVolume.GetDimensions(width, height, slices);

    uint x = id.x;
    uint y = id.y;
    uint z = gtid.z;

    // Macierz projekcji odwraca już Y pod Vulkana, więc NDC = uv * 2 - 1 bez dodatkowego odbicia
    // (ta sama konwencja co w ocean.hlsl i fog_composite.hlsl).
    float2 screenUV = (float2(x, y) + 0.5) / float2(width, height);
    float2 ndc = screenUV * 2.0 - 1.0;

    // Punkt na dalekiej płaszczyźnie (clip-space depth = 1.0) wyznacza kierunek promienia.
    float4 farWorld4 = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float3 farWorld = farWorld4.xyz / farWorld4.w;

    // Plasterki rozłożone równomiernie w odległości od kamery, do fogRadius.
    float t = (z + 0.5) / slices;
    float3 viewDir = normalize(farWorld - cameraPos.xyz);
    float maxDist = min(length(farWorld - cameraPos.xyz), fogRadius);
    float3 worldPos = cameraPos.xyz + viewDir * t * maxDist;
    distances[z] = t * maxDist;

    // Gęstość: globalna mgła wysokościowa + lokalne wolumeny (sumowane, nie zamiast).
    uint localVolumeCount = min((uint)round(lightDir.w), 32u);
    float density = fogDensity * exp(-fogHeightFalloff * max(0.0, worldPos.y - seaLevel));
    if (localVolumeCount > 0u)
        density += localFogDensity(worldPos, localVolumeCount);

    float noise = 0.5 + 0.5 * sin(worldPos.x * 0.015 + worldPos.y * 0.025 + worldPos.z * 0.015);
    density *= (0.6 + 0.4 * noise);

    // lightDir = kierunek promieni słońca; patrząc pod słońce (viewDir ≈ -lightDir) rozpraszanie rośnie.
    float VdotL = dot(viewDir, -lightDir.xyz);
    float phase = 0.15 + 0.85 * max(0.0, VdotL);

    densities[z] = density;
    inscatter[z] = fogColor.rgb * phase; // radiancja rozpraszana przez ośrodek (bez gęstości)

    GroupMemoryBarrierWithGroupSync();

    // Całkowanie od kamery w głąb (front-to-back): światło z każdego odcinka jest tłumione
    // przez transmitancję WSZYSTKIEGO, co leży bliżej kamery. Odcinek o gęstości d i długości
    // ds dodaje inscatter * (1 - exp(-d*ds)) — tyle światła, ile ośrodek pochłania, tyle rozprasza.
    float transmittance = 1.0;
    float3 accumulatedLight = 0.0;
    for (uint s = 0; s <= z; s++)
    {
        float stepSize = (s == 0) ? distances[0] : distances[s] - distances[s - 1];
        float stepT = exp(-densities[s] * max(stepSize, 0.0));

        accumulatedLight += transmittance * inscatter[s] * (1.0 - stepT);
        transmittance *= stepT;
    }

    fogVolume[uint3(x, y, z)] = float4(accumulatedLight, transmittance);
}
