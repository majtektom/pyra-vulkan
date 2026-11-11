#include "render/material_system.h"
#include "object/model_3ds.h"
#include "core/logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

bool textureHasAlpha(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }

    const auto closeFile = [&]() {
        fclose(file);
    };

    std::string extension = path.substr(path.find_last_of('.') + 1);
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (extension == "dds") {
        char magic[4] = {};
        char header[124] = {};
        fread(magic, 1, 4, file);
        fread(header, 1, 124, file);
        closeFile();

        if (std::string(magic, 4) != "DDS ") {
            return false;
        }

        auto read32 = [](const char* h, int off) -> uint32_t {
            return uint32_t(uint8_t(h[off]))
                 | (uint32_t(uint8_t(h[off + 1])) << 8)
                 | (uint32_t(uint8_t(h[off + 2])) << 16)
                 | (uint32_t(uint8_t(h[off + 3])) << 24);
        };

        uint32_t pfFlags = read32(header, 76);
        if (pfFlags & 1u) {
            return true;
        }
        if (pfFlags & 4u) {
            uint32_t fourCC = read32(header, 80);
            return fourCC == 0x33545844u || fourCC == 0x35545844u;
        }
        return false;
    }

    if (extension == "tga") {
        char tga[18] = {};
        fread(tga, 1, 18, file);
        closeFile();
        return (tga[1] == 0 && (tga[2] == 2 || tga[2] == 10)) && tga[16] == 32;
    }

    closeFile();
    return false;
}

} // namespace

namespace engine3d {

Material Convert3dsMaterialToEngineMaterial(const Model3ds::tMaterialInfo& mat3ds) {
    Material mat;
    mat.name = mat3ds.name;
    mat.shaderProgram = "default";
    mat.depthTest = true;

    const bool usesAlpha = mat3ds.transparency < 0.99f
        || !mat3ds.opacityMapPath.empty()
        || textureHasAlpha(mat3ds.texturePath);

    mat.blendMode = usesAlpha ? BlendMode::Alpha : BlendMode::Disabled;
    mat.depthWrite = !usesAlpha;
    mat.cullMode = (usesAlpha || mat3ds.twoSided) ? CullMode::Disabled : CullMode::Back;
    return mat;
}

} // namespace engine3d

