#include "render/backend/vulkan_context.h"
#include "core/logger.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <optional>

namespace engine3d {

namespace {
    // Nazwa warstwy walidacyjnej Vulkana (Khronos Validation Layer).
    // Warstwy walidacyjne przechwytują błędy wywołań API, sprawdzają poprawność parametrów
    // i pomagają wykrywać wycieki pamięci GPU oraz naruszenia specyfikacji Vulkan.
    const std::vector<const char*> kValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // Wymagane rozszerzenia urządzenia (Device Extensions).
    // VK_KHR_swapchain pozwala na wyświetlanie obrazów na ekranie komputera (integracja z systemem okien).
    const std::vector<const char*> kDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    /**
     * @brief Struktura pomocnicza przechowująca indeksy rodzin kolejek (Queue Families).
     *
     * W Vulkanie GPU wykonuje zadania asynchronicznie poprzez kolejki (VkQueue).
     * Kolejki są pogrupowane w tzw. rodziny kolejek (Queue Families). Każda rodzina
     * obsługuje konkretny typ operacji (np. tylko graficzne, tylko obliczeniowe lub transfer).
     */
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily; // Rodzina obsługująca operacje rysowania (Graphics).
        std::optional<uint32_t> presentFamily;  // Rodzina obsługująca wyświetlanie na ekranie (Presentation).
        std::optional<uint32_t> computeFamily;  // Rodzina obsługująca obliczenia ogólne (Compute Shaders).

        // Sprawdza, czy znaleziono co najmniej kolejki do grafiki i prezentacji na ekranie.
        bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    /**
     * @brief Wyszukuje odpowiednie rodziny kolejek dla danej karty graficznej i powierzchni okna.
     */
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            // Sprawdzamy wsparcie dla komend graficznych.
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }
            // Szukamy dedykowanej rodziny obliczeniowej (która nie jest jednocześnie graficzną) dla optymalizacji Compute.
            if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.computeFamily = i;
            }
            // Sprawdzamy wsparcie dla prezentacji klatek na ekranie w danym oknie.
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
            // Jeśli znaleźliśmy komplet kolejek (grafika, prezentacja i obliczenia), możemy przerwać wyszukiwanie.
            if (indices.isComplete() && indices.computeFamily.has_value()) break;
            i++;
        }
        
        // Jeśli nie znaleziono dedykowanej kolejki obliczeniowej, używamy kolejki graficznej do obliczeń (fallback).
        if (!indices.computeFamily.has_value()) {
            indices.computeFamily = indices.graphicsFamily;
        }
        return indices;
    }

    /**
     * @brief Callback wywoływany przez warstwę walidacyjną w przypadku ostrzeżeń lub błędów Vulkana.
     * Przekazuje wiadomości bezpośrednio do systemu logowania silnika.
     */
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void*
    ) {
        std::string type = "GENERAL";
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) type = "VALIDATION";
        else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) type = "PERFORMANCE";

        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            LOG_ERROR("[Vulkan][{}] {}", type, callbackData->pMessage);
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            LOG_WARN("[Vulkan][{}] {}", type, callbackData->pMessage);
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            LOG_INFO("[Vulkan][{}] {}", type, callbackData->pMessage);
        } else {
            LOG_TRACE("[Vulkan][{}] {}", type, callbackData->pMessage);
        }

        return VK_FALSE; // Zwrócenie VK_FALSE oznacza, że wywołanie API, które wywołało ostrzeżenie, nie powinno zostać przerwane.
    }

    /**
     * @brief Sprawdza, czy system operacyjny i sterownik karty graficznej wspierają wymagane warstwy walidacyjne.
     */
    bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for (const char* layerName : kValidationLayers) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
            if (!layerFound) return false;
        }
        return true;
    }
}

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext() {
    // Vulkan wymaga ręcznego niszczenia wszystkich obiektów w kolejności odwrotnej do ich tworzenia.
    if (allocator_) { 
        vmaDestroyAllocator(allocator_); 
        allocator_ = VK_NULL_HANDLE; 
    }
    if (device_) {
        // Czekamy na zakończenie wszystkich operacji GPU przed zniszczeniem interfejsu logicznego.
        vkDeviceWaitIdle(device_);
        if (computeCommandPool_) vkDestroyCommandPool(device_, computeCommandPool_, nullptr);
        if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(instance_, debugMessenger_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

bool VulkanContext::initialize(const std::string& appName, bool enableValidationLayers) {
    try {
        validationLayersEnabled_ = enableValidationLayers;
        
        // Krok 1: Tworzenie instancji Vulkan (punkt startowy biblioteki).
        createInstance(appName);
        
        // Krok 2: Uruchomienie narzędzia przechwytującego błędy walidacyjne sterownika.
        if (validationLayersEnabled_) {
            setupDebugMessenger();
        }
        return true;
    } catch (const std::exception& e) {
        LOG_CRITICAL("[VulkanContext] Initialization failed: {}", e.what());
        return false;
    }
}

void VulkanContext::createSurface(SDL_Window* window) {
    // Krok 3: Połączenie okna SDL z systemem okien Vulkana (powierzchnia rysowania Surface).
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("failed to create SDL Vulkan surface: ") + SDL_GetError());
    }
    
    // Krok 4: Wybór fizycznej karty graficznej (GPU) spełniającej wymagania silnika.
    pickPhysicalDevice();
    
    // Krok 5: Utworzenie urządzenia logicznego (Logical Device) reprezentującego nasze połączenie z wybranym GPU.
    createLogicalDevice();
    
    // Krok 6: Tworzenie pul komend (Command Pools) do rejestrowania instrukcji przesyłanych na GPU.
    createCommandPool();
    createComputeCommandPool();

    // Krok 7: Inicjalizacja alokatora pamięci GPU (Vulkan Memory Allocator).
    // VMA automatycznie grupuje małe alokacje w większe bloki pamięci GPU, co drastycznie zwiększa wydajność.
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = physicalDevice_;
    allocInfo.device = device_;
    allocInfo.instance = instance_;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_4; // Korzystamy ze specyfikacji Vulkan 1.4
    vmaCreateAllocator(&allocInfo, &allocator_);
}

void VulkanContext::createInstance(const std::string& appName) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "engine3d";
    appInfo.apiVersion = VK_API_VERSION_1_4; // Żądamy funkcjonalności Vulkan 1.4

    // Pobieramy listę rozszerzeń instancji wymaganych przez SDL3 (np. integracja z systemem X11/Wayland/Windows).
    unsigned int sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
    
    // Dodajemy rozszerzenie debugujące, jeśli warstwy walidacji są włączone.
    if (validationLayersEnabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Rejestrujemy warstwy walidacyjne Khronosa.
    if (validationLayersEnabled_ && checkValidationLayerSupport()) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan instance");
    }
}

