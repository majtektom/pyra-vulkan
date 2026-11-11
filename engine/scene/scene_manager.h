#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "scene/scene.h"

namespace engine3d {

/**
 * @brief Menedżer scen silnika (Scene Manager).
 *
 * SceneManager zarządza wieloma scenami (światami ECS) w grze (np. scena menu głównego, 
 * scena poziomu 1, scena edytora). Odpowiada za:
 * - Tworzenie, usuwanie i wyszukiwanie scen po ich nazwach.
 * - Kontrolowanie, która scena jest w danej chwili aktywna (activeScene) – czyli aktualizowana i rysowana.
 * - Aktualizację systemów ECS aktywnej sceny.
 */
class SceneManager {
public:
    SceneManager() = default;

    /**
     * @brief Tworzy nową scenę o unikalnej nazwie lub zwraca wskaźnik do już istniejącej.
     */
    Scene* createScene(const std::string& name);
    
    /**
     * @brief Usuwa scenę o podanej nazwie wraz ze wszystkimi jej encjami i systemami.
     * @return Zwraca false, jeśli scena o tej nazwie nie istniała.
     */
    bool removeScene(const std::string& name);
    
    // Wyszukiwanie sceny po nazwie:
    Scene* getScene(const std::string& name);
    const Scene* getScene(const std::string& name) const;
    
    /**
     * @brief Ustawia scenę o podanej nazwie jako aktywną.
     * Silnik w każdej klatce pobiera encje i uruchamia systemy wyłącznie z aktywnej sceny.
     */
    bool setActiveScene(const std::string& name);
    
    Scene* activeScene() { return activeScene_; }
    const Scene* activeScene() const { return activeScene_; }
    const std::unordered_map<std::string, std::unique_ptr<Scene>>& scenes() const { return scenes_; }

    /**
     * @brief Aktualizuje czas w systemach ECS aktywnej sceny. Wywoływane co klatkę.
     */
    void update(float deltaTime);

private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> scenes_; // Rejestr wszystkich scen (własność menedżera).
    Scene* activeScene_ = nullptr;                                    // Słaby wskaźnik do aktualnie aktywnej sceny.
};

} // namespace engine3d
