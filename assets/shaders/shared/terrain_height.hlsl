#ifndef SHARED_TERRAIN_HEIGHT_HLSL
#define SHARED_TERRAIN_HEIGHT_HLSL

/**
 * Wysokość terenu teselowanego w punkcie siatki (x, z w układzie lokalnym terenu).
 *
 * Wspólna dla shadera terenu (ekran) i shadera map cieni terenu - dzięki temu cień powstaje
 * z dokładnie tej samej powierzchni, którą widać. Próbka w środku teksela (wierzchołek siatki CPU
 * w punkcie ix * scaleXZ ma wysokość teksela ix) i zawsze mip 0 (pełna szczegółowość).
 *
 * @param heightmap  tekstura mapy wysokości (0..1 = 0..255 jednostek mapy)
 * @param worldSize  szerokość terenu w jednostkach (rozmiar mapy * scaleXZ)
 * @param scaleY     jednostki świata na jednostkę mapy
 */
float terrainHeightAt(Texture2D heightmap, SamplerState samp, float2 posXZ, float worldSize, float scaleY)
{
    uint w, h;
    heightmap.GetDimensions(w, h);
    float2 uv = posXZ / max(worldSize, 0.0001) + 0.5 / float2(w, h);
    return heightmap.SampleLevel(samp, uv, 0.0).r * 255.0 * scaleY;
}

#endif
