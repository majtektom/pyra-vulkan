#pragma once

#include <vector>

#include "scene/scene.h"

namespace engine3d {

class Terrain;

/**
 * @brief System ECS dopasowujący wysokość obiektów do rzeźby terenu (Terrain Placement System).
 *
 * Umożliwia przypięcie (attachment) encji do podłoża (Terrain). 
 * W każdej klatce system odpytuje teren o wysokość w miejscu, w którym stoi obiekt (pozycja XZ),
 * a następnie aktualizuje jego wysokość (pozycję Y) dodając opcjonalne przesunięcie (yOffset).
 *
 * Przydatne dla:
 * - Utrzymywania postaci gracza/przeciwników na powierzchni ziemi podczas ruchu.
 * - Pozycjonowania obiektów statycznych (drzewa, trawa, kamienie) na nierównym terenie.
 */
class TerrainPlacementSystem : public ISceneSystem {
public:
    explicit TerrainPlacementSystem(const Terrain* terrain = nullptr) : terrain_(terrain) {}

    // Ustawia wskaźnik do terenu, na podstawie którego obliczane są wysokości.
    void setTerrain(const Terrain* terrain) { terrain_ = terrain; }
    
    /**
     * @brief Przypina encję do terenu.
     * @param entityId ID encji do przypięcia.
     * @param yOffset Przesunięcie wysokości (np. aby postać stała stopami na ziemi, a nie w połowie wysokości jej modelu).
     */
    void attach(EntityId entityId, float yOffset = 0.0f);
    
    /**
     * @brief Odpina encję od systemu (przestaje podążać za ukształtowaniem terenu).
     */
    void detach(EntityId entityId);
    
    // Usuwa wszystkie powiązania.
    void clear();

    /**
     * @brief Wyznacza nową wysokość dla wszystkich przypiętych encji. Wywoływane co klatkę.
     */
    void update(Scene& scene, float deltaTime) override;
    
    const char* name() const override { return "TerrainPlacementSystem"; }

private:
    // Struktura przechowująca powiązanie encji z offsetem pionowym.
    struct Attachment {
        EntityId entityId = 0;
        float yOffset = 0.0f;
    };

    const Terrain* terrain_ = nullptr;     // Teren używany do kalkulacji.
    std::vector<Attachment> attachments_;  // Lista przypiętych obiektów.
};

} // namespace engine3d