namespace engine3d {

enum M3D_ENUM {
    NULL_CHUNK                  = 0x0000,
    VERSION                     = 0x0002,
    M3D_KFVERSION               = 0x0005,
    COLOR_F                     = 0x0010,
    COLOR_24                    = 0x0011,
    LIN_COLOR_24                = 0x0012,
    LIN_COLOR_F                 = 0x0013,
    INT_PERCENTAGE              = 0x0030,
    FLOAT_PERCENTAGE            = 0x0031,
    MASTERSCALE                 = 0x0100,
    M3DMAGIC                    = 0x4D4D,
    SMAGIC                      = 0x2D2D,
    LMAGIC                      = 0x2D3D,
    MLIBMAGIC                   = 0x3DAA,
    MATMAGIC                    = 0x3DFF,
    CMAGIC                      = 0xC23D,
    OBJECTINFO                  = 0x3D3D,
    EDIT_MESHVER                = 0x3D3E,
    LO_SHADOW_BIAS              = 0x1400,
    HI_SHADOW_BIAS              = 0x1410,
    SHADOW_MAP_SIZE             = 0x1420,
    SHADOW_SAMPLES              = 0x1430,
    SHADOW_RANGE                = 0x1440,
    SHADOW_FILTER               = 0x1450,
    RAY_BIAS                    = 0x1460,
    O_CONSTS                    = 0x1500,
    BIT_MAP                     = 0x1100,
    SOLID_BGND                  = 0x1200,
    V_GRADIENT                  = 0x1300,
    USE_BIT_MAP                 = 0x1101,
    USE_SOLID_BGND              = 0x1201,
    USE_V_GRADIENT              = 0x1301,
    AMBIENT_LIGHT               = 0x2100,
    FOG                         = 0x2200,
    FOG_BGND                    = 0x2210,
    LAYER_FOG                   = 0x2302,
    DISTANCE_CUE                = 0x2300,
    DCUE_BGND                   = 0x2310,
    USE_FOG                     = 0x2201,
    USE_LAYER_FOG               = 0x2303,
    USE_DISTANCE_CUE            = 0x2301,
    POLY_2D                     = 0x5000,
    SHAPE_OK                    = 0x5010,
    SHAPE_NOT_OK                = 0x5011,
    SHAPE_HOOK                  = 0x5020,
    PATH_3D                     = 0x6000,
    PATH_MATRIX                 = 0x6005,
    SHAPE_2D                    = 0x6010,
    M_SCALE                     = 0x6020,
    M_TWIST                     = 0x6030,
    M_TEETER                    = 0x6040,
    M_FIT                       = 0x6050,
    M_BEVEL                     = 0x6060,
    XZ_CURVE                    = 0x6070,
    YZ_CURVE                    = 0x6080,
    INTERPCT                    = 0x6090,
    DEFORM_LIMIT                = 0x60A0,
    USE_CONTOUR                 = 0x6100,
    USE_TWEEN                   = 0x6110,
    USE_SCALE                   = 0x6120,
    USE_TWIST                   = 0x6130,
    USE_TEETER                  = 0x6140,
    USE_FIT                     = 0x6150,
    USE_BEVEL                   = 0x6160,
    DEFAULT_VIEW                = 0x3000,
    VIEW_TOP                    = 0x3010,
    VIEW_BOTTOM                 = 0x3020,
    VIEW_LEFT                   = 0x3030,
    VIEW_RIGHT                  = 0x3040,
    VIEW_FRONT                  = 0x3050,
    VIEW_BACK                   = 0x3060,
    VIEW_USER                   = 0x3070,
    VIEW_CAMERA                 = 0x3080,
    VIEW_WINDOW                 = 0x3090,
    VIEWPORT_LAYOUT_OLD         = 0x7000,
    VIEWPORT_DATA_OLD           = 0x7010,
    VIEWPORT_LAYOUT             = 0x7001,
    VIEWPORT_DATA               = 0x7011,
    VIEWPORT_DATA_3             = 0x7012,
    VIEWPORT_SIZE               = 0x7020,
    NETWORK_VIEW                = 0x7030,
    MATERIAL                    = 0xAFFF,
    MAT_NAME                    = 0xA000,
    MAT_AMBIENT                 = 0xA010,
    MAT_DIFFUSE                 = 0xA020,
    MAT_SPECULAR                = 0xA030,
    MAT_SHININESS               = 0xA040,
    MAT_SHIN2PCT                = 0xA041,
    MAT_TRANSPARENCY            = 0xA050,
    MAT_XPFALL                  = 0xA052,
    MAT_REFBLUR                 = 0xA053,
    MAT_SELF_ILLUM              = 0xA080,
    MAT_TWO_SIDE                = 0xA081,
    MAT_DECAL                   = 0xA082,
    MAT_ADDITIVE                = 0xA083,
    MAT_SELF_ILPCT              = 0xA084,
    MAT_WIRE                    = 0xA085,
    MAT_WIRESIZE                = 0xA087,
    MAT_FACEMAP                 = 0xA088,
    MAT_XPFALLIN                = 0xA08A,
    MAT_PHONGSOFT               = 0xA08C,
    MAT_WIREABS                 = 0xA08E,
    MAT_SHADING                 = 0xA100,
    MAT_TEXMAP                  = 0xA200,
    MAT_SPECMAP                 = 0xA204,
    MAT_OPACMAP                 = 0xA210,
    MAT_REFLMAP                 = 0xA220,
    MAT_BUMPMAP                 = 0xA230,
    MAP_USE_XPFALL              = 0xA240,
    MAT_USE_REFBLUR             = 0xA250,
    MAT_MAPFILE                 = 0xA300,
    MAT_ACUBIC                  = 0xA310,
    MAT_SXP_TEXT_DATA           = 0xA320,
    MAT_SXP_TEXT2_DATA          = 0xA321,
    MAT_SXP_OPAC_DATA           = 0xA322,
    MAT_SXP_BUMP_DATA           = 0xA324,
    MAT_SXP_SPEC_DATA           = 0xA325,
    MAT_SXP_SHIN_DATA           = 0xA326,
    MAT_SXP_SELFI_DATA          = 0xA328,
    MAT_SXP_TEXTMASK_DATA       = 0xA32A,
    MAT_SXP_TEXT2MASK_DATA      = 0xA32C,
    MAT_SXP_OPACMASK_DATA       = 0xA32E,
    MAT_SXP_BUMPMASK_DATA       = 0xA330,
    MAT_SXP_SPECMASK_DATA       = 0xA332,
    MAT_SXP_SHINMASK_DATA       = 0xA334,
    MAT_SXP_SELFIMASK_DATA      = 0xA336,
    MAT_SXP_REFLMASK_DATA       = 0xA338,
    MAT_TEX2MAP                 = 0xA33A,
    MAT_SHINMAP                 = 0xA33C,
    MAT_SELFIMAP                = 0xA33D,
    MAT_TEXMASK                 = 0xA33E,
    MAT_TEX2MASK                = 0xA340,
    MAT_OPACMASK                = 0xA342,
    MAT_BUMPMASK                = 0xA344,
    MAT_SHINMASK                = 0xA346,
    MAT_SPECMASK                = 0xA348,
    MAT_SELFIMASK               = 0xA34A,
    MAT_REFLMASK                = 0xA34C,
    MAT_MAPTILING               = 0xA351,
    MAT_MAPTEXBLUR              = 0xA353,
    MAT_MAP_USCALE              = 0xA354,
    MAT_MAP_VSCALE              = 0xA356,
    MAT_MAP_UOFFSET             = 0xA358,
    MAT_MAP_VOFFSET             = 0xA35A,
    MAT_MAP_ANG                 = 0xA35C,
    MAT_MAP_COL1                = 0xA360,
    MAT_MAP_COL2                = 0xA362,
    MAT_MAP_RCOL                = 0xA364,
    MAT_MAP_GCOL                = 0xA366,
    MAT_MAP_BCOL                = 0xA368,
    OBJECT                      = 0x4000,
    OBJJECT_HIDDEN              = 0x4010,
    OBJECT_VIS_LOFTER           = 0x4011,
    OBJECT_DOESNT_CAST          = 0x4012,
    OBJECT_MATTE                = 0x4013,
    OBJECT_FAST                 = 0x4014,
    OBJECT_PROCEDURAL           = 0x4015,
    OBJECT_FROZEN               = 0x4016,
    OBJECT_DONT_RECVSHADOW      = 0x4017,
    OBJECT_MESH                 = 0x4100,
    OBJECT_VERTICES             = 0x4110,
    OBJECT_POINT_FLAG_ARRAY     = 0x4111,
    OBJECT_FACES                = 0x4120,
    OBJECT_MATERIAL             = 0x4130,
    OBJECT_UV                   = 0x4140,
    OBJECT_SMOOTHGROUP          = 0x4150,
    OBJECT_MATRIX               = 0x4160,
    OBJECT_MESH_COLOR           = 0x4165,
    OBJECT_MESH_TEXTURE_INFO    = 0x4170,
    OBJECT_MSH_BOXMAP           = 0x4190,
    OBJECT_DIRECT_LIGHT         = 0x4600,
    OBJECT__DL_OFF              = 0x4620,
    OBJECT__DL_OUTER_RANGE      = 0x465A,
    OBJECT__DL_INNER_RANGE      = 0x4659,
    OBJECT__DL_MULTIPLIER       = 0x465B,
    OBJECT__DL_EXCLUDE          = 0x4654,
    OBJECT__DL_ATTENUATE        = 0x4625,
    OBJECT__DL_SPOTLIGHT        = 0x4610,
    OBJECT__DL_SPOT_ROLL        = 0x4656,
    OBJECT__DL_SHADOWED         = 0x4630,
    OBJECT__DL_LOCAL_SHADOW2    = 0x4641,
    OBJECT__DL_SEE_CONE         = 0x4650,
    OBJECT__DL_SPOT_RECTANGULAR = 0x4651,
    OBJECT__DL_SPOT_ASPECT      = 0x4657,
    OBJECT__DL_SPOT_PROJECTOR   = 0x4653,
    OBJECT__DL_SPOT_OVERSHOOT   = 0x4652,
    OBJECT__DL_RAY_BIAS         = 0x4658,
    OBJECT__DL_RAYSHAD          = 0x4627,
    OBJECT_CAMERA               = 0x4700,
    OBJECT__CAM_SEE_CONE        = 0x4710,
    OBJECT__CAM_RANGES          = 0x4720,
    KEYFRAME                    = 0xB000,
    KEYFRAME_HEADER             = 0xB00A,
    KEYFRAME_AMBIENT_NODE_TAG   = 0xB001,
    KEYFRAME_MESH_INFO          = 0xB002,
    KEYFRAME_CAMERA_INFO        = 0xB003,
    KEYFRAME_CAMERA_TARGET      = 0xB004,
    KEYFRAME_LIGHT_NODE_TAG     = 0xB005,
    KEYFRAME_L_TARGET_NODE_TAG  = 0xB006,
    KEYFRAME_SPOTLIGHT_NODE_TAG = 0xB007,
    KEYFRAME_START_AND_END      = 0xB008,
    KEYFRAME_CURTIME            = 0xB009,
    KEYFRAME_OBJECT_NAME        = 0xB010,
    KEYFRAME_INSTANCE_NAME      = 0xB011,
    KEYFRAME_PIVOT              = 0xB013,
    KEYFRAME_BOUNDBOX           = 0xB014,
    KEYFRAME_MORPH_SMOOTH       = 0xB015,
    KEYFRAME_POSITION_TRACK_TAG = 0xB020,
    KEYFRAME_ROTATION_TRACK_TAG = 0xB021,
    KEYFRAME_SCALE_TRACK_TAG    = 0xB022,
    KEYFRAME_FOV_TRACK_TAG      = 0xB023,
    KEYFRAME_ROLL_TRACK_TAG     = 0xB024,
    KEYFRAME_COL_TRACK_TAG      = 0xB025,
    KEYFRAME_MORPH_TRACK_TAG    = 0xB026,
    KEYFRAME_HOT_TRACK_TAG      = 0xB027,
    KEYFRAME_FALL_TRACK_TAG     = 0xB028,
    KEYFRAME_HIDE_TRACK_TAG     = 0xB029,
    KEYFRAME_NODEID             = 0xB030
};

template<class T>
static void deleteAll(T& t) {
    for (auto i = t.begin(); i != t.end(); ++i) delete (*i);
    t.clear();
}

static float roundFloat(float number) {
    if (number > 0 && number < 0.001f) number = 0;
    if (number < 0 && number > -0.001f) number = 0;
    return number;
}

Model3ds::t3DObject::~t3DObject() {
    delete[] pVerts;
    delete[] pNormals;
    delete[] pTexVerts;
    delete[] pFaces;
}

Model3ds::Model3ds() {
    ray = 1.0f;
    modelLoaded = false;
    currentChunk = new tChunk;
    tempChunk = new tChunk;
}

Model3ds::~Model3ds() {
    unload();
}

bool Model3ds::unload() {
    if (modelLoaded) {
        deleteAll(materialList);
        deleteAll(objectList);
        modelLoaded = false;
    }
    return true;
}

bool Model3ds::load(const std::string& name) {
    ray = 1.0f;
    // this->name = name;
    if (name.empty()) {
        LOG_ERROR("[Model3ds] No file name provided");
        return false;
    }
    if (modelLoaded)
        unload();

    numOfObjects = 0;
    numOfMaterials = 0;
    numberOfFrames = 0;
    currentFrame = 0;
    masterScale = 0.0f;
    keyNumberOfNodes = 0;

    filePointer = fopen(name.c_str(), "rb");
    if (!filePointer) {
        LOG_ERROR("[Model3ds] Cannot open file: {}", name);
        return false;
    }

    readChunk(currentChunk);
    if (currentChunk->ID != M3DMAGIC) {
        LOG_ERROR("[Model3ds] Not a valid 3DS file: {}", name);
        return false;
    }

    processNextChunk(currentChunk);
    applyTrackHierarchy();

    cleanUp();
    modelLoaded = true;

    resolveTexturePaths(name);

    for (auto* obj : objectList) {
        if (obj->numOfVerts > 0) {
            buildMeshData(obj);
        }
    }

    return true;
}

void Model3ds::resolveTexturePaths(const std::string& modelPath) {
    size_t slashPos = modelPath.find_last_of("/\\");
    std::string dir = (slashPos != std::string::npos)
        ? modelPath.substr(0, slashPos + 1) : "./";

    auto tryOpen = [](const std::string& path) -> bool {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) { fclose(f); return true; }
        return false;
    };

