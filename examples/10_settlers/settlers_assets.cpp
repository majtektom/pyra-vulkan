/**
 * settlers_assets.cpp - proceduralne modele 3D i materiały gry.
 *
 * Wszystkie modele są składane w kodzie z prostych brył (MeshBuilder z silnika):
 * prostopadłościanów, walców, stożków, sfer i dachów. Nie ma żadnych plików modeli.
 *
 * KONWENCJE
 *  - y = 0 to poziom ziemi; budynki mają fundament schodzący pod ziemię (maskuje nierówny teren),
 *  - przód modelu (drzwi, twarz postaci) jest skierowany w stronę +Z,
 *  - kolory są zapisane w wierzchołkach, więc wszystkie modele używają jednego materiału,
 *  - budynki i postacie mają wersję dla każdej frakcji (flagi, dachy, ubrania w kolorze frakcji),
 *    a nazwy siatek tworzą funkcje buildingMeshName / unitMeshName.
 */
#include "settlers_assets.h"

#include <cmath>

#include "core/engine.h"
#include "core/mesh_builder.h"
#include "core/mesh_manager.h"
#include "render/material_manager.h"

using namespace engine3d;

namespace settlers {

namespace {

// Paleta kolorów
const Vec3 kSkin{0.93f, 0.76f, 0.6f};
const Vec3 kPants{0.33f, 0.24f, 0.15f};
const Vec3 kStraw{0.86f, 0.74f, 0.4f};
const Vec3 kWood{0.55f, 0.36f, 0.19f};
const Vec3 kDarkWood{0.33f, 0.21f, 0.11f};
const Vec3 kStone{0.6f, 0.59f, 0.56f};
const Vec3 kDarkStone{0.45f, 0.44f, 0.42f};
const Vec3 kPlaster{0.9f, 0.85f, 0.72f};
const Vec3 kRoofTile{0.66f, 0.22f, 0.13f};
const Vec3 kThatch{0.78f, 0.66f, 0.33f};
const Vec3 kSlate{0.3f, 0.32f, 0.38f};
const Vec3 kSteel{0.78f, 0.8f, 0.84f};
const Vec3 kMail{0.52f, 0.54f, 0.58f};
const Vec3 kWindow{0.18f, 0.22f, 0.32f};

// Przyciemnia (f < 1) lub rozjaśnia (f > 1) kolor.
Vec3 shade(const Vec3& c, float f) {
    return {std::min(1.0f, c.x * f), std::min(1.0f, c.y * f), std::min(1.0f, c.z * f)};
}

// Maszt z flagą w kolorze frakcji.
void flag(MeshBuilder& b, const Vec3& base, float height, const Vec3& color) {
    b.cylinder(base, 0.22f, height, 6, kDarkWood);
    b.box({base.x + 1.15f, base.y + height - 0.8f, base.z}, {2.2f, 1.4f, 0.12f}, color);
    b.sphere({base.x, base.y + height + 0.2f, base.z}, 0.32f, 3, 6, {0.85f, 0.75f, 0.3f});
}

// Fundament schodzący pod ziemię - maskuje nierówności terenu pod budynkiem.
void foundation(MeshBuilder& b, float half) {
    b.box({0.0f, -2.0f, 0.0f}, {half * 2.0f - 0.6f, 5.0f, half * 2.0f - 0.6f}, kDarkStone);
}

// Zamek: mury z blankami, cztery baszty z dachami w kolorze frakcji, donżon i brama.
MeshData castleMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 16.0f);
    b.box({0.0f, 0.55f, 0.0f}, {27.0f, 0.3f, 27.0f}, {0.5f, 0.42f, 0.3f});  // dziedziniec
    // Mury obronne
    b.box({0.0f, 4.5f, -14.0f}, {29.0f, 9.0f, 2.6f}, kStone);
    b.box({-14.0f, 4.5f, 0.0f}, {2.6f, 9.0f, 29.0f}, kStone);
    b.box({14.0f, 4.5f, 0.0f}, {2.6f, 9.0f, 29.0f}, kStone);
    b.box({-8.75f, 4.5f, 14.0f}, {11.5f, 9.0f, 2.6f}, kStone);
    b.box({8.75f, 4.5f, 14.0f}, {11.5f, 9.0f, 2.6f}, kStone);
    // Brama
    b.box({0.0f, 8.0f, 14.0f}, {6.0f, 2.0f, 2.8f}, kStone);
    b.box({0.0f, 3.5f, 14.3f}, {5.0f, 7.0f, 0.6f}, kDarkWood);
    // Blanki na murach
    for (int i = -4; i <= 4; ++i) {
        const float p = static_cast<float>(i) * 3.2f;
        b.box({p, 9.6f, -14.0f}, {1.4f, 1.2f, 2.8f}, kStone);
        b.box({-14.0f, 9.6f, p}, {2.8f, 1.2f, 1.4f}, kStone);
        b.box({14.0f, 9.6f, p}, {2.8f, 1.2f, 1.4f}, kStone);
        if (std::abs(i) >= 2) b.box({p, 9.6f, 14.0f}, {1.4f, 1.2f, 2.8f}, kStone);
    }
    // Baszty narożne z dachami w kolorze frakcji
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            const Vec3 base{14.0f * sx, 0.0f, 14.0f * sz};
            b.cylinder(base, 3.6f, 14.0f, 10, shade(kStone, 1.05f));
            b.cylinder({base.x, 14.0f, base.z}, 4.1f, 1.0f, 10, kDarkStone);
            b.cone({base.x, 15.0f, base.z}, 4.4f, 6.5f, 10, fc);
        }
    }
    // Donżon
    b.box({0.0f, 9.0f, -3.0f}, {13.0f, 18.0f, 11.0f}, shade(kStone, 1.12f));
    b.box({0.0f, 10.0f, 2.6f}, {2.0f, 2.6f, 0.2f}, kWindow);
    b.box({-4.0f, 13.0f, 2.6f}, {1.6f, 2.2f, 0.2f}, kWindow);
    b.box({4.0f, 13.0f, 2.6f}, {1.6f, 2.2f, 0.2f}, kWindow);
    b.pyramid({0.0f, 18.0f, -3.0f}, 14.5f, 12.5f, 7.0f, shade(fc, 0.8f));
    flag(b, {0.0f, 24.5f, -3.0f}, 7.0f, fc);
    return b.build();
}

