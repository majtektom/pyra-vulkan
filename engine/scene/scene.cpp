#include "scene/scene.h"

#include "camera/camera_component.h"
#include "core/entity.h"
#include "core/hierarchy_component.h"
#include "scene/transform_system.h"

namespace engine3d {

namespace {

/**
 * @brief Upewnia się, że encja posiada komponent hierarchii (HierarchyComponent).
 * Jeśli nie posiada, komponent jest dodawany dynamicznie.
 * Jest to przydatne, ponieważ HierarchyComponent jest tworzony leniwie (lazy initialization)
 * dopiero wtedy, gdy obiekt staje się rodzicem lub dzieckiem innego obiektu.
 */
HierarchyComponent* ensureHierarchy(Entity* entity) {
    if (!entity) {
        return nullptr;
    }
    auto* hierarchy = entity->getComponent<HierarchyComponent>();
    if (!hierarchy) {
        hierarchy = entity->addComponent<HierarchyComponent>();
    }
    return hierarchy;
}

/**
 * @brief Usuwa wskaźnik na dziecko z listy dzieci rodzica.
 */
void eraseChildLink(Entity* parent, Entity* child) {
    if (!parent || !child) {
        return;
    }

    auto* parentHierarchy = parent->getComponent<HierarchyComponent>();
    if (!parentHierarchy) {
        return;
    }

    auto& children = parentHierarchy->children;
    // std::remove przesuwa usuwane elementy na koniec wektora, a erase usuwa je fizycznie z pamięci.
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}

/**
 * @brief Sprawdza, czy przypisanie rodzica nie spowoduje powstania cyklu (np. A jest rodzicem B, a B staje się rodzicem A).
 * W grafie sceny (Scene Graph) cykle są niedozwolone, ponieważ uniemożliwiłyby poprawne
 * przeliczenie macierzy świata (spowodowałyby nieskończoną rekurencję).
 */
bool wouldCreateCycle(Entity* child, Entity* parent) {
    if (!child || !parent) {
        return false;
    }

    if (child == parent) {
        return true;
    }

    Entity* current = parent;
    // Wędrujemy w górę drzewa od potencjalnego rodzica.
    // Jeśli trafimy na dziecko, oznacza to, że dziecko jest już przodkiem rodzica – powstałby cykl.
    while (current) {
        if (current == child) {
            return true;
        }

        auto* hierarchy = current->getComponent<HierarchyComponent>();
        current = hierarchy ? hierarchy->parent : nullptr;
    }

    return false;
}

} // namespace

Scene::Scene(std::string sceneName) : name_(std::move(sceneName)) {
    // Każda nowo utworzona scena domyślnie posiada TransformSystem.
    // Jest on odpowiedzialny za obliczanie macierzy transformacji świata (World Matrices)
    // na podstawie lokalnych transformacji i hierarchii parent-child.
    addSystem(std::make_unique<TransformSystem>());
}

Scene::~Scene() = default;

Entity* Scene::createEntity(const std::string& name) {
    // Generujemy unikalne ID encji automatycznie inkrementując licznik.
    return createEntity(nextEntityId_++, name);
}

Entity* Scene::createEntity(EntityId id, const std::string& name) {
    // Tworzymy encję (pojemnik na komponenty). Właścicielem pamięci staje się std::unique_ptr
    // przechowywany w wektorze entities_. Scena zarządza cyklem życia tego obiektu.
    auto entity = std::make_unique<Entity>(id, name);
    Entity* ptr = entity.get();
    
    // Rejestrujemy wskaźnik roboczy w mapach szybkiego wyszukiwania (O(1)).
    entityIdMap_[ptr->id()] = ptr;
    entityMap_[name] = ptr;
    entities_.push_back(std::move(entity));

    // Aktualizujemy licznik kolejnego ID, aby zapobiec konfliktom identyfikatorów
    // w przypadku wczytywania encji o konkretnych ID z plików.
    if (id >= nextEntityId_) {
        nextEntityId_ = id + 1;
    }

    return ptr;
}

Entity* Scene::getEntity(EntityId id) {
    auto it = entityIdMap_.find(id);
    return it != entityIdMap_.end() ? it->second : nullptr;
}

const Entity* Scene::getEntity(EntityId id) const {
    auto it = entityIdMap_.find(id);
    return it != entityIdMap_.end() ? it->second : nullptr;
}

Entity* Scene::getEntity(const std::string& name) {
    auto it = entityMap_.find(name);
    return it != entityMap_.end() ? it->second : nullptr;
}

const Entity* Scene::getEntity(const std::string& name) const {
    auto it = entityMap_.find(name);
    return it != entityMap_.end() ? it->second : nullptr;
}

bool Scene::removeEntity(Entity* entity) {
    if (!entity) return false;
    return removeEntity(entity->id(), HierarchyRemovalPolicy::RemoveSubtree);
}

bool Scene::removeEntity(EntityId id) {
    auto idIt = entityIdMap_.find(id);
    if (idIt == entityIdMap_.end()) return false;
    return removeEntity(idIt->second->getName(), HierarchyRemovalPolicy::RemoveSubtree);
}

bool Scene::removeEntity(const std::string& name) {
    return removeEntity(name, HierarchyRemovalPolicy::RemoveSubtree);
}

bool Scene::removeEntity(Entity* entity, HierarchyRemovalPolicy policy) {
    if (!entity) {
        return false;
    }
    return removeEntity(entity->id(), policy);
}

bool Scene::removeEntity(EntityId id, HierarchyRemovalPolicy policy) {
    auto idIt = entityIdMap_.find(id);
    if (idIt == entityIdMap_.end()) {
        return false;
    }
    return removeEntity(idIt->second->getName(), policy);
}

bool Scene::removeEntity(const std::string& name, HierarchyRemovalPolicy policy) {
    auto it = entityMap_.find(name);
    if (it == entityMap_.end()) return false;

    Entity* target = it->second;

    // Pobieramy listę dzieci usuwanej encji.
    std::vector<Entity*> children;
    if (auto* hierarchy = target->getComponent<HierarchyComponent>()) {
        children = hierarchy->children;
    }

    // Pobieramy rodzica usuwanej encji.
    Entity* parent = nullptr;
    if (auto* hierarchy = target->getComponent<HierarchyComponent>()) {
        parent = hierarchy->parent;
    }

    // Obsługa polityki usuwania dzieci:
    if (policy == HierarchyRemovalPolicy::RemoveSubtree) {
        // Usuwamy rekurencyjnie całe poddrzewo (dzieci, ich dzieci, itd.).
        for (Entity* child : children) {
            if (!child) {
                continue;
            }
            if (!getEntity(child->id())) {
                continue;
            }
            removeEntity(child->id(), HierarchyRemovalPolicy::RemoveSubtree);
        }
    } else if (policy == HierarchyRemovalPolicy::ReparentChildrenToParent) {
        // Przenosimy dzieci o jeden poziom wyżej w hierarchii (ich nowym rodzicem zostaje rodzic usuwanego obiektu).
        for (Entity* child : children) {
            if (!child || !getEntity(child->id())) {
                continue;
            }
            setParent(child, parent);
        }
    } else {
        // Odpinamy dzieci tak, że stają się obiektami bez rodzica (root level).
        for (Entity* child : children) {
            if (!child || !getEntity(child->id())) {
                continue;
            }
            clearParent(child);
        }
    }

    // Odpinamy usuwany obiekt od jego rodzica.
    clearParent(target);

    // Zapobiegamy pozostawieniu wiszącego wskaźnika (dangling pointer) do usuwanej kamery.
    if (activeCamera_ && activeCamera_->getOwner() == target) {
        activeCamera_ = nullptr;
    }

    // Usuwamy obiekt z głównego kontenera pamięci sceny i z rejestrów wyszukiwania.
    for (auto eit = entities_.begin(); eit != entities_.end(); ++eit) {
        if (eit->get() == target) {
            entityIdMap_.erase(target->id());
            entities_.erase(eit);
            entityMap_.erase(it);
            return true;
        }
    }

    return false;
}

void Scene::clearEntities() {
    entities_.clear();
    entityMap_.clear();
    entityIdMap_.clear();
    activeCamera_ = nullptr;
    nextEntityId_ = 1;
}

bool Scene::setParent(Entity* child, Entity* parent) {
    if (!child) {
        return false;
    }

    // Sprawdzamy bezpieczeństwo przed zapętleniem.
    if (wouldCreateCycle(child, parent)) {
        return false;
    }

    auto* childHierarchy = ensureHierarchy(child);
    if (!childHierarchy) {
        return false;
    }

    // Jeśli dziecko miało już rodzica, odpinamy je od starego.
    if (childHierarchy->parent) {
        eraseChildLink(childHierarchy->parent, child);
    }

    // Przypisujemy nowego rodzica.
    childHierarchy->parent = parent;

    // Dodajemy dziecko do listy dzieci nowego rodzica.
    if (parent) {
        auto* parentHierarchy = ensureHierarchy(parent);
        if (!parentHierarchy) {
            childHierarchy->parent = nullptr;
            return false;
        }

        auto& children = parentHierarchy->children;
        if (std::find(children.begin(), children.end(), child) == children.end()) {
            children.push_back(child);
        }
    }

    return true;
}

bool Scene::setParent(EntityId childId, EntityId parentId) {
    Entity* child = getEntity(childId);
    Entity* parent = getEntity(parentId);
    if (!child || !parent) {
        return false;
    }
    return setParent(child, parent);
}

bool Scene::clearParent(Entity* child) {
    if (!child) {
        return false;
    }

    auto* childHierarchy = child->getComponent<HierarchyComponent>();
    if (!childHierarchy || !childHierarchy->parent) {
        return false; // Obiekt nie miał rodzica.
    }

    // Usuwamy link z listy dzieci rodzica i zerujemy rodzica u dziecka.
    eraseChildLink(childHierarchy->parent, child);
    childHierarchy->parent = nullptr;
    return true;
}

bool Scene::clearParent(EntityId childId) {
    return clearParent(getEntity(childId));
}

Entity* Scene::parentOf(Entity* child) {
    if (!child) {
        return nullptr;
    }
    auto* hierarchy = child->getComponent<HierarchyComponent>();
    return hierarchy ? hierarchy->parent : nullptr;
}

const Entity* Scene::parentOf(const Entity* child) const {
    if (!child) {
        return nullptr;
    }
    auto* hierarchy = child->getComponent<HierarchyComponent>();
    return hierarchy ? hierarchy->parent : nullptr;
}

std::vector<Entity*> Scene::childrenOf(Entity* parent) {
    std::vector<Entity*> result;
    if (!parent) {
        return result;
    }

    auto* hierarchy = parent->getComponent<HierarchyComponent>();
    if (!hierarchy) {
        return result;
    }

    result.reserve(hierarchy->children.size());
    for (Entity* child : hierarchy->children) {
        if (child) {
            result.push_back(child);
        }
    }
    return result;
}

std::vector<const Entity*> Scene::childrenOf(const Entity* parent) const {
    std::vector<const Entity*> result;
    if (!parent) {
        return result;
    }

    auto* hierarchy = parent->getComponent<HierarchyComponent>();
    if (!hierarchy) {
        return result;
    }

    result.reserve(hierarchy->children.size());
    for (Entity* child : hierarchy->children) {
        if (child) {
            result.push_back(child);
        }
    }
    return result;
}

Entity* Scene::createDirectionalLight(const std::string& name, const Vec3& direction,
                                      const Vec3& color, float intensity) {
    auto* entity = createEntity(name);
    auto* light = entity->addComponent<LightComponent>();
    light->type = LightType::Directional;
    light->direction = normalize(direction);
    light->color = color;
    light->intensity = intensity;
    return entity;
}

Entity* Scene::createPointLight(const std::string& name, const Vec3& position,
                                const Vec3& color, float intensity, float range) {
    auto* entity = createEntity(name);
    entity->transform().position = position;
    auto* light = entity->addComponent<LightComponent>();
    light->type = LightType::Point;
    light->color = color;
    light->intensity = intensity;
    light->range = range;
    return entity;
}

Entity* Scene::createSpotLight(const std::string& name, const Vec3& position,
                               const Vec3& direction, const Vec3& color,
                               float intensity, float range,
                               float innerAngleDeg, float outerAngleDeg) {
    auto* entity = createEntity(name);
    entity->transform().position = position;
    auto* light = entity->addComponent<LightComponent>();
    light->type = LightType::Spot;
    light->direction = normalize(direction);
    light->color = color;
    light->intensity = intensity;
    light->range = range;
    light->innerAngleDeg = innerAngleDeg;
    light->outerAngleDeg = outerAngleDeg;
    return entity;
}

void Scene::addSystem(std::unique_ptr<ISceneSystem> system) {
    if (!system) return;
    systems_.push_back(std::move(system));
}

bool Scene::hasSystem(const std::string& systemName) const {
    for (const auto& system : systems_) {
        if (system && systemName == system->name()) {
            return true;
        }
    }
    return false;
}

void Scene::updateSystems(float deltaTime) {
    // Kolejność wykonywania systemów ma kluczowe znaczenie.
    // Przykładowo, TransformSystem musi obliczyć macierze świata zanim RenderExtractionSystem
    // wyekstrahuje pozycje do renderowania.
    for (auto& system : systems_) {
        system->update(*this, deltaTime);
    }
}

void Scene::updateEntities(std::chrono::milliseconds deltaTime) {
    // Aktualizujemy logikę każdego pojedynczego obiektu na scenie.
    for (auto& entity : entities_) {
        entity->update(deltaTime);
    }
}

} // namespace engine3d