#include "core/logger.h"
#include "core/asset_registry.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "feature/sprite/sprite.h"
#include "render/material_manager.h"
#include "render/material_system.h"
#include "scene/scene.h"

namespace engine3d {

namespace {

MeshData createQuadMesh() {
    MeshData quad;
    quad.vertices = {
        {{{-0.5f, -0.5f, 0.0f}}, {{0, 0, 1}}, {{1, 1, 1}}, {{0, 1}}},
        {{{ 0.5f, -0.5f, 0.0f}}, {{0, 0, 1}}, {{1, 1, 1}}, {{1, 1}}},
        {{{ 0.5f,  0.5f, 0.0f}}, {{0, 0, 1}}, {{1, 1, 1}}, {{1, 0}}},
        {{{-0.5f,  0.5f, 0.0f}}, {{0, 0, 1}}, {{1, 1, 1}}, {{0, 0}}},
    };
    quad.indices = {0, 1, 2, 2, 3, 0};
    return quad;
}

SpriteInstanceData buildInstanceData(const SpriteSystem::Sprite& s) {
    SpriteInstanceData inst;
    inst.posX = s.position.x;
    inst.posY = s.position.y;
    inst.posZ = s.position.z;
    inst.size = s.size;
    inst.r = s.color[0];
    inst.g = s.color[1];
    inst.b = s.color[2];
    inst.a = s.color[3];
    inst.uvOffsetU = s.uvOffsetU;
    inst.uvOffsetV = s.uvOffsetV;
    inst.uvScaleU = s.uvScaleU;
    inst.uvScaleV = s.uvScaleV;
    return inst;
}

std::string requireTextureName(AssetRegistry& assetRegistry, TextureHandle handle, const std::string& context) {
    if (!handle.isValid()) {
        LOG_WARN("[SpriteSystem] Invalid TextureHandle for: {}", context);
        return "default";
    }

    const auto textureName = assetRegistry.textureName(handle);
    if (!textureName.has_value()) {
        LOG_WARN("[SpriteSystem] Missing texture name for handle ({}) in: {}", handle.value, context);
        return "default";
    }
    return *textureName;
}

}  // namespace

SpriteSystem::SpriteSystem() = default;
SpriteSystem::~SpriteSystem() { clear(); }

int SpriteSystem::addSprite(const Sprite& sprite) {
    if (spriteCount_ >= MAX_SPRITES) {
        LOG_ERROR("[SpriteSystem] Max sprites reached ({})", MAX_SPRITES);
        return -1;
    }
    int id = spriteCount_++;
    sprites_[id] = sprite;
    sprites_[id].active = true;
    return id;
}

SpriteSystem::Sprite& SpriteSystem::getSprite(int id) {
    return sprites_[id];
}

int SpriteSystem::spriteCount() const {
    return spriteCount_;
}

void SpriteSystem::removeSprite(int id) {
    if (id >= 0 && id < spriteCount_) sprites_[id].active = false;
}

void SpriteSystem::clear() {
    spriteCount_ = 0;
    groups_.clear();
}

void SpriteSystem::setPosition(int id, const Vec3& pos) {
    if (id >= 0 && id < spriteCount_) sprites_[id].position = pos;
}

void SpriteSystem::setSize(int id, float size) {
    if (id >= 0 && id < spriteCount_) sprites_[id].size = size;
}

void SpriteSystem::setColor(int id, const std::array<float, 4>& color) {
    if (id >= 0 && id < spriteCount_) sprites_[id].color = color;
}

void SpriteSystem::setUV(int id, float offsetU, float offsetV, float scaleU, float scaleV) {
    if (id >= 0 && id < spriteCount_) {
        sprites_[id].uvOffsetU = offsetU;
        sprites_[id].uvOffsetV = offsetV;
        sprites_[id].uvScaleU = scaleU;
        sprites_[id].uvScaleV = scaleV;
    }
}

void SpriteSystem::setAnim(int id, int framesX, int framesY, float timePerFrame,
                           int startFrame, int endFrame, bool loop) {
    if (id < 0 || id >= spriteCount_) return;
    auto& s = sprites_[id];
    s.framesX = framesX;
    s.framesY = framesY;
    s.timePerFrame = timePerFrame;
    s.startFrame = startFrame;
    s.endFrame = (endFrame < 0) ? framesX * framesY - 1 : endFrame;
    s.currentFrame = startFrame;
    s.elapsed = 0.0f;
    s.playing = true;
    s.loop = loop;
    if (!s.isSequence) {
        applyFrameUV(id);
    }
}

void SpriteSystem::playAnim(int id) {
    if (id >= 0 && id < spriteCount_) sprites_[id].playing = true;
}

void SpriteSystem::pauseAnim(int id) {
    if (id >= 0 && id < spriteCount_) sprites_[id].playing = false;
}

void SpriteSystem::stopAnim(int id) {
    if (id >= 0 && id < spriteCount_) {
        auto& s = sprites_[id];
        s.playing = false;
        s.currentFrame = s.startFrame;
        s.elapsed = 0.0f;
        if (!s.isSequence) {
            applyFrameUV(id);
        }
    }
}

void SpriteSystem::applyFrameUV(int id) {
    auto& s = sprites_[id];
    s.uvScaleU = 1.0f / static_cast<float>(s.framesX);
    s.uvScaleV = 1.0f / static_cast<float>(s.framesY);
    s.uvOffsetU = static_cast<float>(s.currentFrame % s.framesX) * s.uvScaleU;
    s.uvOffsetV = static_cast<float>(s.currentFrame / s.framesX) * s.uvScaleV;
}

void SpriteSystem::advanceAnimation(int id, float dt) {
    auto& s = sprites_[id];
    if (!s.playing) return;
    if (!s.isSequence && s.framesX <= 1 && s.framesY <= 1) return;
    if (s.isSequence && s.frameTextureHandles.size() <= 1) return;

    int totalFrames = s.isSequence ? static_cast<int>(s.frameTextureHandles.size())
                                   : s.framesX * s.framesY;
    int maxFrame = (s.endFrame >= 0) ? s.endFrame : totalFrames - 1;

    s.elapsed += dt;
    while (s.elapsed >= s.timePerFrame) {
        s.elapsed -= s.timePerFrame;
        s.currentFrame++;
        if (s.currentFrame > maxFrame) {
            if (s.loop) {
                s.currentFrame = s.startFrame;
            } else {
                s.currentFrame = maxFrame;
                s.playing = false;
            }
        }
        if (!s.isSequence) {
            applyFrameUV(id);
        }
    }
}

void SpriteSystem::createSheetGroup(TextureHandle texHandle,
                                    const std::vector<int>& indices, int groupIdx) {
    int count = static_cast<int>(indices.size());

    TextureGroup group;
    group.textureHandle = texHandle;
    group.capacity = count;
    group.meshName = "spriteSheetMesh_" + std::to_string(groupIdx);

    Material mat;
    mat.name = "spriteSheetMat_" + std::to_string(groupIdx);
    mat.textureHandle = texHandle;
    mat.shaderProgram = "sprite";
    mat.blendMode = BlendMode::Alpha;
    mat.renderPriority = 1;
    mat.depthWrite = true;
    mat.cullMode = CullMode::Disabled;
    const auto materialHandle = materialManager_->add(mat);

    auto matInst = materialSystem_->getOrCreateMaterialInstance(mat);

    auto quadMesh = createQuadMesh();
    meshManager_->loadMesh(group.meshName, quadMesh);

    VkDeviceSize instBufSize = static_cast<VkDeviceSize>(count) * sizeof(SpriteInstanceData);
    meshManager_->loadInstanceBuffer(group.meshName, instBufSize);

    auto* entity = scene_->createEntity("spriteSheetGroup_" + std::to_string(groupIdx));
    auto* mc = entity->addComponent<MeshComponent>(group.meshName);
    mc->materialHandle = materialHandle;
    mc->materialInstance = matInst;

    groups_.push_back(group);
}

void SpriteSystem::createSequenceEntity(int spriteId) {
    auto& s = sprites_[spriteId];
    std::string meshName = "spriteSeqMesh_" + std::to_string(spriteId);
    s.meshName = meshName;
    s.entityName = "spriteSeqEntity_" + std::to_string(spriteId);

    s.frameMaterials.clear();
    for (const auto texHandle : s.frameTextureHandles) {
        Material mat;
        mat.name = "spriteSeqMat_" + std::to_string(texHandle.value);
        mat.textureHandle = texHandle;
        mat.shaderProgram = "sprite";
        mat.blendMode = BlendMode::Additive;
        // mat.blendMode = BlendMode::Alpha;
        mat.renderPriority = 1;
        mat.depthWrite = false;
        mat.cullMode = CullMode::Disabled;
        const auto materialHandle = materialManager_->add(mat);
        s.frameMaterials.push_back(
            materialSystem_->getOrCreateMaterialInstance(mat));
        // Każdy frame sequence ma oddzielny materiał, dlatego handle nie jest
        // przechowywany globalnie - aktywny frame ustawia materialInstance.
        (void)materialHandle;
    }

    auto quadMesh = createQuadMesh();
    meshManager_->loadMesh(meshName, quadMesh);

    VkDeviceSize instBufSize = sizeof(SpriteInstanceData);
    meshManager_->loadInstanceBuffer(meshName, instBufSize);

    auto* entity = scene_->createEntity(s.entityName);
    auto* mc = entity->addComponent<MeshComponent>(meshName);

    s.currentFrame = s.startFrame;
    size_t frameIdx = static_cast<size_t>(s.currentFrame);
    if (frameIdx < s.frameMaterials.size()) {
        mc->materialInstance = s.frameMaterials[frameIdx];
    }

    SpriteInstanceData inst = buildInstanceData(s);
    meshManager_->updateInstanceBuffer(
        meshName, &inst, sizeof(SpriteInstanceData), 1);
}

void SpriteSystem::initialize(Scene& scene,
                              MeshManager& meshManager,
                              MaterialManager& materialManager,
                              MaterialSystem& materialSystem,
                              AssetRegistry& assetRegistry) {
    if (initialized_) return;
    scene_ = &scene;
    meshManager_ = &meshManager;
    materialManager_ = &materialManager;
    materialSystem_ = &materialSystem;
    assetRegistry_ = &assetRegistry;

    std::vector<TextureHandle> uniqueTextures;
    std::vector<std::vector<int>> sheetIndices;

    for (int i = 0; i < spriteCount_; ++i) {
        if (!sprites_[i].active) continue;
        if (sprites_[i].isSequence) {
            createSequenceEntity(i);
            continue;
        }
        const auto tex = sprites_[i].textureHandle;
        if (!tex.isValid()) {
            continue;
        }
        bool found = false;
        for (size_t t = 0; t < uniqueTextures.size(); ++t) {
            if (uniqueTextures[t].value == tex.value) {
                sheetIndices[t].push_back(i);
                found = true;
                break;
            }
        }
        if (!found) {
            uniqueTextures.push_back(tex);
            sheetIndices.push_back({i});
        }
    }

    for (size_t gi = 0; gi < uniqueTextures.size(); ++gi) {
        createSheetGroup(uniqueTextures[gi], sheetIndices[gi],
                         static_cast<int>(gi));
    }

    initialized_ = true;
}

void SpriteSystem::updateSheetInstances() {
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        const auto& group = groups_[gi];
        std::vector<SpriteInstanceData> instData;

        for (int i = 0; i < spriteCount_; ++i) {
            if (!sprites_[i].active) continue;
            if (sprites_[i].isSequence) continue;
            if (sprites_[i].textureHandle.value != group.textureHandle.value) continue;

            instData.push_back(buildInstanceData(sprites_[i]));
        }

        uint32_t count = static_cast<uint32_t>(instData.size());
        if (count > 0) {
            meshManager_->updateInstanceBuffer(
                group.meshName, instData.data(),
                instData.size() * sizeof(SpriteInstanceData), count);
        } else {
            meshManager_->updateInstanceBuffer(
                group.meshName, nullptr, 0, 0);
        }
    }
}

void SpriteSystem::updateSequenceEntities() {
    for (int i = 0; i < spriteCount_; ++i) {
        auto& s = sprites_[i];
        if (!s.active || !s.isSequence) continue;

        auto* entity = scene_->getEntity(s.entityName);
        if (!entity) continue;

        if (auto* mc = entity->getComponent<MeshComponent>()) {
            size_t frameIdx = static_cast<size_t>(s.currentFrame);
            if (frameIdx < s.frameMaterials.size()) {
                mc->materialInstance = s.frameMaterials[frameIdx];
            }
        }

        SpriteInstanceData inst = buildInstanceData(s);
        meshManager_->updateInstanceBuffer(
            s.meshName, &inst, sizeof(SpriteInstanceData), 1);
    }
}

    void SpriteSystem::update(float dt) {
    if (!initialized_) return;

    for (int i = 0; i < spriteCount_; ++i) {
        if (sprites_[i].active) advanceAnimation(i, dt);
    }

    updateSheetInstances();
    updateSequenceEntities();
}

}  // namespace engine3d
