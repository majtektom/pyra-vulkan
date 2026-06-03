#pragma once

#include <functional>
#include <vector>

namespace engine3d {

/**
 * @brief Orkiestrator etapów renderowania (Render Orchestrator).
 * 
 * Klasa ta definiuje elastyczny i modularny sposób porządkowania oraz wykonywania 
 * poszczególnych kroków rysowania klatki (np. rysowanie geometrii nieprzezroczystej, 
 * cieniowanie odroczone - deferred, rysowanie obiektów przezroczystych, efekty postprocessingu, interfejs użytkownika).
 * 
 * Pozwala to na odseparowanie niskopoziomowych wywołań rysujących od samej logiki 
 * kompozycji klatki. Dzięki temu kolejność wykonywania passów (np. czy niebo rysuje się 
 * na początku klatki czy na końcu) można zdefiniować za pomocą prostego planu (std::vector<Step>).
 */
class RenderOrchestrator {
public:
    /**
     * @brief Kroki renderowania klatki.
     */
    enum class Step {
        FeaturesBeforeOpaque,     // 1. Etap przygotowawczy (np. generowanie map cieni, pre-pass głębi)
        OpaquePass,               // 2. Rysowanie obiektów nieprzezroczystych (zapis do bufora głębi, optymalizacja Early-Z)
        FeaturesAfterOpaque,      // 3. Efekty wymagające gotowej geometrii (np. SSAO, Screen Space Reflections)
        DeferredLighting,         // 4. Cieniowanie odroczone (Deferred Shading) - aplikowanie światła do bufora G-Buffer
        FeaturesAfterTransparent, // 5. Efekty po obiektach przezroczystych (np. mgła, głębia ostrości - DoF, postprocessing)
        TransparentPass,          // 6. Obiekty przezroczyste (wymagające sortowania back-to-front i blendingu alfa)
        FeaturesOverlay,          // 7. Nakładki 2D (np. interfejs użytkownika UI, tekst, gizma edytora)
    };

    /**
     * @brief Zbiór funkcji zwrotnych (callbacks) podpinanych przez silnik, 
     * odpowiadających za faktyczne renderowanie obiektów w danym kroku.
     */
    struct Callbacks {
        std::function<void()> onFeaturesBeforeOpaque;
        std::function<void()> onOpaquePass;
        std::function<void()> onFeaturesAfterOpaque;
        std::function<void()> onDeferredLighting;
        std::function<void()> onFeaturesAfterTransparent;
        std::function<void()> onTransparentPass;
        std::function<void()> onFeaturesOverlay;
    };

    /**
     * @brief Wykonuje zdefiniowany plan kroków klatki po kolei.
     * @param plan Wektor kroków określający kolejność ich wykonania.
     * @param callbacks Funkcje wykonujące renderowanie powiązane z poszczególnymi krokami.
     */
    void execute(const std::vector<Step>& plan, const Callbacks& callbacks) const;
};

} // namespace engine3d
