#include "render/render_orchestrator.h"

namespace engine3d {

void RenderOrchestrator::execute(const std::vector<Step>& plan, const Callbacks& callbacks) const {
    // Sekwencyjne przechodzenie przez zdefiniowane kroki planu renderowania klatki.
    // Każdy krok uruchamia przypisany mu callback (jeśli został poprawnie zarejestrowany).
    for (const Step step : plan) {
        switch (step) {
        case Step::FeaturesBeforeOpaque:
            if (callbacks.onFeaturesBeforeOpaque) callbacks.onFeaturesBeforeOpaque();
            break;
        case Step::OpaquePass:
            if (callbacks.onOpaquePass) callbacks.onOpaquePass();
            break;
        case Step::FeaturesAfterOpaque:
            if (callbacks.onFeaturesAfterOpaque) callbacks.onFeaturesAfterOpaque();
            break;
        case Step::DeferredLighting:
            if (callbacks.onDeferredLighting) callbacks.onDeferredLighting();
            break;
        case Step::FeaturesAfterTransparent:
            if (callbacks.onFeaturesAfterTransparent) callbacks.onFeaturesAfterTransparent();
            break;
        case Step::TransparentPass:
            if (callbacks.onTransparentPass) callbacks.onTransparentPass();
            break;
        case Step::FeaturesOverlay:
            if (callbacks.onFeaturesOverlay) callbacks.onFeaturesOverlay();
            break;
        }
    }
}

} // namespace engine3d