    for (auto* mat : materialList) {
        if (mat->textureFile.empty()) continue;

        std::string resolved = dir + mat->textureFile;
        if (tryOpen(resolved)) {
            mat->texturePath = resolved;
        } else {
            std::string lowerName = mat->textureFile;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            std::string resolvedLower = dir + lowerName;
            if (tryOpen(resolvedLower)) {
                mat->texturePath = resolvedLower;
            }
        }

        if (!mat->opacityMapPath.empty()) {
            std::string opResolved = dir + mat->opacityMapPath;
            if (tryOpen(opResolved)) {
                mat->opacityMapPath = opResolved;
            } else {
                mat->opacityMapPath.clear();
            }
        }
    }
}

void Model3ds::buildMeshData(t3DObject* object) {
    auto& mesh = object->mesh;
    mesh.vertices.resize(object->numOfVerts);
    mesh.indices.resize(object->numOfFaces * 3);

    float maxDistSq = 0.0f;
    for (int i = 0; i < object->numOfVerts; i++) {
        Vertex& v = mesh.vertices[i];
        v.position[0] = object->pVerts[i].x;
        v.position[1] = object->pVerts[i].y;
        v.position[2] = object->pVerts[i].z;
        if (object->pNormals) {
            v.normal[0] = object->pNormals[i].x;
            v.normal[1] = object->pNormals[i].y;
            v.normal[2] = object->pNormals[i].z;
        }
        v.color = {1.0f, 1.0f, 1.0f};
        if (object->numTexVertex > 0 && object->pTexVerts) {
            v.texCoord = object->pTexVerts[i];
        }

        float dsq = object->pVerts[i].x * object->pVerts[i].x +
                    object->pVerts[i].y * object->pVerts[i].y +
                    object->pVerts[i].z * object->pVerts[i].z;
        if (dsq > maxDistSq) maxDistSq = dsq;
    }
    if (maxDistSq > ray) ray = std::sqrt(maxDistSq);

    for (int i = 0; i < object->numOfFaces; i++) {
        mesh.indices[i * 3 + 0] = static_cast<uint32_t>(object->pFaces[i].vertIndex[0]);
        mesh.indices[i * 3 + 1] = static_cast<uint32_t>(object->pFaces[i].vertIndex[1]);
        mesh.indices[i * 3 + 2] = static_cast<uint32_t>(object->pFaces[i].vertIndex[2]);
    }
}

