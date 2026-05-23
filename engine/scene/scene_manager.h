#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine3d {

class Layer {
public:
    explicit Layer(std::string name);
    virtual ~Layer() = default;

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&) = delete;
    Layer& operator=(Layer&&) = delete;

    const std::string& name() const { return name_; }
    bool isActive() const { return active_; }
    void setActive(bool active);

    virtual void onActivate() {}
    virtual void onDeactivate() {}
    virtual void update(float deltaTime) {}

    std::function<void(float, uint32_t, uint32_t)> updateOverlay;

private:
    std::string name_;
    bool active_ = false;
};

class SceneManager {
public:
    SceneManager() = default;

    void addLayer(std::unique_ptr<Layer> layer);
    void removeLayer(const std::string& name);
    Layer* getLayer(const std::string& name);
    void activateLayer(const std::string& name, bool singleActive = true);
    void deactivateLayer(const std::string& name);
    void toggleLayer(const std::string& name);
    void update(float deltaTime);

private:
    std::vector<std::unique_ptr<Layer>> layers_;
};

} // namespace engine3d
