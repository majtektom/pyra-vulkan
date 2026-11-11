#pragma once
/**
 * ai_planner.h - komputerowy przeciwnik (AiPlanner). Implementacja i opis zasad: ai_planner.cpp.
 *
 * AI gra na tych samych zasadach co gracz i korzysta z tych samych poleceń świata (placeBuilding, demolish,
 * rozkazy wojsku). Klasa nie ma własnej kopii stanu gry - czyta World przez referencję (jest jego "friend",
 * bo potrzebuje też wewnętrznych zapytań, np. szukania miejsca na budynek). Stan AI zapisywany w grze leży
 * w FactionState (aiThinkTimer, aiExpandFor...), tu jest tylko pamięć robocza jednego kroku myślenia.
 */
#include <array>
#include <string>
#include <vector>

#include "core/math.h"
#include "settlers_defs.h"

namespace settlers {

using engine3d::Vec3;
class World;
enum class NodeKind;

class AiPlanner {
public:
    explicit AiPlanner(World& world) : w_(world) {}

    // Jeden krok AI frakcji: co 3 s decyzje (budowa, drogi, szkolenie), co klatkę zwiad i fale ataku.
    void update(int faction, float dt);
    // Czy AI frakcji zna ten punkt mapy (mgła wojny AI): własne terytorium albo teren odkryty przez jej ludzi.
    [[nodiscard]] bool knows(int faction, const Vec3& p) const;

    // Stan gospodarki frakcji widziany przez AI (liczony raz na krok myślenia).
    struct Economy {
        std::array<int, kBuildingTypeCount> total{}, inProgress{}, warmingUp{}, blocked{}, starved{};
        std::array<int, kBuildingTypeCount> backlog{};   // producenci z zaległymi towarami na fladze (>= 4)
        int underConstruction = 0;
        int onFlags = 0;
        bool peopleShort = false;   // za mało tragarzy / brak jedzenia na nowych osadników
        Resources have;      // magazyny + wolne towary na flagach
        Resources waiting;   // dostawy czekające na towar (brak w magazynach)
        // Pamięć na jeden krok myślenia AI (zamiast przeglądać wszystkie budynki świata w każdym zapytaniu):
        std::array<std::vector<int>, kBuildingTypeCount> own;   // żywe budynki frakcji według typu
        int buildingCount = 0;
        int storageBlocked = 0;     // warsztaty wstrzymane, bo magazyny pełne
        int soldiers = 0;           // żołnierze frakcji (załogi wież)
        std::vector<int> knownEnemyBuildings;   // budynki wroga na terenie odkrytym przez AI
    };
    [[nodiscard]] Economy economyOf(int faction) const;

    // Dla testów: jaki budynek AI postawiłoby po brakujący towar (pamięć ekonomii z bieżącego stanu).
    BuildingType planForTest(int faction, CarryType good) {
        eco_[faction] = economyOf(faction);
        return planFor(faction, eco_[faction], good, 0);
    }

private:
    // Wybór budynku w planiście: reguły po kolei, pierwsza, która wybierze, wygrywa.
    struct Choice {
        BuildingType next = BuildingType::Count;
        std::string why;   // powód do logu [ai]
        [[nodiscard]] bool chosen() const { return next != BuildingType::Count; }
        void choose(BuildingType t, std::string reason) {
            if (chosen() || t == BuildingType::Count) return;
            next = t;
            why = std::move(reason);
        }
    };

    // --- Budowa (planista) ---
    void buildNext(int faction);
    bool demolishUseless(int faction, const Economy& e);
    void chooseUrgent(int faction, const Economy& e, Choice& choice);
    void chooseDefenseTower(int faction, const Economy& e, Choice& choice);
    void chooseByRatios(const Economy& e, Choice& choice) const;
    void chooseMilitary(const Economy& e, Choice& choice) const;
    void chooseByShortages(int faction, const Economy& e, Choice& choice);
    void chooseBaseAndGrowth(int faction, const Economy& e, Choice& choice);
    void applyMaterialReserve(int faction, const Economy& e, Choice& choice);
    bool redirectToMaterialProducer(int faction, const Economy& e, Choice& choice);
    void placeChosen(int faction, Choice choice);

    // --- Gospodarka: kto produkuje towar, gdzie stawiać ---
    [[nodiscard]] bool producerReady(const Economy& e, BuildingType t) const;
    // Jaki budynek wyprodukuje brakujący towar, schodząc po łańcuchu (Count = żaden / już powstaje).
    BuildingType planFor(int faction, const Economy& e, CarryType good, int depth);
    int resourceCluster(int faction, NodeKind kind, BuildingType worker, Vec3& center) const;
    bool findSpot(int faction, BuildingType type, Vec3& spot);
    // Punkt poza terytorium, w stronę którego warto postawić wieżę (las / skały / góry / wolny teren).
    // blind (opcjonalnie): cel "na ślepo" - AI nie zna miejsca z potrzebnym surowcem (mgła wojny AI).
    bool expansionTarget(int faction, BuildingType wanted, Vec3& target, bool* blind = nullptr) const;
    [[nodiscard]] bool needsWarehouse(int faction, const Economy& e) const;
    // Łączy drogami warsztaty i magazyny, które nie mają połączenia z magazynem.
    void connectRoads(int faction);
    // Jaka część zasięgu wieży w punkcie p to nowy ląd dla frakcji (nie jej teren, nie woda, osiągalny z zamku).
    [[nodiscard]] float towerGain(int faction, const Vec3& p) const;
    // Front obrony: punkt na naszej granicy od strony zagrożenia (ostatni atak albo najbliższy znany budynek wroga).
    // level: 0 = brak zagrożenia, 1 = wróg w pobliżu, 2 = niedawny atak.
    bool defenseFront(int faction, Vec3& front, int& level) const;
    [[nodiscard]] Vec3 homeOf(int faction) const;   // zamek frakcji (albo pozycja startowa)

    // --- Wojsko ---
    void trainArmy(int faction);
    void launchAttackWave(int faction, float dt);
    // Zwiad: jeden żołnierz idzie na najbliższy nieodkryty ląd, dopóki AI nie zna wroga i większej części mapy.
    void scout(int faction, float dt);
    bool unexploredTarget(int faction, const Vec3& from, Vec3& target) const;

    void logDecision(int faction, const std::string& text);

    World& w_;
    std::array<Economy, kFactionCount> eco_;                      // stan z ostatniego kroku myślenia
    std::array<std::vector<Vec3>, kFactionCount> scoutFailed_;     // cele zwiadu, do których nie dało się dojść
    std::array<Vec3, kFactionCount> scoutGoal_{};                  // ostatni cel zwiadowcy
    bool defenseTower_ = false;     // findSpot: wieża obronna (gęsto przy froncie) zamiast poszerzającej teren
    float towerBestGain_ = 0.0f;    // diagnostyka: najlepszy zysk lądu z ostatniego szukania miejsca na wieżę
};

} // namespace settlers
