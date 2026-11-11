#pragma once

/**
 * renderer_impl.h - PRYWATNY nagłówek renderera (dołączany tylko przez pliki renderer_*.cpp).
 *
 * Renderer (renderer.h) to fasada; całą pracę wykonuje Renderer::RendererImpl. Klasa jest jedna, bo przebiegi
 * dzielą zasoby (obrazy G-Buffera, mapę cieni, descriptor sety, UBO klatki), ale jej metody są rozłożone
 * po plikach według przebiegów klatki:
 *   renderer_core.cpp          - inicjalizacja, pętla klatki (render), UBO, światła, synchronizacja, fasada
 *   renderer_shadow_pass.cpp   - mapy cieni: kaskady słońca, atlas świateł lokalnych, potoki cieni
 *   renderer_gbuffer_pass.cpp  - G-Buffer: zasoby, potoki, sortowanie i rysowanie obiektów nieprzezroczystych
 *   renderer_deferred_pass.cpp - oświetlenie deferred (pełny ekran) + obiekty forward
 *   renderer_scene_copy.cpp    - kopia koloru/głębokości sceny dla oceanu (odbicia, załamania)
 *   renderer_overlay_pass.cpp  - przezroczyste, nakładka GUI, wywołania IRenderFeature
 *   renderer_textures.cpp      - tekstury, tablica bindless, indeksy tekstur paczek
 *   renderer_fog_of_war.cpp    - tekstura mgły wojny
 * Układ push constants: push_constants.h; rysowanie paczki materiałem: packet_draw.h.
 */
#include "core/profiler.h"
#include "render/material_compiler.h"
#include "render/packet_draw.h"
#include "render/pipeline_builder.h"
#include "render/push_constants.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "render/backend/vulkan_context.h"
#include "render/backend/vulkan_swapchain.h"
#include "render/standard_passes.h"
#include "platform/platform.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"
#include "render/render_pass.h"
#include "render/texture_manager.h"
#include "render/vulkan_util.h"
#include "render/texture_asset_loader.h"
#include "render/renderer.h"
#include "render/render_feature.h"
#include "render/material_manager.h"
#include "core/asset_registry.h"
#include "core/logger.h"

namespace engine3d {

// Triple buffering: CPU może przygotowywać klatkę N+2 gdy GPU renderuje N.
// Eliminuje vkWaitForFences stall który ograniczał do ~120fps.
constexpr int kMaxFrames = Renderer::kMaxFrames;
constexpr int kMaxDescriptorSets = 64;

constexpr int kMaxLights = 16;
constexpr int kShadowCascades = 4;

// --- Układ pamięci map cieni (jedna tablica tekstur głębokości kShadowMapSize x kShadowMapSize) ---
// Warstwy 0..kShadowCascades-1: kaskady słońca (tylko pierwsze światło kierunkowe - cień na całej mapie).
// Warstwa kLocalShadowLayer: atlas 2x2 dla świateł o ograniczonym zasięgu (reflektory, punktowe),
// każde dostaje ćwiartkę (kShadowMapSize/2). Cień dostają tylko najbliższe kamerze światła lokalne.
// Wcześniej każde z 16 świateł miało 4 warstwy (64 warstwy = ok. 1 GB VRAM), dziś 5 warstw (ok. 80 MB).
constexpr uint32_t kLocalShadowSlots = 4;
constexpr uint32_t kLocalShadowLayer = kShadowCascades;
constexpr uint32_t kShadowLayerCount = kShadowCascades + 1;

struct LightData {
    Matrix4 lightViewProj[kShadowCascades];
    float position[4];    // xyz = world position (point/spot), w = warstwa mapy cieni (-1 = światło bez cienia)
    float direction[4];   // xyz = normalized direction OF light rays, w = type: 1=dir, 2=point, 3=spot
    float color[4];       // rgb = color, w = intensity multiplier
    float params[4];      // spot: innerCutoffCos, outerCutoffCos, range; w = liczba kaskad (słońce) / ćwiartka atlasu (lokalne)
};

// Światło rzucające cień w bieżącej klatce (wynik assignShadowSlots).
struct ShadowCaster {
    uint32_t lightIndex;
    uint32_t layer;
    uint32_t cascadeCount;   // słońce: kShadowCascades, lokalne: 1
    int atlasSlot;           // -1 dla słońca, 0..3 dla świateł lokalnych
};

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
    Matrix4 invViewProj;
    float camRight[4];
    float camUp[4];
    float cameraPos[4];
    float ambientColor[4];
    float ambientStrength;
    uint32_t lightingEnabled;
    uint32_t lightCount;
    uint32_t shadowsEnabled;
    LightData lights[kMaxLights];
    float directionalShadowSplits[4];
    uint32_t directionalShadowCascadeCount;
    float _shadowPadding[3];
    float fogColor[4];
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint32_t fogEnabled;
    float windTime;
    float windStrength;
    // Mgła wojny (fog of war): tekstura widoczności nad prostokątem mapy [origin, origin + size] (XZ).
    float fowOriginX;
    float fowOriginZ;
    float fowSize;
    float fowExplored;     // jasność terenu odkrytego, ale nie widzianego teraz
    uint32_t fowEnabled;
    uint32_t fowTextureIndex;   // indeks tekstury widoczności bieżącej klatki w tablicy bindless (shader terenu)
};

