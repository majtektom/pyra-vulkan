#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "core/types.h"
#include "render/render_feature.h"
#include "render/render_types.h"

namespace engine3d {

class Engine;

class SpriteSystem : public IRenderFeature {
public:
    struct Sprite {
        Vec3 position = {0.0f, 0.0f, 0.0f};
        float size = 1.0f;
        std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
        std::string textureName;
        bool active = false;

        bool isSequence = false;
        std::vector<std::string> frameTextures;
        std::vector<std::shared_ptr<MaterialInstance>> frameMaterials;

        float uvOffsetU = 0.0f;
        float uvOffsetV = 0.0f;
        float uvScaleU = 1.0f;
        float uvScaleV = 1.0f;

        int framesX = 1;
        int framesY = 1;
        float timePerFrame = 0.1f;
        float elapsed = 0.0f;
        int currentFrame = 0;
        int startFrame = 0;
        int endFrame = -1;
        bool playing = true;
        bool loop = true;

        std::string entityName;
        std::string meshName;
    };

    SpriteSystem();
    ~SpriteSystem();

    void initialize(Engine& engine);
    void update(float dt, Engine& engine);

    int addSprite(const Sprite& sprite);
    Sprite& getSprite(int id);
    int spriteCount() const;
    void removeSprite(int id);
    void clear();

    void setPosition(int id, const Vec3& pos);
    void setSize(int id, float size);
    void setColor(int id, const std::array<float, 4>& color);
    void setUV(int id, float offsetU, float offsetV, float scaleU, float scaleV);

    void setAnim(int id, int framesX, int framesY, float timePerFrame,
                 int startFrame = 0, int endFrame = -1, bool loop = true);
    void playAnim(int id);
    void pauseAnim(int id);
    void stopAnim(int id);

    void init(Renderer*) override {}
    void render(VkCommandBuffer, uint32_t, const RenderView&) override {}
    bool isActive() const override { return initialized_; }
    const std::string& getName() const override { static std::string n = "SpriteSystem"; return n; }

private:
    static constexpr int MAX_SPRITES = 1024;

    struct TextureGroup {
        std::string textureName;
        std::string meshName;
        std::string materialName;
        int capacity = 0;
    };

    void applyFrameUV(int id);
    void advanceAnimation(int id, float dt);
    void createSheetGroup(Engine& engine, const std::string& texName,
                          const std::vector<int>& indices, int groupIdx);
    void createSequenceEntity(Engine& engine, int spriteId);
    void updateSheetInstances(Engine& engine);
    void updateSequenceEntities(Engine& engine);

    Sprite sprites_[MAX_SPRITES];
    int spriteCount_ = 0;
    std::vector<TextureGroup> groups_;
    Engine* engine_ = nullptr;
    bool initialized_ = false;
};

}  // namespace engine3d
