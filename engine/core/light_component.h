#pragma once

#include "core/component.h"
#include "core/math.h"

namespace engine3d {

// Typ swiatla obslugiwanego przez deferred/PBR pipeline.
enum class LightType {
    Directional,
    Point,
    Spot,
};

// Komponent ECS opisujacy swiatlo przypiete do encji.
// Dla Point i Spot pozycja jest pobierana z TransformComponent encji.
struct LightComponent : public Component {
    LightComponent() : Component("Light") {}

    // Rodzaj swiatla (kierunkowe, punktowe, reflektor).
    LightType type = LightType::Point;

    // Kolor swiatla.
    Vec3 color{1.0f, 1.0f, 1.0f};

    // Mnoznik jasnosci.
    float intensity = 1.0f;

    // Zasieg dla Point/Spot.
    float range = 30.0f;

    // Kierunek swiatla dla Directional/Spot.
    Vec3 direction{0.0f, -1.0f, 0.0f};

    // Katy stozka (w stopniach) dla Spot.
    float innerAngleDeg = 20.0f;
    float outerAngleDeg = 30.0f;
};

} // namespace engine3d