#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/light_component.h"
#include "render/render_types.h"

namespace engine3d {

class Entity;
class CameraComponent;

/**
 * @brief Polityka usuwania encji z hierarchii.
 * Określa, co powinno stać się z obiektami-dziećmi, gdy ich rodzic jest usuwany.
 */
enum class HierarchyRemovalPolicy {
    RemoveSubtree,            // Usuń również całe poddrzewo (rodzica i wszystkie dzieci).
    ReparentChildrenToParent, // Podepnij dzieci pod rodzica usuwanego obiektu (przeniesienie poziom wyżej).
    DetachChildren,           // Odczep dzieci, stawiając je bezpośrednio w korzeniu sceny (brak rodzica).
};

class Scene;

/**
 * @brief Interfejs dla systemów ECS (Entity-Component-System) operujących na scenie.
 *
 * Systemy realizują logikę gry w sposób masowy, oddzielając zachowanie od danych.
 * Dane przechowywane są w komponentach (Component), a systemy (ISceneSystem) przetwarzają je w pętli.
 */
class ISceneSystem {
public:
    virtual ~ISceneSystem() = default;

    /**
     * @brief Aktualizacja logiki systemu. Uruchamiana w każdej klatce.
     * @param scene Referencja do aktualizowanej sceny.
     * @param deltaTime Czas od ostatniej klatki w sekundach.
     */
    virtual void update(Scene& scene, float deltaTime) = 0;

    // Nazwa systemu przydatna do logowania i profilowania wydajności.
    virtual const char* name() const = 0;
};

/**
 * @brief Klasa reprezentująca "Świat" w architekturze ECS.
 *
 * Scene zarządza czasem życia encji (Entity), przechowuje je, utrzymuje hierarchię
 * parent-child, oraz uruchamia zarejestrowane systemy logiczne (ISceneSystem).
 * Odpowiada również za zbieranie danych do renderowania (ekstrakcja klatki).
 */
class Scene {
public:
    explicit Scene(std::string sceneName = "default");
    ~Scene();

    // Zwraca nazwę sceny.
    const std::string& name() const { return name_; }

    /**
     * @brief Tworzy nową encję o podanej nazwie z automatycznie przydzielonym, unikalnym ID.
     */
    Entity* createEntity(const std::string& name);

    /**
     * @brief Tworzy encję z określonym ID (używane głównie przy ładowaniu/deserializacji sceny z pliku).
     */
    Entity* createEntity(EntityId id, const std::string& name);

    // Wyszukiwanie encji po unikalnym identyfikatorze ID. Zwraca nullptr, jeśli nie znaleziono.
    Entity* getEntity(EntityId id);
    const Entity* getEntity(EntityId id) const;

    // Wyszukiwanie encji po nazwie. Zwraca nullptr, jeśli nie znaleziono.
    Entity* getEntity(const std::string& name);
    const Entity* getEntity(const std::string& name) const;

    // Zwraca listę wszystkich encji na scenie (tylko do odczytu).
    const std::vector<std::unique_ptr<Entity>>& getEntities() const { return entities_; }

    // Usuwanie encji o podanym wskaźniku lub identyfikatorze z domyślną polityką usuwania dzieci.
    bool removeEntity(Entity* entity);
    bool removeEntity(EntityId id);
    bool removeEntity(const std::string& name);

    // Usuwanie encji z jawnym określeniem polityki zachowania obiektów-dzieci.
    bool removeEntity(Entity* entity, HierarchyRemovalPolicy policy);
    bool removeEntity(EntityId id, HierarchyRemovalPolicy policy);
    bool removeEntity(const std::string& name, HierarchyRemovalPolicy policy);

    /**
     * @brief Ustawia relację rodzic-dziecko w hierarchii sceny.
     * Zapobiega powstawaniu cykli w hierarchii.
     */
    bool setParent(Entity* child, Entity* parent);
    bool setParent(EntityId childId, EntityId parentId);

    // Usuwa rodzica z danej encji (odpina ją z drzewa do poziomu głównego).
    bool clearParent(Entity* child);
    bool clearParent(EntityId childId);

    // Pobiera rodzica danej encji.
    Entity* parentOf(Entity* child);
    const Entity* parentOf(const Entity* child) const;
    
