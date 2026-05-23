# Tekstury, czcionki i tekst

## Ładowanie tekstur

### Obsługiwane formaty

| Format | Rozszerzenie | Mipmapy | Kompresja |
|--------|-------------|---------|-----------|
| DDS DXT1 | `.dds` | Tak (z pliku) | BC1 (RGBA, 8 bajtów/blok) |
| DDS DXT3 | `.dds` | Tak (z pliku) | BC2 (RGBA, 16 bajtów/blok) |
| DDS DXT5 | `.dds` | Tak (z pliku) | BC3 (RGBA, 16 bajtów/blok) |
| BMP 24-bit | `.bmp` | Generowane na GPU | Brak (RGBA8) |
| TGA 24/32-bit | `.tga` | Generowane na GPU | Brak (RGBA8) |

### Generowanie mipmap

Dla formatów BMP i TGA (które nie mają wbudowanych mipmap), silnik automatycznie
generuje mipmapy na GPU po uploadzie:

```cpp
// Oblicz liczbę poziomów
while (maxDim > 1) { maxDim /= 2; mipLevels++; }

// Dla każdego poziomu (oprócz 0):
vkCmdBlitImage(cmd, image, SRC_OPTIMAL, image, DST_OPTIMAL, 1, &blit, LINEAR);
```

`vkCmdBlitImage` z `VK_FILTER_LINEAR` tworzy pomniejszone wersje tekstury z
wygładzaniem. Każdy poziom jest 2x mniejszy od poprzedniego.

---

## Samplery (VkSampler)

Silnik tworzy 3 typy samplerów:

| Typ | Filtrowanie | Wrap mode | Użycie |
|-----|------------|-----------|--------|
| `createTextureSampler` | Linear + mipmap linear | REPEAT | Zwykłe tekstury |
| `createClampSampler` | Linear | CLAMP_TO_EDGE | Atlasy fontów, skybox |
| `createMirrorRepeatSampler` | Linear + mipmap linear | MIRRORED_REPEAT | Tekstury z odbiciem |

`createTextureSampler` używa `VK_SAMPLER_MIPMAP_MODE_LINEAR` — to kluczowe dla
uniknięcia aliasingu na oddalonych powierzchniach.

---

## Font Atlas

Czcionki TTF są rasteryzowane przez **SDL3_ttf** do atlasu — jednej dużej tekstury
zawierającej wszystkie glify:

```
FontAtlasLoader::load(fontAsset, codepoints)
│
├─ TTF_OpenFont() — otwórz plik .ttf z zadanym rozmiarem w pikselach
├─ Dla każdego codepoint (domyślnie ASCII 32-126):
│   ├─ Konwertuj na UTF-8
│   ├─ TTF_RenderText_Blended() — renderuj glif do SDL_Surface (RGBA)
│   ├─ TTF_GetGlyphMetrics() — pobierz metryki (minX, maxX, minY, maxY, advance)
│   │   ├─ bearingX = minX  (odległość od kursora do lewej krawędzi)
│   │   ├─ bearingY = maxY  (odległość od baseline do góry glifu, dodatnia = ponad)
│   │   └─ advanceX = advance (szerokość przesunięcia kursora)
│   ├─ Wyodrębnij kanał alfa z powierzchni RGBA → R8
│   └─ Spakuj w atlas (rząd poziomych glifów, zawijanie gdy pełny wiersz)
├─ Oblicz lineHeight = TTF_GetFontHeight()
└─ Zwróć FontAtlasData: piksele R8 + metryki glifów + UV
```

Format atlasu: `VK_FORMAT_R8_UNORM` — jeden bajt na piksel, wartość 0-255 = krycie.
W shaderze fragmentu, coverage = `texture(fontAtlas, uv).r`.

### Metryki glifów

Kluczowe metryki z `TTF_GetGlyphMetrics()`:

| Metryka | Znaczenie | Użycie w silniku |
|---------|-----------|-----------------|
| `minX` | lewy bearing (odległość kursora od lewej krawędzi glifu) | `bearingX` |
| `maxX` | prawa krawędź glifu względem kursora | `width ≈ maxX - minX` (przybliżone) |
| `minY` | dolna krawędź względem baseline (zwykle ≤ 0) | — |
| `maxY` | górna krawędź względem baseline (zwykle > 0) | `bearingY` |
| `advance` | przesunięcie kursora po glifie | `advanceX` |

**Uwaga**: `width` i `height` quada glifu pochodzą z wymiarów powierzchni SDL
(`glyphSurf->w`, `glyphSurf->h`), a nie z metryk. Metryki są używane tylko
do pozycjonowania (`bearingX`, `bearingY`) i przesuwania kursora (`advanceX`).

---

## TextOverlayRenderer

Klasa odpowiedzialna za renderowanie tekstu na ekranie (UI, FPS).

### Pipeline tekstu

| Stan | Wartość |
|------|---------|
| Topologia | `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` |
| Depth test | `VK_FALSE` (tekst zawsze na wierzchu) |
| Blend mode | Alpha (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`) |
| Cull mode | `VK_CULL_MODE_NONE` |
| Descriptor set | Binding 0: Combined Image Sampler (atlas fontu) |

Vertex shader przepuszcza pozycje bezpośrednio (są już w NDC):

```hlsl
VertexOutput vertexMain(VertexInput input) {
    output.position = float4(input.position, 0.0, 1.0);
    // input.position jest już w NDC [-1, 1]
    return output;
}
```

### Vertex format tekstu

```cpp
struct TextVertex {
    float pos[2];       // pozycja w NDC
    float texCoord[2];  // UV w atlasie fontu
    float color[4];     // kolor RGBA
};
```

Pozycje są wstępnie obliczone na CPU — funkcja `setText()` przelicza
współrzędne pikselowe na NDC i pakuje je w bufor wierzchołków.

### Przepływ

```
setText("FPS: 60", x, y, scale, color, screenW, screenH)
│
├─ Dla każdego znaku:
│   ├─ Pobierz FontAtlasGlyph z fontAtlas_
│   ├─ Oblicz pozycję quada w pikselach:
│   │   gx = cursorX + bearingX * scale
│   │   gy = baselineY - bearingY * scale
│   ├─ Przelicz na NDC:
│   │   ndcX = (px / screenW) * 2 - 1
│   │   ndcY = 1 - (py / screenH) * 2    // uwaga: Y odwrócone dla Vulkan
│   ├─ Generuj 6 wierzchołków (2 trójkąty na glif)
│   └─ cursorX += advanceX * scale
│
├─ Zapisz do bufora HOST_VISIBLE przez memcpy
└─ recordDrawCommands() → vkCmdDraw(vertexCount)
```

### Bufor wierzchołków

Bufor jest typu `HOST_VISIBLE | HOST_COHERENT`, aktualizowany przez `memcpy`
co klatkę (bez stagingu). Re-alokowany tylko gdy tekst się wydłuży.
