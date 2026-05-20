#include "mediaLibrary.h"
#include "3ds.h"

#include <algorithm>
#include <cctype>

namespace engine3d {

MediaLibrary::~MediaLibrary() {
    deleteAllObjectMedia();
}

VObjectMedia* MediaLibrary::getObjectMedia(const std::string& name) {
    auto it = mediaList.find(name);
    if (it != mediaList.end()) {
        it->second->reference++;
        return it->second;
    }

    VObjectMedia* obj = nullptr;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.size() >= 4) {
        std::string ext = lower.substr(lower.size() - 4);
        if (ext == ".3ds") {
            obj = new Model3ds();
        }
    }

    if (!obj) {
        obj = new Model3ds();
    }

    if (obj->load(name)) {
        mediaList[name] = obj;
        return obj;
    }

    delete obj;
    return nullptr;
}

bool MediaLibrary::deleteObjectMedia(VObjectMedia* object) {
    for (auto it = mediaList.begin(); it != mediaList.end(); ++it) {
        if (it->second == object) {
            return deleteObjectMedia(it->first);
        }
    }
    return false;
}

bool MediaLibrary::deleteObjectMedia(const std::string& name) {
    auto it = mediaList.find(name);
    if (it != mediaList.end()) {
        if (it->second->reference <= 1) {
            it->second->unload();
            delete it->second;
            mediaList.erase(it);
        } else {
            it->second->reference--;
        }
        return true;
    }
    return false;
}

void MediaLibrary::deleteAllObjectMedia() {
    for (auto& entry : mediaList) {
        entry.second->unload();
        delete entry.second;
    }
    mediaList.clear();
}

} // namespace engine3d
