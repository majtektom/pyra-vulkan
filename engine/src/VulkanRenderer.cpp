#define VK_USE_PLATFORM_XLIB_KHR

#include "VulkanRenderer.hpp"

#include "FontAtlasLoader.hpp"
#include "Utf8.hpp"
#include "TextureAssetLoader.hpp"
#include "X11Window.hpp"

#include "engine/FontManager.hpp"
#include "engine/ShaderManager.hpp"
#include "engine/TextureManager.hpp"

#include <X11/Xlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine3d {
namespace {

constexpr int kMaxFramesInFlight = 2;

#ifndef ENGINE3D_HAS_FREETYPE
#define ENGINE3D_HAS_FREETYPE 0
#endif

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

constexpr bool kHasFreeType = ENGINE3D_HAS_FREETYPE != 0;

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    // Sprawdza, czy znalezlismy komplet kolejek potrzebnych do renderowania i prezentacji obrazu.
    [[nodiscard]] bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*
) {
    // W trybie debug wypisujemy ostrzezenia i bledy walidacyjne prosto na stderr.
    std::cerr << "validation: " << callbackData->pMessage << '\n';
    return VK_FALSE;
}

// Tworzy debug messenger przez funkcje pobierana dynamicznie z instancji Vulkan.
VkResult createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* debugMessenger
) {
    const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (function == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return function(instance, createInfo, allocator, debugMessenger);
}

// Niszczy debug messenger tylko wtedy, gdy rozszerzenie jest dostepne.
void destroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* allocator
) {
    const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
    );
    if (function != nullptr) {
        function(instance, debugMessenger, allocator);
    }
}

}  // namespace

class VulkanRendererImpl {
public:
    // Konstruktor przejmuje konfiguracje i buduje menedzery zasobow uzywane pozniej przez renderer.
    explicit VulkanRendererImpl(AppConfig config)
        : config_(std::move(config)),
          fontManager_(std::make_unique<FontManager>(config_.fonts)),
          shaderManager_(std::make_unique<ShaderManager>(config_.shaderManifest)),
          textureManager_(std::make_unique<TextureManager>(config_.textures)) {
        if (!config_.sceneObjects.empty() && config_.meshes.empty()) {
            throw std::runtime_error("engine3d requires at least one mesh asset");
        }
        if (config_.sceneObjects.empty() && config_.textLabels.empty()) {
            throw std::runtime_error("engine3d requires at least one scene object or text label");
        }
        if (!config_.textLabels.empty() && config_.fonts.empty()) {
            throw std::runtime_error("text rendering requires at least one configured font asset");
        }
        if (!config_.textLabels.empty() && !kHasFreeType) {
            throw std::runtime_error("text rendering requires freetype-devel headers during build");
        }
    }

    // Startuje kompletna sekwencje zycia renderera: okno, Vulkan, petla klatek i sprzatanie.
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    struct MeshGpuResources {
        std::string name;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct TextureGpuResources {
        std::string name;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct TextVertex {
        std::array<float, 2> position;
        std::array<float, 2> texCoord;
        std::array<float, 4> color;

        [[nodiscard]] static VkVertexInputBindingDescription bindingDescription() {
            VkVertexInputBindingDescription description{};
            description.binding = 0;
            description.stride = sizeof(TextVertex);
            description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return description;
        }

        [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 3> descriptions{};

            descriptions[0].binding = 0;
            descriptions[0].location = 0;
            descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            descriptions[0].offset = offsetof(TextVertex, position);

            descriptions[1].binding = 0;
            descriptions[1].location = 1;
            descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
            descriptions[1].offset = offsetof(TextVertex, texCoord);

            descriptions[2].binding = 0;
            descriptions[2].location = 2;
            descriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            descriptions[2].offset = offsetof(TextVertex, color);

            return descriptions;
        }
    };

    struct FontGpuResources {
        std::string name;
        float lineHeight = 0.0f;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::unordered_map<std::uint32_t, FontAtlasGlyph> glyphs;
    };

    struct TextGpuResources {
        std::size_t fontIndex = 0;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
    };

    AppConfig config_;
    std::unique_ptr<FontManager> fontManager_;
    std::unique_ptr<ShaderManager> shaderManager_;
    std::unique_ptr<TextureManager> textureManager_;

    std::unique_ptr<X11Window> window_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages_;
    VkFormat swapChainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent_{};
    std::vector<VkImageView> swapChainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout textPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline textGraphicsPipeline_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<MeshGpuResources> meshes_;
    std::unordered_map<std::string, std::size_t> meshLookup_;
    std::vector<TextureGpuResources> textures_;
    std::unordered_map<std::string, std::size_t> textureLookup_;
    std::vector<FontGpuResources> fonts_;
    std::unordered_map<std::string, std::size_t> fontLookup_;
    std::vector<TextGpuResources> texts_;
    std::vector<TextLabel> lastTextLabels_;
    std::array<VkBuffer, kMaxFramesInFlight> globalUniformBuffers_{};
    std::array<VkDeviceMemory, kMaxFramesInFlight> globalUniformBuffersMemory_{};
    std::array<void*, kMaxFramesInFlight> globalUniformBuffersMapped_{};
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorPool textDescriptorPool_ = VK_NULL_HANDLE;
    std::vector<std::array<VkDescriptorSet, kMaxFramesInFlight>> descriptorSetsByTexture_;
    std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers_{};
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailableSemaphores_{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinishedSemaphores_{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFences_{};
    std::vector<VkFence> imagesInFlight_;

    uint32_t currentFrame_ = 0;
    bool enableValidationLayers_ = false;
    std::chrono::steady_clock::time_point startTime_{};

    // Tworzy natywne okno X11, do ktorego Vulkan bedzie prezentowal gotowe klatki.
    void initWindow() {
        // Sam renderer nie zna szczegolow tworzenia okna - deleguje to do osobnej klasy backendu okienkowego.
        window_ = std::make_unique<X11Window>(config_.windowTitle, config_.width, config_.height);
    }

    // Buduje wszystkie glowne obiekty Vulkan w poprawnej kolejnosci zaleznosci.
    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createTextDescriptorSetLayout();
        createGraphicsPipeline();
        createTextGraphicsPipeline();
        createCommandPool();
        createDepthResources();
        createFramebuffers();
        createMeshBuffers();
        createGlobalUniformBuffers();
        createTextureResources();
        createFontResources();
        createDescriptorPool();
        createDescriptorSets();
        createTextDescriptorPool();
        createTextDescriptorSets();
        createTextBuffers();
        lastTextLabels_ = config_.textLabels;
        createCommandBuffers();
        createSyncObjects();
        // Zapisujemy moment startu, aby animacje mogly bazowac na czasie od uruchomienia programu.
        startTime_ = std::chrono::steady_clock::now();
    }

    // Sprawdza, czy scena potrzebuje nakladki tekstowej ponad renderem 3D.
    [[nodiscard]] bool hasTextOverlay() const {
        return !config_.textLabels.empty();
    }

    // Porownuje dwie listy etykiet, aby wykryc realna zmiane tresci lub polozenia tekstu.
    [[nodiscard]] static bool areTextLabelsEqual(const std::vector<TextLabel>& lhs, const std::vector<TextLabel>& rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (std::size_t index = 0; index < lhs.size(); ++index) {
            const TextLabel& left = lhs[index];
            const TextLabel& right = rhs[index];
            if (left.fontName != right.fontName ||
                left.text != right.text ||
                left.x != right.x ||
                left.y != right.y ||
                left.scale != right.scale ||
                left.color != right.color ||
                left.horizontalAlign != right.horizontalAlign) {
                return false;
            }
        }

        return true;
    }

    // Sklada finalny zestaw glifow dla fontu: znaki jawnie podane w assetcie oraz te uzyte w etykietach.
    [[nodiscard]] std::vector<std::uint32_t> collectFontGlyphs(const FontAsset& fontAsset) const {
        std::vector<std::uint32_t> glyphs;

        for (std::uint32_t codepoint : utf8::decode(fontAsset.glyphsUtf8)) {
            utf8::appendUnique(glyphs, codepoint);
        }

        for (const auto& label : config_.textLabels) {
            if (label.fontName != fontAsset.name) {
                continue;
            }

            for (std::uint32_t codepoint : utf8::decode(label.text)) {
                if (codepoint == static_cast<std::uint32_t>('\n') || codepoint == static_cast<std::uint32_t>('\r')) {
                    continue;
                }
                utf8::appendUnique(glyphs, codepoint);
            }
        }

        return glyphs;
    }

    // Petla glowna odswieza zdarzenia oraz renderuje kolejne klatki az do zamkniecia okna.
    void mainLoop() {
        while (!window_->shouldClose()) {
            processEvents();
            if (window_->shouldClose()) {
                break;
            }
            drawFrame();
        }

        // Przed wyjsciem czekamy, az GPU skonczy prace, aby niczego nie zniszczyc za wczesnie.
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }
    }

    // Przetwarza zdarzenia X11: zamkniecie okna, zmiane rozmiaru i wyjscie klawiszem Escape.
    void processEvents() {
        window_->processEvents();
    }

    // Niszczy wszystkie zasoby zalezne od rozmiaru okna i swapchaina.
    void cleanupSwapChain() {
        // Framebuffery sa bezposrednio zwiazane z obrazami swapchaina, wiec zawsze usuwamy je jako pierwsze.
        for (auto framebuffer : swapChainFramebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        swapChainFramebuffers_.clear();

        if (depthImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, depthImageView_, nullptr);
            depthImageView_ = VK_NULL_HANDLE;
        }
        if (depthImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, depthImage_, nullptr);
            depthImage_ = VK_NULL_HANDLE;
        }
        if (depthImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, depthImageMemory_, nullptr);
            depthImageMemory_ = VK_NULL_HANDLE;
        }

        if (graphicsPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
            graphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (textGraphicsPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, textGraphicsPipeline_, nullptr);
            textGraphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }

        for (auto imageView : swapChainImageViews_) {
            vkDestroyImageView(device_, imageView, nullptr);
        }
        swapChainImageViews_.clear();

        if (swapChain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapChain_, nullptr);
            swapChain_ = VK_NULL_HANDLE;
        }