void Model3ds::cleanUp() {
    if (filePointer) {
        fclose(filePointer);
        filePointer = nullptr;
    }
    delete currentChunk;
    currentChunk = nullptr;
    delete tempChunk;
    tempChunk = nullptr;
}

void Model3ds::readChunk(tChunk* chunk) {
    chunk->bytesRead = fread(&chunk->ID, 1, 2, filePointer);
    chunk->bytesRead += fread(&chunk->length, 1, 4, filePointer);
}

int Model3ds::getString(char* buffer) {
    int index = 0;
    fread(buffer, 1, 1, filePointer);
    while (*(buffer + index++) != 0) {
        fread(buffer + index, 1, 1, filePointer);
    }
    return static_cast<int>(strlen(buffer)) + 1;
}

void Model3ds::readColorChunk(std::array<float, 4>& color, tChunk* chunk) {
    readChunk(tempChunk);
    unsigned char c[3];
    tempChunk->bytesRead += fread(c, 1, tempChunk->length - tempChunk->bytesRead, filePointer);
    color[0] = c[0] / 255.0f;
    color[1] = c[1] / 255.0f;
    color[2] = c[2] / 255.0f;
    color[3] = 1.0f;
    chunk->bytesRead += tempChunk->bytesRead;
}

void Model3ds::readPercentageChunk(float& value, tChunk* chunk) {
    readChunk(tempChunk);
    char buffer[64] = {};

    if (tempChunk->ID == FLOAT_PERCENTAGE) {
        float percent = 0.0f;
        tempChunk->bytesRead += fread(&percent, 1, sizeof(float), filePointer);
        value = 1.0f - percent;
    } else if (tempChunk->ID == INT_PERCENTAGE) {
        unsigned short percent = 0;
        tempChunk->bytesRead += fread(&percent, 1, sizeof(unsigned short), filePointer);
        value = 1.0f - (static_cast<float>(percent) / 100.0f);
    } else {
        tempChunk->bytesRead += fread(buffer, 1, tempChunk->length - tempChunk->bytesRead, filePointer);
    }

    chunk->bytesRead += tempChunk->bytesRead;
}