void VulkanContext::setupDebugMessenger() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    bool hasValidation = false;
    for (const auto& layer : layers) {
        if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            hasValidation = true; break;
        }
    }
    if (!hasValidation) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    // Funkcja do rejestracji debuggera musi być pobrana dynamicznie, ponieważ jest to funkcja rozszerzenia (extension function).
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (func) func(instance_, &createInfo, nullptr, &debugMessenger_);
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    // Iterujemy po dostępnych kartach graficznych i wybieramy pierwszą wspierającą kolejki rysowania i prezentacji.
    for (const auto& device : devices) {
        auto indices = findQueueFamilies(device, surface_);
        if (indices.isComplete()) {
            physicalDevice_ = device;
            
            VkPhysicalDeviceFeatures deviceFeatures;
            vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
            tessellationSupported_ = (deviceFeatures.tessellationShader == VK_TRUE);
            LOG_INFO("[VulkanContext] Selected GPU. Tessellation support: {}", tessellationSupported_ ? "YES" : "NO");
            
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) throw std::runtime_error("failed to find suitable GPU");
}

void VulkanContext::createLogicalDevice() {
    auto indices = findQueueFamilies(physicalDevice_, surface_);
    graphicsQueueFamily_ = indices.graphicsFamily.value();
    computeQueueFamily_ = indices.computeFamily.value();

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    // Używamy std::set, aby wyeliminować duplikaty rodzin kolejek (często kolejka graficzna i prezentacji to ta sama rodzina).
    std::set<uint32_t> uniqueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.computeFamily.value() != indices.graphicsFamily.value()) {
        uniqueFamilies.insert(indices.computeFamily.value());
    }
    float priority = 1.0f; // Priorytet kolejki (od 0.0 do 1.0) określający ważność wykonywanych na niej zadań.
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueCreateInfos.push_back(q);
    }

    // Włączamy filtrowanie anizotropowe (anisotropic filtering) poprawiające wygląd tekstur pod kątem.
    // Włączamy tylko funkcje, które GPU faktycznie wspiera (inaczej vkCreateDevice jest niepoprawne).
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy;
    // fillModeNonSolid jest wymagany przez VK_POLYGON_MODE_LINE (tryb wireframe).
    features.fillModeNonSolid = supported.fillModeNonSolid;
    if (tessellationSupported_) {
        features.tessellationShader = VK_TRUE;
    }
    // depthClamp - mapy cieni: obiekty rzucające cień leżące przed bliską płaszczyzną kaskady (np. wzgórze
    // daleko w stronę słońca) nie są obcinane, tylko dociskane do niej, więc dalej rzucają cień.
    depthClampSupported_ = supported.depthClamp == VK_TRUE;
    features.depthClamp = supported.depthClamp;
    if (!supported.fillModeNonSolid) {
        LOG_WARN("[VulkanContext] fillModeNonSolid not supported - wireframe mode unavailable");
    }

    // Funkcjonalności z wersji Vulkan 1.2 – kluczowe dla zaawansowanego bindowania tekstur.
    // Descriptor Indexing (Bindless Textures) pozwala nam przechowywać tablice tekstur w pamięci GPU
    // i odwoływać się do nich dynamicznie z poziomu shaderów bez ciągłego przełączania stanu.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.timelineSemaphore = VK_TRUE;

    // Funkcjonalności z wersji Vulkan 1.3 – dynamiczny rendering.
    // Dynamic Rendering eliminuje potrzebę tworzenia skomplikowanych i powolnych obiektów VkRenderPass i VkFramebuffer.
    // Dzięki temu możemy rysować bezpośrednio do tekstur i ekranu w bardzo prosty sposób (zbliżony do OpenGL/DirectX 12).
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;
    features13.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }
    
    // Pobieramy uchwyty do poszczególnych kolejek urządzenia logicznego.
    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
    vkGetDeviceQueue(device_, indices.computeFamily.value(), 0, &computeQueue_);
}

void VulkanContext::createCommandPool() {
    auto indices = findQueueFamilies(physicalDevice_, surface_);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // Pula komend graficznych powiązana jest z indeksami rodziny graficznej.
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT zezwala na resetowanie pojedynczych buforów komend alokowanych z tej puli.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void VulkanContext::createComputeCommandPool() {
    auto indices = findQueueFamilies(physicalDevice_, surface_);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // Pula komend obliczeniowych powiązana z indeksami rodziny compute.
    poolInfo.queueFamilyIndex = indices.computeFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &computeCommandPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute command pool");
    }
}

} // namespace engine3d