// Dom: ściany z belkami, okna, drzwi, dach dwuspadowy z kominem.
MeshData houseMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 7.0f);
    b.box({0.0f, 3.5f, 0.0f}, {10.0f, 6.0f, 9.0f}, kPlaster);
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            b.box({5.0f * sx, 3.5f, 4.5f * sz}, {0.7f, 6.0f, 0.7f}, kDarkWood);
        }
    }
    b.box({0.0f, 6.4f, 0.0f}, {10.4f, 0.5f, 9.4f}, kDarkWood);
    b.box({0.0f, 2.2f, 4.55f}, {2.2f, 3.4f, 0.2f}, kDarkWood);
    b.box({-3.0f, 4.0f, 4.55f}, {1.6f, 1.4f, 0.2f}, kWindow);
    b.box({3.0f, 4.0f, 4.55f}, {1.6f, 1.4f, 0.2f}, kWindow);
    b.gableRoof({0.0f, 6.5f, 0.0f}, 11.6f, 10.6f, 5.0f, kRoofTile);
    b.box({3.0f, 10.0f, -2.0f}, {1.3f, 3.6f, 1.3f}, {0.55f, 0.3f, 0.22f});
    flag(b, {5.8f, 0.0f, 5.6f}, 9.0f, fc);
    return b.build();
}

// Chata drwala: drewniane ściany, stos bali, pieniek z siekierą.
MeshData woodcutterMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 7.5f);
    b.box({0.0f, 3.0f, -1.5f}, {10.0f, 5.0f, 8.0f}, kWood);
    for (int i = 0; i < 4; ++i) {
        b.box({0.0f, 1.0f + i * 1.2f, 2.55f}, {10.2f, 0.35f, 0.15f}, kDarkWood);  // bale
    }
    b.box({-2.5f, 2.0f, 2.55f}, {2.0f, 3.2f, 0.2f}, kDarkWood);
    b.gableRoof({0.0f, 5.5f, -1.5f}, 11.0f, 9.4f, 3.8f, shade(kThatch, 0.75f));
    // Stos drewna
    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i < 3 - layer; ++i) {
            b.box({3.2f, 0.55f + layer * 0.95f, 3.6f + (i - (2 - layer) * 0.5f) * 1.0f},
                  {4.2f, 0.9f, 0.9f}, shade(kWood, 1.1f - layer * 0.08f));
        }
    }
    // Pieniek z siekierą
    b.cylinder({-3.5f, 0.0f, 5.0f}, 0.9f, 1.1f, 8, kWood);
    b.box({-3.5f, 1.6f, 5.0f}, {0.15f, 1.6f, 0.15f}, kDarkWood);
    b.box({-3.5f, 2.3f, 5.3f}, {0.15f, 0.6f, 0.7f}, kSteel);
    flag(b, {-5.8f, 0.0f, -5.5f}, 9.0f, fc);
    return b.build();
}