void Model3ds::processNextChunk(tChunk* previousChunk) {
    t3DObject* newObject = nullptr;
    tMaterialInfo* newTexture = nullptr;
    int version = 0;
    char buffer[50000] = {0};
    currentChunk = new tChunk;

    while (previousChunk->bytesRead < previousChunk->length) {
        readChunk(currentChunk);

        switch (currentChunk->ID) {
        case VERSION:
            currentChunk->bytesRead += fread(&version, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            if (version > 0x03)
                LOG_WARN("[Model3ds] Warning: file version > 3 (found version {})", version);
            break;

        case OBJECTINFO:
            readChunk(tempChunk);
            tempChunk->bytesRead += fread(&version, 1, tempChunk->length - tempChunk->bytesRead, filePointer);
            currentChunk->bytesRead += tempChunk->bytesRead;
            processNextChunk(currentChunk);
            break;

        case MATERIAL:
            numOfMaterials++;
            newTexture = new tMaterialInfo();
            materialList.push_back(newTexture);
            processNextMaterialChunk(currentChunk);
            break;

        case OBJECT:
            numOfObjects++;
            newObject = new t3DObject();
            objectList.push_back(newObject);
            currentChunk->bytesRead += getString(buffer);
            objectList[numOfObjects - 1]->strName = buffer;
            processNextObjectChunk(objectList[numOfObjects - 1], currentChunk);
            computeNormals(objectList[numOfObjects - 1]);
            break;

        case KEYFRAME:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        case MASTERSCALE:
            currentChunk->bytesRead += fread(&masterScale, 1, 4, filePointer);
            break;

        case AMBIENT_LIGHT:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        default:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;
        }

        previousChunk->bytesRead += currentChunk->bytesRead;
    }

    delete currentChunk;
    currentChunk = previousChunk;
}

void Model3ds::processNextObjectChunk(t3DObject* object, tChunk* previousChunk) {
    int buffer[50000] = {0};
    currentChunk = new tChunk;

    while (previousChunk->bytesRead < previousChunk->length) {
        readChunk(currentChunk);

        switch (currentChunk->ID) {
        case OBJECT_MESH:
            object->typ = MESH;
            processNextObjectChunk(object, currentChunk);
            break;

        case OBJECT_VERTICES:
            readVertices(object, currentChunk);
            break;

        case OBJECT_FACES:
            readVertexIndices(object, currentChunk);
            break;

        case OBJECT_MATERIAL:
            readObjectMaterial(object, currentChunk);
            break;

        case OBJECT_UV:
            readUVCoordinates(object, currentChunk);
            break;

        case OBJECT_MATRIX:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        case OBJECT_CAMERA:
            object->typ = CAMERA;
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        case OBJECT_DIRECT_LIGHT:
            object->typ = LIGHT;
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        case OBJECT_SMOOTHGROUP:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        default:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;
        }

        previousChunk->bytesRead += currentChunk->bytesRead;
    }

    delete currentChunk;
    currentChunk = previousChunk;
}

void Model3ds::processNextMaterialChunk(tChunk* previousChunk) {
    char buffer[50000] = {0};
    currentChunk = new tChunk;
    std::array<float, 4> color{0, 0, 0, 1};
    char n[256];


    while (previousChunk->bytesRead < previousChunk->length) {
        readChunk(currentChunk);

        switch (currentChunk->ID) {
        case MAT_NAME:
            currentChunk->bytesRead += fread(n, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            materialList[numOfMaterials - 1]->name = n;
            break;

        case MAT_DIFFUSE: {
            auto* mat = materialList[numOfMaterials - 1];
            readColorChunk(color, currentChunk);
            mat->diffuseColor = color;
            break;
        }

        case MAT_AMBIENT: {
            auto* mat = materialList[numOfMaterials - 1];
            readColorChunk(color, currentChunk);
            mat->ambientColor = color;
            break;
        }

        case MAT_SPECULAR: {
            auto* mat = materialList[numOfMaterials - 1];
            readColorChunk(color, currentChunk);
            mat->specularColor = color;
            break;
        }

        case MAT_TEXMAP: {
            int saved = texMapType;
            texMapType = 1;
            processNextMaterialChunk(currentChunk);
            texMapType = saved;
            break;
        }

        case MAT_OPACMAP: {
            int saved = texMapType;
            texMapType = 2;
            processNextMaterialChunk(currentChunk);
            texMapType = saved;
            break;
        }

        case MAT_MAPFILE: {
            currentChunk->bytesRead += fread(n, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            if (texMapType == 2) {
                materialList[numOfMaterials - 1]->opacityMapPath = n;
            } else {
                materialList[numOfMaterials - 1]->textureFile = n;
            }
            break;
        }

        case MAT_TWO_SIDE: {
            // Flaga dwustronności materiału
            materialList[numOfMaterials - 1]->twoSided = true;
            break;
        }

        case MAT_SHININESS:
        case MAT_SHIN2PCT:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        case MAT_TRANSPARENCY: {
            auto* mat = materialList[numOfMaterials - 1];
            readPercentageChunk(mat->transparency, currentChunk);
            break;
        }

        default:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;
        }

        previousChunk->bytesRead += currentChunk->bytesRead;
    }

    delete currentChunk;
    currentChunk = previousChunk;
}

void Model3ds::readVertices(t3DObject* object, tChunk* previousChunk) {
    previousChunk->bytesRead += fread(&(object->numOfVerts), 1, 2, filePointer);
    object->pVerts = new Vec3[object->numOfVerts];
    memset(object->pVerts, 0, sizeof(Vec3) * object->numOfVerts);

    float* rawVerts = new float[object->numOfVerts * 3];
    previousChunk->bytesRead += fread(rawVerts, 1, previousChunk->length - previousChunk->bytesRead, filePointer);

    for (int i = 0; i < object->numOfVerts; i++) {
        object->pVerts[i].x = rawVerts[i * 3 + 0];
        object->pVerts[i].y = rawVerts[i * 3 + 2];
        object->pVerts[i].z = -rawVerts[i * 3 + 1];
    }
    delete[] rawVerts;
}

void Model3ds::readVertexIndices(t3DObject* object, tChunk* previousChunk) {
    unsigned short index = 0;
    previousChunk->bytesRead += fread(&object->numOfFaces, 1, 2, filePointer);
    object->pFaces = new tFace[object->numOfFaces];
    memset(object->pFaces, 0, sizeof(tFace) * object->numOfFaces);

    for (int i = 0; i < object->numOfFaces; i++) {
        for (int j = 0; j < 4; j++) {
            previousChunk->bytesRead += fread(&index, 1, sizeof(index), filePointer);
            if (j < 3) {
                object->pFaces[i].vertIndex[j] = index;
            }
        }
    }
}

void Model3ds::readUVCoordinates(t3DObject* object, tChunk* previousChunk) {
    previousChunk->bytesRead += fread(&object->numTexVertex, 1, 2, filePointer);
    object->pTexVerts = new std::array<float, 2>[object->numTexVertex];

    float* rawUV = new float[object->numTexVertex * 2];
    previousChunk->bytesRead += fread(rawUV, 1, previousChunk->length - previousChunk->bytesRead, filePointer);

    for (int i = 0; i < object->numTexVertex; i++) {
        object->pTexVerts[i][0] = rawUV[i * 2 + 0];
        object->pTexVerts[i][1] = 1.0f - rawUV[i * 2 + 1];
    }
    delete[] rawUV;
}

void Model3ds::readObjectMaterial(t3DObject* object, tChunk* previousChunk) {
    char strMaterial[255] = {0};
    int buffer[50000] = {0};

    previousChunk->bytesRead += getString(strMaterial);

    for (int i = 0; i < numOfMaterials; i++) {
        if (strcmp(strMaterial, materialList[i]->name.c_str()) == 0) {
            object->materialIndex = i;
        }
    }

    previousChunk->bytesRead += fread(buffer, 1, previousChunk->length - previousChunk->bytesRead, filePointer);
}

void Model3ds::readKeyFramePositions(tChunk* previousChunk) {
    short frameNumber = 0, ignored = 0;
    long lunknown = 0;

    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);

    previousChunk->bytesRead += fread(&(currentObject->positionFrames), 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);

    for (int i = 0; i <= (numberOfFrames + 1); i++) {
        currentObject->vPosition.push_back(Vec3());
        if (i < currentObject->positionFrames) {
            previousChunk->bytesRead += fread(&frameNumber, 1, sizeof(short), filePointer);
            previousChunk->bytesRead += fread(&lunknown, 1, sizeof(long), filePointer);

            float pos[3];
            previousChunk->bytesRead += fread(pos, 1, sizeof(float) * 3, filePointer);

            Vec3 v;
            v.x = pos[0];
            v.y = pos[2];
            v.z = -pos[1];
            currentObject->vPosition[i] = v;
        } else {
            currentObject->vPosition[i] = currentObject->vPosition[currentObject->positionFrames - 1];
        }
    }
}

void Model3ds::readKeyFrameRotations(tChunk* previousChunk) {
    short frameNumber = 0, ignored = 0;
    long lunknown = 0;
    float rotationDegree = 0;
    std::vector<int> vFrameNumber;
    std::vector<float> vRotDegree;
    std::vector<Vec3> vRotation;

    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);

    previousChunk->bytesRead += fread(&(currentObject->rotationFrames), 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignored, 1, sizeof(short), filePointer);

    for (int i = 0; i < currentObject->rotationFrames; i++) {
        vRotation.push_back(Vec3());
        previousChunk->bytesRead += fread(&frameNumber, 1, sizeof(short), filePointer);
        vFrameNumber.push_back(frameNumber);
        previousChunk->bytesRead += fread(&lunknown, 1, sizeof(long), filePointer);
        previousChunk->bytesRead += fread(&rotationDegree, 1, sizeof(float), filePointer);
        rotationDegree = rotationDegree * (180.0f / PI);
        vRotDegree.push_back(rotationDegree);

        float rot[3];
        previousChunk->bytesRead += fread(rot, 1, sizeof(float) * 3, filePointer);

        Vec3 v;
        v.x = roundFloat(rot[0]);
        v.y = roundFloat(rot[1]);
        v.z = roundFloat(rot[2]);

        float temp = v.y;
        v.y = -v.z;
        v.z = temp;
        v.x *= -1;

        vRotation[i] = v;
    }

    currentObject->vRotation.push_back(vRotation[0]);
    currentObject->vRotDegree.push_back(vRotDegree[0]);

    int currentKey = 1;
    for (int i = 1; i <= (numberOfFrames + 1); i++) {
        if (currentKey < currentObject->rotationFrames) {
            int currentFrameNum = vFrameNumber[currentKey];
            int previousFrameNum = vFrameNumber[currentKey - 1];
            float degree = vRotDegree[currentKey];
            float rotDegree = degree / (currentFrameNum - previousFrameNum);

            currentObject->vRotation.push_back(vRotation[currentKey]);
            currentObject->vRotDegree.push_back(rotDegree);

            if (vFrameNumber[currentKey] <= i)
                currentKey++;
        } else {
            currentObject->vRotation.push_back(vRotation[currentKey - 1]);
            currentObject->vRotDegree.push_back(0.0f);
        }
    }
}

void Model3ds::readKeyFrameScales(tChunk* previousChunk) {
    short frameNumber = 0, ignore = 0;
    long lunknown = 0;

    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);

    previousChunk->bytesRead += fread(&(currentObject->scaleFrames), 1, sizeof(short), filePointer);
    previousChunk->bytesRead += fread(&ignore, 1, sizeof(short), filePointer);

    for (int i = 0; i <= (numberOfFrames + 1); i++) {
        currentObject->vScale.push_back(Vec3());
        if (i < currentObject->scaleFrames) {
            previousChunk->bytesRead += fread(&frameNumber, 1, sizeof(short), filePointer);
            previousChunk->bytesRead += fread(&lunknown, 1, sizeof(long), filePointer);

            float s[3];
            previousChunk->bytesRead += fread(s, 1, sizeof(float) * 3, filePointer);

            Vec3 v;
            v.x = s[0];
            v.y = s[2];
            v.z = s[1];
            currentObject->vScale[i] = v;
        } else {
            currentObject->vScale[i] = currentObject->vScale[currentObject->scaleFrames - 1];
        }
    }
}