struct TextureResources {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t mipLevels = 1;
    AddressMode addressMode = AddressMode::Repeat;
};

constexpr int kMaxBindlessTextures = 1024;

class Renderer::RendererImpl {
public:
    RendererImpl(
        Platform* platform,
        MaterialManager* materialManager,
        std::shared_ptr<VulkanContext> context,
        AssetRegistry* assetRegistry)
        : platform_(platform),
          materialManager_(materialManager),
          vulkanContext_(context),
          assetRegistry_(assetRegistry) {}

    ~RendererImpl() { cleanup(); }

    bool initialize(const std::string& appName, bool enableValidationLayers);

    void render(const RenderScene& scene, const RenderView& view);

    // ---------------------------------------------------------------------------------------------------
    // Przebiegi jednej klatki (kolejność w render()): cienie -> G-Buffer -> oświetlenie (deferred) i obiekty
    // forward -> kopia koloru sceny (dla oceanu) -> przezroczyste -> nakładka GUI.
    // ---------------------------------------------------------------------------------------------------
    VkViewport swapchainViewport() const {
        return VkViewport{0.0f, 0.0f, static_cast<float>(swapchain_->extent().width),
                          static_cast<float>(swapchain_->extent().height), 0.0f, 1.0f};
    }
    VkRect2D swapchainScissor() const { return VkRect2D{{0, 0}, swapchain_->extent()}; }

    // Mapy cieni: 4 kaskady słońca (warstwy 0-3) i atlas 2x2 świateł lokalnych (warstwa 4).
    // Rodzaj potoku cienia (kolejność = kolejność rysowania po sortowaniu).
    enum class ShadowDrawKind { TessellatedTerrain = 0, Mesh = 1, Skinned = 2 };
    struct ShadowDraw {
        const RenderPacket* packet = nullptr;
        ShadowDrawKind kind = ShadowDrawKind::Mesh;
        Vec3 center{}, extent{};                // granice paczki (świat): środek i połowa rozmiaru
    };
    void recordShadowPass(VkCommandBuffer cmd, const RenderScene& scene);
    void sortShadowDraws(const RenderScene& scene);
    void drawShadowCasters(VkCommandBuffer cmd, const Matrix4& lightViewProj, const Vec3& cullCenter, float cullRadius,
                           bool sun);
    void drawShadowCaster(VkCommandBuffer cmd, DrawBindCache& bound, const ShadowDraw& draw, const Matrix4& lightViewProj,
                          bool sun);
    void beginShadowLayer(VkCommandBuffer cmd, uint32_t layer);
    void setShadowViewport(VkCommandBuffer cmd, int atlasSlot);