// Kamieniarz: kamienna chata z łupkowym dachem i ułożone bloki kamienia.
MeshData quarryMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 7.5f);
    b.box({-1.0f, 3.0f, -2.0f}, {9.0f, 5.0f, 7.0f}, kStone);
    b.box({-1.0f, 2.0f, 1.55f}, {2.0f, 3.2f, 0.2f}, kDarkWood);
    b.gableRoof({-1.0f, 5.5f, -2.0f}, 10.0f, 8.4f, 3.6f, kSlate);
    // Bloki kamienne
    b.box({3.6f, 0.75f, 3.6f}, {1.5f, 1.5f, 1.5f}, shade(kStone, 1.1f));
    b.box({5.2f, 0.75f, 3.6f}, {1.5f, 1.5f, 1.5f}, shade(kStone, 0.95f));
    b.box({3.6f, 0.75f, 5.2f}, {1.5f, 1.5f, 1.5f}, shade(kStone, 1.0f));
    b.box({4.4f, 2.25f, 4.2f}, {1.5f, 1.5f, 1.5f}, shade(kStone, 1.15f));
    b.box({-4.5f, 0.5f, 4.8f}, {2.4f, 1.0f, 1.2f}, shade(kStone, 0.9f));
    flag(b, {-5.8f, 0.0f, 5.8f}, 9.0f, fc);
    return b.build();
}

// Farma: zagroda na tyłach i pole ze zbożem z płotkiem (pole jest częścią modelu).
MeshData farmMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 12.0f);
    // Zagroda na tyłach
    b.box({0.0f, 3.0f, -7.0f}, {10.0f, 5.0f, 7.0f}, kPlaster);
    b.box({0.0f, 2.0f, -3.45f}, {2.0f, 3.2f, 0.2f}, kDarkWood);
    b.gableRoof({0.0f, 5.5f, -7.0f}, 11.0f, 8.0f, 4.2f, kThatch);
    // Pole uprawne
    b.box({0.0f, 0.35f, 4.0f}, {22.0f, 0.7f, 14.0f}, {0.42f, 0.29f, 0.16f});
    for (int i = -3; i <= 3; ++i) {
        b.box({static_cast<float>(i) * 3.0f, 1.3f, 4.0f}, {1.3f, 1.4f, 12.5f}, {0.88f, 0.76f, 0.28f});
    }
    // Płotek
    for (int i = -5; i <= 5; ++i) {
        b.box({static_cast<float>(i) * 2.1f, 0.9f, 11.4f}, {0.25f, 1.8f, 0.25f}, kWood);
    }
    b.box({0.0f, 1.4f, 11.4f}, {22.0f, 0.2f, 0.15f}, kWood);
    flag(b, {-10.5f, 0.0f, -10.5f}, 10.0f, fc);
    return b.build();
}

// Koszary: długi budynek, plac ćwiczeń z manekinem i stojakiem na broń.
MeshData barracksMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 11.0f);
    b.box({0.0f, 4.0f, -3.0f}, {18.0f, 7.0f, 11.0f}, shade(kStone, 1.08f));
    for (int i = -2; i <= 2; ++i) {
        b.box({static_cast<float>(i) * 4.4f, 4.0f, 2.55f}, {0.8f, 7.0f, 0.3f}, kDarkWood);
    }
    b.box({0.0f, 2.6f, 2.6f}, {3.2f, 4.4f, 0.25f}, kDarkWood);
    b.gableRoof({0.0f, 7.5f, -3.0f}, 19.0f, 12.4f, 4.6f, {0.48f, 0.15f, 0.1f});
    // Plac ćwiczeń: manekin i stojak na broń
    b.cylinder({5.0f, 0.0f, 7.0f}, 0.25f, 3.6f, 6, kWood);
    b.box({5.0f, 3.0f, 7.0f}, {2.4f, 0.3f, 0.3f}, kWood);
    b.sphere({5.0f, 4.0f, 7.0f}, 0.7f, 4, 6, kStraw);
    b.box({5.0f, 2.4f, 7.0f}, {1.2f, 1.6f, 0.8f}, kStraw);
    b.box({-5.0f, 1.8f, 7.0f}, {4.0f, 0.25f, 0.4f}, kWood);
    for (int i = -1; i <= 1; ++i) {
        b.box({-5.0f + i * 1.2f, 1.6f, 7.2f}, {0.15f, 3.2f, 0.15f}, kSteel);
    }
    flag(b, {-9.5f, 0.0f, 9.0f}, 13.0f, fc);
    flag(b, {9.5f, 0.0f, 9.0f}, 13.0f, fc);
    return b.build();
}

