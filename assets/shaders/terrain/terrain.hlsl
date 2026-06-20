#include "shared/light_eval.hlsl"
#include "shared/shadow_pcf.hlsl"

// Globalny bufor jednolity (UBO) - dane widoku, kamery i oświetlenia
cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 invViewProj;
    float4 camRight;
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16];
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
};

// Parametry push constants dla terenu (w tym tablice indeksów warstw i masek)
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float bumpIntensity;
    uint baseColorTextureSet;
    uint physicalDescriptorTextureSet;
    uint normalTextureSet;
    uint occlusionTextureSet;
    uint emissiveTextureSet;
    float alphaMaskCutoff;
    uint layerBlendEnabled;                 // Czy mieszanie warstw teksturowych jest włączone
    uint layerCount;                        // Liczba warstw terenu (maksymalnie 12)
    float layerWorldSize;                   // Szerokość świata terenu (używana przy skalingu UV masek)
    uint4 layerBaseTextureSets[3];          // Spakowane indeksy tekstur warstwowych (3 * 4 = 12 warstw)
    uint4 layerMaskTextureSets[3];          // Spakowane indeksy tekstur masek przejść (12 masek)
    uint heightmapTextureSet;
    float scaleY;
    float tessellationFactor;
    uint heightmapEnabled;
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0); // Dynamiczna tablica bindless

// Powiązanie z tablicą map cieni oraz samplerem porównującym
[[vk::binding(2, 0)]] Texture2DArray<float> shadowMapArray;
[[vk::binding(2, 0)]] SamplerComparisonState shadowSampler;

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
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldPos : TEXCOORD2;
    [[vk::location(4)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(5)]] float2 localPosXZ : TEXCOORD4; // Współrzędne lokalne X i Z (potrzebne do wyznaczenia UV dla masek)
};

struct HS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
};

struct HS_CONSTANT_DATA_OUTPUT
{
    float EdgeTessFactor[3]  : SV_TessFactor;
    float InsideTessFactor   : SV_InsideTessFactor;
};

struct HS_OUTPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
};

float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