void Model3ds::processNextKeyFrameChunk(tChunk* previousChunk) {
    int buffer[50000] = {0};
    char strKeyFrameObject[50] = {0};
    char instanceName[50] = {0};
    currentChunk = new tChunk;

    while (previousChunk->bytesRead < previousChunk->length) {
        readChunk(currentChunk);

        switch (currentChunk->ID) {
        case KEYFRAME_HEADER: {
            short revisionLevel;
            currentChunk->bytesRead += fread(&revisionLevel, 1, 2, filePointer);
            char _3dsFileName[13] = {0};
            currentChunk->bytesRead += getString(_3dsFileName);
            _3dsFileName[12] = '\0';
            long animationLength;
            currentChunk->bytesRead += fread(&animationLength, 1, 4, filePointer);
            break;
        }

        case KEYFRAME_MESH_INFO:
            currentObject = nullptr;
            keyNodeID = 65535;
            vParentIndex.push_back(65535);
            parentNode = nullptr;
            keyGroupToggle = 0;
            linkedObjectToggle = 0;
            nodeHash.push_back(keyNodeID);
            keyNumberOfNodes++;
            processNextKeyFrameChunk(currentChunk);

            if (currentObject)
                currentObject->m_NodeID = keyNodeID;
            if (keyNodeID != 65535)
                vParentIndex[keyNodeID] = parentIndex;
            if (currentObject)
                currentObject->m_ParentNode = parentNode;
            if (currentObject) {
                currentObject->m_GroupToggle = keyGroupToggle;
                currentObject->m_LinkedObjectToggle = linkedObjectToggle;
            }
            break;

        case KEYFRAME_NODEID:
            currentChunk->bytesRead += fread(&keyNodeID, 1, 2, filePointer);
            break;

        case KEYFRAME_OBJECT_NAME: {
            currentChunk->bytesRead += getString(strKeyFrameObject);
            setCurrentObject(strKeyFrameObject);
            currentChunk->bytesRead += fread(&buffer, 1, 2, filePointer);
            currentChunk->bytesRead += fread(&buffer, 1, 2, filePointer);
            currentChunk->bytesRead += fread(&parentIndex, 1, 2, filePointer);
            break;
        }

        case KEYFRAME_INSTANCE_NAME:
            currentChunk->bytesRead += getString(instanceName);
            createInstanceObject(instanceName);
            break;

        case KEYFRAME_START_AND_END:
            currentChunk->bytesRead += fread(&(numberOfFrames), 1, 4, filePointer);
            currentChunk->bytesRead += fread(&(numberOfFrames), 1, 4, filePointer);
            break;

        case KEYFRAME_PIVOT:
            currentChunk->bytesRead += fread(&(currentObject->vPivot), 1, sizeof(Vec3), filePointer);
            {
                float temp = currentObject->vPivot.y;
                currentObject->vPivot.y = currentObject->vPivot.z;
                currentObject->vPivot.z = -temp;
            }
            break;

        case KEYFRAME_POSITION_TRACK_TAG:
            readKeyFramePositions(currentChunk);
            break;

        case KEYFRAME_ROTATION_TRACK_TAG:
            readKeyFrameRotations(currentChunk);
            break;

        case KEYFRAME_SCALE_TRACK_TAG:
            readKeyFrameScales(currentChunk);
            break;

        case KEYFRAME_BOUNDBOX:
        case KEYFRAME_CURTIME:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;

        default:
            currentChunk->bytesRead += fread(buffer, 1, currentChunk->length - currentChunk->bytesRead, filePointer);
            break;
        }

        previousChunk->bytesRead += currentChunk->bytesRead;
    }

    delete currentChunk;
    currentChunk = previousChunk;
}

