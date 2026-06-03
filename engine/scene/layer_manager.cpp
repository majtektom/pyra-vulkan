#include "scene/layer_manager.h"

#include <algorithm>
#include <utility>

namespace engine3d {

Layer::Layer(std::string name) : name_(std::move(name)) {}

void Layer::setActive(bool active) {
    if (active_ == active) return; // Brak zmiany stanu.
    active_ = active;
    
    // Wywołujemy powiązane zdarzenia cyklu życia.
    if (active_) {
        onActivate();
    } else {
        onDeactivate();
    }
}

void LayerManager::addLayer(std::unique_ptr<Layer> layer) {
    // Rejestrujemy warstwę w kontenerze. Menedżer przejmuje jej własność.
    layers_.push_back(std::move(layer));
}

void LayerManager::removeLayer(const std::string& name) {
    // Usuwamy warstwę o pasującej nazwie z wektora za pomocą algorytmu remove_if (idiom erase-remove).
    layers_.erase(
        std::remove_if(layers_.begin(), layers_.end(),
            [&](const auto& layer) { return layer->name() == name; }),
        layers_.end()
    );
}

Layer* LayerManager::getLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) return layer.get();
    }
    return nullptr;
}

void LayerManager::activateLayer(const std::string& name, bool singleActive) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            layer->setActive(true);
        } else if (singleActive) {
            // Jeśli włączono opcję singleActive, wyłączamy wszystkie pozostałe warstwy.
            layer->setActive(false);
        }
    }
}

void LayerManager::deactivateLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            layer->setActive(false);
            break;
        }
    }
}

void LayerManager::toggleLayer(const std::string& name) {
    for (auto& layer : layers_) {
        if (layer->name() == name) {
            // Odwracamy aktualny stan aktywności.
            layer->setActive(!layer->isActive());
            break;
        }
    }
}

void LayerManager::update(float deltaTime) {
    // W każdej klatce gry aktualizujemy wyłącznie te warstwy, które są włączone (aktywne).
    for (auto& layer : layers_) {
        if (layer->isActive()) {
            layer->update(deltaTime);
        }
    }
}

} // namespace engine3d
