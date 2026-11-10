// Mgła wojny (fog of war) - wspólna dla przebiegu deferred i shadera terenu.
// Wartość widoczności z tekstury (filtrowanej liniowo - miękkie krawędzie):
//   0   = nieodkryte - czarne,
//   0.5 = odkryte, ale poza zasięgiem wzroku - przyciemnione do "explored" i wyszarzone
//         (widać ukształtowanie terenu, nie widać, co się tam teraz dzieje),
//   1   = widoczne - bez zmian.
#ifndef FOG_OF_WAR_HLSL
#define FOG_OF_WAR_HLSL

float2 fogOfWarUv(float3 worldPos, float originX, float originZ, float size)
{
    return (worldPos.xz - float2(originX, originZ)) / size;
}

// Poza obszarem mapy (np. ocean sięgający daleko za wyspę) widoczność = 0. Bez tego sampler z powtarzaniem
// (wspólny sampler tablicy tekstur) zawijał teksturę i odkryte miejsca pojawiały się drugi raz za krawędzią mapy.
float fogOfWarVisibility(Texture2D tex, SamplerState s, float2 uv)
{
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    return tex.SampleLevel(s, uv, 0).r;
}

float3 applyFogOfWar(float3 color, float vis, float explored)
{
    float brightness = vis < 0.5 ? lerp(0.0, explored, vis * 2.0) : lerp(explored, 1.0, (vis - 0.5) * 2.0);
    float gray = dot(color, float3(0.299, 0.587, 0.114));
    float3 dimmed = lerp(float3(gray, gray, gray), color, saturate(vis * 2.0 - 0.6));
    return dimmed * brightness;
}

#endif
