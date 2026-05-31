#include <algorithm>

#include "scene/scene_manager.h"
namespace engine3d {

Layer::Layer(std::string name) : name_(std::move(name)) {}

void Layer::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (active_) onActivate();
    else onDeactivate();
}

void SceneManager::addLayer(std::unique_ptr<Layer> layer) {
    layers_.push_back(std::move(layer));
}

void SceneManager::removeLayer(const std::string& name) {
    layers_.erase(
        std::remove_if(layers_.begin(), layers_.end(),
            [&](const auto& layer) { return layer->name() == name; }),
        layers_.end()
    );
}

Layer* SceneManager::getLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) return layer.get();
    }
    return nullptr;
}

void SceneManager::activateLayer(const std::string& name, bool singleActive) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            layer->setActive(true);
        } else if (singleActive) {
            layer->setActive(false);
        }
    }
}

void SceneManager::deactivateLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            layer->setActive(false);
            break;
        }
    }
}

void SceneManager::toggleLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            layer->setActive(!layer->isActive());
            break;
        }
    }
}

void SceneManager::update(float deltaTime) {
    for (auto& layer : layers_) {
        if (layer->isActive()) {
            layer->update(deltaTime);
        }
    }
}

} // namespace engine3d
