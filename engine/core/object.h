#pragma once

#include <string>
#include <vector>
#include "math.h"

namespace engine3d {

class VObjectMedia;

enum class ObjectType { MODEL_MD2 = 0, MODEL_MD5, MODEL_SPRITE, MODEL_3DS };

class VObject {
public:
    Vec3 position;
    unsigned int ID = 0;
    ObjectType type;
    VObjectMedia* model = nullptr;
    float ray = 1.0f;
    VObject* children = nullptr;
    VObject* parent = nullptr;
    VObject* next = nullptr;

    VObject() = default;
    virtual ~VObject() = default;

    virtual void render() = 0;
};

} // namespace engine3d
