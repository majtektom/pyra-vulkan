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

Czcionki TTF są rasteryzowane przez **FreeType** do atlasu — jednej dużej tekstury
zawierającej wszystkie glify:

```
FontAtlasLoader::load(fontAsset, codepoints)
│
├─ FreeType: otwórz plik .ttf
├─ Ustaw pixelHeight (np. 24 → 24 piksele)
├─ Dla każdego codepoint (domyślnie ASCII 32-126):
│   ├─ FT_Load_Char() — rasteryzuj glif
│   ├─ Zapisz metryki: bearingX, bearingY, advanceX, width, height
│   └─ Skopiuj bitmapę do atlasu
├─ Spakuj glify w atlas (prosty algorytm: rząd poziomych glifów)
└─ Zwróć FontAtlasData: piksele R8 + metryki glifów
```

Format atlasu: `VK_FORMAT_R8_UNORM` — jeden bajt na piksel, wartość 0-255 = krycie.
W shaderze fragmentu, coverage = `texture(fontAtlas, uv).r`.

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
