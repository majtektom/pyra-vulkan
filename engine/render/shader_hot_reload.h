#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/inotify.h>

namespace engine3d {

class ShaderHotReload {
public:
    using Callback = std::function<void(const std::string& shaderName)>;

    ShaderHotReload(const std::string& shaderDir, const std::string& outputDir);
    ~ShaderHotReload();

    void setOnChanged(Callback cb) { onChanged_ = std::move(cb); }
    void check();

private:
    void scanDirectory();
    void recompile(const std::string& shaderName);

    int inotifyFd_ = -1;
    int watchFd_ = -1;
    std::string shaderDir_;
    std::string outputDir_;
    Callback onChanged_;
    std::unordered_map<int, std::string> wdToName_;  // watch descriptor → shader name
};

} // namespace engine3d