        imagesInFlight_.clear();
    }

    // Sprzata caly renderer w odwrotnej kolejnosci do tworzenia zasobow.
    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            cleanupSwapChain();

            if (descriptorPool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
            }
            if (textDescriptorPool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, textDescriptorPool_, nullptr);
            }

            for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
                if (globalUniformBuffersMapped_[index] != nullptr) {
                    vkUnmapMemory(device_, globalUniformBuffersMemory_[index]);
                }
                if (globalUniformBuffers_[index] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device_, globalUniformBuffers_[index], nullptr);
                }
                if (globalUniformBuffersMemory_[index] != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, globalUniformBuffersMemory_[index], nullptr);
                }
            }

            // Bufory siatek sa zasobami GPU, wiec kazdy obiekt trzeba zniszczyc i zwolnic osobno.
            for (auto& mesh : meshes_) {
                if (mesh.indexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device_, mesh.indexBuffer, nullptr);
                }
                if (mesh.indexBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, mesh.indexBufferMemory, nullptr);
                }
                if (mesh.vertexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device_, mesh.vertexBuffer, nullptr);
                }
                if (mesh.vertexBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, mesh.vertexBufferMemory, nullptr);
                }
            }

            // Tekstura w Vulkanie sklada sie z kilku obiektow: samplera, view, image oraz pamieci.
            for (auto& texture : textures_) {
                if (texture.sampler != VK_NULL_HANDLE) {
                    vkDestroySampler(device_, texture.sampler, nullptr);
                }
                if (texture.imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device_, texture.imageView, nullptr);
                }
                if (texture.image != VK_NULL_HANDLE) {
                    vkDestroyImage(device_, texture.image, nullptr);
                }
                if (texture.imageMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, texture.imageMemory, nullptr);
                }
            }

            destroyTextBuffers();
            destroyFontResources();

            if (descriptorSetLayout_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
            }
            if (textDescriptorSetLayout_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, textDescriptorSetLayout_, nullptr);
            }
            if (pipelineLayout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            }
            if (textPipelineLayout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_, textPipelineLayout_, nullptr);
            }

            for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
                if (renderFinishedSemaphores_[index] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, renderFinishedSemaphores_[index], nullptr);
                }
                if (imageAvailableSemaphores_[index] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, imageAvailableSemaphores_[index], nullptr);
                }
                if (inFlightFences_[index] != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, inFlightFences_[index], nullptr);
                }
            }

            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            }

            vkDestroyDevice(device_, nullptr);
        }

        if (debugMessenger_ != VK_NULL_HANDLE) {
            destroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        }
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
        }

        window_.reset();
    }

    // Zwalnia bufory geometrii tekstu, aby mozna je bylo odtworzyc po zmianie tresci etykiet.
    void destroyTextBuffers() {
        for (auto& text : texts_) {
            if (text.vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, text.vertexBuffer, nullptr);
                text.vertexBuffer = VK_NULL_HANDLE;
            }
            if (text.vertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, text.vertexBufferMemory, nullptr);
                text.vertexBufferMemory = VK_NULL_HANDLE;
            }
        }
        texts_.clear();
    }

    // Zwalnia zasoby GPU fontow, gdy atlas musi zostac przebudowany pod nowy zestaw znakow.
    void destroyFontResources() {
        for (auto& font : fonts_) {
            if (font.sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device_, font.sampler, nullptr);
                font.sampler = VK_NULL_HANDLE;
            }
            if (font.imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, font.imageView, nullptr);
                font.imageView = VK_NULL_HANDLE;
            }
            if (font.image != VK_NULL_HANDLE) {
                vkDestroyImage(device_, font.image, nullptr);
                font.image = VK_NULL_HANDLE;
            }
            if (font.imageMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, font.imageMemory, nullptr);
                font.imageMemory = VK_NULL_HANDLE;
            }
        }

        fonts_.clear();
        fontLookup_.clear();
    }

    // Wczytuje caly plik binarny do pamieci, np. skompilowany shader SPIR-V.
    [[nodiscard]] static std::vector<char> readFile(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + path);
        }

        const auto fileSize = static_cast<std::size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        return buffer;
    }

    // Wypelnia wspolne pola struktury opisujacej debug messenger.
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    // Sprawdza, czy wymagane warstwy walidacyjne sa dostepne na lokalnej maszynie.
    [[nodiscard]] bool checkValidationLayerSupport() const {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : kValidationLayers) {
            bool found = false;
            for (const auto& layerProperties : availableLayers) {
                if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }

        return true;
    }

    // Zwraca liste rozszerzen instancji potrzebnych do powierzchni X11 i ewentualnego debugowania.
    [[nodiscard]] std::vector<const char*> getRequiredExtensions() const {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
        };

        if (enableValidationLayers_) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    // Tworzy instancje Vulkan, czyli glowny obiekt startowy calego API.
    void createInstance() {
        enableValidationLayers_ = kEnableValidationLayers && checkValidationLayerSupport();
        if (kEnableValidationLayers && !enableValidationLayers_) {
            std::cerr << "validation layers unavailable, continuing without them\n";
        }

        // Ten blok przekazuje Vulkanowi podstawowe informacje o aplikacji i silniku.
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = config_.windowTitle.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "engine3d";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        const auto requiredExtensions = getRequiredExtensions();

        // Tu skladamy finalna strukture z rozszerzeniami i warstwami potrzebnymi przy starcie instancji.
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (enableValidationLayers_) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
            createInfo.ppEnabledLayerNames = kValidationLayers.data();
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan instance");
        }
    }

    // Tworzy debug messenger tylko w trybie, w ktorym walidacja jest aktywna.
    void setupDebugMessenger() {
        if (!enableValidationLayers_) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);
        if (createDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger");
        }
    }

    // Tworzy powierzchnie prezentacji laczaca Vulkan z oknem X11.
    void createSurface() {
        VkXlibSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.dpy = static_cast<Display*>(window_->nativeDisplay());
        createInfo.window = static_cast<Window>(window_->nativeWindow());

        if (vkCreateXlibSurfaceKHR(instance_, &createInfo, nullptr, &surface_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface");
        }
    }

    // Wybiera pierwsza karte graficzna, ktora spelnia wymagania renderera.
    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice_ = device;
                break;
            }
        }

        if (physicalDevice_ == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU");
        }
    }

    // Tworzy logiczne urzadzenie oraz pobiera uchwyty do kolejek grafiki i prezentacji.
    void createLogicalDevice() {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        const std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value(),
        };

        constexpr float queuePriority = 1.0f;
        // Dla kazdej unikalnej rodziny kolejek przygotowujemy osobny opis tworzenia.
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

        if (enableValidationLayers_) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
            createInfo.ppEnabledLayerNames = kValidationLayers.data();
        }

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device");
        }

        vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
    }

    // Tworzy swapchain, czyli zestaw obrazow prezentowanych potem w oknie.
    void createSwapChain() {
        const SwapChainSupportDetails supportDetails = querySwapChainSupport(physicalDevice_);
        const VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(supportDetails.formats);
        const VkPresentModeKHR presentMode = chooseSwapPresentMode(supportDetails.presentModes);
        const VkExtent2D extent = chooseSwapExtent(supportDetails.capabilities);

        uint32_t imageCount = supportDetails.capabilities.minImageCount + 1;
        if (supportDetails.capabilities.maxImageCount > 0 && imageCount > supportDetails.capabilities.maxImageCount) {
            imageCount = supportDetails.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
        const uint32_t queueFamilyIndices[] = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value(),
        };

        // Gdy grafika i prezentacja sa w innych rodzinach, swapchain musi wspierac wspoldzielenie obrazow.
        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = supportDetails.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
        swapChainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());

        swapChainImageFormat_ = surfaceFormat.format;
        swapChainExtent_ = extent;
    }

    // Tworzy image view dla kazdego obrazu swapchaina, aby pipeline mogl z nich korzystac jako z attachmentow.
    void createImageViews() {
        swapChainImageViews_.resize(swapChainImages_.size());
        for (std::size_t index = 0; index < swapChainImages_.size(); ++index) {
            swapChainImageViews_[index] = createImageView(
                swapChainImages_[index],
                swapChainImageFormat_,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
        }
    }

    // Konfiguruje render pass opisujacy, jak traktujemy bufor koloru i glebokosci w trakcie rysowania.
    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // W tym prostym rendererze mamy jeden subpass grafiki korzystajacy z koloru i depth.
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments = {
            colorAttachment,
            depthAttachment,
        };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass");
        }
    }

    // Definiuje uklad descriptor setu: globalny UBO oraz sampler tekstury.
    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            uboLayoutBinding,
            samplerLayoutBinding,
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }
    }

    // Definiuje descriptor set dla atlasu fontu probkowanego przez shader tekstu.
    void createTextDescriptorSetLayout() {
        if (!hasTextOverlay()) {
            return;
        }

        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerLayoutBinding;

        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &textDescriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create text descriptor set layout");
        }
    }

    // Zamienia surowe bajty SPIR-V na obiekt shader module rozumiany przez Vulkan.
    [[nodiscard]] VkShaderModule createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module");
        }
        return shaderModule;
    }

    // Tworzy pelny pipeline graficzny odpowiedzialny za renderowanie obiektow sceny.
    void createGraphicsPipeline() {
        // Najpierw pobieramy sciezki aktywnego programu shaderowego z manifestu.
        const ShaderProgram& shaderProgram = shaderManager_->require(config_.activeShaderProgram);
        const auto vertShaderCode = readFile(shaderProgram.vertexPath);
        const auto fragShaderCode = readFile(shaderProgram.fragmentPath);

        const VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        const VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = shaderProgram.vertexEntryPoint.c_str();

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = shaderProgram.fragmentEntryPoint.c_str();

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
            vertShaderStageInfo,
            fragShaderStageInfo,
        };

        const auto bindingDescription = Vertex::bindingDescription();
        const auto attributeDescriptions = Vertex::attributeDescriptions();

        // Ten blok opisuje, jak uklad danych Vertex ma byc interpretowany przez shader wierzcholkow.
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChainExtent_.width);
        viewport.height = static_cast<float>(swapChainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent_;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                              VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT |
                                              VK_COLOR_COMPONENT_A_BIT;

        // Na razie nie mieszamy kolorow z tlem, ale zostawiamy pelny zapis RGBA.
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Matrix4);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        // Finalny opis pipeline'u sklada wszystkie poprzednie ustawienia w jeden obiekt GPU.
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline");
        }

        // Po utworzeniu pipeline'u osobne moduly shaderow nie sa juz potrzebne.
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    }

    // Tworzy prosty pipeline do rysowania tekstu 2D ponad gotowa scena.
    void createTextGraphicsPipeline() {
        if (!hasTextOverlay()) {
            return;
        }

        const auto vertShaderCode = readFile(std::string(ENGINE_TEXT_SHADER_DIR) + "/text.vert.spv");
        const auto fragShaderCode = readFile(std::string(ENGINE_TEXT_SHADER_DIR) + "/text.frag.spv");

        const VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        const VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
            vertShaderStageInfo,
            fragShaderStageInfo,
        };

        const auto bindingDescription = TextVertex::bindingDescription();
        const auto attributeDescriptions = TextVertex::attributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChainExtent_.width);
        viewport.height = static_cast<float>(swapChainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent_;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                              VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT |
                                              VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(std::array<float, 2>);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &textDescriptorSetLayout_;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &textPipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create text pipeline layout");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = textPipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &textGraphicsPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create text graphics pipeline");
        }

        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    }

    // Tworzy framebuffer dla kazdego obrazu swapchaina, dolaczajac tez wspolny bufor glebokosci.
    void createFramebuffers() {
        swapChainFramebuffers_.resize(swapChainImageViews_.size());

        for (std::size_t index = 0; index < swapChainImageViews_.size(); ++index) {
            const std::array<VkImageView, 2> attachments = {
                swapChainImageViews_[index],
                depthImageView_,
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent_.width;
            framebufferInfo.height = swapChainExtent_.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &swapChainFramebuffers_[index]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer");
            }
        }
    }

    // Tworzy command pool, z ktorego pozniej alokujemy wszystkie command buffery.
    void createCommandPool() {
        const QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice_);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool");
        }
    }

    // Rozpoczyna krotkotrwaly command buffer do prostych operacji transferu i barier.
    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool_;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate single-time command buffer");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin single-time command buffer");
        }

        return commandBuffer;
    }

    // Konczy, wysyla i zwalnia pomocniczy command buffer jednorazowego uzycia.
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to end single-time command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit single-time command buffer");
        }
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    }

    // Wyszukuje typ pamieci GPU/CPU zgodny z wymaganiami bufora lub obrazu.
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            const bool typeMatches = (typeFilter & (1u << index)) != 0;
            const bool propertiesMatch =
                (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
            if (typeMatches && propertiesMatch) {
                return index;
            }
        }

        throw std::runtime_error("failed to find suitable memory type");
    }

    // Tworzy bufor Vulkan oraz przydziela dla niego pamiec o odpowiednich wlasciwosciach.
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    ) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device_, buffer, &memoryRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory");
        }

        vkBindBufferMemory(device_, buffer, bufferMemory, 0);
    }

    // Kopiuje dane z jednego bufora do drugiego przy pomocy jednorazowego command buffera.
    void copyBuffer(VkBuffer sourceBuffer, VkBuffer destinationBuffer, VkDeviceSize size) {
        const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, sourceBuffer, destinationBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    // Tworzy szybki bufor device-local i zasila go danymi przez staging buffer widoczny dla CPU.
    void createDeviceLocalBufferWithStaging(
        const void* sourceData,
        VkDeviceSize bufferSize,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    ) {
        // Najpierw tworzymy bufor stagingowy, do ktorego CPU moze bezposrednio zapisac dane.
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        // Potem tworzymy wlasciwy bufor na GPU i kopiujemy do niego dane ze stagingu.
        createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory
        );

        void* data = nullptr;
        vkMapMemory(device_, stagingBufferMemory, 0, bufferSize, 0, &data);
        std::memcpy(data, sourceData, static_cast<std::size_t>(bufferSize));
        vkUnmapMemory(device_, stagingBufferMemory);

        createBuffer(
            bufferSize,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            buffer,
            bufferMemory
        );

        copyBuffer(stagingBuffer, buffer, bufferSize);

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
    }

    // Zamienia wszystkie siatki z konfiguracji na bufory GPU gotowe do rysowania.
    void createMeshBuffers() {
        meshes_.clear();
        meshLookup_.clear();

        // Kazda siatka dostaje osobny vertex buffer i index buffer.
        for (const auto& meshAsset : config_.meshes) {
            if (meshAsset.mesh.vertices.empty() || meshAsset.mesh.indices.empty()) {
                throw std::runtime_error("mesh asset must contain vertices and indices: " + meshAsset.name);
            }

            MeshGpuResources gpuMesh{};
            gpuMesh.name = meshAsset.name;
            gpuMesh.indexCount = static_cast<uint32_t>(meshAsset.mesh.indices.size());

            const VkDeviceSize vertexBufferSize = sizeof(Vertex) * meshAsset.mesh.vertices.size();
            createDeviceLocalBufferWithStaging(
                meshAsset.mesh.vertices.data(),
                vertexBufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                gpuMesh.vertexBuffer,
                gpuMesh.vertexBufferMemory
            );

            const VkDeviceSize indexBufferSize = sizeof(uint16_t) * meshAsset.mesh.indices.size();
            createDeviceLocalBufferWithStaging(
                meshAsset.mesh.indices.data(),
                indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                gpuMesh.indexBuffer,
                gpuMesh.indexBufferMemory
            );

            meshLookup_.emplace(gpuMesh.name, meshes_.size());
            meshes_.push_back(gpuMesh);
        }
    }

    // Tworzy uniform buffer dla kazdej klatki w locie, aby unikac konfliktow CPU-GPU.
    void createGlobalUniformBuffers() {
        const VkDeviceSize bufferSize = sizeof(GlobalUniformBufferObject);

        for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                globalUniformBuffers_[index],
                globalUniformBuffersMemory_[index]
            );

            if (vkMapMemory(device_, globalUniformBuffersMemory_[index], 0, bufferSize, 0, &globalUniformBuffersMapped_[index]) != VK_SUCCESS) {
                throw std::runtime_error("failed to map uniform buffer memory");
            }
        }
    }

    // Tworzy pule, z ktorej bedziemy alokowac descriptor sety dla wszystkich tekstur i klatek.
    void createDescriptorPool() {
        const uint32_t descriptorSetCount = static_cast<uint32_t>(textures_.size()) * kMaxFramesInFlight;
        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSetCount},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorSetCount},
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = descriptorSetCount;

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    // Przygotowuje descriptor set dla kazdej tekstury oraz dla kazdej klatki w locie.
    void createDescriptorSets() {
        descriptorSetsByTexture_.assign(textures_.size(), {});
        std::vector<VkDescriptorSetLayout> layouts(static_cast<std::size_t>(textures_.size()) * kMaxFramesInFlight, descriptorSetLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        std::vector<VkDescriptorSet> allocatedDescriptorSets(layouts.size());
        if (vkAllocateDescriptorSets(device_, &allocInfo, allocatedDescriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets");
        }

        // Kazdy zestaw laczy ten sam UBO z inna tekstura, co pozwala zmieniac sampler per obiekt.
        for (std::size_t textureIndex = 0; textureIndex < textures_.size(); ++textureIndex) {
            const TextureGpuResources& texture = textures_[textureIndex];
            for (std::size_t frameIndex = 0; frameIndex < kMaxFramesInFlight; ++frameIndex) {
                descriptorSetsByTexture_[textureIndex][frameIndex] =
                    allocatedDescriptorSets[textureIndex * kMaxFramesInFlight + frameIndex];

            // binding 0 wskazuje globalne dane kamery dla konkretnej klatki.
            VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = globalUniformBuffers_[frameIndex];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(GlobalUniformBufferObject);

            // binding 1 wskazuje obraz i sampler aktywnej tekstury.
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture.imageView;
            imageInfo.sampler = texture.sampler;

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet = descriptorSetsByTexture_[textureIndex][frameIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet = descriptorSetsByTexture_[textureIndex][frameIndex];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
        }
    }

    // Tworzy osobna pule descriptor setow dla atlasow fontow uzywanych przez tekst 2D.
    void createTextDescriptorPool() {
        if (!hasTextOverlay()) {
            return;
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<uint32_t>(fonts_.size());

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(fonts_.size());

        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &textDescriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create text descriptor pool");
        }
    }

    // Kazdy font dostaje descriptor set wskazujacy jego atlas i sampler.
    void createTextDescriptorSets() {
        if (!hasTextOverlay()) {
            return;
        }

        std::vector<VkDescriptorSetLayout> layouts(fonts_.size(), textDescriptorSetLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = textDescriptorPool_;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        std::vector<VkDescriptorSet> descriptorSets(fonts_.size());
        if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate text descriptor sets");
        }

        for (std::size_t index = 0; index < fonts_.size(); ++index) {
            fonts_[index].descriptorSet = descriptorSets[index];

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = fonts_[index].imageView;
            imageInfo.sampler = fonts_[index].sampler;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = fonts_[index].descriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
        }
    }

    // Alokuje glowne command buffery uzywane do nagrywania polecen dla kolejnych klatek.
    void createCommandBuffers() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

        if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers");
        }
    }

    // Tworzy semafory i ploty synchronizujace CPU, renderowanie i prezentacje obrazu.
    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::size_t index = 0; index < kMaxFramesInFlight; ++index) {
            if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[index]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[index]) != VK_SUCCESS ||
                vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[index]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create synchronization objects");
            }
        }

        imagesInFlight_.assign(swapChainImages_.size(), VK_NULL_HANDLE);
    }

    // Tworzy obraz Vulkan wraz z pamiecia, np. dla tekstury lub bufora glebokosci.
    void createImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkImage& image,
        VkDeviceMemory& imageMemory
    ) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device_, image, &memoryRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memoryRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory");
        }

        vkBindImageMemory(device_, image, imageMemory, 0);
    }

    // Wstawia bariere zmieniajaca uklad obrazu pomiedzy etapami transferu i probkowania.
    void transitionImageLayout(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask,
        uint32_t mipLevels
    ) {
        const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::runtime_error("unsupported image layout transition");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        endSingleTimeCommands(commandBuffer);
    }

    // Kopiuje dane ze staging buffera do obrazu tekstury.
    void copyBufferToImage(VkBuffer buffer, VkImage image, const std::vector<VkBufferImageCopy>& regions) {
        const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(regions.size()),
            regions.data()
        );

        endSingleTimeCommands(commandBuffer);
    }

    // Tworzy widok obrazu, czyli sposob patrzenia na obraz jako na teksture albo attachment.
    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels = 1) const {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view");
        }
        return imageView;
    }

    // Szuka pierwszego formatu wspieranego przez GPU dla podanego zastosowania.
    [[nodiscard]] VkFormat findSupportedFormat(
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features
    ) const {
        for (VkFormat format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);

            if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format");
    }

    // Wybiera format odpowiedni dla bufora glebokosci.
    [[nodiscard]] VkFormat findDepthFormat() const {
        return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    // Upewnia sie, ze GPU potrafi probkowac teksture w danym formacie.
    void requireTextureFormatSupport(VkFormat format) const {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
            throw std::runtime_error("selected GPU does not support sampling this texture format");
        }
    }

    // Tworzy obraz i widok obrazu dla testu glebokosci.
    void createDepthResources() {
        const VkFormat depthFormat = findDepthFormat();
        createImage(
            swapChainExtent_.width,
            swapChainExtent_.height,
            1,
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthImage_,
            depthImageMemory_
        );
        depthImageView_ = createImageView(depthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    // Uploaduje zdekodowana teksture do GPU i tworzy do niej sampler oraz image view.
    void createTextureFromData(const std::string& name, const TextureUploadData& textureData) {
        TextureGpuResources texture{};
        texture.name = name;

        requireTextureFormatSupport(textureData.format);

        // Najpierw wysylamy piksele do bufora stagingowego widocznego dla CPU.
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(textureData.pixelBytes.size());
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        createBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory
        );

        void* data = nullptr;
        vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
        std::memcpy(data, textureData.pixelBytes.data(), textureData.pixelBytes.size());
        vkUnmapMemory(device_, stagingBufferMemory);

        // Potem tworzymy docelowy obraz GPU i kopiujemy do niego dane.
        createImage(
            textureData.width,
            textureData.height,
            textureData.mipLevels,
            textureData.format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            texture.image,
            texture.imageMemory
        );

        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, textureData.mipLevels);
        copyBufferToImage(stagingBuffer, texture.image, textureData.copyRegions);
        transitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, textureData.mipLevels);

        texture.imageView = createImageView(texture.image, textureData.format, VK_IMAGE_ASPECT_COLOR_BIT, textureData.mipLevels);

        // Sampler opisuje, jak odczytywac teksture w shaderze.
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.maxLod = static_cast<float>(textureData.mipLevels);

        if (vkCreateSampler(device_, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture sampler");
        }

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);

        textureLookup_.emplace(texture.name, textures_.size());
        textures_.push_back(texture);
    }

    // Tworzy awaryjna biala teksture uzywana, gdy obiekt nie wskazuje zadnego assetu.
    void createDefaultWhiteTexture() {
        createTextureFromData("__default_white", TextureAssetLoader::fromRgbaPixels(1, 1, {255, 255, 255, 255}));
    }

    // Wczytuje i uploaduje wszystkie tekstury opisane w konfiguracji sceny.
    void createTextureResources() {
        textures_.clear();
        textureLookup_.clear();

        createDefaultWhiteTexture();

        for (const auto& textureAsset : config_.textures) {
            const TextureAsset& resolvedTexture = textureManager_->require(textureAsset.name);
            const TextureUploadData textureData = TextureAssetLoader::load(resolvedTexture.path);
            createTextureFromData(resolvedTexture.name, textureData);
        }
    }

    // Uploaduje atlas fontu do GPU i zapamietuje metryki potrzebne do budowania geometrii znakow.
    void createFontFromAtlas(const FontAtlasData& atlasData) {
        FontGpuResources font{};
        font.name = atlasData.name;
        font.lineHeight = atlasData.lineHeight;
        font.glyphs = atlasData.glyphs;

        requireTextureFormatSupport(atlasData.format);

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(atlasData.pixelBytes.size());
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
        createBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory
        );

        void* data = nullptr;
        vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
        std::memcpy(data, atlasData.pixelBytes.data(), atlasData.pixelBytes.size());
        vkUnmapMemory(device_, stagingBufferMemory);

        createImage(
            atlasData.atlasWidth,
            atlasData.atlasHeight,
            1,
            atlasData.format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            font.image,
            font.imageMemory
        );

        transitionImageLayout(font.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        copyBufferToImage(stagingBuffer, font.image, atlasData.copyRegions);
        transitionImageLayout(font.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        font.imageView = createImageView(font.image, atlasData.format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.maxLod = 1.0f;

        if (vkCreateSampler(device_, &samplerInfo, nullptr, &font.sampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create font sampler");
        }

        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);

        fontLookup_.emplace(font.name, fonts_.size());
        fonts_.push_back(std::move(font));
    }

    // Wczytuje wszystkie TTF potrzebne do etykiet tekstowych i buduje ich atlasy glifow.
    void createFontResources() {
        fonts_.clear();
        fontLookup_.clear();

        if (!hasTextOverlay()) {
            return;
        }

        for (const auto& fontAsset : config_.fonts) {
            const FontAsset& resolvedFont = fontManager_->require(fontAsset.name);
            const std::vector<std::uint32_t> glyphs = collectFontGlyphs(resolvedFont);
            const FontAtlasData atlasData = FontAtlasLoader::load(resolvedFont, glyphs);
            createFontFromAtlas(atlasData);
        }
    }

    // Zwraca zasoby GPU fontu wskazanego przez etykiete tekstowa.
    [[nodiscard]] const FontGpuResources& requireFontByName(const std::string& fontName) const {
        const auto iterator = fontLookup_.find(fontName);
        if (iterator == fontLookup_.end()) {
            throw std::runtime_error("text label references unknown font: " + fontName);
        }
        return fonts_[iterator->second];
    }

    // Buduje geometre trojkatow dla napisu na podstawie atlasu glifow i metryk fontu.
    [[nodiscard]] std::vector<TextVertex> buildTextVertices(const TextLabel& label, const FontGpuResources& font) const {
        std::vector<TextVertex> vertices;
        const std::vector<std::uint32_t> codepoints = utf8::decode(label.text);
        auto resolveGlyph = [&](std::uint32_t codepoint) -> const FontAtlasGlyph* {
            auto glyphIterator = font.glyphs.find(codepoint);
            if (glyphIterator != font.glyphs.end()) {
                return &glyphIterator->second;
            }

            glyphIterator = font.glyphs.find(static_cast<std::uint32_t>('?'));
            if (glyphIterator != font.glyphs.end()) {
                return &glyphIterator->second;
            }

            return nullptr;
        };

        std::vector<float> lineWidths(1, 0.0f);
        for (std::uint32_t codepoint : codepoints) {
            if (codepoint == static_cast<std::uint32_t>('\r')) {
                continue;
            }
            if (codepoint == static_cast<std::uint32_t>('\n')) {
                lineWidths.push_back(0.0f);
                continue;
            }

            const FontAtlasGlyph* glyph = resolveGlyph(codepoint);
            if (glyph != nullptr) {
                lineWidths.back() += glyph->advanceX * label.scale;
            }
        }

        std::size_t currentLine = 0;
        auto lineStartX = [&](std::size_t lineIndex) {
            const float lineWidth = lineWidths[lineIndex];
            switch (label.horizontalAlign) {
                case TextHorizontalAlign::Center:
                    return label.x - lineWidth * 0.5f;
                case TextHorizontalAlign::Right:
                    return label.x - lineWidth;
                case TextHorizontalAlign::Left:
                default:
                    return label.x;
            }
        };

        float penX = lineStartX(currentLine);
        float baselineY = label.y + font.lineHeight * label.scale;

        for (std::uint32_t codepoint : codepoints) {
            if (codepoint == static_cast<std::uint32_t>('\r')) {
                continue;
            }
            if (codepoint == static_cast<std::uint32_t>('\n')) {
                ++currentLine;
                penX = lineStartX(currentLine);
                baselineY += font.lineHeight * label.scale;
                continue;
            }

            const FontAtlasGlyph* glyph = resolveGlyph(codepoint);
            if (glyph == nullptr) {
                continue;
            }

            const float x0 = penX + glyph->bearingX * label.scale;
            const float y0 = baselineY - glyph->bearingY * label.scale;
            const float x1 = x0 + glyph->width * label.scale;
            const float y1 = y0 + glyph->height * label.scale;

            if (glyph->width > 0.0f && glyph->height > 0.0f) {
                const std::array<float, 4> color = label.color;
                vertices.push_back({{{x0, y0}}, {{glyph->u0, glyph->v0}}, color});
                vertices.push_back({{{x1, y0}}, {{glyph->u1, glyph->v0}}, color});
                vertices.push_back({{{x1, y1}}, {{glyph->u1, glyph->v1}}, color});
                vertices.push_back({{{x0, y0}}, {{glyph->u0, glyph->v0}}, color});
                vertices.push_back({{{x1, y1}}, {{glyph->u1, glyph->v1}}, color});
                vertices.push_back({{{x0, y1}}, {{glyph->u0, glyph->v1}}, color});
            }

            penX += glyph->advanceX * label.scale;
        }

        return vertices;
    }

    // Zamienia wszystkie etykiety tekstowe na bufory GPU gotowe do rysowania w kazdej klatce.
    void createTextBuffers() {
        destroyTextBuffers();

        if (!hasTextOverlay()) {
            return;
        }

        for (const auto& label : config_.textLabels) {
            const FontGpuResources& font = requireFontByName(label.fontName);
            const std::vector<TextVertex> vertices = buildTextVertices(label, font);

            TextGpuResources text{};
            text.fontIndex = fontLookup_.at(label.fontName);
            text.vertexCount = static_cast<uint32_t>(vertices.size());

            if (!vertices.empty()) {
                const VkDeviceSize bufferSize = sizeof(TextVertex) * vertices.size();
                createDeviceLocalBufferWithStaging(
                    vertices.data(),
                    bufferSize,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    text.vertexBuffer,
                    text.vertexBufferMemory
                );
            }

            texts_.push_back(text);
        }
    }

    // Sprawdza, czy aktualna tresc etykiet wymaga nowych glifow, ktorych nie ma jeszcze w atlasie fontu.
    [[nodiscard]] bool textLabelsNeedFontRebuild() const {
        for (const auto& label : config_.textLabels) {
            const FontGpuResources& font = requireFontByName(label.fontName);
            for (std::uint32_t codepoint : utf8::decode(label.text)) {
                if (codepoint == static_cast<std::uint32_t>('\r') || codepoint == static_cast<std::uint32_t>('\n')) {
                    continue;
                }
                if (font.glyphs.find(codepoint) == font.glyphs.end()) {
                    return true;
                }
            }
        }

        return false;
    }

    // Odtwarza cala warstwe tekstowa po pojawieniu sie nowych znakow, ktorych nie bylo w atlasie fontu.
    void rebuildTextOverlayResources() {
        vkDeviceWaitIdle(device_);

        destroyTextBuffers();
        if (textDescriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, textDescriptorPool_, nullptr);
            textDescriptorPool_ = VK_NULL_HANDLE;
        }
        destroyFontResources();

        createFontResources();
        createTextDescriptorPool();
        createTextDescriptorSets();
        createTextBuffers();
    }

    // Odtwarza tylko bufory geometrii tekstu po zmianie tresci, gdy atlas glifow nadal jest wystarczajacy.
    void rebuildTextBuffersOnly() {
        vkDeviceWaitIdle(device_);
        createTextBuffers();
    }

    // Aktualizuje dynamiczne etykiety tekstowe i przebudowuje atlas tylko wtedy, gdy pojawiaja sie nowe glify.
    void updateDynamicTextOverlay(float elapsedSeconds) {
        if (!config_.updateTextLabels) {
            return;
        }

        config_.updateTextLabels(elapsedSeconds, swapChainExtent_.width, swapChainExtent_.height, config_.textLabels);
        if (areTextLabelsEqual(config_.textLabels, lastTextLabels_)) {
            return;
        }

        if (textLabelsNeedFontRebuild()) {
            rebuildTextOverlayResources();
        } else {
            rebuildTextBuffersOnly();
        }

        lastTextLabels_ = config_.textLabels;
    }

    // Wyznacza nazwe tekstury dla obiektu, korzystajac z lokalnego override'u albo globalnego fallbacku.
    [[nodiscard]] std::string resolveTextureName(const SceneObject& object) const {
        if (!object.textureName.empty()) {
            return object.textureName;
        }
        if (!config_.activeTexture.empty()) {
            return config_.activeTexture;
        }
        return "__default_white";
    }

    // Zwraca zasoby GPU tekstury o podanej nazwie lub zglasza blad konfiguracji.
    [[nodiscard]] const TextureGpuResources& requireTextureByName(const std::string& textureName) const {
        const auto iterator = textureLookup_.find(textureName);
        if (iterator == textureLookup_.end()) {
            throw std::runtime_error("texture not found: " + textureName);
        }
        return textures_[iterator->second];
    }

    // Dobiera descriptor set odpowiadajacy teksturze konkretnego obiektu i aktualnej klatce.
    [[nodiscard]] VkDescriptorSet requireDescriptorSet(const SceneObject& object, uint32_t frameIndex) const {
        const std::string textureName = resolveTextureName(object);
        const auto iterator = textureLookup_.find(textureName);
        if (iterator == textureLookup_.end()) {
            throw std::runtime_error("scene object references unknown texture: " + textureName);
        }
        return descriptorSetsByTexture_[iterator->second][frameIndex];
    }

    // Odtwarza swapchain po zmianie rozmiaru okna lub innych zdarzeniach uniewazniajacych obrazy prezentacji.
    void recreateSwapChain() {
        while (window_->width() == 0 || window_->height() == 0) {
            processEvents();
            if (window_->shouldClose()) {
                return;
            }
        }

        vkDeviceWaitIdle(device_);

        cleanupSwapChain();
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (textPipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, textPipelineLayout_, nullptr);
            textPipelineLayout_ = VK_NULL_HANDLE;
        }
        createSwapChain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createTextGraphicsPipeline();
        createDepthResources();
        createFramebuffers();
        imagesInFlight_.assign(swapChainImages_.size(), VK_NULL_HANDLE);
    }

    // Buduje macierz modelu, laczac translacje, animowany obrot i skale obiektu.
    [[nodiscard]] Matrix4 buildModelMatrix(const SceneObject& object, float elapsedSeconds) const {
        const Matrix4 translationMatrix = translation(
            object.transform.translation.x,
            object.transform.translation.y,
            object.transform.translation.z
        );
        const Matrix4 rotationMatrix = multiply(
            multiply(
                rotationY(object.transform.rotation.y + elapsedSeconds * object.rotation.y),
                rotationX(object.transform.rotation.x + elapsedSeconds * object.rotation.x)
            ),
            rotationZ(object.transform.rotation.z + elapsedSeconds * object.rotation.z)
        );
        const Matrix4 scaleMatrix = scale(
            object.transform.scale.x,
            object.transform.scale.y,
            object.transform.scale.z
        );
        return multiply(translationMatrix, multiply(rotationMatrix, scaleMatrix));
    }

    // Aktualizuje macierze widoku i projekcji dla danej klatki.
    void updateGlobalUniformBuffer(uint32_t frameIndex) {
        GlobalUniformBufferObject ubo{};
        ubo.view = lookAt(config_.camera.position, config_.camera.target, config_.camera.up);
        ubo.proj = perspective(
            config_.camera.fieldOfViewDegrees * 3.1415926535f / 180.0f,
            static_cast<float>(swapChainExtent_.width) / static_cast<float>(swapChainExtent_.height),
            config_.camera.nearPlane,
            config_.camera.farPlane
        );
        ubo.proj.values[5] *= -1.0f;

        std::memcpy(globalUniformBuffersMapped_[frameIndex], &ubo, sizeof(ubo));
    }

    // Pobiera zasoby GPU siatki wskazanej przez obiekt sceny.
    [[nodiscard]] const MeshGpuResources& requireMesh(const std::string& meshName) const {
        const auto iterator = meshLookup_.find(meshName);
        if (iterator == meshLookup_.end()) {
            throw std::runtime_error("scene object references unknown mesh: " + meshName);
        }
        return meshes_[iterator->second];
    }

    // Nagrywa wszystkie polecenia potrzebne do wyrenderowania jednej klatki.
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex) {
        const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer");
        }

        // Ustawiamy wartosci czyszczenia dla koloru tla i bufora glebokosci.
        VkClearValue colorClear{};
        colorClear.color = {
            {
                config_.clearColor[0],
                config_.clearColor[1],
                config_.clearColor[2],
                config_.clearColor[3],
            }
        };
        VkClearValue depthClear{};
        depthClear.depthStencil = {1.0f, 0};
        const std::array<VkClearValue, 2> clearValues = {colorClear, depthClear};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = swapChainFramebuffers_[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChainExtent_;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // Od tego miejsca wszystkie polecenia trafiaja do glownego render passu.
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
        for (const auto& object : config_.sceneObjects) {
            // Kazdy obiekt moze miec inna teksture, ale korzystac z tej samej geometrii i pipeline'u.
            const VkDescriptorSet descriptorSet = requireDescriptorSet(object, frameIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_,
                0,
                1,
                &descriptorSet,
                0,
                nullptr
            );

            const MeshGpuResources& mesh = requireMesh(object.meshName);
            const VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
            const VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            // Push constants przesylaja macierz modelu bez potrzeby tworzenia osobnego UBO per obiekt.
            const Matrix4 model = buildModelMatrix(object, elapsed);
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(Matrix4),
                &model
            );

            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }

        if (textGraphicsPipeline_ != VK_NULL_HANDLE) {
            const std::array<float, 2> screenSize = {
                static_cast<float>(swapChainExtent_.width),
                static_cast<float>(swapChainExtent_.height),
            };

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, textGraphicsPipeline_);
            vkCmdPushConstants(
                commandBuffer,
                textPipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(screenSize),
                screenSize.data()
            );

            for (const auto& text : texts_) {
                if (text.vertexCount == 0) {
                    continue;
                }

                const FontGpuResources& font = fonts_[text.fontIndex];
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    textPipelineLayout_,
                    0,
                    1,
                    &font.descriptorSet,
                    0,
                    nullptr
                );

                const VkBuffer vertexBuffers[] = {text.vertexBuffer};
                const VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdDraw(commandBuffer, text.vertexCount, 1, 0, 0);
            }
        }

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    // Realizuje pelny cykl pojedynczej klatki: acquire, update, submit i present.
    void drawFrame() {
        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, std::numeric_limits<uint64_t>::max());

        const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();
        updateDynamicTextOverlay(elapsed);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
            device_,
            swapChain_,
            std::numeric_limits<uint64_t>::max(),
            imageAvailableSemaphores_[currentFrame_],
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image");
        }

        // Jezeli obraz jest jeszcze uzywany przez poprzednia klatke, czekamy na jego zwolnienie.
        if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, std::numeric_limits<uint64_t>::max());
        }
        imagesInFlight_[imageIndex] = inFlightFences_[currentFrame_];

        updateGlobalUniformBuffer(currentFrame_);

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, currentFrame_);

        // Submit laczy semafory oczekiwania, command buffer renderingu oraz semafor sygnalizujacy zakonczenie pracy GPU.
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        const VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
        const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];

        const VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        const VkSwapchainKHR swapChains[] = {swapChain_};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        // Po zakonczeniu renderingu oddajemy obraz do prezentacji w oknie.
        result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_->wasResized()) {
            window_->clearResizeFlag();
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image");
        }

        currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
    }

    // Sprawdza, czy dana karta spelnia wszystkie wymagania renderera.
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const {
        const QueueFamilyIndices indices = findQueueFamilies(device);
        const bool extensionsSupported = checkDeviceExtensionSupport(device);
        const bool swapChainAdequate = extensionsSupported && hasAdequateSwapChain(device);
        return indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    // Potwierdza, ze urzadzenie potrafi obsluzyc sensowny swapchain dla naszego okna.
    [[nodiscard]] bool hasAdequateSwapChain(VkPhysicalDevice device) const {
        const SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        return !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    // Wyszukuje rodziny kolejek obslugujace grafike oraz prezentacje.
    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        uint32_t index = 0;
        for (const auto& queueFamily : queueFamilies) {
            if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                indices.graphicsFamily = index;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &presentSupport);
            if (presentSupport == VK_TRUE) {
                indices.presentFamily = index;
            }

            if (indices.isComplete()) {
                break;
            }

            ++index;
        }

        return indices;
    }

    // Sprawdza, czy karta wspiera wymagane rozszerzenia, np. swapchain.
    [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice device) const {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(kDeviceExtensions.begin(), kDeviceExtensions.end());
        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    // Pobiera komplet informacji potrzebnych do skonfigurowania swapchaina.
    [[nodiscard]] SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    // Wybiera preferowany format obrazu swapchaina, a gdy go nie ma bierze pierwszy dostepny.
    [[nodiscard]] static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }

        return formats.front();
    }

    // Wybiera tryb prezentacji, preferujac mailbox dla plynnosci bez tearingu.
    [[nodiscard]] static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
        for (const auto& presentMode : presentModes) {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return presentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // Wyznacza finalny rozmiar obrazow swapchaina zgodny z ograniczeniami systemu okienkowego.
    [[nodiscard]] VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        VkExtent2D actualExtent = {
            window_->width(),
            window_->height(),
        };
        actualExtent.width = std::clamp(
            actualExtent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        actualExtent.height = std::clamp(
            actualExtent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );

        return actualExtent;
    }
};

// Publiczny wrapper tylko tworzy prywatna implementacje renderera.
VulkanRenderer::VulkanRenderer(AppConfig config)
    : impl_(std::make_unique<VulkanRendererImpl>(std::move(config))) {}

// Destruktor jest domyslny, bo cala robota dzieje sie w implementacji trzymanej przez unique_ptr.
VulkanRenderer::~VulkanRenderer() = default;

// Przenoszenie pozostawiamy kompilatorowi, bo stan jest opakowany w pojedynczy unique_ptr.
VulkanRenderer::VulkanRenderer(VulkanRenderer&&) noexcept = default;

// Analogicznie operator przypisania przenoszacego nie wymaga recznej logiki.
VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&&) noexcept = default;

// Publiczna metoda run() deleguje dzialanie do prywatnej implementacji.
void VulkanRenderer::run() {
    impl_->run();
}

}  // namespace engine3d