// Wieża: kamienna, z blankami i stożkowym dachem w kolorze frakcji.
MeshData towerMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 5.5f);
    b.cylinder({0.0f, 0.0f, 0.0f}, 5.0f, 3.0f, 10, kDarkStone);
    b.cylinder({0.0f, 3.0f, 0.0f}, 4.2f, 19.0f, 10, kStone, 3.8f);
    b.box({0.0f, 12.0f, 3.9f}, {1.0f, 2.0f, 0.3f}, kWindow);
    b.box({0.0f, 2.2f, 4.3f}, {2.0f, 3.2f, 0.3f}, kDarkWood);
    b.cylinder({0.0f, 22.0f, 0.0f}, 5.0f, 1.4f, 10, kDarkStone);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * PI * 0.25f;
        b.box({std::cos(a) * 4.5f, 24.0f, std::sin(a) * 4.5f}, {1.3f, 1.6f, 1.3f}, kStone);
    }
    b.cone({0.0f, 24.2f, 0.0f}, 4.4f, 6.0f, 10, fc);
    flag(b, {0.0f, 30.0f, 0.0f}, 4.5f, fc);
    return b.build();
}

// Chata leśniczego: drewniany domek z zielonym dachem, grządka z sadzonkami i łopata.
MeshData foresterMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 7.0f);
    b.box({0.0f, 3.0f, -1.5f}, {9.0f, 5.0f, 7.5f}, shade(kWood, 1.1f));
    b.box({-2.0f, 2.0f, 2.3f}, {2.0f, 3.2f, 0.2f}, kDarkWood);
    b.box({2.2f, 3.6f, 2.3f}, {1.4f, 1.2f, 0.2f}, kWindow);
    b.gableRoof({0.0f, 5.5f, -1.5f}, 10.0f, 8.6f, 3.6f, {0.22f, 0.45f, 0.2f});
    // Grządka z sadzonkami przed chatą
    b.box({2.5f, 0.2f, 4.8f}, {5.0f, 0.4f, 2.6f}, {0.38f, 0.26f, 0.15f});
    for (int i = 0; i < 3; ++i) {
        const float x = 0.9f + static_cast<float>(i) * 1.6f;
        b.cylinder({x, 0.3f, 4.8f}, 0.12f, 0.8f, 5, kDarkWood);
        b.cone({x, 0.9f, 4.8f}, 0.55f, 1.2f, 6, {0.2f, 0.5f, 0.2f});
    }
    // Łopata oparta o ścianę
    b.box({-4.2f, 1.5f, 2.6f}, {0.15f, 3.0f, 0.15f}, kWood);
    b.box({-4.2f, 0.3f, 2.6f}, {0.6f, 0.7f, 0.1f}, kSteel);
    flag(b, {-5.5f, 0.0f, -5.0f}, 9.0f, fc);
    return b.build();
}

