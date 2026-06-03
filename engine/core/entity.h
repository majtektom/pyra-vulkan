#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "core/component.h"
#include "core/types.h"
#include "core/transform_component.h"

namespace engine3d {

/**
 * @brief Klasa reprezentująca encję w silniku (byt/obiekt).
 * 
 * Silnik używa wzorca ECS-lite (Entity-Component-System). W tym podejściu:
 * - Encja (Entity) to "pojemnik" na komponenty. Sama w sobie nie posiada logiki,
 *   a jedynie tożsamość w postaci unikalnego identyfikatora (EntityId).
 * - Komponenty (Component) przechowują dane i specyficzne zachowania.
 * - Systemy (System) operują na grupach komponentów w sposób masowy.
 * 
 * Dlaczego używamy EntityId?
 * Stabilne ID pozwala na bezpieczne odwoływanie się do obiektów między systemami
 * oraz ułatwia serializację i sieciową synchronizację stanu sceny. W przeciwieństwie
 * do surowych wskaźników, ID nie zmienia się, gdy wektor encji jest realokowany w pamięci.
 */
class Entity {
public:
    Entity(EntityId id, const std::string& entityName);
    ~Entity() = default;

    // Stabilny identyfikator encji (nie zalezy od nazwy).
    // Pozwala on na jednoznaczną identyfikację obiektu w ramach sceny,
    // co jest kluczowe dla systemów zarządzania zasobami i fizyki.
    EntityId id() const { return id_; }
    const std::string& getName() const { return name_; }
    bool isActive() const { return active_; }
    void setActive(bool isActive) { active_ = isActive; }

    // Każda encja posiada komponent transformacji, ponieważ w silniku 3D
    // niemal każdy obiekt musi mieć określoną pozycję, rotację i skalę w świecie.
    TransformComponent& transform() { return *transform_; }
    const TransformComponent& transform() const { return *transform_; }

    void initialize();
    void update(std::chrono::milliseconds deltaTime);
    void render();

    /**
     * @brief Dynamiczne dodawanie komponentu do encji.
     * 
     * Używamy szablonów (templates), aby zapewnić bezpieczeństwo typów na etapie kompilacji.
     * Komponent jest automatycznie rejestrowany i przypisany do tej encji jako właściciela.
     */
    template <typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPtr = component.get();
        componentPtr->setOwner(this);
        components_.push_back(std::move(component));
        componentPtr->initialize();
        return componentPtr;
    }

    /**
     * @brief Pobieranie komponentu określonego typu.
     * 
     * W ECS-lite wyszukiwanie komponentu odbywa się poprzez iterację. Dla optymalizacji
     * wydajności (cache-friendliness), w pełnych systemach ECS komponenty tego samego typu
     * leżą obok siebie w pamięci. Tutaj stosujemy podejście uproszczone, oparte na wektorze wskaźników.
     */
    template <typename T>
    T* getComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
            if (auto* casted = dynamic_cast<T*>(it->get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template <typename T>
    // Udostepnia odczyt wszystkich komponentow danego typu,
    // co ulatwia prace systemom ECS (np. Render/Light).
    std::vector<T*> getComponents() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        std::vector<T*> result;
        result.reserve(components_.size());
        for (const auto& component : components_) {
            if (auto* casted = dynamic_cast<T*>(component.get())) {
                result.push_back(casted);
            }
        }
        return result;
    }

    template <typename T>
    bool removeComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
            if (dynamic_cast<T*>(it->get()) != nullptr) {
                components_.erase(std::next(it).base());
                return true;
            }
        }
        return false;
    }

    template <typename T>
    bool hasComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        return getComponent<T>() != nullptr;
    }

private:
    EntityId id_ = 0;
    std::string name_;
    bool active_ = true;
    TransformComponent* transform_ = nullptr;
    std::vector<std::unique_ptr<Component>> components_;
};

} // namespace engine3d