HS_INPUT vertexMain(VertexInput input)
{
    HS_INPUT output;
    output.position = input.position;
    output.normal = input.normal;
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

VertexOutput vertexMainNoTess(VertexInput input)
{
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.matColor = baseColorFactor;
    output.localPosXZ = input.position.xz;
    return output;
}

HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<HS_INPUT, 3> ip, uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT output;
    float tf = tessellationFactor;
    output.EdgeTessFactor[0] = tf;
    output.EdgeTessFactor[1] = tf;
    output.EdgeTessFactor[2] = tf;
    output.InsideTessFactor  = tf;
    return output;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")]
HS_OUTPUT hullMain(InputPatch<HS_INPUT, 3> ip, uint i : SV_OutputControlPointID, uint PatchID : SV_PrimitiveID)
{
    HS_OUTPUT output;
    output.position = ip[i].position;
    output.normal = ip[i].normal;
    output.color = ip[i].color;
    output.texCoord = ip[i].texCoord;
    return output;
}

[domain("tri")]
VertexOutput domainMain(HS_CONSTANT_DATA_OUTPUT input, float3 barycentricCoords : SV_DomainLocation, const OutputPatch<HS_OUTPUT, 3> patch)
{
    VertexOutput output;
    
    // Interpolacja wierzchołków za pomocą współrzędnych barycentrycznych
    float3 position = patch[0].position * barycentricCoords.x +
                      patch[1].position * barycentricCoords.y +
                      patch[2].position * barycentricCoords.z;
                      
    float3 normal = patch[0].normal * barycentricCoords.x +
                    patch[1].normal * barycentricCoords.y +
                    patch[2].normal * barycentricCoords.z;
                    
    float3 color = patch[0].color * barycentricCoords.x +
                   patch[1].color * barycentricCoords.y +
                   patch[2].color * barycentricCoords.z;
                   
    float2 texCoord = patch[0].texCoord * barycentricCoords.x +
                      patch[1].texCoord * barycentricCoords.y +
                      patch[2].texCoord * barycentricCoords.z;

    // Przemieszczenie na podstawie mapy wysokości (Heightmap displacement)
    if (heightmapEnabled != 0)
    {
        float2 heightmapUV = position.xz / max(layerWorldSize, 0.0001);
        // Dobór mip level heightmapy na podstawie odległości: im dalej/drobniejsza siatka, tym wyższy mip
        float heightmapMip = max(0, 2.0 - log2(max(tessellationFactor, 1.0)));
        float h = textures[heightmapTextureSet].SampleLevel(globalSampler, heightmapUV, heightmapMip).r * 255.0 * scaleY;
        position.y = h;

        // Obliczenie szczegółowej normalnej z mapy wysokości za pomocą różnic centralnych
        float offset = 0.5; // Krok w jednostkach świata
        float uvOffset = offset / max(layerWorldSize, 0.0001);
        
        float hL = textures[heightmapTextureSet].SampleLevel(globalSampler, heightmapUV + float2(-uvOffset, 0), heightmapMip).r * 255.0 * scaleY;
        float hR = textures[heightmapTextureSet].SampleLevel(globalSampler, heightmapUV + float2(uvOffset, 0), heightmapMip).r * 255.0 * scaleY;
        float hD = textures[heightmapTextureSet].SampleLevel(globalSampler, heightmapUV + float2(0, -uvOffset), 0).r * 255.0 * scaleY;
        float hU = textures[heightmapTextureSet].SampleLevel(globalSampler, heightmapUV + float2(0, uvOffset), 0).r * 255.0 * scaleY;
        
        normal = normalize(float3(hL - hR, 2.0 * offset, hD - hU));
    }
    
    float4 worldPos4 = mul(model, float4(position, 1.0));
    
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), normal));
    output.color = color;
    output.texCoord = texCoord;
    output.matColor = baseColorFactor;
    output.localPosXZ = position.xz;
    
    return output;
}

/**
 * @brief Próbkuje maskę splat-mapy (wagę warstwy) na podstawie spakowanej tablicy.
 */
float sampleTerrainMask(uint index, float2 worldUV)
{
    uint group = index / 4u; // Grupa w tablicy uint4
    uint lane = index % 4u;  // Indeks składowej (x, y, z lub w)
    if (group == 0u) return textures[layerMaskTextureSets[0][lane]].Sample(globalSampler, worldUV).r;
    if (group == 1u) return textures[layerMaskTextureSets[1][lane]].Sample(globalSampler, worldUV).r;
    return textures[layerMaskTextureSets[2][lane]].Sample(globalSampler, worldUV).r;
}

/**
 * @brief Pobiera indeks tekstury tekstury fizycznej z tablicy bindless.
 */
uint sampleTerrainTextureSet(uint index)
{
    uint group = index / 4u;
    uint lane = index % 4u;
    if (group == 0u) return layerBaseTextureSets[0][lane];
    if (group == 1u) return layerBaseTextureSets[1][lane];
    return layerBaseTextureSets[2][lane];
}

/**
 * @brief Próbkuje kolor konkretnej warstwy terenu.
 */
float3 sampleTerrainLayer(uint index, float2 uv)
{
    return textures[sampleTerrainTextureSet(index)].Sample(globalSampler, uv).rgb;
}