// Kopalnia: skalny kopiec z wejściem obudowanym belkami, tory i wózek z kamieniem.
MeshData mineMesh(const Vec3& fc) {
    MeshBuilder b;
    foundation(b, 8.0f);
    const Vec3 rock{0.48f, 0.47f, 0.45f};
    // Skalny kopiec (część zbocza)
    b.sphere({0.0f, 1.5f, -2.5f}, 7.5f, 4, 7, rock);
    b.sphere({-4.0f, 0.5f, -1.0f}, 4.0f, 3, 6, shade(rock, 0.9f));
    b.sphere({4.2f, 0.8f, -1.5f}, 4.2f, 3, 6, shade(rock, 1.1f));
    // Wejście: ciemny otwór i drewniana obudowa
    b.box({0.0f, 2.2f, 4.0f}, {3.4f, 4.4f, 1.2f}, {0.08f, 0.07f, 0.06f});
    b.box({-2.0f, 2.4f, 4.4f}, {0.6f, 4.8f, 0.6f}, kDarkWood);
    b.box({2.0f, 2.4f, 4.4f}, {0.6f, 4.8f, 0.6f}, kDarkWood);
    b.box({0.0f, 5.0f, 4.4f}, {4.8f, 0.7f, 0.8f}, kDarkWood);
    // Tory i wózek z urobkiem
    b.box({-0.6f, 0.15f, 6.5f}, {0.2f, 0.2f, 4.0f}, kSteel);
    b.box({0.6f, 0.15f, 6.5f}, {0.2f, 0.2f, 4.0f}, kSteel);
    b.box({0.0f, 0.9f, 7.0f}, {1.8f, 1.1f, 2.2f}, kWood);
    b.box({0.0f, 1.6f, 7.0f}, {1.5f, 0.5f, 1.8f}, kStone);
    flag(b, {4.5f, 0.0f, 5.5f}, 9.0f, fc);
    return b.build();
}

// Osadnik w słomkowym kapeluszu; wariant z niesionym surowcem (drewno / kamień / worek zboża).
MeshData settlerMesh(const Vec3& fc, CarryType carry) {
    MeshBuilder b;
    b.box({-0.4f, 1.1f, 0.0f}, {0.6f, 2.2f, 0.8f}, kPants);
    b.box({0.4f, 1.1f, 0.0f}, {0.6f, 2.2f, 0.8f}, kPants);
    b.cylinder({0.0f, 2.1f, 0.0f}, 0.95f, 2.5f, 8, fc, 0.8f);
    b.box({0.0f, 2.3f, 0.0f}, {2.0f, 0.35f, 1.7f}, kDarkWood);   // pasek
    b.box({-1.15f, 3.4f, 0.0f}, {0.45f, 1.9f, 0.55f}, shade(fc, 0.85f));
    b.box({1.15f, 3.4f, 0.0f}, {0.45f, 1.9f, 0.55f}, shade(fc, 0.85f));
    b.sphere({0.0f, 5.15f, 0.0f}, 0.78f, 5, 8, kSkin);
    b.cylinder({0.0f, 5.55f, 0.0f}, 1.35f, 0.15f, 10, kStraw);
    b.cone({0.0f, 5.7f, 0.0f}, 0.85f, 0.9f, 8, kStraw);
    switch (carry) {
    case CarryType::Wood:
        b.box({0.75f, 4.6f, 0.0f}, {0.65f, 0.65f, 3.4f}, kWood);
        break;
    case CarryType::Stone:
        b.box({0.0f, 3.7f, 1.0f}, {1.4f, 1.1f, 1.1f}, kStone);
        break;
    case CarryType::Food:
        b.sphere({0.0f, 3.6f, 1.1f}, 0.85f, 4, 7, {0.85f, 0.75f, 0.45f});
        break;
    default:
        break;
    }
    return b.build();
}

// Miecznik: kolczuga, tunika w kolorze frakcji, hełm, miecz i tarcza.
MeshData swordsmanMesh(const Vec3& fc) {
    MeshBuilder b;
    b.box({-0.42f, 1.15f, 0.0f}, {0.62f, 2.3f, 0.85f}, {0.25f, 0.25f, 0.28f});
    b.box({0.42f, 1.15f, 0.0f}, {0.62f, 2.3f, 0.85f}, {0.25f, 0.25f, 0.28f});
    b.cylinder({0.0f, 2.2f, 0.0f}, 1.1f, 2.6f, 8, kMail, 0.95f);
    b.box({0.0f, 3.2f, 0.72f}, {1.5f, 2.4f, 0.35f}, fc);              // tunika
    b.box({-1.25f, 3.6f, 0.0f}, {0.5f, 1.9f, 0.6f}, kMail);
    b.box({1.25f, 3.6f, 0.0f}, {0.5f, 1.9f, 0.6f}, kMail);
    b.sphere({0.0f, 5.35f, 0.0f}, 0.78f, 5, 8, kSkin);
    b.cylinder({0.0f, 5.45f, 0.0f}, 0.88f, 0.55f, 8, kSteel);
    b.cone({0.0f, 6.0f, 0.0f}, 0.88f, 0.75f, 8, kSteel);
    // Miecz (prawa ręka) i tarcza (lewa)
    b.box({1.45f, 4.1f, 0.85f}, {0.2f, 3.2f, 0.32f}, kSteel);
    b.box({1.45f, 2.55f, 0.85f}, {0.25f, 0.25f, 1.1f}, {0.6f, 0.5f, 0.2f});
    b.box({-1.55f, 3.3f, 0.35f}, {0.3f, 2.4f, 1.9f}, fc);
    b.sphere({-1.75f, 3.3f, 0.35f}, 0.32f, 3, 6, kSteel);
    return b.build();
}

