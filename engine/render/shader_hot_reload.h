#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/inotify.h>

namespace engine3d {

/**
 * @brief System automatycznego przeładowywania shaderów na gorąco (Shader Hot Reload).
 *
 * Klasa monitoruje katalog z plikami źródłowymi shaderów HLSL (`shaderDir`) na dysku 
 * przy użyciu mechanizmu jądra Linux **inotify**.
 * 
 * Gdy plik `.hlsl` zostanie zmodyfikowany i zapisany przez programistę w trakcie działania silnika:
 * 1. Wykrywany jest event modyfikacji pliku.
 * 2. Silnik automatycznie rekompiluje shader przy użyciu kompilatora `dxc` lub `glslangValidator` do formatu SPIR-V.
 * 3. Wywoływany jest callback `onChanged`, informujący system materiałów o konieczności usunięcia 
 *    starych i utworzenia nowych potoków graficznych (Vulkan Pipelines) z nowym kodem shaderów.
 *
 * Pozwala to na natychmiastowe testowanie zmian w oświetleniu i efektach graficznych bez restartowania gry.
 */
class ShaderHotReload {
public:
    using Callback = std::function<void(const std::string& shaderName)>;

    ShaderHotReload(const std::string& shaderDir, const std::string& outputDir);
    ~ShaderHotReload();

    /**
     * @brief Ustawia callback wywoływany po pomyślnej rekompilacji shadera.
     */
    void setOnChanged(Callback cb) { onChanged_ = std::move(cb); }
    
    /**
     * @brief Odpytuje system inotify o zdarzenia systemowe (non-blocking). Wywoływane co klatkę gry.
     */
    void check();

private:
    // Skanuje i dodaje obserwowanie dla katalogu głównego i podkatalogów shaderów.
    void scanDirectory();
    // Rekompiluje plik HLSL do binarek SPIR-V (.spv).
    void recompile(const std::string& shaderName);

    int inotifyFd_ = -1;                              // Deskryptor pliku inotify.
    int watchFd_ = -1;                               // Deskryptor obserwacji katalogu bazowego.
    std::string shaderDir_;                          // Ścieżka do kodu źródłowego HLSL.
    std::string outputDir_;                          // Ścieżka do skompilowanych plików binarnych SPIR-V.
    Callback onChanged_;
    std::unordered_map<int, std::string> wdToName_;  // Mapowanie: watch descriptor -> nazwa shadera.
};

} // namespace engine3d