/**
 * @brief Shader pikseli terenu (Splatting + PBR + Cienie).
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // Domyślny kolor pobierany z głównej tekstury dyfuzyjnej
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * baseColorFactor;

    // KROK 1: MIESZANIE WIELU WARSTW TEKSTUR (Texture Splatting)
    if (layerBlendEnabled != 0 && layerCount > 0)
    {
        float worldSize = max(layerWorldSize, 0.0001);
        // Współrzędne UV dla maski są rozciągnięte na całą wielkość terenu [0.0, 1.0]
        float2 maskWorldUV = input.localPosXZ / worldSize;
        
        // Zaczynamy od warstwy podstawowej (tło terenu np. trawa pod spodem)
        float3 blended = sampleTerrainLayer(0u, input.texCoord);

        uint blendLayerCount = min(layerCount, 12u);
        // Iteracyjne nakładanie kolejnych warstw na podstawie masek wagowych (Splat Maps)
        for (uint i = 1u; i < blendLayerCount; ++i)
        {
            float maskVal = saturate(sampleTerrainMask(i, maskWorldUV));
            if (maskVal > 0.001)
            {
                float3 layerColor = sampleTerrainLayer(i, input.texCoord);
                blended = lerp(blended, layerColor, maskVal); // Liniowe mieszanie (blendowanie)
            }
        }
        baseColor.rgb = blended;
    }

    if (baseColor.a < alphaMaskCutoff)
        discard;

    // Normal mapping
    float3 N = normalize(input.worldNormal);
    float3 dp1 = ddx(input.worldPos);
    float3 dp2 = ddy(input.worldPos);
    float2 duv1 = ddx(input.texCoord);
    float2 duv2 = ddy(input.texCoord);

    if (bumpIntensity > 0.001)
    {
        float3 normalTex = textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz * 2.0 - 1.0;
        normalTex.xy *= bumpIntensity;
        normalTex = normalize(normalTex);

        float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
        float3 T_raw = (dp1 * duv2.y - dp2 * duv1.y) * r;
        float3 T_proj = T_raw - N * dot(N, T_raw);
        float lenSq = dot(T_proj, T_proj);
        if (lenSq > 0.00001)
        {
            float3 T = T_proj / sqrt(lenSq);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            N = normalize(mul(normalTex, TBN));
        }
    }

    float3 color;
    if (lightingEnabled == 0)
    {
        color = baseColor.rgb;
        if (shadowsEnabled != 0u && lightCount > 0u)
        {
            float shadow = sampleShadowPcf(shadowMapArray, shadowSampler, lights[0], input.worldPos, N);
            color = baseColor.rgb * lerp(0.35, 1.0, shadow);
        }
    }
    else
    {
        // Odczyt parametrów PBR
        float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
        float metallic = metallicRoughness.x * metallicFactor;
        float roughness = max(metallicRoughness.y * roughnessFactor, 0.04);

        float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
        float3 emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;

        float3 V = normalize(cameraPos.xyz - input.worldPos);
        float NdotV = max(dot(N, V), 0.0);

        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, baseColor.rgb, metallic);

        float3 Lo = float3(0.0, 0.0, 0.0);

        // KROK 2: WYDLICZENIE OŚWIETLENIA BEZPOŚREDNIEGO Z UWZGLĘDNIENIEM MAPY CIENI
        uint count = min(lightCount, 16u);
        for (uint i = 0; i < count; i++)
        {
            float3 L;
            float3 radiance;
            uint valid;
            unpackDirectLight(lights[i], input.worldPos, L, radiance, valid);
            if (valid == 0u) continue;

            // Próbkowanie mapy cieni (Shadow Map) z filtrowaniem PCF
            float shadow = (shadowsEnabled != 0) ? sampleShadowPcf(shadowMapArray, shadowSampler, lights[i], input.worldPos, N) : 1.0;
            
            // Zastosowanie cienia jako mnożnik oświetlenia bezpośredniego
            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance) * shadow;
        }

        float3 ambientContrib = baseColor.rgb * ambientStrength * ambientColor.rgb * ao;
        color = ambientContrib + Lo + emissive;
        color = saturate(color);
    }

    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, input.worldPos);
        float fogFactor = (fogEnd - dist) / (fogEnd - fogStart);
        fogFactor = saturate(fogFactor);
        color = lerp(fogColor.rgb, color, fogFactor);
    }

    return float4(color, baseColor.a);
}