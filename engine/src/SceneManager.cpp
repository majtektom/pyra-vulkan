#include "engine/SceneManager.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace engine3d {

// --- Layer ---

Layer::Layer(std::string name)
    : name_(std::move(name)) {}

void Layer::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (active_) {
        onActivate();
    } else {
        onDeactivate();
    }
}

// --- SceneManager ---

void SceneManager::addLayer(std::unique_ptr<Layer> layer) {
    if (!layer) {
        throw std::runtime_error("cannot add null layer");
    }
    layers_.push_back(std::move(layer));
}

void SceneManager::removeLayer(const std::string& name) {
    auto it = std::remove_if(layers_.begin(), layers_.end(),
        [&](const auto& layer) { return layer->name() == name; });
    if (it != layers_.end()) {
        layers_.erase(it, layers_.end());
    }
}

Layer* SceneManager::getLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            return layer.get();
        }
    }
    return nullptr;
}

void SceneManager::activateLayer(const std::string& name, bool singleActive) {
    if (singleActive) {
        for (auto& layer : layers_) {
            if (layer->name() != name) {
                layer->setActive(false);
            }
        }
    }
    Layer* layer = getLayer(name);
    if (layer) {
        layer->setActive(true);
    }
}

void SceneManager::deactivateLayer(const std::string& name) {
    Layer* layer = getLayer(name);
    if (layer) {
        layer->setActive(false);
    }
}

void SceneManager::toggleLayer(const std::string& name) {
    Layer* layer = getLayer(name);
    if (layer) {
        layer->setActive(!layer->isActive());
    }
}

void SceneManager::buildConfig(AppConfig& config) const {
    // Budujemy scene od nowa z aktywnych warstw.
    // UWAGA: nie nadpisujemy config.camera, config.skybox, config.grid, config.clearColor,
    // bo te moga byc zmieniane w onUpdate w runtime. Warstwy ustawiaja tylko sceneObjects.
    config.sceneObjects.clear();

    for (const auto& layer : layers_) {
        if (!layer->isActive()) {
            continue;
        }

        config.sceneObjects.insert(
            config.sceneObjects.end(),
            layer->sceneObjects.begin(),
            layer->sceneObjects.end()
        );
    }
}

void SceneManager::update(float deltaTime) {
    for (auto& layer : layers_) {
        if (layer->isActive()) {
            layer->update(deltaTime);
        }
    }
}

}  // namespace engine3d