void Model3ds::applyTrackHierarchy() {
    for (int i = 0; i < keyNumberOfNodes; i++) {
        if (nodeHash[i] >= objectList.size()) continue;
        t3DObject* pObject = objectList[nodeHash[i]];
        if (pObject->m_NodeID == 65535)
            continue;

        for (int j = i + 1; j < keyNumberOfNodes; j++) {
            if (nodeHash[j] >= objectList.size()) continue;
            t3DObject* cObject = objectList[nodeHash[j]];
            if (pObject->m_NodeID != vParentIndex[cObject->m_NodeID])
                continue;

            cObject->m_ParentNode = pObject;

            if (!pObject->m_GroupToggle) {
                if (!cObject->m_GroupToggle) {
                    pObject->m_LinkedObjectToggle = true;
                    cObject->m_LinkedObjectToggle = true;
                }
            }
        }
    }

    for (int i = 0; i < keyNumberOfNodes; i++) {
        if (nodeHash[i] >= objectList.size()) continue;
        t3DObject* pObject = objectList[nodeHash[i]];
        if (pObject->m_GroupToggle || pObject->m_LinkedObjectToggle)
            continue;

        for (int k = 0; k < pObject->numOfVerts; k++) {
            Vec3 offset = add(pObject->vPosition[0], pObject->vPivot);
            pObject->pVerts[k] = subtract(pObject->pVerts[k], offset);
        }
    }
}

