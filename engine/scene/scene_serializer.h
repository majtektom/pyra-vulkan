#pragma once

#include <string>

namespace engine3d {

class Scene;

/**
 * @brief Klasa narzędziowa do serializacji sceny (Scene Serializer).
 *
 * Umożliwia zapisanie stanu całej sceny ECS (wszystkich encji, ich hierarchii parent-child
 * oraz dołączonych komponentów) do pliku w formacie JSON oraz wczytanie go z powrotem.
 *
 * Jest to kluczowy element silnika pozwalający na implementację zapisu stanu gry (Save Game)
 * lub wczytywanie map stworzonych w edytorze poziomów.
 */
class SceneSerializer {
public:
    /**
     * @brief Zapisuje aktualny stan sceny do pliku JSON.
     * @param scene Referencja do sceny, która ma być zapisana.
     * @param filePath Ścieżka docelowa do pliku.
     * @return Zwraca true w przypadku powodzenia, false przy błędzie zapisu/IO.
     */
    static bool saveToFile(const Scene& scene, const std::string& filePath);

    /**
     * @brief Wczytuje stan sceny z pliku JSON. Aktualne encje na scenie są usuwane przed wczytaniem.
     * @param scene Scena docelowa, do której wczytane zostaną encje.
     * @param filePath Ścieżka do pliku JSON.
     * @return Zwraca true w przypadku powodzenia, false jeśli plik jest uszkodzony lub nie istnieje.
     */
    static bool loadFromFile(Scene& scene, const std::string& filePath);
};

} // namespace engine3d