    // G-Buffer: obiekty nieprzezroczyste zapisują albedo, normalne, materiał i pozycję (oświetlenie - deferred).
    void recordGBufferPass(VkCommandBuffer cmd, const RenderScene& scene, uint32_t imageIndex);
    void beginGBufferRendering(VkCommandBuffer cmd, uint32_t imageIndex);
    void sortGBufferDraws(const RenderScene& scene);
    void drawGBufferPacket(VkCommandBuffer cmd, DrawBindCache& bound, const RenderPacket& packet);

    // Część 1 na obrazie swapchaina: oświetlenie deferred (pełny ekran), obiekty forward, wstępna głębokość przezroczystych.
    void recordLightingPass(VkCommandBuffer cmd, const RenderScene& scene, const RenderView& view, uint32_t imageIndex);

    // Kopia koloru i głębokości sceny do tekstur (ocean czyta je do odbić/załamań), przejście głębokości w tryb odczytu.
    void copySceneColor(VkCommandBuffer cmd, uint32_t imageIndex);
    void copySceneColorImage(VkCommandBuffer cmd, uint32_t imageIndex);
    void transitionDepthForSampling(VkCommandBuffer cmd, uint32_t imageIndex);

    // Część 2: ocean i obiekty przezroczyste (głębokość tylko do odczytu).
    void recordTransparentPass(VkCommandBuffer cmd, const RenderScene& scene, const RenderView& view, uint32_t imageIndex);

    // Część 3: nakładka (GUI, tekst) z własnym, wyczyszczonym buforem głębokości.
    void recordOverlayPass(VkCommandBuffer cmd, const RenderView& view, uint32_t imageIndex);


