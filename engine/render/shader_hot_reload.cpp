#include "shader_hot_reload.h"
#include "vulkan_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

namespace engine3d {

ShaderHotReload::ShaderHotReload(const std::string& shaderDir, const std::string& outputDir)
    : shaderDir_(shaderDir), outputDir_(outputDir) {
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        std::cerr << "[ShaderHotReload] inotify_init failed\n";
        return;
    }
    scanDirectory();
}

ShaderHotReload::~ShaderHotReload() {
    if (watchFd_ >= 0) close(watchFd_);
    if (inotifyFd_ >= 0) close(inotifyFd_);
}

void ShaderHotReload::scanDirectory() {
    // Watch the base shader dir for new subdirectories
    watchFd_ = inotify_add_watch(inotifyFd_, shaderDir_.c_str(),
                                  IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_MODIFY);
    if (watchFd_ < 0) return;

    // Also watch existing subdirectories
    DIR* dir = opendir(shaderDir_.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.') continue;
        std::string subdir = shaderDir_ + "/" + entry->d_name;
        int wd = inotify_add_watch(inotifyFd_, subdir.c_str(),
                                    IN_CLOSE_WRITE | IN_MODIFY);
        if (wd >= 0) wdToName_[wd] = entry->d_name;
    }
    closedir(dir);
}

void ShaderHotReload::check() {
    if (inotifyFd_ < 0) return;

    char buf[4096];
    ssize_t len = read(inotifyFd_, buf, sizeof(buf));
    if (len <= 0) return;

    for (char* ptr = buf; ptr < buf + len;) {
        auto* event = (struct inotify_event*)ptr;
        if (event->len > 0) {
            std::string name(event->name);
            // Find which shader this file belongs to
            auto it = wdToName_.find(event->wd);
            std::string shaderName;
            if (it != wdToName_.end()) {
                shaderName = it->second;
            } else if (event->wd == watchFd_) {
                // New directory created — rescan
                scanDirectory();
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            } else {
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Only react to .hlsl files
            if (name.size() > 5 && name.substr(name.size() - 5) == ".hlsl") {
                recompile(shaderName);
                if (onChanged_) onChanged_(shaderName);
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
    mkdir(outDir.c_str(), 0755);

    // Try dxc first, then glslangValidator
    auto compile = [&](const std::string& stage, const std::string& entry, const std::string& out) -> bool {
        // dxc
        std::string cmd = "dxc -spirv -T " + stage + " -E " + entry + " \"" + src + "\" -Fo \"" + out + "\" 2>&1";
        if (system(cmd.c_str()) == 0) return true;

        // glslangValidator
        cmd = "glslangValidator -V -D -e " + entry + " -S " + stage + " \"" + src + "\" -o \"" + out + "\" 2>&1";
        return system(cmd.c_str()) == 0;
    };

    if (!compile("vert", "vertexMain", outVert)) {
        std::cerr << "[ShaderHotReload] vertex compile failed for " << shaderName << "\n";
        return;
    }
    if (!compile("frag", "fragmentMain", outFrag)) {
        std::cerr << "[ShaderHotReload] fragment compile failed for " << shaderName << "\n";
        return;
    }
    std::cout << "[ShaderHotReload] recompiled: " << shaderName << "\n";
}

} // namespace engine3d
