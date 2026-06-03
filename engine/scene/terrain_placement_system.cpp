#include "scene/terrain_placement_system.h"

#include <algorithm>

#include "core/entity.h"
#include "object/terrain.h"

namespace engine3d {

void TerrainPlacementSystem::attach(EntityId entityId, float yOffset) {
    // Sprawdzamy, czy dana encja jest już na liście przypiętych obiektów.
    for (auto& entry : attachments_) {
        if (entry.entityId == entityId) {
            entry.yOffset = yOffset; // Aktualizujemy wyłącznie offset pionowy.
            return;
        }
    }

    // Dodajemy nowe powiązanie do listy.
    attachments_.push_back(Attachment{entityId, yOffset});
}

void TerrainPlacementSystem::detach(EntityId entityId) {
    // Usuwamy powiązanie z listy.
    attachments_.erase(
        std::remove_if(attachments_.begin(), attachments_.end(),
                       [entityId](const Attachment& item) { return item.entityId == entityId; }),
        attachments_.end());
}

void TerrainPlacementSystem::clear() {
    attachments_.clear();
}

void TerrainPlacementSystem::update(Scene& scene, float) {
    if (!terrain_) {
        return; // Brak terenu - system nie może wyznaczyć wysokości.
    }

    // Przetwarzamy listę przypiętych obiektów.
    // Jednocześnie automatycznie usuwamy z listy te powiązania, których encje już nie istnieją
    // na scenie lub zostały wyłączone (clean-up starych połączeń).
    attachments_.erase(
        std::remove_if(attachments_.begin(), attachments_.end(),
                       [&](const Attachment& item) {
                           Entity* entity = scene.getEntity(item.entityId);
                           if (!entity || !entity->isActive()) {
                               return true; // Zwrócenie true oznacza usunięcie powiązania z listy.
                           }

                           auto& transform = entity->transform();
                           
                           // Odpytujemy teren o wysokość pod współrzędnymi X i Z obiektu, 
                           // a następnie nakładamy nową wysokość na pozycję Y obiektu wraz z yOffset.
                           transform.position.y = terrain_->getHeight(transform.position.x, transform.position.z) + item.yOffset;
                           return false;
                       }),
        attachments_.end());
}

} // namespace engine3d
