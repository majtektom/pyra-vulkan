#include "core/component.h"

namespace engine3d {

Component::Component(const std::string& componentName) : name(componentName) {}

void Component::initialize() {}

void Component::update(std::chrono::milliseconds /*deltaTime*/) {}

void Component::render() {}

} // namespace engine3d