    TextureHandle loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat, bool disableAlpha = false);

    TextureHandle loadTextureFromMemory(const std::string& name, const std::vector<float>& data, uint32_t width, uint32_t height, bool mirroredRepeat);

    TextureHandle uploadTexture(const std::string& name, TextureUploadData& uploadData, bool mirroredRepeat,

                                bool disableAlpha);

    // layout: układ obrazu w chwili próbkowania (tekstury: SHADER_READ_ONLY; obrazy kopiowane z CPU: vk::hostCopyLayout()).
    void registerTextureInBindless(const std::string& name, VkImageView imageView, VkSampler sampler,
                                   VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // Bez vkDeviceWaitIdle: podmiana deskryptorów per klatka i opóźnione niszczenie (renderer_textures.cpp).
    struct PendingDescriptorWrite {
        uint32_t index;
        VkImageView view;
        VkSampler sampler;
        std::array<bool, kMaxFrames> done;
    };
    struct RetiredTexture {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
        uint64_t frame;
    };
    std::vector<PendingDescriptorWrite> pendingDescriptorWrites_;
    std::vector<RetiredTexture> retiredTextures_;
    uint64_t frameNumber_ = 0;   // licznik klatek (opóźnione niszczenie zasobów)
    void retireTexture(VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler);
    void queueDescriptorWrite(uint32_t index, VkImageView view, VkSampler sampler);
    void applyDescriptorWrites();
    void destroyRetired(const RetiredTexture& r);


    // Dostęp dla pod-rendererów
    VkDevice device() const { return vulkanContext_->device(); }
    VkPhysicalDevice physicalDevice() const { return vulkanContext_->physicalDevice(); }
    VkFormat colorFormat() const { return swapchain_ ? swapchain_->imageFormat() : VK_FORMAT_UNDEFINED; }
    VkFormat depthFormat() const { return swapchain_ ? swapchain_->depthFormat() : VK_FORMAT_UNDEFINED; }

    VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayout_; }
    VkCommandPool commandPool() const { return vulkanContext_->commandPool(); }
    VkQueue graphicsQueue() const { return vulkanContext_->graphicsQueue(); }
    VkExtent2D extent() const { return swapchain_ ? swapchain_->extent() : VkExtent2D{0,0}; }
    void setTessellationEnabled(bool enabled) { tessellationEnabled_ = enabled; }
    bool isTessellationEnabled() const { return tessellationEnabled_; }
    bool isTessellationSupported() const { return vulkanContext_ && vulkanContext_->isTessellationSupported(); }
    void setTessellationLevel(float level) { tessellationLevel_ = level; }
    float getTessellationLevel() const { return tessellationLevel_; }
    void setTextureFilter(TextureFilter filter);
    TextureFilter getTextureFilter() const { return textureFilter_; }
    
    uint32_t getTextureIndex(const std::string& name) const {
        auto it = textureNameToIndex_.find(name);
        if (it != textureNameToIndex_.end()) return it->second;
        return 0;
    }

    uint32_t getTextureIndex(TextureHandle handle) const;

    void prepareRenderPacket(RenderPacket& packet);

    // Indeksy bindless warstw i mapy wysokości terenu. Wywoływane na początku render(), na jednym wątku - paczki
    // jednego terenu dzielą TerrainSurface, więc prepareRenderPacket (równolegle w wątkach) nie może jej zmieniać.
    void resolveTerrainSurface(TerrainSurface& t) const;

    void resolveTerrainSurfaces(const RenderScene& scene);

    VkDescriptorSet getDescriptorSet(uint32_t frame) const {
        return frame < descriptorSets_.size() ? descriptorSets_[frame] : VK_NULL_HANDLE;
    }

    bool getTextureInfo(const std::string& name, VkImageView& v, VkSampler& s) const;

    bool getTextureInfo(TextureHandle handle, VkImageView& v, VkSampler& s) const;

    VkBuffer getUniformBuffer(uint32_t frame) const {
        return frame < uniformBuffers_.size() ? uniformBuffers_[frame] : VK_NULL_HANDLE;
    }

    void waitIdle() {
        if (vulkanContext_ && vulkanContext_->device()) {
            vkDeviceWaitIdle(vulkanContext_->device());
        }
    }

    void cleanupGBuffer();

    // ---------------------------------------------------------------------------------
    // Mgła wojny: tekstura widoczności R8 kFogOfWarSize^2, osobna na każdą klatkę w locie (na początku klatki, po jej
    // fence, CPU kopiuje dane prosto do jej obrazu - hostImageCopy; bez wyścigu z klatką, która jeszcze czyta
    // poprzednią wersję). Shadery czytają ją z tablicy bindless
    // (indeks bieżącej klatki w UBO: fowTextureIndex) - shared/surface_lighting.hlsl, applyAtmosphere().
    // ---------------------------------------------------------------------------------
    static constexpr uint32_t kFowSize = Renderer::kFogOfWarSize;
    VkImage fowImage_[kMaxFrames]{};
    VkDeviceMemory fowImageMem_[kMaxFrames]{};
    VkImageView fowView_[kMaxFrames]{};
    VkSampler fowSampler_ = VK_NULL_HANDLE;
    std::vector<uint8_t> fowData_ = std::vector<uint8_t>(kFowSize * kFowSize, 255);
    uint64_t fowVersion_ = 1;
    uint64_t fowUploaded_[kMaxFrames]{};
    uint32_t fowBindless_[kMaxFrames]{};   // indeksy w tablicy bindless (teren czyta mgłę stamtąd)
    bool fowEnabled_ = false;
    float fowOriginX_ = 0.0f, fowOriginZ_ = 0.0f, fowSize_ = 1.0f, fowExplored_ = 0.45f;

    void createFogOfWarResources();


    // Na początku klatki: gdy dane widoczności się zmieniły, przesyłamy je do obrazu tej klatki.
    void uploadFogOfWarIfChanged();

    void setFogOfWar(bool enabled, float originX, float originZ, float worldSize, float exploredBrightness);
    void updateFogOfWar(const std::vector<uint8_t>& visibility) {
        if (visibility.size() != fowData_.size()) return;
        fowData_ = visibility;
        ++fowVersion_;
    }

    void destroyFogOfWarResources();

    void cleanup();

    uint32_t width() const { return platform_->width(); }
    uint32_t height() const { return platform_->height(); }
    std::array<float, 4> clearColor_ = {{0.1f, 0.1f, 0.1f, 1.0f}};

    void addFeature(std::shared_ptr<IRenderFeature> feature, Renderer* renderer) {
        feature->init(renderer);
        features_.push_back(feature);
    }

    void setSynchronizationMode(SynchronizationMode mode);

    SynchronizationMode synchronizationMode() const { return synchronizationMode_; }

    void setPresentMode(PresentMode mode);

    PresentMode presentMode() const { return presentMode_; }

    void setShadowsEnabled(bool enabled) { shadowsEnabled_ = enabled; }
    bool shadowsEnabled() const { return shadowsEnabled_; }
    void setWireframeEnabled(bool enabled);
    bool isWireframeEnabled() const { return wireframeEnabled_; }

    void setLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
    bool isLightingEnabled() const { return lightingEnabled_; }

    void setFogEnabled(bool enabled) { fogEnabled_ = enabled; }
    bool isFogEnabled() const { return fogEnabled_; }
    void setFogColor(float r, float g, float b) { fogColor_[0] = r; fogColor_[1] = g; fogColor_[2] = b; }
    void setFogStart(float start) { fogStart_ = start; }
    void setFogEnd(float end) { fogEnd_ = end; }
    void setFogDensity(float density) { fogDensity_ = density; }

    void setWindTime(float time) { windTime_ = time; }
    float getWindTime() const { return windTime_; }
    void setWindStrength(float strength) { windStrength_ = strength; }
    float getWindStrength() const { return windStrength_; }

    void setAmbientStrength(float s) { ambientStrength_ = s; }

    void setDirectionalShadowConfig(float halfSize, float startDistance, float splitLambda) {
        dirShadowHalfSize_ = std::max(1.0f, halfSize);
        dirShadowStart_ = std::max(0.0f, startDistance);
        dirShadowLambda_ = std::clamp(splitLambda, 0.0f, 1.0f);
    }

    /**
     * Podział kaskad (Practical Split Scheme): mieszanka podziału logarytmicznego (gęstsze kaskady
     * blisko kamery) i równomiernego, sterowana lambda. Kaskady obejmują głębokości widoku
     * [start, koniec], gdzie start = max(near kamery, dirShadowStart_) - kamera RTS widzi teren
     * dopiero z pewnej odległości, więc nie marnujemy rozdzielczości na pustą przestrzeń.
     * Wynik: dirShadowSplitDepths_[i] = dalsza granica kaskady i (głębokość w przestrzeni widoku).
     */
    void computeCascadeSplits(float nearDepth, float startDepth, float farDepth);

    /**
     * Zakres głębokości (w przestrzeni widoku), w którym kamera widzi geometrię: z granic paczek
     * nieprzezroczystych (siatki, węzły terenu), które mogą być w kadrze. Pozwala rozłożyć kaskady cieni
     * tam, gdzie coś jest - np. kamera wysoko nad terenem nie marnuje kaskad na pusty odcinek między sobą
     * a ziemią, a oddalenie kamery nie "wypycha" terenu poza zasięg cieni.
     */
    void updateVisibleDepthRange(const RenderScene& scene, const RenderView& v);

    void setAmbientColor(float r, float g, float b) {
        ambientColor_[0] = r; ambientColor_[1] = g; ambientColor_[2] = b;
    }

    void setLights(const std::vector<SceneLight>& lights);

    /**
     * Wybiera światła rzucające cień w tej klatce i przydziela im miejsce w tablicy map cieni:
     *  - pierwsze światło kierunkowe (słońce) -> kaskady w warstwach 0..kShadowCascades-1,
     *  - światła lokalne (reflektor, punktowe): do kLocalShadowSlots najbliższych kamerze, których zasięg
     *    sięga w obszar cieni (odległość - zasięg < zasięg cieni) -> ćwiartki atlasu w warstwie kLocalShadowLayer.
     * Pozostałe światła oświetlają scenę normalnie, ale bez cienia (position.w = -1).
     */
    void assignShadowSlots(const Vec3& cameraPos);
    IRenderFeature* getFeature(const std::string& name);
    void removeFeature(const std::string& name);

