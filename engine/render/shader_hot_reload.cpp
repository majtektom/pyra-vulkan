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
        LOG_ERROR("[ShaderHotReload] inotify_init się nie powiódł - brak śledzenia zmian shaderów");
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
        LOG_ERROR("[ShaderHotReload] nie można śledzić katalogu shaderów: {}", shaderDir_);
        return;
    }

    // Otwieramy katalog bazowy shaderów, aby dodać obserwowanie istniejących podkatalogów.
    DIR* dir = opendir(shaderDir_.c_str());
    if (!dir) {
        LOG_ERROR("[ShaderHotReload] nie można otworzyć katalogu shaderów: {}", shaderDir_);
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
            LOG_WARN("[ShaderHotReload] nie można śledzić podkatalogu: {}", subdir);
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
    std::string outTesc = outputDir_ + "/" + shaderName + "/" + shaderName + ".tesc.spv";
    std::string outTese = outputDir_ + "/" + shaderName + "/" + shaderName + ".tese.spv";

    std::string outDir = outputDir_ + "/" + shaderName;
    mkdir(outDir.c_str(), 0755); // Upewniamy się, że katalog docelowy na binary istnieje.

    // Funkcja lambda kompilująca shader za pomocą zewnętrznych programów systemowych:
    auto compile = [&](const std::string& stage, const std::string& entry, const std::string& out) -> bool {
        std::string dxcStage = stage;
        if (stage == "vert") dxcStage = "vs_6_0";
        else if (stage == "frag") dxcStage = "ps_6_0";
        else if (stage == "hull") dxcStage = "hs_6_0";
        else if (stage == "domain") dxcStage = "ds_6_0";

        // Podejście 1: Używamy kompilatora dxc (DirectX Shader Compiler - standard dla HLSL) z generowaniem SPIR-V.
        std::string cmd = "dxc -spirv -T " + dxcStage + " -E " + entry + " \"" + src + "\" -Fo \"" + out + "\" 2>&1";
        if (system(cmd.c_str()) == 0) return true;

        std::string glslangStage = stage;
        if (stage == "hull") glslangStage = "tesc";
        else if (stage == "domain") glslangStage = "tese";

        // Podejście 2 (fallback): Używamy alternatywnego glslangValidator.
        cmd = "glslangValidator -V -D -e " + entry + " -S " + glslangStage + " \"" + src + "\" -o \"" + out + "\" 2>&1";
        return system(cmd.c_str()) == 0;
    };

    // Kompilujemy shader wierzchołków (vertexMain):
    if (!compile("vert", "vertexMain", outVert)) {
        LOG_ERROR("[ShaderHotReload] błąd kompilacji vertex shadera: {}", shaderName);
        return;
    }
    
    // Kompilujemy shader pikseli/fragmentów (fragmentMain):
    if (!compile("frag", "fragmentMain", outFrag)) {
        LOG_ERROR("[ShaderHotReload] błąd kompilacji fragment shadera: {}", shaderName);
        return;
    }

    // Sprawdzamy czy plik zawiera hullMain/domainMain i opcjonalnie kompilujemy shadery teselacji
    bool hasTess = false;
    std::ifstream infile(src);
    if (infile.good()) {
        std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        if (content.find("hullMain") != std::string::npos && content.find("domainMain") != std::string::npos) {
            hasTess = true;
        }
    }
    if (hasTess) {
        if (!compile("hull", "hullMain", outTesc)) {
            LOG_ERROR("[ShaderHotReload] błąd kompilacji hull shadera: {}", shaderName);
            return;
        }
        if (!compile("domain", "domainMain", outTese)) {
            LOG_ERROR("[ShaderHotReload] błąd kompilacji domain shadera: {}", shaderName);
            return;
        }
    }
    
    LOG_INFO("[ShaderHotReload] przekompilowano: {}", shaderName);
}

} // namespace engine3d
