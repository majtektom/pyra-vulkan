#pragma once
/**
 * slot_ref.h - stabilne uchwyty z generacją (Ref<T>) i ponowne używanie slotów (SlotFreeList).
 *
 * PROBLEM
 * -------
 * Obiekty świata (jednostki, budynki, flagi...) leżą w wektorach, a indeks jest identyfikatorem. Gdyby martwy
 * slot nigdy nie był używany ponownie, wektory rosłyby przez całą partię (każda strzała, ścięte drzewo, poległy
 * żołnierz zostawia "trupa"), a pętle skanowałyby coraz więcej martwych wpisów. Gdy slot jest używany ponownie,
 * stare odwołanie "cel = jednostka nr 17" wskazywałoby nagle zupełnie inną jednostkę (np. własną).
 *
 * ROZWIĄZANIE
 * -----------
 * Każdy obiekt ma licznik gen, zwiększany przy ponownym użyciu slotu. Odwołanie Ref<T> pamięta indeks i gen
 * z chwili przypisania. Gdy gen w slocie jest już inny, obiekt, do którego się odwoływaliśmy, nie istnieje -
 * Ref zachowuje się jak -1 ("brak"). Dzięki niejawnej konwersji na int istniejący kod w stylu
 *     if (u.targetUnit >= 0 && units[u.targetUnit].alive) ...
 * działa bez zmian, a przypisanie u.targetUnit = i zapamiętuje generację slotu i.
 *
 * Skąd Ref wie, jaki jest aktualny gen slotu? World rejestruje swoje wektory w SlotRegistry<T> (naraz istnieje
 * jeden świat). Zapis gry: Ref zapisuje się jako int (nieaktualny jako -1); po wczytaniu wszystkie gen = 0.
 */
#include <cstdint>
#include <deque>
#include <format>
#include <utility>
#include <vector>

namespace settlers {

// Wektor obiektów typu T świata, który właśnie działa (World::activate - konstruktor, krok symulacji, zapis,
// wczytanie). Służy tylko przy PRZYPISANIU: Ref zapamiętuje wtedy wektor swojego świata, więc odczyt zawsze
// sprawdza właściwy świat - także gdy istnieją dwa światy naraz (np. podgląd zapisu).
template <class T>
struct SlotRegistry {
    static inline const std::vector<T>* items = nullptr;
};

template <class T>
class Ref {
public:
    Ref() = default;
    explicit Ref(int index) { *this = index; }

    // Przypisanie indeksu (w trakcie gry): zapamiętujemy wektor bieżącego świata i generację, jaką slot ma TERAZ.
    Ref& operator=(int index) {
        pool_ = SlotRegistry<T>::items;
        index_ = index < 0 ? -1 : index;
        gen_ = (pool_ && index_ >= 0 && index_ < static_cast<int>(pool_->size())) ? (*pool_)[static_cast<size_t>(index_)].gen : 0u;
        return *this;
    }
    // Indeks z zapisu gry: po wczytaniu każdy slot ma generację 0 - nie czytamy jej z wektora (ten może być
    // jeszcze w trakcie wypełniania albo, przy wczytaniu do istniejącego świata, mieć stare generacje).
    void setLoadedIndex(int index) {
        pool_ = SlotRegistry<T>::items;
        index_ = index < 0 ? -1 : index;
        gen_ = 0;
    }

    // Indeks obiektu albo -1, gdy brak odwołania lub slot zajął już inny obiekt.
    operator int() const { return valid() ? index_ : -1; }
    [[nodiscard]] int get() const { return *this; }
    // Poza zakresem wektora = nieważne (np. uszkodzony zapis z indeksem 999 - bez wyjścia poza tablicę).
    [[nodiscard]] bool valid() const {
        if (index_ < 0 || !pool_ || index_ >= static_cast<int>(pool_->size())) return false;
        return (*pool_)[static_cast<size_t>(index_)].gen == gen_;
    }
    void reset() {
        index_ = -1;
        gen_ = 0;
    }

private:
    const std::vector<T>* pool_ = nullptr;   // wektor świata, do którego należy obiekt
    int index_ = -1;
    uint32_t gen_ = 0;
};

/**
 * Lista wolnych slotów: martwy obiekt trafia tu z czasem śmierci, a nowy obiekt zajmuje najstarszy wolny slot,
 * o ile minęła kwarantanna (w tym czasie reszta gry "widzi" jeszcze martwy obiekt pod starym indeksem -
 * np. tragarz może zauważyć, że jego zlecenie przepadło). Ponowne użycie zwiększa gen slotu.
 */
class SlotFreeList {
public:
    explicit SlotFreeList(float quarantineSeconds) : quarantine_(quarantineSeconds) {}

    void release(int index, float time) { free_.emplace_back(index, time); }
    void clear() { free_.clear(); }
    [[nodiscard]] size_t size() const { return free_.size(); }

    // Slot dla nowego obiektu: ponownie użyty (gen + 1) albo nowy na końcu wektora.
    template <class T>
    int allocate(std::vector<T>& items, float now) {
        while (!free_.empty() && now - free_.front().second >= quarantine_) {
            const int i = free_.front().first;
            free_.pop_front();
            if (i < 0 || i >= static_cast<int>(items.size()) || items[static_cast<size_t>(i)].alive) continue;
            const uint32_t gen = items[static_cast<size_t>(i)].gen + 1;
            items[static_cast<size_t>(i)] = T{};
            items[static_cast<size_t>(i)].gen = gen;
            return i;
        }
        items.emplace_back();
        return static_cast<int>(items.size()) - 1;
    }

private:
    std::deque<std::pair<int, float>> free_;
    float quarantine_;
};

} // namespace settlers

// Logi (std::format / LOG_INFO): Ref wypisuje się jak indeks (-1 = brak).
template <class T>
struct std::formatter<settlers::Ref<T>> : std::formatter<int> {
    auto format(const settlers::Ref<T>& r, std::format_context& ctx) const {
        return std::formatter<int>::format(static_cast<int>(r), ctx);
    }
};
