#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine3d {

/**
 * @brief Klasa reprezentująca warstwę logiczno-graficzną (Layer).
 *
 * Warstwy są używane do oddzielenia dodatkowej logiki i interfejsu (np. menu gry, 
 * konsola debugująca, nakładka HUD) od głównego świata 3D.
 * Mogą być dynamicznie włączane, wyłączane, a także rysowane na wierzchu sceny.
 *
 * Wykorzystuje callback updateOverlay do renderowania interfejsu 2D/GUI.
 */
class Layer {
public:
    explicit Layer(std::string name);
    virtual ~Layer() = default;

    // Blokujemy kopiowanie i przenoszenie warstwy.
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&) = delete;
    Layer& operator=(Layer&&) = delete;

    const std::string& name() const { return name_; }
    bool isActive() const { return active_; }
    
    /**
     * @brief Zmienia stan aktywności warstwy, wywołując zdarzenia onActivate/onDeactivate.
     */
    void setActive(bool active);

    // Zdarzenia cyklu życia warstwy:
    virtual void onActivate() {}   // Wywoływane w momencie włączenia warstwy.
    virtual void onDeactivate() {} // Wywoływane w momencie wyłączenia warstwy.
    virtual void update(float deltaTime) {} // Aktualizacja logiki warstwy w każdej klatce.

    // Callback służący do rysowania nakładek graficznych 2D/GUI na ekranie:
    // Parametry: deltaTime, width (szerokość ekranu), height (wysokość ekranu).
    std::function<void(float, uint32_t, uint32_t)> updateOverlay;

private:
    std::string name_;
    bool active_ = false;
};

/**
 * @brief Menedżer warstw (Layer Manager).
 *
 * Przechowuje listę zarejestrowanych warstw i wywołuje ich metody aktualizacji logicznej.
 */
class LayerManager {
public:
    /**
     * @brief Dodaje nową warstwę do menedżera.
     */
    void addLayer(std::unique_ptr<Layer> layer);
    
    /**
     * @brief Usuwa warstwę o podanej nazwie.
     */
    void removeLayer(const std::string& name);
    
    // Wyszukuje warstwę po nazwie.
    Layer* getLayer(const std::string& name);
    
    /**
     * @brief Aktywuje warstwę.
     * @param name Nazwa warstwy do włączenia.
     * @param singleActive Jeśli true, wszystkie pozostałe warstwy zostaną automatycznie dezaktywowane (wyłączone).
     */
    void activateLayer(const std::string& name, bool singleActive = true);
    
    /**
     * @brief Dezaktywuje (wyłącza) warstwę.
     */
    void deactivateLayer(const std::string& name);
    
    /**
     * @brief Przełącza stan włączenia warstwy (aktywna <-> nieaktywna).
     */
    void toggleLayer(const std::string& name);
    
    /**
     * @brief Aktualizuje wszystkie aktywne warstwy. Wywoływane co klatkę.
     */
    void update(float deltaTime);

private:
    std::vector<std::unique_ptr<Layer>> layers_; // Lista wszystkich warstw posiadanych przez menedżer.
};

} // namespace engine3d
