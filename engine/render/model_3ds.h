#pragma once
#pragma once

#include <string>
#include <vector>

#include "object.h"
#include "media_library.h"
#include "math.h"
#include "types.h"

namespace engine3d {

class Object3ds;

class Model3ds : public VObjectMedia {
public:
    struct tChunk {
        unsigned short int ID;
        unsigned int length;
        unsigned int bytesRead;
    };

    class tMaterialInfo {
    public:
        std::string name;
        std::string textureFile;
        std::string texturePath;
        std::string opacityMapPath;
        std::array<float, 4> ambientColor{0.2f, 0.2f, 0.2f, 1.0f};
        std::array<float, 4> diffuseColor{0.8f, 0.8f, 0.8f, 1.0f};
        std::array<float, 4> specularColor{0.0f, 0.0f, 0.0f, 1.0f};
        float shininess = 32.0f;
        float transparency = 1.0f;
        bool twoSided = false;
    };

    struct tFace {
        int vertIndex[3];
        int coordIndex[3];
    };

    enum TYP_OBJECT_3DS { CAMERA = 0, MESH, LIGHT };

    class t3DObject {
    public:
        std::string strName;
        int positionFrames = 0;
        int rotationFrames = 0;
        int scaleFrames = 0;
        Vec3 vPivot;
        std::vector<Vec3> vPosition;
        std::vector<Vec3> vRotation;
        std::vector<Vec3> vScale;
        std::vector<float> vRotDegree;
        int numOfVerts = 0;
        int numOfFaces = 0;
        int numTexVertex = 0;
        Vec3* pVerts = nullptr;
        Vec3* pNormals = nullptr;
        std::array<float, 2>* pTexVerts = nullptr;
        tFace* pFaces = nullptr;
        unsigned short m_NodeID = 65535;
        t3DObject* m_ParentNode = nullptr;
        bool m_GroupToggle = false;
        bool m_LinkedObjectToggle = false;
        MeshData mesh;
        int materialIndex = -1;
        TYP_OBJECT_3DS typ = MESH;

        t3DObject() = default;
        ~t3DObject();
        t3DObject(const t3DObject&) = delete;
        t3DObject& operator=(const t3DObject&) = delete;
    };

public:
    bool modelLoaded = false;
    int numOfObjects = 0;
    int numOfMaterials = 0;
    int numberOfFrames = 0;
    int currentFrame = 0;
    float masterScale = 0.0f;
    float ray = 1.0f;

    std::vector<tMaterialInfo*> materialList;
    std::vector<t3DObject*> objectList;

    Model3ds();
    ~Model3ds() override;

    bool load(const std::string& name) override;
    bool unload() override;

    void setCurrentFrame(int frame);

private:
    int getString(char* buffer);
    void readChunk(tChunk* chunk);
    void processNextChunk(tChunk* previousChunk);
    void processNextObjectChunk(t3DObject* object, tChunk* previousChunk);
    void processNextMaterialChunk(tChunk* previousChunk);
    void processNextKeyFrameChunk(tChunk* previousChunk);
    void readColorChunk(std::array<float, 4>& color, tChunk* chunk);
    void readVertices(t3DObject* object, tChunk* previousChunk);
    void readVertexIndices(t3DObject* object, tChunk* previousChunk);
    void readUVCoordinates(t3DObject* object, tChunk* previousChunk);
    void readObjectMaterial(t3DObject* object, tChunk* previousChunk);
    void readKeyFramePositions(tChunk* previousChunk);
    void readKeyFrameRotations(tChunk* previousChunk);
    void readKeyFrameScales(tChunk* previousChunk);
    void applyTrackHierarchy();
    void setCurrentObject(char* strObjectName);
    void createGroupObject();
    void createInstanceObject(char* instanceName);
    void computeNormals(t3DObject* object);
    void cleanUp();
    void buildMeshData(t3DObject* object);
    void resolveTexturePaths(const std::string& modelPath);

    FILE* filePointer = nullptr;
    tChunk* currentChunk = nullptr;
    tChunk* tempChunk = nullptr;
    t3DObject* currentObject = nullptr;
    unsigned short keyNodeID = 65535;
    unsigned short parentIndex = 0;
    std::vector<unsigned short> vParentIndex;
    t3DObject* parentNode = nullptr;
    bool keyGroupToggle = false;
    bool linkedObjectToggle = false;
    std::vector<unsigned short> nodeHash;
    unsigned short keyNumberOfNodes = 0;

    int texMapType = 0; // 0=default, 1=texture, 2=opacity

    friend Object3ds;
};

class Object3ds : public VObject {
    Model3ds* mo = nullptr;
public:
    Object3ds() = default;
    ~Object3ds() override;

    void setModel(Model3ds* m);
    void render() override;
    Model3ds* getModel() const { return mo; }
};


// Deklaracja funkcji konwersji materiału 3ds -> silnikowy
Material Convert3dsMaterialToEngineMaterial(const Model3ds::tMaterialInfo& mat3ds);

} // namespace engine3d
