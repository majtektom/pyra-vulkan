#pragma once

#include <map>
#include <string>
#include <strings.h>

namespace engine3d {

enum class MediaType { ME_MD2 = 0, ME_MD5, ME_3DS };

class VObjectMedia {
protected:
    MediaType type;
public:
    std::string name;
    unsigned int reference = 0;

    VObjectMedia() : reference(0) {}
    virtual ~VObjectMedia() = default;

    virtual bool load(const std::string& name) = 0;
    virtual bool unload() = 0;
};

struct istring_less {
    bool operator()(const std::string& l, const std::string& r) const {
        return strcasecmp(l.c_str(), r.c_str()) < 0;
    }
};

class MediaLibrary {
    typedef std::map<std::string, VObjectMedia*, istring_less> MediaIndex;
    MediaIndex mediaList;
public:
    MediaLibrary() = default;
    ~MediaLibrary();

    VObjectMedia* getObjectMedia(const std::string& name);
    bool deleteObjectMedia(VObjectMedia* object);
    bool deleteObjectMedia(const std::string& name);
    void deleteAllObjectMedia();
};

} // namespace engine3d
