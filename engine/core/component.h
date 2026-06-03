#pragma once

#include <chrono>
#include <string>

namespace engine3d {

class Entity;

/**
 * @brief Bazowa klasa dla wszystkich komponentów w silniku.
 * 
 * Komponenty stanowią "cegiełki" funkcjonalności, które można przypinać do encji.
 * Zamiast tworzyć głębokie hierarchie dziedziczenia (np. Gracz -> Postać -> Obiekt),
 * stosujemy kompozycję. 
 * 
 * Dlaczego kompozycja jest lepsza?
 * Pozwala na większą elastyczność – możemy stworzyć "Płonący Miecz", dodając komponent 
 * oświetlenia i cząsteczek do encji modelu 3D, bez modyfikacji kodu klasy bazowej.
 */
class Component {
public:
    explicit Component(const std::string& componentName = "Component");
    virtual ~Component() = default;

    /**
     * @brief Inicjalizacja komponentu po przypisaniu do encji.
     * Tutaj powinny odbywać się operacje wymagające dostępu do właściciela (Entity).
     */
    virtual void initialize();

    /**
     * @brief Aktualizacja stanu logicznego komponentu w każdej klatce.
     * @param deltaTime Czas, jaki upłynął od ostatniej klatki (delta time).
     */
    virtual void update(std::chrono::milliseconds deltaTime);

    /**
     * @brief Metoda pomocnicza do renderingu (opcjonalna w systemach ECS).
     */
    virtual void render();

    void setOwner(Entity* entity) { owner = entity; }
    Entity* getOwner() const { return owner; }

    const std::string& getName() const { return name; }
    bool isActive() const { return active; }
    void setActive(bool isActive) { active = isActive; }

protected:
    Entity* owner = nullptr;
    std::string name;
    bool active = true;
};

} // namespace engine3d
