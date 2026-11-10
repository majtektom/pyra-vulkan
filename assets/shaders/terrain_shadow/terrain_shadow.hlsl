// Mapa cieni dla terenu teselowanego: ta sama teselacja i ta sama wysokość (shared/terrain_height.hlsl)
// co shader terenu na ekranie, tylko pozycja liczona macierzą światła. Cień powstaje więc z dokładnie
// tej geometrii, którą widać - bez "cieni znikąd" w dolinach.
#include "shared/terrain_height.hlsl"

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;          // macierz terenu
    column_major float4x4 lightViewProj;  // widok-projekcja światła (kaskada / światło lokalne)
    uint heightmapTextureSet;             // indeks mapy wysokości w tablicy bindless
    float scaleY;
    float tessellationFactor;             // ten sam co na ekranie (zależny od LOD węzła)
    float layerWorldSize;                 // szerokość terenu w jednostkach
};

#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct ControlPoint
{
    float3 position : POSITION;
};

struct PatchConstants
{
    float EdgeTessFactor[3] : SV_TessFactor;
    float InsideTessFactor  : SV_InsideTessFactor;
};

struct DomainOutput
{
    float4 position : SV_Position;
};

ControlPoint vertexMain(VertexInput input)
{
    ControlPoint o;
    o.position = input.position;
    return o;
}

PatchConstants ConstantsHS(InputPatch<ControlPoint, 3> ip, uint patchId : SV_PrimitiveID)
{
    PatchConstants o;
    o.EdgeTessFactor[0] = tessellationFactor;
    o.EdgeTessFactor[1] = tessellationFactor;
    o.EdgeTessFactor[2] = tessellationFactor;
    o.InsideTessFactor = tessellationFactor;
    return o;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")]
ControlPoint hullMain(InputPatch<ControlPoint, 3> ip, uint i : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    ControlPoint o;
    o.position = ip[i].position;
    return o;
}

[domain("tri")]
DomainOutput domainMain(PatchConstants pc, float3 bary : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch)
{
    float3 p = patch[0].position * bary.x + patch[1].position * bary.y + patch[2].position * bary.z;
    p.y = terrainHeightAt(textures[heightmapTextureSet], globalSampler, p.xz, layerWorldSize, scaleY);
    DomainOutput o;
    o.position = mul(lightViewProj, mul(model, float4(p, 1.0)));
    return o;
}

// Tylko głębokość - fragment shader nic nie zapisuje.
void fragmentMain()
{
}
