#include "render/material_system.h"
#include "render/base_material_instance.h"
#include "render/material_compiler.h"

namespace engine3d {

MaterialSystem::MaterialSystem(std::shared_ptr<VulkanContext> context, VkFormat colorFormat, VkFormat depthFormat) 
    : compiler_(std::make_unique<MaterialCompiler>(std::move(context), colorFormat, depthFormat)) {}

MaterialSystem::~MaterialSystem() = default;

void MaterialSystem::setFormats(VkFormat colorFormat, VkFormat depthFormat) {
    if (compiler_) {
        compiler_->setFormats(colorFormat, depthFormat);
    }
}

std::shared_ptr<MaterialInstance> MaterialSystem::getOrCreateMaterialInstance(const Material& materialData) {
    // Sprawdzamy w cache'u, czy instancja materiału o danej nazwie została już skompilowana.
    // Jeśli nie – tworzymy nową instancję klasy BaseMaterialInstance powiązaną z kompilatorem.
    return materialCache_.load(materialData.name, [&]() {
        return std::make_shared<BaseMaterialInstance>(materialData, compiler_.get(), VK_NULL_HANDLE);
    });
}

void MaterialSystem::invalidatePipelinesForShader(const std::string& shaderName) {
    if (compiler_) {
        // Informujemy kompilator o konieczności usunięcia starego potoku z pamięci GPU.
        compiler_->invalidatePipelinesForShader(shaderName);
    }
    // Czyścimy pamięć podręczną instancji materiałów, aby wymusić ich ponowną kompilację w kolejnej klatce.
    materialCache_.clear();
}

void MaterialSystem::setWireframe(bool enabled) {
    if (compiler_) {
        compiler_->setWireframe(enabled);
    }
    materialCache_.clear();
}

bool MaterialSystem::isWireframe() const {
    return compiler_ ? compiler_->isWireframe() : false;
}

void MaterialSystem::setTessellationEnabled(bool enabled) {
    if (compiler_) {
        compiler_->setTessellationEnabled(enabled);
    }
    materialCache_.clear();
}

bool MaterialSystem::isTessellationEnabled() const {
    return compiler_ ? compiler_->isTessellationEnabled() : true;
}

} // namespace engine3d
