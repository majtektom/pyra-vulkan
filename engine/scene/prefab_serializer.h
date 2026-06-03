#pragma once

#include <string>

namespace engine3d {

class Entity;
class Scene;

/**
 * @brief Klasa narzędziowa do serializacji i instancjowania prefabrykatów (Prefab Serializer).
 *
 * Prefabrykat (Prefab) to szablon pojedynczego obiektu (Entity) lub całej gałęzi obiektów-dzieci,
 * który jest zdefiniowany w zewnętrznym pliku JSON. Pozwala na wielokrotne tworzenie kopii
 * tego samego złożonego obiektu (np. przeciwnicy, beczki, drzewa, pociski) na scenie.
 *
 * Klasa pozwala na:
 * - Zapisanie wybranej encji wraz ze wszystkimi komponentami i dziećmi do pliku.
 * - Instancjowanie (klonowanie) prefabrykatu do aktywnej sceny.
 */
class PrefabSerializer {
public:
    /**
     * @brief Zapisuje encję (wraz z całoroczną hierarchią dzieci) do pliku prefabrykatu JSON.
     * @param entity Encja będąca korzeniem (root) zapisywanego poddrzewa.
     * @param filePath Ścieżka docelowa do pliku.
     */
    static bool saveEntityToFile(const Entity& entity, const std::string& filePath);

    /**
     * @brief Tworzy kopię prefabrykatu i dodaje ją do sceny.
     * @param scene Scena docelowa, do której zostanie dodana nowa instancja.
     * @param filePath Ścieżka do pliku prefabrykatu JSON.
     * @param nameOverride Opcjonalne nadpisanie nazwy korzenia nowo utworzonej encji.
     * @return Wskaźnik do nowo stworzonej encji głównej (root instancji).
     */
    static Entity* instantiateFromFile(Scene& scene, const std::string& filePath,
                                       const std::string& nameOverride = "");
};

} // namespace engine3d