private:
    void createUniformBuffers();

    std::array<Vec3, 8> buildCameraFrustumCorners(const RenderView& view) const;

    Matrix4 buildDirectionalCascadeMatrix(const std::array<Vec3, 8>& frustumCorners,
                                          const Vec3& lightDirection,
                                          float splitNear,
                                          float splitFar) const {
        std::array<Vec3, 8> cascadeCorners{};
        for (int i = 0; i < 4; ++i) {
            const Vec3 nearCorner = frustumCorners[i];
            const Vec3 farCorner = frustumCorners[i + 4];
            cascadeCorners[i] = lerp(nearCorner, farCorner, splitNear);
            cascadeCorners[i + 4] = lerp(nearCorner, farCorner, splitFar);
        }

        Vec3 center{0.0f, 0.0f, 0.0f};
        for (const Vec3& corner : cascadeCorners) {
            center = add(center, corner);
        }
        center = multiply(center, 1.0f / static_cast<float>(cascadeCorners.size()));

        float radius = 0.0f;
        for (const Vec3& corner : cascadeCorners) {
            const Vec3 delta = subtract(corner, center);
            radius = std::max(radius, std::sqrt(dot(delta, delta)));
        }
        radius = std::max(radius, 8.0f);
        radius = std::ceil(radius * 16.0f) / 16.0f;

        Vec3 up = std::abs(lightDirection.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 lightForward = normalize(multiply(lightDirection, -1.0f));
        const Vec3 lightRight = normalize(cross(lightForward, up));
        const Vec3 lightUp = cross(lightRight, lightForward);
        const float texelSize = (radius * 2.0f) / static_cast<float>(kShadowMapSize);
        const float snappedX = std::round(dot(center, lightRight) / texelSize) * texelSize;
        const float snappedY = std::round(dot(center, lightUp) / texelSize) * texelSize;
        center = add(center,
                     add(multiply(lightRight, snappedX - dot(center, lightRight)),
                         multiply(lightUp, snappedY - dot(center, lightUp))));

        const Vec3 lightPosition = add(center, multiply(lightDirection, radius * 2.0f));
        const Matrix4 lightView = lookAt(lightPosition, center, up);

        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const Vec3& corner : cascadeCorners) {
            const Vec3 lightSpace = transformPoint(lightView, corner);
            minZ = std::min(minZ, lightSpace.z);
            maxZ = std::max(maxZ, lightSpace.z);
        }

        // Stały, symetryczny obszar XY zachowuje stabilizację do siatki tekseli.
        // Ciasne min/max zmieniało skalę projekcji przy każdym obrocie kamery,
        // przez co krawędzie cieni migotały mimo zaokrąglania środka.
        const float zPadding = std::max(80.0f, radius);
        const float nearPlane = std::max(0.5f, -maxZ - zPadding);
        const float farPlane = std::max(nearPlane + 1.0f, -minZ + zPadding);
        return multiply(ortho(-radius, radius, -radius, radius, nearPlane, farPlane), lightView);
    }

    void updateUniformBuffer(const RenderView& view);

    // Domyślne tekstury 1x1 (biała, płaska normalna, ORM, czarna) - podstawiane, gdy materiał nie ma własnej.
    struct SolidTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };
    SolidTexture createSolidTexture(const uint8_t rgba[4], VkSampler sampler);
    void destroySolidTexture(SolidTexture& t);
    // Tworzy cztery domyślne tekstury i rejestruje je w tablicy bindless ("default", "default_normal", ...).
    void createDefaultTextures();

    void createDescriptorResources();

    void recreateSwapChain();

    VkCommandBuffer beginSingleTimeCommands();

    void endSingleTimeCommands(VkCommandBuffer cmd);

    void createShadowResources();

    void transitionShadowMapLayout(VkImageLayout newLayout);

    /**
     * Potok map cieni dla terenu teselowanego (shader terrain_shadow): ta sama teselacja i wysokość z mapy
     * co teren na ekranie, więc cień powstaje z widocznej geometrii. Dwa warianty: słońce (depthClamp)
     * i światła lokalne. Tworzony tylko, gdy GPU obsługuje teselację.
     */
    void createTerrainShadowPipeline();

    void createShadowPipeline();
    void buildShadowPipelinePair(vk::GraphicsPipelineBuilder& builder, VkPipelineLayout layout,
                                 VkPipeline& localPipeline, VkPipeline& sunPipeline, const std::string& name);

    void createShadowSkinnedPipeline();

    void createGBufferResources();

    void createGBufferPipeline();
    vk::GraphicsPipelineBuilder gbufferPipelineBase() const;
    VkPipelineLayout createGBufferPipelineLayout(const std::string& name);

    void createGBufferSkinnedPipeline();

    void createSceneColorResources();

    void registerSceneColorForFrame(uint32_t frameIndex);

    void destroySceneColorResources();

    void registerSceneDepthForFrame(uint32_t frameIndex, uint32_t imageIndex);

    void registerGBufferWorldPosForFrame(uint32_t frameIndex);

    void createDeferredPipeline();

    void createSyncObjects();

    void createPresentSemaphores();

    void destroyPresentSemaphores();

    void createCommandBuffers();

