#include "render/backend/vulkan_context.h"
#include "core/logger.h"
#include "render/vulkan_util.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
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

    // Krok 7: Inicjalizacja alokatora pamięci GPU (Vulkan Memory Allocator).
    // VMA automatycznie grupuje małe alokacje w większe bloki pamięci GPU, co drastycznie zwiększa wydajność.
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = physicalDevice_;
    allocInfo.device = device_;
    allocInfo.instance = instance_;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_4;
    vmaCreateAllocator(&allocInfo, &allocator_);
}

void VulkanContext::createInstance(const std::string& appName) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "engine3d";
    // Silnik wymaga Vulkan 1.4 (push descriptors, kopiowanie obrazów z CPU, synchronizacja 2 - bez wersji zapasowych).
    instanceApiVersion_ = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&instanceApiVersion_) != VK_SUCCESS) instanceApiVersion_ = VK_API_VERSION_1_0;
    if (instanceApiVersion_ < VK_API_VERSION_1_4) {
        throw std::runtime_error("Vulkan loader obsługuje tylko " + std::to_string(VK_API_VERSION_MAJOR(instanceApiVersion_)) +
                                 "." + std::to_string(VK_API_VERSION_MINOR(instanceApiVersion_)) +
                                 " - silnik wymaga Vulkan 1.4 (zaktualizuj sterownik karty i vulkan-loader)");
    }
    appInfo.apiVersion = VK_API_VERSION_1_4;

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

// Wybór karty graficznej: tylko urządzenia z Vulkan >= 1.3, wymaganymi funkcjami (dynamic rendering, bindless,
// timeline semaphore), kolejkami rysowania i prezentacji oraz swapchainem. Z pasujących - najpierw karta dedykowana
// (np. obok jest też programowy llvmpipe albo zintegrowane GPU), potem zintegrowana, na końcu reszta.
void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    auto typeRank = [](VkPhysicalDeviceType t) {
        switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 0;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
        default: return 3;   // CPU (llvmpipe) i inne
        }
    };
    int bestRank = 99;
    std::string rejected;
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        std::string why;
        if (props.apiVersion < VK_API_VERSION_1_4) why = "Vulkan < 1.4";
        if (why.empty() && !findQueueFamilies(device, surface_).isComplete()) why = "brak kolejek rysowania/prezentacji";
        if (why.empty()) {
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());
            auto hasExt = [&](const char* need) {
                return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, need) == 0; });
            };
            for (const char* need : kDeviceExtensions) {
                if (!hasExt(need)) why = std::string("brak rozszerzenia ") + need;
            }
        }
        if (why.empty()) {
            VkPhysicalDeviceVulkan12Features f12{};
            f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceVulkan13Features f13{};
            f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            f13.pNext = &f12;
            VkPhysicalDeviceVulkan14Features f14{};
            f14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            f14.pNext = &f13;
            VkPhysicalDeviceFeatures2 f2{};
            f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            f2.pNext = &f14;
            vkGetPhysicalDeviceFeatures2(device, &f2);
            if (!f14.pushDescriptor) why = "brak push descriptors";
            if (!f14.hostImageCopy) why = "brak kopiowania obrazów z CPU (hostImageCopy)";
            // Formaty obrazów kopiowanych z CPU (mgła wojny R8, GUI RGBA8) muszą to obsługiwać.
            for (VkFormat fmt : {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
                VkFormatProperties3 fp3{};
                fp3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
                VkFormatProperties2 fp2{};
                fp2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
                fp2.pNext = &fp3;
                vkGetPhysicalDeviceFormatProperties2(device, fmt, &fp2);
                if (!(fp3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT)) why = "format bez kopiowania z CPU";
            }
            if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore || !f12.descriptorIndexing || !f12.runtimeDescriptorArray ||
                !f12.descriptorBindingPartiallyBound || !f12.descriptorBindingSampledImageUpdateAfterBind ||
                !f12.descriptorBindingUpdateUnusedWhilePending || !f12.shaderSampledImageArrayNonUniformIndexing) {
                why = "brak wymaganych funkcji (dynamic rendering / bindless / timeline semaphore)";
            }
        }
        if (!why.empty()) {
            rejected += std::string(" ") + props.deviceName + " (" + why + ");";
            continue;
        }
        const int rank = typeRank(props.deviceType);
        if (rank < bestRank) {
            bestRank = rank;
            physicalDevice_ = device;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("nie znaleziono karty graficznej z Vulkan 1.4 i wymaganymi funkcjami:" + rejected);
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    // Kopiowanie z CPU do obrazu (hostImageCopy): obrazy aktualizowane w trakcie gry (mgła wojny, minimapa) trzymamy
    // stale w jednym układzie - do odczytu w shaderze, gdy sterownik pozwala w nim kopiować, inaczej GENERAL.
    {
        VkPhysicalDeviceHostImageCopyProperties hic{};
        hic.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES;
        VkPhysicalDeviceProperties2 p2{};
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        p2.pNext = &hic;
        vkGetPhysicalDeviceProperties2(physicalDevice_, &p2);   // najpierw liczba układów
        std::vector<VkImageLayout> dst(hic.copyDstLayoutCount);
        hic.pCopyDstLayouts = dst.data();
        hic.copySrcLayoutCount = 0;
        vkGetPhysicalDeviceProperties2(physicalDevice_, &p2);
        const bool sampled = std::find(dst.begin(), dst.end(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) != dst.end();
        vk::setHostCopyLayout(sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL);
    }

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice_, &deviceFeatures);
    tessellationSupported_ = (deviceFeatures.tessellationShader == VK_TRUE);
    LOG_INFO("[VulkanContext] GPU: {} (sterownik Vulkan {}.{}.{}, loader {}.{}.{}). Teselacja: {}", props.deviceName,
             VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
             VK_API_VERSION_MAJOR(instanceApiVersion_), VK_API_VERSION_MINOR(instanceApiVersion_),
             VK_API_VERSION_PATCH(instanceApiVersion_), tessellationSupported_ ? "tak" : "nie");
    if (!rejected.empty()) LOG_INFO("[VulkanContext] Pominięte urządzenia:{}", rejected);
}

void VulkanContext::createLogicalDevice() {
    auto indices = findQueueFamilies(physicalDevice_, surface_);

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
    features13.synchronization2 = VK_TRUE;   // vkCmdPipelineBarrier2 (vk::imageBarriers) - bariery z etapami przy każdym obrazie

    // Funkcje z rdzenia Vulkan 1.4. Wymagane (sprawdzone przy wyborze karty): push descriptors - deskryptory podawane
    // wprost przy rysowaniu, hostImageCopy - kopiowanie pikseli z CPU prosto do obrazu. Pozostałe włączamy, gdy są.
    VkPhysicalDeviceVulkan14Features supported14{};
    supported14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &supported14;
    vkGetPhysicalDeviceFeatures2(physicalDevice_, &query);
    VkPhysicalDeviceVulkan14Features features14{};
    features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    features14.pNext = &features13;
    features14.pushDescriptor = VK_TRUE;
    features14.hostImageCopy = VK_TRUE;
    features14.maintenance5 = supported14.maintenance5;
    features14.maintenance6 = supported14.maintenance6;
    features14.dynamicRenderingLocalRead = supported14.dynamicRenderingLocalRead;
    features14.indexTypeUint8 = supported14.indexTypeUint8;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features14;
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

} // namespace engine3d