void Model3ds::setCurrentObject(char* strObjectName) {
    if (!strObjectName) {
        currentObject = nullptr;
        return;
    }

    if (strcmp(strObjectName, "$$$DUMMY") == 0) {
        createGroupObject();
        return;
    }

    for (int i = 0; i < numOfObjects; i++) {
        if (objectList[i]->strName == strObjectName) {
            currentObject = objectList[i];
            nodeHash[keyNumberOfNodes - 1] = i;
            return;
        }
    }

    LOG_WARN("[Model3ds] Unknown object: {}", strObjectName);
    currentObject = nullptr;
}

void Model3ds::createGroupObject() {
    auto* newObject = new t3DObject();
    numOfObjects++;
    objectList.push_back(newObject);

    objectList[numOfObjects - 1]->numOfVerts = 0;
    objectList[numOfObjects - 1]->numOfFaces = 0;
    objectList[numOfObjects - 1]->strName = "$$$DUMMY";

    currentObject = objectList[numOfObjects - 1];
    keyGroupToggle = true;
    nodeHash[keyNumberOfNodes - 1] = numOfObjects - 1;
}

void Model3ds::createInstanceObject(char* InstanceName) {
    if (keyGroupToggle) {
        currentObject->strName = InstanceName;
        return;
    }

    auto* newObject = new t3DObject();
    numOfObjects++;
    objectList.push_back(newObject);

    if (currentObject) {
        newObject->numOfVerts = currentObject->numOfVerts;
        if (currentObject->numOfVerts != 0) {
            newObject->pVerts = new Vec3[currentObject->numOfVerts];
            for (int i = 0; i < currentObject->numOfVerts; i++) {
                newObject->pVerts[i] = currentObject->pVerts[i];
            }
        }

        newObject->numOfFaces = currentObject->numOfFaces;
        if (currentObject->numOfFaces != 0) {
            newObject->pFaces = new tFace[currentObject->numOfFaces];
            for (int i = 0; i < currentObject->numOfFaces; i++) {
                newObject->pFaces[i] = currentObject->pFaces[i];
            }
        }
    }

    currentObject = objectList[numOfObjects - 1];
    currentObject->strName = InstanceName;
    keyGroupToggle = false;
    nodeHash[keyNumberOfNodes - 1] = numOfObjects - 1;
}

void Model3ds::computeNormals(t3DObject* object) {
    if (!object || object->numOfFaces <= 0)
        return;

    Vec3* pNormals = new Vec3[object->numOfFaces];
    Vec3* pTempNormals = new Vec3[object->numOfFaces];
    object->pNormals = new Vec3[object->numOfVerts];

    for (int i = 0; i < object->numOfFaces; i++) {
        Vec3 vPoly[3];
        vPoly[0] = object->pVerts[object->pFaces[i].vertIndex[0]];
        vPoly[1] = object->pVerts[object->pFaces[i].vertIndex[1]];
        vPoly[2] = object->pVerts[object->pFaces[i].vertIndex[2]];

        Vec3 vVector1 = subtract(vPoly[0], vPoly[2]);
        Vec3 vVector2 = subtract(vPoly[2], vPoly[1]);
        Vec3 vNormal = cross(vVector1, vVector2);
        pTempNormals[i] = vNormal;
        pNormals[i] = normalize(vNormal);
    }

    for (int i = 0; i < object->numOfVerts; i++) {
        Vec3 vSum{0, 0, 0};
        int shared = 0;

        for (int j = 0; j < object->numOfFaces; j++) {
            if (object->pFaces[j].vertIndex[0] == i ||
                object->pFaces[j].vertIndex[1] == i ||
                object->pFaces[j].vertIndex[2] == i) {
                vSum = add(vSum, pTempNormals[j]);
                shared++;
            }
        }

        if (shared > 0) {
            float inv = -1.0f / shared;
            object->pNormals[i] = multiply(vSum, inv);
            object->pNormals[i] = normalize(object->pNormals[i]);
        }
    }

    delete[] pTempNormals;
    delete[] pNormals;
}

} // namespace engine3d
