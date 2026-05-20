#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/Types.hpp"

namespace engine3d {

// Warstwa sceny - pojedynczy "ekran" (menu, gra, itp.).
// Kazda warstwa moze miec wlasne obiekty, kamere, kolor tla, grid, skybox.
// SceneManager zarzadza warstwami i sklada aktywne warstwy w koncowa scene.
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

    // Aktywuje/dezaktywuje warstwe. Wywoluje onActivate/onDeactivate.
    void setActive(bool active);

    // Callbacki wywolywane przy zmianie stanu aktywacji.
    virtual void onActivate() {}
    virtual void onDeactivate() {}

    // Callback aktualizacyjny wywolywany co klatke dla aktywnych warstw.
    virtual void update(float deltaTime) {}

    // Konfiguracja warstwy - wypelnij przed dodaniem do SceneManager.
    std::vector<SceneObject> sceneObjects;
    CameraConfig camera;
    std::array<float, 4> clearColor = {0.06f, 0.08f, 0.12f, 1.0f};
    GridConfig grid;
    SkyboxConfig skybox;
    std::string activeShaderProgram = "default";

    // Opcjonalne etykiety tekstowe dla tej warstwy.
    std::vector<TextLabel> textLabels;
    std::function<void(float, uint32_t, uint32_t, std::vector<TextLabel>&)> updateTextLabels;

private:
    std::string name_;
    bool active_ = false;
};

// Zarzadca warstw - odpowiednik starego Core::AddLayer.
// Zarzadza lista warstw, pozwala dodawac, aktywowac, dezaktywowac i odczytac warstwe po nazwie.
class SceneManager {
public:
    SceneManager() = default;

    // Dodaje warstwe do menedzera.
    void addLayer(std::unique_ptr<Layer> layer);

    // Usuwa warstwe o podanej nazwie.
    void removeLayer(const std::string& name);

    // Zwraca wskaznik do warstwy o podanej nazwie lub nullptr.
    Layer* getLayer(const std::string& name);

    // Aktywuje warstwe (dezaktywujac inne jesli singleActive = true).
    void activateLayer(const std::string& name, bool singleActive = true);

    // Dezaktywuje warstwe.
    void deactivateLayer(const std::string& name);

    // Przelacza stan aktywacji warstwy.
    void toggleLayer(const std::string& name);

    // Wypelnia AppConfig danymi ze wszystkich aktywnych warstw.
    // Scalarne wartosci (camera, clearColor, grid, skybox) sa brane z pierwszej aktywnej.
    // Obiekty i etykiety sa laczone ze wszystkich aktywnych warstw.
    void buildConfig(AppConfig& config) const;

    // Aktualizuje wszystkie aktywne warstwy.
    void update(float deltaTime);

private:
    std::vector<std::unique_ptr<Layer>> layers_;
};

}  // namespace engine3d