    // Pobiera listę bezpośrednich dzieci danej encji.
    std::vector<Entity*> childrenOf(Entity* parent);
    std::vector<const Entity*> childrenOf(const Entity* parent) const;

    // Usuwa wszystkie encje i czyści rejestry sceny.
    void clearEntities();

    // Funkcje pomocnicze do szybkiego tworzenia encji reprezentujących światła:
    Entity* createDirectionalLight(const std::string& name, const Vec3& direction,
                                   const Vec3& color, float intensity);

    Entity* createPointLight(const std::string& name, const Vec3& position,
                             const Vec3& color, float intensity, float range);

    Entity* createSpotLight(const std::string& name, const Vec3& position,
                            const Vec3& direction, const Vec3& color,
                            float intensity, float range,
                            float innerAngleDeg, float outerAngleDeg);

    // Dodawanie systemu ECS do sceny. Systemy są wykonywane w kolejności ich dodania.
    void addSystem(std::unique_ptr<ISceneSystem> system);
    bool hasSystem(const std::string& systemName) const;

    // Uruchamia wszystkie zarejestrowane systemy ECS.
    void updateSystems(float deltaTime);

    // Wywołuje metodę update na wszystkich komponentach wszystkich encji.
    void updateEntities(std::chrono::milliseconds deltaTime);

    // Zwraca wyekstrahowane paczki geometrii przygotowane do wysłania na GPU w tej klatce.
    const std::vector<RenderPacket>& renderPackets() const { return renderPackets_; }

    // Dostęp mutowalny dla systemow renderowania w celu zapisu paczek.
    std::vector<RenderPacket>& mutableRenderPackets() { return renderPackets_; }

    // Czyści listę paczek przed rozpoczęciem generowania nowej klatki.
    void clearRenderPackets() { renderPackets_.clear(); }

    // Zwraca wyekstrahowaną listę źródeł światła dla aktualnej klatki.
    const std::vector<SceneLight>& frameLights() const { return frameLights_; }

    // Dostęp mutowalny dla systemów oświetlenia.
    std::vector<SceneLight>& mutableFrameLights() { return frameLights_; }

    // Czyści światła przed nową klatką.
    void clearFrameLights() { frameLights_.clear(); }

    // Ustawia aktywną kamerę, która definiuje rzutowanie i widok sceny.
    void setActiveCamera(CameraComponent* camera) { activeCamera_ = camera; }

    // Zwraca wskaźnik do aktualnie aktywnej kamery.
    CameraComponent* activeCamera() { return activeCamera_; }
    const CameraComponent* activeCamera() const { return activeCamera_; }

    // Zwraca macierze i parametry rzutowania kamery dla tej klatki.
    const RenderView& frameView() const { return frameView_; }

    // Zwraca informację, czy dla bieżącej klatki wyekstrahowano widok kamery.
    bool hasFrameView() const { return hasFrameView_; }

    // Ustawia wyekstrahowany widok kamery przygotowany do rysowania.
    void setFrameView(const RenderView& view) {
        frameView_ = view;
        hasFrameView_ = true;
    }

    // Czyści widok kamery klatki.
    void clearFrameView() { hasFrameView_ = false; }

private:
    std::string name_;                                            // Nazwa sceny.
    std::vector<std::unique_ptr<Entity>> entities_;               // Wszystkie encje posiadane przez scenę.
    EntityId nextEntityId_ = 1;                                   // Generator unikalnych ID dla nowych encji.
    std::unordered_map<EntityId, Entity*> entityIdMap_;           // Szybkie wyszukiwanie: ID -> wskaźnik.
    std::unordered_map<std::string, Entity*> entityMap_;          // Szybkie wyszukiwanie: nazwa -> wskaźnik.
    std::vector<std::unique_ptr<ISceneSystem>> systems_;          // Systemy ECS przypisane do sceny.
    std::vector<RenderPacket> renderPackets_;                     // Dane geometrii wyekstrahowane do rysowania.
    std::vector<SceneLight> frameLights_;                         // Dane oświetlenia wyekstrahowane do rysowania.
    CameraComponent* activeCamera_ = nullptr;                     // Wskaźnik na aktywną kamerę.
    RenderView frameView_{};                                      // Wyekstrahowany widok kamery dla renderera.
    bool hasFrameView_ = false;                                   // Flaga poprawności widoku kamery.
};

} // namespace engine3d