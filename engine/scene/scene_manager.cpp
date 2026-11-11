#include "scene/scene_manager.h"


namespace engine3d {

Scene* SceneManager::createScene(const std::string& name) {
    // Sprawdzamy, czy scena o tej nazwie już istnieje. Nazwa sceny musi być unikalna.
    auto it = scenes_.find(name);
    if (it != scenes_.end()) {
        return it->second.get();
    }

    // Tworzymy nową scenę. Menedżer przejmuje własność nad pamięcią sceny (std::unique_ptr).
    auto scene = std::make_unique<Scene>(name);
    Scene* ptr = scene.get();
    scenes_[name] = std::move(scene);

    // Jeśli w silniku nie ma jeszcze żadnej sceny, nowo utworzona staje się automatycznie aktywna.
    if (!activeScene_) {
        activeScene_ = ptr;
    }

    return ptr;
}

bool SceneManager::removeScene(const std::string& name) {
    auto it = scenes_.find(name);
    if (it == scenes_.end()) {
        return false;
    }

    // Jeśli usuwamy scenę, która jest obecnie wyświetlana, resetujemy wskaźnik aktywności.
    if (activeScene_ == it->second.get()) {
        activeScene_ = nullptr;
    }

    // Usuwamy obiekt sceny z pamięci.
    scenes_.erase(it);

    // Po usunięciu aktywnej sceny wybieramy dowolną inną zarejestrowaną scenę,
    // aby zapobiec sytuacji, w której silnik nie ma żadnego świata do aktualizacji.
    if (!activeScene_ && !scenes_.empty()) {
        activeScene_ = scenes_.begin()->second.get();
    }

    return true;
}

Scene* SceneManager::getScene(const std::string& name) {
    auto it = scenes_.find(name);
    return it != scenes_.end() ? it->second.get() : nullptr;
}

const Scene* SceneManager::getScene(const std::string& name) const {
    auto it = scenes_.find(name);
    return it != scenes_.end() ? it->second.get() : nullptr;
}

bool SceneManager::setActiveScene(const std::string& name) {
    auto* scene = getScene(name);
    if (!scene) {
        return false;
    }
    activeScene_ = scene;
    return true;
}

void SceneManager::update(float deltaTime) {
    // W każdej klatce gry aktualizujemy wyłącznie systemy ECS aktywnej sceny.
    if (activeScene_) {
        activeScene_->updateSystems(deltaTime);
    }
}

} // namespace engine3d