// Łucznik: zielona tunika, kaptur w kolorze frakcji, łuk i kołczan.
MeshData archerMesh(const Vec3& fc) {
    MeshBuilder b;
    const Vec3 tunic{0.32f, 0.45f, 0.2f};
    b.box({-0.4f, 1.1f, 0.0f}, {0.6f, 2.2f, 0.8f}, kPants);
    b.box({0.4f, 1.1f, 0.0f}, {0.6f, 2.2f, 0.8f}, kPants);
    b.cylinder({0.0f, 2.1f, 0.0f}, 0.95f, 2.5f, 8, tunic, 0.82f);
    b.box({0.0f, 3.3f, 0.0f}, {0.45f, 2.6f, 1.95f}, fc);              // szarfa
    b.box({-1.15f, 3.4f, 0.0f}, {0.45f, 1.9f, 0.55f}, tunic);
    b.box({1.15f, 3.4f, 0.0f}, {0.45f, 1.9f, 0.55f}, tunic);
    b.sphere({0.0f, 5.15f, 0.0f}, 0.76f, 5, 8, kSkin);
    b.cone({0.0f, 5.2f, 0.0f}, 0.95f, 1.6f, 8, shade(fc, 0.7f));      // kaptur
    // Łuk i kołczan
    b.box({-1.4f, 3.7f, 0.6f}, {0.16f, 4.2f, 0.26f}, kDarkWood);
    b.box({-1.4f, 3.7f, 0.95f}, {0.05f, 4.0f, 0.05f}, {0.9f, 0.9f, 0.85f});
    b.cylinder({0.55f, 2.6f, -0.95f}, 0.35f, 2.2f, 6, kWood);
    return b.build();
}

// Sosna: pień i trzy stożki igliwia (tint zmienia odcień zieleni).
MeshData pineMesh(float tint) {
    MeshBuilder b;
    const Vec3 leaves{0.12f * tint, 0.38f * tint, 0.16f * tint};
    b.cylinder({0.0f, -1.0f, 0.0f}, 0.75f, 5.0f, 7, kDarkWood);
    b.cone({0.0f, 2.5f, 0.0f}, 4.6f, 6.0f, 8, leaves);
    b.cone({0.0f, 5.6f, 0.0f}, 3.7f, 5.4f, 8, shade(leaves, 1.08f));
    b.cone({0.0f, 8.6f, 0.0f}, 2.6f, 4.8f, 8, shade(leaves, 1.15f));
    return b.build();
}

// Drzewo liściaste: pień i trzy kule korony.
MeshData oakMesh(float tint) {
    MeshBuilder b;
    const Vec3 leaves{0.27f * tint, 0.55f * tint, 0.18f * tint};
    b.cylinder({0.0f, -1.0f, 0.0f}, 0.9f, 7.5f, 7, kWood, 0.65f);
    b.sphere({0.0f, 9.0f, 0.0f}, 4.3f, 4, 7, leaves);
    b.sphere({2.1f, 7.6f, 1.0f}, 2.9f, 4, 6, shade(leaves, 0.92f));
    b.sphere({-1.8f, 8.0f, -1.3f}, 3.1f, 4, 6, shade(leaves, 1.08f));
    return b.build();
}

