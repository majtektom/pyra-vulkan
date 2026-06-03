#include "core/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "render/shader_hot_reload.h"
#include "render/vulkan_util.h"

namespace engine3d {

ShaderHotReload::ShaderHotReload(const std::string& shaderDir, const std::string& outputDir)
    : shaderDir_(shaderDir), outputDir_(outputDir) {
    // Inicjalizacja inotify w trybie nieblokującym (IN_NONBLOCK).
    // Dzięki temu funkcja read() nie zawiesi działania wątku głównego gry, jeśli nie ma nowych powiadomień.
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        LOG_ERROR("[ShaderHotReload] inotify_init failed");
        return;
    }
    scanDirectory();
}

ShaderHotReload::~ShaderHotReload() {
    if (watchFd_ >= 0) close(watchFd_);
    if (inotifyFd_ >= 0) close(inotifyFd_);
}

void ShaderHotReload::scanDirectory() {
    // Obserwujemy katalog główny w celu wykrywania tworzenia nowych podkatalogów (nowych shaderów).
    watchFd_ = inotify_add_watch(inotifyFd_, shaderDir_.c_str(),
                                  IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_MODIFY);
    if (watchFd_ < 0) {
        LOG_ERROR("[ShaderHotReload] Failed to add watch for base shader dir: {}", shaderDir_);
        return;
    }

    // Otwieramy katalog bazowy shaderów, aby dodać obserwowanie istniejących podkatalogów.
    DIR* dir = opendir(shaderDir_.c_str());
    if (!dir) {
        LOG_ERROR("[ShaderHotReload] Failed to open shader dir for scanning: {}", shaderDir_);
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        // Interesują nas wyłącznie katalogi inne niż "." i ".." (katalogi ukryte).
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.') continue;
        std::string subdir = shaderDir_ + "/" + entry->d_name;
        
        // Rejestrujemy inotify watch dla danego podkatalogu shadera (nasłuchujemy zapisu i modyfikacji plików).
        int wd = inotify_add_watch(inotifyFd_, subdir.c_str(),
                                    IN_CLOSE_WRITE | IN_MODIFY);
        if (wd >= 0) {
            wdToName_[wd] = entry->d_name;
        } else {
            LOG_WARN("[ShaderHotReload] Failed to add watch for subdir: {}", subdir);
        }
    }
    closedir(dir);
}

void ShaderHotReload::check() {
    if (inotifyFd_ < 0) return;

    // Próbujemy odczytać zdarzenia z kolejki inotify.
    char buf[4096];
    ssize_t len = read(inotifyFd_, buf, sizeof(buf));
    if (len <= 0) return; // Brak nowych zdarzeń w tej klatce.

    // Przetwarzamy bufor ze zdarzeniami inotify.
    for (char* ptr = buf; ptr < buf + len;) {
        auto* event = (struct inotify_event*)ptr;
        if (event->len > 0) {
            std::string name(event->name);
            
            // Szukamy, do jakiego podkatalogu (shadera) należy zmodyfikowany plik.
            auto it = wdToName_.find(event->wd);
            std::string shaderName;
            if (it != wdToName_.end()) {
                shaderName = it->second;
            } else if (event->wd == watchFd_) {
                // Utworzono nowy katalog shadera – wywołujemy ponowne skanowanie.
                scanDirectory();
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            } else {
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Interesują nas wyłącznie pliki z kodem HLSL (*.hlsl).
            if (name.size() > 5 && name.substr(name.size() - 5) == ".hlsl") {
                recompile(shaderName);
                if (onChanged_) {
                    onChanged_(shaderName); // Powiadamiamy system o konieczności rekompilacji potoków.
                }
            }
        }
        ptr += sizeof(struct inotify_event) + event->len;
    }
}

void ShaderHotReload::recompile(const std::string& shaderName) {
    std::string src = shaderDir_ + "/" + shaderName + "/" + shaderName + ".hlsl";
    std::string outVert = outputDir_ + "/" + shaderName + "/" + shaderName + ".vert.spv";
    std::string outFrag = outputDir_ + "/" + shaderName + "/" + shaderName + ".frag.spv";

    std::string outDir = outputDir_ + "/" + shaderName;
    mkdir(outDir.c_str(), 0755); // Upewniamy się, że katalog docelowy na binary istnieje.

    // Funkcja lambda kompilująca shader za pomocą zewnętrznych programów systemowych:
    auto compile = [&](const std::string& stage, const std::string& entry, const std::string& out) -> bool {
        // Podejście 1: Używamy kompilatora dxc (DirectX Shader Compiler - standard dla HLSL) z generowaniem SPIR-V.
        std::string cmd = "dxc -spirv -T " + stage + " -E " + entry + " \"" + src + "\" -Fo \"" + out + "\" 2>&1";
        if (system(cmd.c_str()) == 0) return true;

        // Podejście 2 (fallback): Używamy alternatywnego glslangValidator.
        cmd = "glslangValidator -V -D -e " + entry + " -S " + stage + " \"" + src + "\" -o \"" + out + "\" 2>&1";
        return system(cmd.c_str()) == 0;
    };

    // Kompilujemy shader wierzchołków (vertexMain):
    if (!compile("vert", "vertexMain", outVert)) {
        LOG_ERROR("[ShaderHotReload] vertex compile failed for {}", shaderName);
        return;
    }
    
    // Kompilujemy shader pikseli/fragmentów (fragmentMain):
    if (!compile("frag", "fragmentMain", outFrag)) {
        LOG_ERROR("[ShaderHotReload] fragment compile failed for {}", shaderName);
        return;
    }
    
    LOG_INFO("[ShaderHotReload] recompiled: {}", shaderName);
}

} // namespace engine3d
