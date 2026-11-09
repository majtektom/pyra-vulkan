#pragma once

#include <string>

#include "core/asset_registry.h"
#include "settlers_defs.h"

namespace engine3d {
class Engine;
}

namespace settlers {

/**
 * Materiały wspólne dla całej gry. Modele są low-poly z kolorami w wierzchołkach,
 * więc wystarczy jeden materiał "solid" + materiały półprzezroczyste dla podglądu budowy.
 */
struct GameMaterials {
    engine3d::MaterialHandle solid{};
    engine3d::MaterialHandle ghostOk{};
    engine3d::MaterialHandle ghostBad{};
    engine3d::MaterialHandle terrain{};
    engine3d::MaterialHandle debug{};     // półprzezroczysty, kolor z wierzchołków (siatka kolizji)
};

/**
 * Generuje (jednorazowo) wszystkie siatki gry i rejestruje je w MeshManagerze.
 */
void buildGameMeshes(engine3d::Engine& engine);
GameMaterials createGameMaterials(engine3d::Engine& engine);

// Nazwy siatek
std::string buildingMeshName(BuildingType type, int faction);
std::string unitMeshName(UnitType type, int faction, CarryType carry);
std::string treeMeshName(int variant);
std::string rockMeshName(int variant);
constexpr int kTreeVariants = 4;
constexpr int kRockVariants = 3;
inline const char* kArrowMesh = "s_arrow";
inline const char* kStumpMesh = "s_stump";
inline const char* kMarkerMesh = "s_marker";
inline const char* kRingPlayerMesh = "s_ring_player";
inline const char* kRingEnemyMesh = "s_ring_enemy";
inline const char* kRingNeutralMesh = "s_ring_neutral";
inline const char* kScaffoldMesh = "s_scaffold";
inline const char* kRubbleMesh = "s_rubble";

} // namespace settlers