// Skała: kilka nieregularnych brył (sfery o małej liczbie ścian wyglądają jak kamień).
MeshData rockMesh(int variant) {
    MeshBuilder b;
    const Vec3 c{0.55f, 0.54f, 0.52f};
    if (variant == 0) {
        b.sphere({0.0f, 1.0f, 0.0f}, 4.0f, 3, 6, c);
        b.sphere({2.8f, 0.2f, 1.6f}, 2.4f, 3, 5, shade(c, 0.9f));
        b.sphere({-2.2f, 0.4f, -1.8f}, 2.6f, 3, 5, shade(c, 1.1f));
    } else if (variant == 1) {
        b.sphere({0.0f, 2.0f, 0.0f}, 3.4f, 3, 5, shade(c, 1.05f));
        b.sphere({0.5f, 4.2f, 0.3f}, 2.0f, 3, 5, shade(c, 0.95f));
        b.sphere({-2.5f, 0.3f, 1.5f}, 2.2f, 3, 5, shade(c, 0.88f));
    } else {
        b.box({0.0f, 1.5f, 0.0f}, {5.0f, 4.0f, 4.2f}, shade(c, 0.95f));
        b.sphere({1.5f, 3.3f, 0.8f}, 2.2f, 3, 5, c);
        b.sphere({-2.0f, 0.5f, 1.8f}, 2.0f, 3, 5, shade(c, 1.1f));
    }
    return b.build();
}

// Strzała wzdłuż osi +Z: drzewce, grot i lotki.
MeshData arrowMesh() {
    MeshBuilder b;
    b.box({0.0f, 0.0f, 0.0f}, {0.14f, 0.14f, 3.4f}, kWood);
    b.box({0.0f, 0.0f, 1.85f}, {0.32f, 0.32f, 0.5f}, kSteel);
    b.box({0.0f, 0.0f, -1.45f}, {0.6f, 0.05f, 0.6f}, {0.95f, 0.95f, 0.95f});
    b.box({0.0f, 0.0f, -1.45f}, {0.05f, 0.6f, 0.6f}, {0.95f, 0.95f, 0.95f});
    return b.build();
}

// Rusztowanie o jednostkowym rozmiarze - skalowane do obrysu budowanego budynku.
MeshData scaffoldMesh() {
    // Rusztowanie o jednostkowym rozmiarze, skalowane do obrysu budynku.
    MeshBuilder b;
    const Vec3 c{0.75f, 0.6f, 0.38f};
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            b.box({0.92f * sx, 0.5f, 0.92f * sz}, {0.07f, 1.0f, 0.07f}, c);
        }
    }
    for (float y : {0.35f, 0.7f, 0.98f}) {
        b.box({0.0f, y, 0.92f}, {1.9f, 0.04f, 0.05f}, c);
        b.box({0.0f, y, -0.92f}, {1.9f, 0.04f, 0.05f}, c);
        b.box({0.92f, y, 0.0f}, {0.05f, 0.04f, 1.9f}, c);
        b.box({-0.92f, y, 0.0f}, {0.05f, 0.04f, 1.9f}, c);
    }
    b.box({0.0f, 0.02f, 0.0f}, {1.8f, 0.04f, 1.8f}, {0.5f, 0.4f, 0.3f});
    return b.build();
}

// Gruzowisko po zniszczonym budynku (skalowane do jego rozmiaru).
MeshData rubbleMesh() {
    MeshBuilder b;
    const Vec3 c{0.3f, 0.28f, 0.26f};
    b.box({-0.4f, 0.1f, -0.3f}, {0.8f, 0.3f, 0.6f}, c);
    b.box({0.4f, 0.12f, 0.35f}, {0.7f, 0.35f, 0.7f}, shade(c, 1.3f));
    b.box({0.1f, 0.08f, -0.6f}, {0.5f, 0.2f, 0.4f}, shade(c, 0.7f));
    b.box({-0.5f, 0.06f, 0.5f}, {0.5f, 0.15f, 0.5f}, {0.15f, 0.12f, 0.1f});
    b.box({0.6f, 0.2f, -0.2f}, {0.12f, 0.5f, 0.9f}, kDarkWood);
    return b.build();
}

} // namespace

// Nazwa siatki budynku dla frakcji, np. "b_house_0".
std::string buildingMeshName(BuildingType type, int faction) {
    return std::string(buildingDef(type).meshBase) + "_" + std::to_string(faction);
}

// Nazwa siatki jednostki; osadnik ma osobne siatki dla każdego niesionego surowca.
std::string unitMeshName(UnitType type, int faction, CarryType carry) {
    static const char* names[] = {"u_settler", "u_sword", "u_archer"};
    std::string n = std::string(names[static_cast<int>(type)]) + "_" + std::to_string(faction);
    if (type == UnitType::Settler) {
        n += "_" + std::to_string(static_cast<int>(carry));
    }
    return n;
}

