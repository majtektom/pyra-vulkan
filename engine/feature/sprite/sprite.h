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

class AssetRegistry;
class MaterialManager;
class MaterialSystem;
class MeshManager;
class Scene;

/**
 * @brief System zarządzający sprajtami i ich animacjami (Sprite System).
 *
 * Klasa integruje renderowanie dwuwymiarowych obrazów (sprites) w trójwymiarowej przestrzeni świata 
 * (tzw. billboardy - płaskie obiekty, które mogą być ustawione przodem do kamery).
 * Wspiera:
 * - Klasyczne sprajty statyczne.
 * - Animacje oparte na siatkach klatek (Sprite Sheets / Texture Atlases).
 * - Grupowanie w celu wydajnego rysowania (batching).
 *
 * Dziedziczy z IRenderFeature, co pozwala wpiąć go bezpośrednio do potoku renderującego.
 */
class SpriteSystem : public IRenderFeature {
public:
    /**
     * @brief Struktura reprezentująca pojedynczy obiekt graficzny 2D (Sprite).
     */
    struct Sprite {
        Vec3 position = {0.0f, 0.0f, 0.0f};             // Pozycja 3D w świecie.
        float size = 1.0f;                              // Skala/Rozmiar sprajtu.
        std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f}; // Mnożnik koloru (RGBA).
        TextureHandle textureHandle{};                  // Uchwyt do tekstury GPU.
        bool active = false;                            // Czy sprajt jest aktualnie przetwarzany i rysowany.

        // Obsługa animacji ze sprite sheet / sekwencji tekstur:
        bool isSequence = false;                        // Czy animacja opiera się na sekwencji oddzielnych plików tekstur.
        std::vector<TextureHandle> frameTextureHandles; // Klatki animacji jako lista uchwytów tekstur.
        std::vector<std::shared_ptr<MaterialInstance>> frameMaterials; // Instancje materiałów dla każdej klatki.

        // Przesunięcie i skala tekstury (współrzędne UV):
        float uvOffsetU = 0.0f;
        float uvOffsetV = 0.0f;
        float uvScaleU = 1.0f;
        float uvScaleV = 1.0f;

        // Parametry siatki klatek (dla arkusza sprite sheet):
        int framesX = 1;                     // Liczba kolumn klatek w arkuszu.
        int framesY = 1;                     // Liczba wierszy klatek w arkuszu.
        float timePerFrame = 0.1f;           // Czas trwania pojedynczej klatki w sekundach.
        float elapsed = 0.0f;                // Czas, jaki upłynął od wyświetlenia bieżącej klatki.
        int currentFrame = 0;                // Indeks aktualnie wyświetlanej klatki.
        int startFrame = 0;                  // Klatka początkowa pętli animacji.
        int endFrame = -1;                   // Klatka końcowa animacji (-1 oznacza ostatnią możliwą).
        bool playing = true;                 // Flaga określająca czy animacja jest odtwarzana.
        bool loop = true;                    // Czy zapętlać animację po dojściu do końca.

        std::string entityName;              // Nazwa powiązanej encji ECS.
        std::string meshName;                // Nazwa powiązanej siatki geometrycznej w menedżerze.
    };

    SpriteSystem();
    ~SpriteSystem();

    /**
     * @brief Inicjalizuje system sprajtów, rejestrując wskaźniki do głównych podsystemów silnika.
     */
    void initialize(Scene& scene,
                    MeshManager& meshManager,
                    MaterialManager& materialManager,
                    MaterialSystem& materialSystem,
                    AssetRegistry& assetRegistry);
    
    /**
     * @brief Aktualizuje czas animacji wszystkich aktywnych sprajtów (wywoływane w każdej klatce).
     * @param dt Delta time (czas od poprzedniej klatki w sekundach).
     */
    void update(float dt);

    // Zarządzanie zasobami sprajtów w puli:
    int addSprite(const Sprite& sprite);
    Sprite& getSprite(int id);
    int spriteCount() const;
    void removeSprite(int id);
    void clear();

    // Modyfikatory stanu sprajtów:
    void setPosition(int id, const Vec3& pos);
    void setSize(int id, float size);
    void setColor(int id, const std::array<float, 4>& color);
    void setUV(int id, float offsetU, float offsetV, float scaleU, float scaleV);

    // Sterowanie animacją:
    void setAnim(int id, int framesX, int framesY, float timePerFrame,
                 int startFrame = 0, int endFrame = -1, bool loop = true);
    void playAnim(int id);
    void pauseAnim(int id);
    void stopAnim(int id);

    // Nadpisane metody z interfejsu IRenderFeature:
    void init(Renderer*) override {}
    void render(VkCommandBuffer, uint32_t, const RenderView&) override {}
    bool isActive() const override { return initialized_; }
    const std::string& getName() const override { static std::string n = "SpriteSystem"; return n; }

private:
    static constexpr int MAX_SPRITES = 1024; // Maksymalna liczba sprajtów wspierana w puli.

    // Grupa tekstur używana do optymalizacji batchingu.
    struct TextureGroup {
        TextureHandle textureHandle{};
        std::string meshName;
        int capacity = 0;
    };

    void applyFrameUV(int id);
    void advanceAnimation(int id, float dt);
    void createSheetGroup(TextureHandle texHandle,
                          const std::vector<int>& indices, int groupIdx);
    void createSequenceEntity(int spriteId);
    void updateSheetInstances();
    void updateSequenceEntities();

    Sprite sprites_[MAX_SPRITES];
    int spriteCount_ = 0;
    std::vector<TextureGroup> groups_;
    Scene* scene_ = nullptr;
    MeshManager* meshManager_ = nullptr;
    MaterialManager* materialManager_ = nullptr;
    MaterialSystem* materialSystem_ = nullptr;
    AssetRegistry* assetRegistry_ = nullptr;
    bool initialized_ = false;
};

}  // namespace engine3d