private:
    bool hasActiveFeature(IRenderFeature::Stage stage) const;

    void executeFeatures(VkCommandBuffer cmd, const RenderView& view, IRenderFeature::Stage stage);

    Platform* platform_;
    MaterialManager* materialManager_;
    std::shared_ptr<VulkanContext> vulkanContext_;
    AssetRegistry* assetRegistry_ = nullptr;
    std::unique_ptr<VulkanSwapchain> swapchain_;

    std::vector<std::shared_ptr<IRenderFeature>> features_;
    std::vector<std::unique_ptr<IRenderPass>> passes_;


    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBuffersMemory_;
    std::vector<void*> uniformBuffersMapped_;

    SolidTexture defaultTex_;
    SolidTexture defaultNormalTex_;
    SolidTexture defaultOrmTex_;
    SolidTexture defaultBlackTex_;

    std::unordered_map<std::string, TextureResources> textures_;
    std::unordered_map<std::string, uint32_t> textureNameToIndex_;
    std::vector<uint32_t> textureHandleToIndex_;
    uint32_t nextTextureIndex_ = 0;

    uint32_t defaultDiffuseIndex_ = 0;
    uint32_t defaultNormalIndex_ = 0;
    uint32_t defaultOrmIndex_ = 0;
    uint32_t defaultEmissiveIndex_ = 0;
    uint32_t defaultMaskIndex_ = 0;

    std::array<VkSemaphore, kMaxFrames> imageAvailableSemaphores_;
    // Semafory sygnalizowane dla prezentacji są per OBRAZ swapchaina, nie per klatkę w locie:
    // fence klatki nie gwarantuje, że silnik prezentacji przestał już używać semafora.
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkSemaphore> presentReadySemaphores_;
    std::array<VkSemaphore, kMaxFrames> renderTimelineSemaphores_;
    std::array<uint64_t, kMaxFrames> timelineCounters_{};
    uint64_t globalTimelineCounter_ = 0;
    std::array<VkFence, kMaxFrames> inFlightFences_;
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t currentFrame_ = 0;
    SynchronizationMode synchronizationMode_ = SynchronizationMode::Standard;
    PresentMode presentMode_ = PresentMode::Fifo;
    std::array<std::array<float, 4>, 3> lightDirection_{};
    float lightAmbient_ = 0.25f;
    std::array<std::array<float, 4>, 3> lightColor_{};
    bool lightingEnabled_ = false;
    float ambientStrength_ = 0.3f;
    float ambientColor_[3] = {1.0f, 1.0f, 1.0f};
    bool shadowsEnabled_ = false;
    bool wireframeEnabled_ = false;
    bool tessellationEnabled_ = true;
    float tessellationLevel_ = 4.0f;
    TextureFilter textureFilter_ = TextureFilter::Anisotropic16x;
    float dirShadowHalfSize_ = 100.0f;
    std::array<float, kShadowCascades> dirShadowSplitDepths_ = {50.0f, 150.0f, 400.0f, 1000.0f};
    float shadowFarDepth_ = 1000.0f;                  // koniec zasięgu kaskad w bieżącej klatce
    bool visDepthValid_ = false;
    bool visDepthSmoothed_ = false;                      // zakres głębokości widocznej geometrii (updateVisibleDepthRange)
    float visMinDepth_ = 0.0f, visMaxDepth_ = 0.0f;   // ustabilizowany kierunek świateł kierunkowych dla map cieni
    float dirShadowSplitStart_ = 0.0f;
    float dirShadowStart_ = 0.0f;     // od jakiej głębokości widoku zaczynają się kaskady
    float dirShadowLambda_ = 0.6f;    // 0 = podział równomierny, 1 = logarytmiczny
    bool cleanedUp_ = false;
    bool fogEnabled_ = false;
    float fogColor_[3] = {0.8f, 0.8f, 0.8f};
    float fogStart_ = 10.0f;
    float fogEnd_ = 100.0f;
    float fogDensity_ = 0.01f;

    float windTime_ = 0.0f;
    float windStrength_ = 0.3f;

    LightData storedLights_[kMaxLights] = {};
    std::vector<ShadowCaster> shadowCasters_;   // światła rzucające cień w bieżącej klatce
    uint32_t storedLightCount_ = 0;

    static constexpr uint32_t kShadowMapSize = 2048;
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    std::vector<VkImageView> shadowMapLayerViews_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowSunPipeline_ = VK_NULL_HANDLE;
    // Cienie terenu teselowanego (terrain_shadow).
    VkPipeline terrainShadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline terrainShadowSunPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout terrainShadowPipelineLayout_ = VK_NULL_HANDLE;          // jak shadowPipeline_, z depthClamp (kaskady słońca)
    VkPipeline shadowSkinnedSunPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowSkinnedPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowSkinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkImageLayout shadowMapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // GBuffer
    static constexpr int kGBufferAttachments = 4;

    // Format bufora G-Buffera. Kolor, normalna i materiał mieszczą się w 16-bit float, ale POZYCJA ŚWIATA
    // (bufor 3) potrzebuje 32 bitów: half float ma tylko 11 bitów mantysy, więc przy współrzędnych
    // rzędu 256-512 krok wynosi 0,25 jednostki. Cienie liczone z tak zaokrąglonej pozycji miały
    // regularne paski (shadow acne) niezależnie od biasu.
    VkImage gbufferImages_[kGBufferAttachments][kMaxFrames] = {};
    VkDeviceMemory gbufferMemories_[kGBufferAttachments][kMaxFrames] = {};
    VkImageView gbufferViews_[kGBufferAttachments][kMaxFrames] = {};
    VkSampler gbufferSampler_ = VK_NULL_HANDLE;

    // GBuffer pipeline
    VkPipelineLayout gbufferPipelineLayout_ = VK_NULL_HANDLE;
    // Kolejność rysowania (pamięć wektorów między klatkami - bez alokacji co klatkę).
    struct GBufferDraw {
        const RenderPacket* packet = nullptr;
        int group = 0;                          // 0 zwykłe, 1 animowane, 2 własny materiał (teren)
        VkPipeline pipeline = VK_NULL_HANDLE;   // potok materiału dla grupy 2
        int ring = 0;                           // pierścień odległości od kamery
    };
    std::vector<GBufferDraw> gbufferOrder_;
    std::vector<ShadowDraw> shadowOrder_;
    std::vector<const RenderPacket*> shadowPacketPtrs_;
    std::vector<std::pair<uint32_t, float>> shadowCandidates_;   // assignShadowSlots - bez alokacji co klatkę
    VkPipeline gbufferPipeline_ = VK_NULL_HANDLE;

    // GBuffer skinned pipeline
    VkPipelineLayout gbufferSkinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline gbufferSkinnedPipeline_ = VK_NULL_HANDLE;

    // Deferred lighting pipeline
    VkPipelineLayout deferredPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline deferredPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout deferredDescriptorSetLayout_ = VK_NULL_HANDLE;

    // Ocean scene color (captured scene before ocean renders)
    VkImage sceneColorImages_[kMaxFrames] = {};
    VkDeviceMemory sceneColorMemories_[kMaxFrames] = {};
    VkImageView sceneColorViews_[kMaxFrames] = {};
    VkSampler sceneColorSampler_ = VK_NULL_HANDLE;

};

} // namespace engine3d