// Nazwy wariantów drzew i skał.
std::string treeMeshName(int variant) {
    return "s_tree_" + std::to_string(variant);
}

std::string rockMeshName(int variant) {
    return "s_rock_" + std::to_string(variant);
}

// Tworzy wszystkie siatki gry raz, na starcie programu (są wspólne dla wszystkich partii).
void buildGameMeshes(Engine& engine) {
    auto* mm = engine.getMeshManager();
    for (int f = 0; f < kFactionCount; ++f) {
        const Vec3 fc = factionColor(f);
        mm->loadMesh(buildingMeshName(BuildingType::Castle, f), castleMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::House, f), houseMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Woodcutter, f), woodcutterMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Quarry, f), quarryMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Farm, f), farmMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Barracks, f), barracksMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Tower, f), towerMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Forester, f), foresterMesh(fc));
        mm->loadMesh(buildingMeshName(BuildingType::Mine, f), mineMesh(fc));

        for (int c = 0; c <= 3; ++c) {
            const auto carry = static_cast<CarryType>(c);
            mm->loadMesh(unitMeshName(UnitType::Settler, f, carry), settlerMesh(fc, carry));
        }
        mm->loadMesh(unitMeshName(UnitType::Swordsman, f, CarryType::None), swordsmanMesh(fc));
        mm->loadMesh(unitMeshName(UnitType::Archer, f, CarryType::None), archerMesh(fc));
    }

    mm->loadMesh(treeMeshName(0), pineMesh(1.0f));
    mm->loadMesh(treeMeshName(1), pineMesh(0.82f));
    mm->loadMesh(treeMeshName(2), oakMesh(1.0f));
    mm->loadMesh(treeMeshName(3), oakMesh(0.85f));
    for (int i = 0; i < kRockVariants; ++i) {
        mm->loadMesh(rockMeshName(i), rockMesh(i));
    }

    {
        MeshBuilder b;
        b.cylinder({0.0f, -0.5f, 0.0f}, 0.85f, 1.4f, 7, kWood);
        b.disk({0.0f, 0.91f, 0.0f}, 0.75f, 7, {0.8f, 0.65f, 0.45f});
        mm->loadMesh(kStumpMesh, b.build());
    }
    mm->loadMesh(kArrowMesh, arrowMesh());
    mm->loadMesh(kMarkerMesh, MeshBuilder::makeRing(0.7f, 1.0f, 20, {0.3f, 1.0f, 0.3f}));
    mm->loadMesh(kRingPlayerMesh, MeshBuilder::makeRing(0.82f, 1.0f, 24, {0.25f, 1.0f, 0.35f}));
    mm->loadMesh(kRingEnemyMesh, MeshBuilder::makeRing(0.82f, 1.0f, 24, {1.0f, 0.25f, 0.2f}));
    mm->loadMesh(kRingNeutralMesh, MeshBuilder::makeRing(0.82f, 1.0f, 24, {1.0f, 0.9f, 0.3f}));
    mm->loadMesh(kScaffoldMesh, scaffoldMesh());
    mm->loadMesh(kRubbleMesh, rubbleMesh());
}

// Materiały: zwykły (kolory z wierzchołków), półprzezroczysty zielony/czerwony do podglądu budowy
// i półprzezroczysty do siatki kolizji w trybie debug.
GameMaterials createGameMaterials(Engine& engine) {
    auto* mats = engine.getMaterialManager();
    GameMaterials out;

    Material solid;
    solid.name = "settlers_solid";
    solid.roughnessFactor = 0.85f;
    solid.metallicFactor = 0.0f;
    solid.cullMode = CullMode::Back;
    out.solid = mats->add(solid);

    Material ghost;
    ghost.name = "settlers_ghost_ok";
    ghost.blendMode = BlendMode::Alpha;
    ghost.depthWrite = false;
    ghost.cullMode = CullMode::Back;
    ghost.color = {0.45f, 1.0f, 0.45f, 0.5f};
    out.ghostOk = mats->add(ghost);

    ghost.name = "settlers_ghost_bad";
    ghost.color = {1.0f, 0.3f, 0.25f, 0.5f};
    out.ghostBad = mats->add(ghost);

    ghost.name = "settlers_debug";
    ghost.color = {1.0f, 1.0f, 1.0f, 0.55f};
    ghost.cullMode = CullMode::Disabled;
    out.debug = mats->add(ghost);
    return out;
}

} // namespace settlers
