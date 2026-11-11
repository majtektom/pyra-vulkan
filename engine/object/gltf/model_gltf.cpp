#include "object/gltf/model_gltf.h"
#include "core/math.h"
#include "core/logger.h"
#include "render/renderer.h"
#include "core/mesh_manager.h"
#include "render/material_manager.h"
#include "core/asset_registry.h"
#include "scene/scene.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "render/vulkan_util.h"
#include <algorithm>


// Obrazy (tekstury w plikach glTF) dekoduje SDL3_image - tiny_gltf bez wbudowanego stb_image.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION

#include <cstring>
#include <stdexcept>
#include <string>
#include "object/gltf/tiny_gltf.h"
#include <SDL3_image/SDL_image.h>

namespace engine3d {
namespace {

// Dekoder obrazów dla tiny_gltf: bajty pliku (PNG/JPG z glTF) -> SDL3_image -> RGBA 8 bitów na kanał.
bool loadGltfImage(tinygltf::Image* image, const int, std::string* err, std::string*, int, int,
                   const unsigned char* bytes, int size, void*) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes, static_cast<size_t>(size));
    SDL_Surface* loaded = io ? IMG_Load_IO(io, true) : nullptr;   // true = IMG_Load_IO zamyka strumień
    SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (loaded) SDL_DestroySurface(loaded);
    if (!rgba) {
        if (err) *err += std::string("SDL3_image: ") + SDL_GetError() + "\n";
        return false;
    }
    image->width = rgba->w;
    image->height = rgba->h;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y) {
        std::memcpy(image->image.data() + static_cast<size_t>(y) * rgba->w * 4,
                    static_cast<const unsigned char*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                    static_cast<size_t>(rgba->w) * 4);
    }
    SDL_DestroySurface(rgba);
    return true;
}

template <typename T>
const T* accessorData(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0 || accessorIdx >= (int)model.accessors.size()) return nullptr;
    const auto& acc = model.accessors[accessorIdx];
    if (acc.bufferView < 0 || acc.bufferView >= (int)model.bufferViews.size()) return nullptr;
    const auto& bv = model.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)model.buffers.size()) return nullptr;
    const auto& buf = model.buffers[bv.buffer];
    return reinterpret_cast<const T*>(buf.data.data() + bv.byteOffset + acc.byteOffset);
}

template <typename T>
std::vector<uint32_t> readIndices(const tinygltf::Model& model, int accessorIdx) {
    std::vector<uint32_t> out;
    if (accessorIdx < 0) return out;
    const auto& acc = model.accessors[accessorIdx];
    out.resize(acc.count);
    const unsigned char* src = accessorData<unsigned char>(model, accessorIdx);
    if (!src) return out;
    int stride = acc.ByteStride(model.bufferViews[acc.bufferView]);
    for (size_t i = 0; i < acc.count; ++i) {
        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            out[i] = *reinterpret_cast<const uint8_t*>(src + i * stride);
        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            out[i] = *reinterpret_cast<const uint16_t*>(src + i * stride);
        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            out[i] = *reinterpret_cast<const uint32_t*>(src + i * stride);
        }
    }
    return out;
}

std::string texturePath(const tinygltf::Model& model, int texIdx, const std::string& baseDir,
                        std::vector<std::string>& tempFiles) {
    if (texIdx < 0 || texIdx >= (int)model.textures.size()) return "";
    const auto& tex = model.textures[texIdx];
    if (tex.source < 0 || tex.source >= (int)model.images.size()) return "";
    const auto& img = model.images[tex.source];
    if (!img.uri.empty()) {
        if (img.uri.find("://") == std::string::npos && img.uri[0] != '/')
            return baseDir + "/" + img.uri;
        return img.uri;
    }
    if (!img.image.empty()) {
        std::string tmpPath = "/tmp/gltf_embed_" + std::to_string(texIdx) + ".bmp";
        FILE* f = fopen(tmpPath.c_str(), "wb");
        if (f) {
            int w = img.width, h = img.height;
            int rowSize = (w * 3 + 3) & ~3;
            int dataSize = rowSize * h;
            int fileSize = 54 + dataSize;
            unsigned char hdr[54] = {};
            hdr[0] = 'B'; hdr[1] = 'M';
            hdr[2] = fileSize & 0xFF; hdr[3] = (fileSize >> 8) & 0xFF;
            hdr[4] = (fileSize >> 16) & 0xFF; hdr[5] = (fileSize >> 24) & 0xFF;
            hdr[10] = 54;
            hdr[14] = 40;
            hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
            hdr[20] = (w >> 16) & 0xFF; hdr[21] = (w >> 24) & 0xFF;
            hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
            hdr[24] = (h >> 16) & 0xFF; hdr[25] = (h >> 24) & 0xFF;
            hdr[26] = 1;
            hdr[28] = 24;
            fwrite(hdr, 1, 54, f);
            for (int y = h - 1; y >= 0; --y) {
                const unsigned char* src = img.image.data() + y * w * img.component;
                for (int x = 0; x < w; ++x) {
                    unsigned char bgr[3] = {src[2], src[1], src[0]};
                    fwrite(bgr, 1, 3, f);
                    src += img.component;
                }
                unsigned char pad[4] = {};
                fwrite(pad, 1, rowSize - w * 3, f);
            }
            fclose(f);
            tempFiles.push_back(tmpPath);
            return tmpPath;
        }
    }
    return "";
}

// Indeks akcesora atrybutu wierzchołka (np. "POSITION"), -1 gdy siatka go nie ma.
int attributeAccessor(const tinygltf::Primitive& prim, const char* name) {
    const auto it = prim.attributes.find(name);
    return it != prim.attributes.end() ? it->second : -1;
}

// Obrót z kwaternionu węzła jako macierz 4x4 (kolumny = obrócone osie X, Y, Z).
Matrix4 rotationMatrix(const std::vector<double>& rotation) {
    const Quaternion q{(float)rotation[0], (float)rotation[1], (float)rotation[2], (float)rotation[3]};
    const Vec3 x = rotateVector(q, Vec3{1, 0, 0});
    const Vec3 y = rotateVector(q, Vec3{0, 1, 0});
    const Vec3 z = rotateVector(q, Vec3{0, 0, 1});
    Matrix4 r = Matrix4::identity();
    r.values[0] = x.x; r.values[1] = x.y; r.values[2] = x.z;
    r.values[4] = y.x; r.values[5] = y.y; r.values[6] = y.z;
    r.values[8] = z.x; r.values[9] = z.y; r.values[10] = z.z;
    return r;
}

int parentOf(const tinygltf::Model& model, int nodeIdx) {
    for (int pi = 0; pi < (int)model.nodes.size(); ++pi) {
        for (int c : model.nodes[pi].children) {
            if (c == nodeIdx) return pi;
        }
    }
    return -1;
}

// Macierz świata węzła używana do "wypalenia" siatek statycznych w wierzchołki: obrót, potem przesunięcie,
// złożone z rodzicami. (Animacja szkieletowa używa osobnych macierzy lokalnych - nodeLocalTransforms.)
Matrix4 staticNodeWorldMatrix(const tinygltf::Model& model, int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size()) return Matrix4::identity();
    const auto& node = model.nodes[nodeIdx];
    Matrix4 local = Matrix4::identity();
    if (node.matrix.size() == 16) {
        for (int i = 0; i < 16; ++i) local.values[i] = (float)node.matrix[i];
    } else {
        if (!node.rotation.empty()) local = multiply(local, rotationMatrix(node.rotation));
        if (!node.translation.empty()) {
            local = multiply(local, translation((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
        }
    }
    const int parent = parentOf(model, nodeIdx);
    return parent < 0 ? local : multiply(staticNodeWorldMatrix(model, parent), local);
}

// Macierz lokalna węzła w kolejności glTF: T * R * S (skala, potem obrót, potem przesunięcie).
Matrix4 localTRS(const tinygltf::Node& node) {
    Matrix4 local = Matrix4::identity();
    if (node.matrix.size() == 16) {
        for (int i = 0; i < 16; ++i) local.values[i] = (float)node.matrix[i];
        return local;
    }
    if (!node.rotation.empty()) local = rotationMatrix(node.rotation);
    if (!node.scale.empty()) local = multiply(local, scale((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
    if (!node.translation.empty()) {
        local = multiply(translation((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]), local);
    }
    return local;
}

// Materiał siatki: kolor bazowy i ścieżki tekstur PBR (osadzone w pliku są zapisywane do plików tymczasowych).
void readPrimitiveMaterial(const tinygltf::Model& model, const tinygltf::Primitive& prim, const std::string& baseDir,
                           std::vector<std::string>& tempFiles, GltfPrimitive& out) {
    out.materialIndex = prim.material;
    if (prim.material < 0 || prim.material >= (int)model.materials.size()) return;
    const auto& mat = model.materials[prim.material];
    const auto& pbr = mat.pbrMetallicRoughness;
    out.materialName = mat.name;
    out.baseColor = {(float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1], (float)pbr.baseColorFactor[2],
                     (float)pbr.baseColorFactor[3]};
    auto path = [&](int textureIndex) {
        return textureIndex >= 0 ? texturePath(model, textureIndex, baseDir, tempFiles) : std::string{};
    };
    out.texturePath = path(pbr.baseColorTexture.index);
    out.ormPath = path(pbr.metallicRoughnessTexture.index);
    out.normalMapPath = path(mat.normalTexture.index);
    out.occlusionPath = path(mat.occlusionTexture.index);
    out.emissivePath = path(mat.emissiveTexture.index);
}

// Wierzchołki siatki. Siatki animowane zostają w układzie lokalnym - całe przekształcenie z pozy spoczynkowej
// robią macierze kości na GPU (przemnożenie tu przez worldMat rozrzuciłoby części modelu). Siatkom statycznym
// wliczamy przekształcenie węzła od razu w wierzchołki.
void readPrimitiveVertices(const tinygltf::Model& model, const tinygltf::Primitive& prim, const Matrix4& worldMat,
                           int vertexCount, MeshData& mesh) {
    const float* positions = accessorData<float>(model, attributeAccessor(prim, "POSITION"));
    const float* normals = accessorData<float>(model, attributeAccessor(prim, "NORMAL"));
    const float* uvs = accessorData<float>(model, attributeAccessor(prim, "TEXCOORD_0"));
    const float* colors = accessorData<float>(model, attributeAccessor(prim, "COLOR_0"));
    const bool skinned = attributeAccessor(prim, "JOINTS_0") >= 0;
    const auto& w = worldMat.values;
    const Matrix4 normalMat = removeTranslation(worldMat);
    const auto& nw = normalMat.values;

    mesh.vertices.resize(vertexCount);
    for (int i = 0; i < vertexCount; ++i) {
        auto& v = mesh.vertices[i];
        const float px = positions ? positions[i * 3] : 0, py = positions ? positions[i * 3 + 1] : 0,
                    pz = positions ? positions[i * 3 + 2] : 0;
        const float nx = normals ? normals[i * 3] : 0, ny = normals ? normals[i * 3 + 1] : 0,
                    nz = normals ? normals[i * 3 + 2] : 0;
        if (skinned) {
            v.position = {px, py, pz};
            v.normal = {nx, ny, nz};
        } else {
            v.position = {px * w[0] + py * w[4] + pz * w[8] + w[12],
                          px * w[1] + py * w[5] + pz * w[9] + w[13],
                          px * w[2] + py * w[6] + pz * w[10] + w[14]};
            v.normal = {nx * nw[0] + ny * nw[4] + nz * nw[8],
                        nx * nw[1] + ny * nw[5] + nz * nw[9],
                        nx * nw[2] + ny * nw[6] + nz * nw[10]};
        }
        v.color = colors ? std::array<float, 3>{colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]}
                         : std::array<float, 3>{1, 1, 1};
        // glTF ma początek UV w lewym górnym rogu, silnik w lewym dolnym - odwracamy V.
        v.texCoord = uvs ? std::array<float, 2>{uvs[i * 2], 1.0f - uvs[i * 2 + 1]} : std::array<float, 2>{0, 0};
    }
}

// Indeksy 4 kości na wierzchołek (uint8 albo uint16 w pliku) i ich wagi.
template <typename JointIndex>
void readJoints(const tinygltf::Model& model, int jointAccessor, const float* weights, int vertexCount, GltfPrimitive& out) {
    const auto& acc = model.accessors[jointAccessor];
    const int stride = acc.ByteStride(model.bufferViews[acc.bufferView]);
    const auto* bytes = accessorData<uint8_t>(model, jointAccessor);
    if (!bytes || !weights) return;
    out.joints.resize(vertexCount);
    out.weights.resize(vertexCount);
    for (int i = 0; i < vertexCount; ++i) {
        const auto* joint = reinterpret_cast<const JointIndex*>(bytes + i * stride);
        for (int j = 0; j < 4; ++j) {
            out.joints[i][j] = static_cast<uint32_t>(joint[j]);
            out.weights[i][j] = weights[i * 4 + j];
        }
    }
}

void readPrimitiveSkinning(const tinygltf::Model& model, const tinygltf::Primitive& prim, int vertexCount, GltfPrimitive& out) {
    const int jointAccessor = attributeAccessor(prim, "JOINTS_0");
    if (jointAccessor < 0) return;
    const float* weights = accessorData<float>(model, attributeAccessor(prim, "WEIGHTS_0"));
    const int type = model.accessors[jointAccessor].componentType;
    if (type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        readJoints<uint8_t>(model, jointAccessor, weights, vertexCount, out);
    } else if (type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        readJoints<uint16_t>(model, jointAccessor, weights, vertexCount, out);
    } else {
        LOG_ERROR("nieobsługiwany typ danych JOINTS_0 (componentType {})", type);
    }
}

// Wszystkie siatki z trójkątów, z każdego węzła, który ma siatkę.
void readPrimitives(const tinygltf::Model& model, const std::string& baseDir, std::vector<std::string>& tempFiles,
                    GltfModel& result) {
    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        const auto& node = model.nodes[ni];
        if (node.mesh < 0 || node.mesh >= (int)model.meshes.size()) continue;
        const Matrix4 worldMat = staticNodeWorldMatrix(model, ni);

        for (const auto& prim : model.meshes[node.mesh].primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) continue;
            const int positionAccessor = attributeAccessor(prim, "POSITION");
            const int vertexCount = positionAccessor >= 0 ? (int)model.accessors[positionAccessor].count : 0;
            if (vertexCount == 0) continue;

            GltfPrimitive out;
            readPrimitiveMaterial(model, prim, baseDir, tempFiles, out);
            readPrimitiveVertices(model, prim, worldMat, vertexCount, out.mesh);
            readPrimitiveSkinning(model, prim, vertexCount, out);

            out.mesh.indices = readIndices<unsigned char>(model, prim.indices);
            if (out.mesh.indices.empty()) {   // siatka bez indeksów: wierzchołki po kolei
                out.mesh.indices.resize(vertexCount);
                for (int i = 0; i < vertexCount; ++i) out.mesh.indices[i] = (uint32_t)i;
            }
            result.primitives.push_back(std::move(out));
        }
    }
}

// Hierarchia węzłów dla animacji: rodzic, macierz lokalna (T*R*S), przesunięcie i skala, macierz świata.
void readNodeHierarchy(const tinygltf::Model& model, GltfModel& result) {
    const int count = (int)model.nodes.size();
    result.nodeParents.assign(count, -1);
    result.nodeLocalTransforms.assign(count, Matrix4::identity());
    result.nodeTranslations.assign(count, {0, 0, 0});
    result.nodeScales.assign(count, {1, 1, 1});
    for (int ni = 0; ni < count; ++ni) {
        const auto& node = model.nodes[ni];
        for (int c : node.children) result.nodeParents[c] = ni;
        result.nodeLocalTransforms[ni] = localTRS(node);
        if (!node.translation.empty()) {
            result.nodeTranslations[ni] = {(float)node.translation[0], (float)node.translation[1], (float)node.translation[2]};
        }
        if (!node.scale.empty()) result.nodeScales[ni] = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};
    }

    // Macierze świata: rodzic przed dzieckiem - w każdym przejściu liczymy węzły, których rodzic jest już gotowy.
    result.nodeTransforms.assign(count, Matrix4::identity());
    std::vector<bool> done(count, false);
    for (int pass = 0; pass < count; ++pass) {
        bool any = false;
        for (int i = 0; i < count; ++i) {
            const int p = result.nodeParents[i];
            if (done[i] || (p >= 0 && !done[p])) continue;
            result.nodeTransforms[i] = p >= 0 ? multiply(result.nodeTransforms[p], result.nodeLocalTransforms[i])
                                              : result.nodeLocalTransforms[i];
            done[i] = true;
            any = true;
        }
        if (!any) break;
    }
}

// Szkielety: lista węzłów-kości i macierze odwrotne pozy spoczynkowej (inverse bind matrices).
void readSkins(const tinygltf::Model& model, GltfModel& result) {
    for (const auto& skin : model.skins) {
        GltfSkin gs;
        gs.joints = skin.joints;
        if (const float* ibm = accessorData<float>(model, skin.inverseBindMatrices)) {
            const int count = (int)model.accessors[skin.inverseBindMatrices].count;
            gs.inverseBindMatrices.resize(count);
            for (int i = 0; i < count; ++i) {
                for (int j = 0; j < 16; ++j) gs.inverseBindMatrices[i].values[j] = ibm[i * 16 + j];
            }
        }
        result.skins.push_back(std::move(gs));
    }
}

// Animacje: dla każdego kanału (węzeł + przesunięcie/obrót/skala) czasy klatek kluczowych i wartości.
void readAnimations(const tinygltf::Model& model, GltfModel& result) {
    for (const auto& anim : model.animations) {
        GltfAnimation ga;
        ga.name = anim.name;
        for (const auto& ch : anim.channels) {
            GltfAnimChannel gc;
            gc.nodeIndex = ch.target_node;
            gc.path = ch.target_path;
            const auto& sampler = anim.samplers[ch.sampler];
            const float* times = accessorData<float>(model, sampler.input);
            const float* values = accessorData<float>(model, sampler.output);
            if (times && values) {
                const auto& out = model.accessors[sampler.output];
                const int components = out.type == TINYGLTF_TYPE_VEC3 ? 3 : out.type == TINYGLTF_TYPE_VEC4 ? 4 : 1;
                gc.times.assign(times, times + model.accessors[sampler.input].count);
                gc.values.assign(values, values + out.count * components);
            }
            ga.channels.push_back(std::move(gc));
        }
        result.animations.push_back(std::move(ga));
    }
}

} // namespace

GltfModel loadGltfModel(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(loadGltfImage, nullptr);
    std::string err, warn;
    const bool binary = path.size() >= 4 && path.substr(path.size() - 4) == ".glb";
    const bool ok = binary ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                           : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!warn.empty()) LOG_WARN("glTF ostrzeżenie: {}", warn);
    if (!err.empty()) throw std::runtime_error("błąd glTF: " + err);
    if (!ok) throw std::runtime_error("nie udało się wczytać glTF: " + path);

    // Ścieżki tekstur w pliku są względne wobec katalogu modelu.
    const auto slash = path.find_last_of("/\\");
    const std::string baseDir = slash != std::string::npos ? path.substr(0, slash) : std::string{};

    GltfModel result;
    std::vector<std::string> tempFiles;
    readPrimitives(model, baseDir, tempFiles, result);
    readNodeHierarchy(model, result);
    readSkins(model, result);
    readAnimations(model, result);
    return result;
}

void evaluateAnimation(const GltfModel& model, int animIdx, float time, std::vector<Matrix4>& jointMatrices) {
    if (animIdx < 0 || animIdx >= (int)model.animations.size()) return;
    const auto& anim = model.animations[animIdx];
    int ncount = (int)model.nodeTransforms.size();

    struct AnimatedLocal { Vec3 t{0,0,0}; Quaternion r{0,0,0,1}; Vec3 s{1,1,1}; bool animated = false; bool hasT=false, hasR=false, hasS=false; };
    std::vector<AnimatedLocal> local(ncount);
    for (const auto& ch : anim.channels) {
        if (ch.nodeIndex < 0 || ch.nodeIndex >= ncount) continue;
        int count = (int)ch.times.size();
        if (count < 2) continue;
        int stride = ch.path == "rotation" ? 4 : 3;
        if ((int)ch.values.size() < count * stride) continue;

        int i0 = count - 2;
        for (int i = 0; i < count - 1; ++i)
            if (time < ch.times[i+1]) { i0 = i; break; }
        int i1 = i0 + 1;
        float range = ch.times[i1] - ch.times[i0];
        float frac = (range > 0.0001f) ? (time - ch.times[i0]) / range : 0.0f;
        frac = std::max(0.0f, std::min(1.0f, frac));

        auto& l = local[ch.nodeIndex];
        l.animated = true;
        if (ch.path == "translation") {
            l.hasT = true;
            l.t = {ch.values[i0*3]+frac*(ch.values[i1*3]-ch.values[i0*3]),
                   ch.values[i0*3+1]+frac*(ch.values[i1*3+1]-ch.values[i0*3+1]),
                   ch.values[i0*3+2]+frac*(ch.values[i1*3+2]-ch.values[i0*3+2])};
        } else if (ch.path == "rotation") {
            l.hasR = true;
            Quaternion q0{ch.values[i0*4], ch.values[i0*4+1], ch.values[i0*4+2], ch.values[i0*4+3]};
            Quaternion q1{ch.values[i1*4], ch.values[i1*4+1], ch.values[i1*4+2], ch.values[i1*4+3]};
            q0 = normalize(q0); q1 = normalize(q1);
            if (q0.x*q1.x+q0.y*q1.y+q0.z*q1.z+q0.w*q1.w < 0) { q1.x=-q1.x; q1.y=-q1.y; q1.z=-q1.z; q1.w=-q1.w; }
            l.r = normalize({q0.x+frac*(q1.x-q0.x), q0.y+frac*(q1.y-q0.y), q0.z+frac*(q1.z-q0.z), q0.w+frac*(q1.w-q0.w)});
        } else if (ch.path == "scale") {
            l.hasS = true;
            l.s = {ch.values[i0*3]+frac*(ch.values[i1*3]-ch.values[i0*3]),
                   ch.values[i0*3+1]+frac*(ch.values[i1*3+1]-ch.values[i0*3+1]),
                   ch.values[i0*3+2]+frac*(ch.values[i1*3+2]-ch.values[i0*3+2])};
        }
    }

    // Węzły animowane: brakujące TRS bierzemy z oryginalnych danych węzła (nie z rozłożonej macierzy!)
    for (int i = 0; i < ncount; ++i) {
        if (!local[i].animated) continue;
        if (!local[i].hasT) local[i].t = model.nodeTranslations[i];
        if (!local[i].hasS) local[i].s = model.nodeScales[i];
    }

    std::vector<Matrix4> worldMats(ncount, Matrix4::identity());
    std::vector<bool> done(ncount, false);
    for (int pass = 0; pass < ncount; ++pass) {
        bool anyChange = false;
        for (int i = 0; i < ncount; ++i) {
            if (done[i]) continue;
            int p = model.nodeParents[i];
            if (p >= 0 && !done[p]) continue;
            Matrix4 localMat;
            if (!local[i].animated) {
                localMat = model.nodeLocalTransforms[i];
            } else {
                auto& l = local[i];
                if (!l.hasT && !l.hasR && !l.hasS) {
                    localMat = model.nodeLocalTransforms[i];
                } else {
                    Matrix4 S = scale(l.s.x, l.s.y, l.s.z);
                    Matrix4 R = Matrix4::identity();
                    Vec3 v{1,0,0}; v = rotateVector(l.r, v);
                    Vec3 v2{0,1,0}; v2 = rotateVector(l.r, v2);
                    Vec3 v3{0,0,1}; v3 = rotateVector(l.r, v3);
                    R.values[0]=v.x; R.values[1]=v.y; R.values[2]=v.z;
                    R.values[4]=v2.x; R.values[5]=v2.y; R.values[6]=v2.z;
                    R.values[8]=v3.x; R.values[9]=v3.y; R.values[10]=v3.z;
                    Matrix4 T = translation(l.t.x, l.t.y, l.t.z);
                    // GLTF: local = T * R * S
                    localMat = multiply(T, multiply(R, S));
                }
            }
            worldMats[i] = (p >= 0) ? multiply(worldMats[p], localMat) : localMat;
            done[i] = true;
            anyChange = true;
        }
        if (!anyChange) break;
    }

    if (!model.skins.empty()) {
        const auto& skin = model.skins[0];
        jointMatrices.resize(skin.joints.size());
        for (size_t j = 0; j < skin.joints.size(); ++j) {
            int ni = skin.joints[j];
            Matrix4 wm = (ni >= 0 && ni < ncount) ? worldMats[ni] : Matrix4::identity();
            Matrix4 ibm = (j < skin.inverseBindMatrices.size()) ? skin.inverseBindMatrices[j] : Matrix4::identity();
            jointMatrices[j] = multiply(wm, ibm);
        }
    }
}

float getAnimationDuration(const GltfModel& model, int animIdx) {
    if (animIdx < 0 || animIdx >= (int)model.animations.size()) return 0.0f;
    const auto& anim = model.animations[animIdx];
    float maxTime = 0.0f;
    for (const auto& ch : anim.channels) {
        if (!ch.times.empty()) {
            float last = ch.times.back();
            if (last > maxTime) maxTime = last;
        }
    }
    return maxTime;
}

// GltfSkinnedModel implementation
GltfSkinnedModel::~GltfSkinnedModel() {
    cleanup();
}

bool GltfSkinnedModel::load(Renderer* renderer, MeshManager* meshManager, MaterialManager* materialManager, AssetRegistry* assets, const std::string& path) {
    cleanup();
    device_ = renderer->device();

    try {
        model_ = loadGltfModel(path);
    } catch (const std::exception& e) {
        LOG_ERROR("nie udało się wczytać modelu glTF ({}): {}", path, e.what());
        return false;
    }

    if (model_.primitives.empty()) {
        LOG_WARN("model glTF nie ma siatek: {}", path);
        return false;
    }

    primitives_.resize(model_.primitives.size());

    for (size_t i = 0; i < model_.primitives.size(); ++i) {
        const auto& prim = model_.primitives[i];
        std::string meshName = path + "_mesh_" + std::to_string(i);
        meshManager->loadMesh(meshName, prim.mesh);

        std::string texName = path + "_prim_" + std::to_string(i) + "_tex";

        Material mat;
        mat.name = path + "_prim_" + std::to_string(i) + "_mat";
        mat.color = prim.baseColor;
        mat.cullMode = CullMode::Disabled;

        if (!prim.texturePath.empty()) {
            const auto texHandle = renderer->loadTexture(texName, prim.texturePath);
            mat.textureHandle = texHandle;
        }

        MaterialHandle handle = materialManager->add(mat);

        primitives_[i].meshName = meshName;
        primitives_[i].materialHandle = handle;

        // Tworzenie bufora wierzchołków dla wag i indeksów kości (Skinning Vertex Buffer)
        if (!prim.joints.empty()) {
            // Układ jak SkinVertex (core/types.h) - ten sam opis czytają potoki animowanych siatek.
            std::vector<SkinVertex> skinData(prim.joints.size());
            for (size_t v = 0; v < prim.joints.size(); ++v) {
                for (int k = 0; k < 4; ++k) {
                    skinData[v].joints[k] = static_cast<uint32_t>(prim.joints[v][k]);
                    skinData[v].weights[k] = prim.weights[v][k];
                }
            }
            const size_t skinSize = skinData.size() * sizeof(SkinVertex);
            vk::createBuffer(renderer->device(), renderer->physicalDevice(), skinSize,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             primitives_[i].skinVertexBuffer, primitives_[i].skinVertexMemory);

            void* mapped;
            vkMapMemory(renderer->device(), primitives_[i].skinVertexMemory, 0, skinSize, 0, &mapped);
            memcpy(mapped, skinData.data(), skinSize);
            vkUnmapMemory(renderer->device(), primitives_[i].skinVertexMemory);
        }
    }

    return true;
}

void GltfSkinnedModel::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        for (auto& prim : primitives_) {
            if (prim.skinVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, prim.skinVertexBuffer, nullptr);
                prim.skinVertexBuffer = VK_NULL_HANDLE;
            }
            if (prim.skinVertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, prim.skinVertexMemory, nullptr);
                prim.skinVertexMemory = VK_NULL_HANDLE;
            }
        }
        device_ = VK_NULL_HANDLE;
    }
    primitives_.clear();
}

// GltfSkinnedInstance implementation
static_assert(GltfSkinnedInstance::kBoneSlots == Renderer::kMaxFrames + 1,
              "slotów kości musi być o jeden więcej niż klatek w locie");

GltfSkinnedInstance::~GltfSkinnedInstance() {
    cleanup();
}

GltfSkinnedInstance::GltfSkinnedInstance(GltfSkinnedInstance&& other) noexcept {
    *this = std::move(other);
}

GltfSkinnedInstance& GltfSkinnedInstance::operator=(GltfSkinnedInstance&& other) noexcept {
    if (this != &other) {
        cleanup();
        renderer_ = other.renderer_;
        model_ = other.model_;
        boneBuffer_ = other.boneBuffer_;
        boneMemory_ = other.boneMemory_;
        boneMapped_ = other.boneMapped_;
        descriptorPool_ = other.descriptorPool_;
        descriptorSetLayout_ = other.descriptorSetLayout_;
        for (uint32_t i = 0; i < kBoneSlots; ++i) {
            boneDescriptorSets_[i] = other.boneDescriptorSets_[i];
            other.boneDescriptorSets_[i] = VK_NULL_HANDLE;
        }
        boneSlot_ = other.boneSlot_;
        rootEntity_ = other.rootEntity_;
        partEntities_ = std::move(other.partEntities_);
        animTime_ = other.animTime_;

        other.renderer_ = nullptr;
        other.model_ = nullptr;
        other.boneBuffer_ = VK_NULL_HANDLE;
        other.boneMemory_ = VK_NULL_HANDLE;
        other.boneMapped_ = nullptr;
        other.descriptorPool_ = VK_NULL_HANDLE;
        other.descriptorSetLayout_ = VK_NULL_HANDLE;
        other.rootEntity_ = nullptr;
        other.animTime_ = 0.0f;
    }
    return *this;
}

bool GltfSkinnedInstance::initialize(Renderer* renderer, const GltfSkinnedModel& model) {
    cleanup();
    renderer_ = renderer;
    model_ = &model;
    VkDevice dev = renderer->device();

    // UBO na macierze kości (max 64 kości) — kBoneSlots slotów, po jednym na klatkę w locie + 1.
    // Rozmiar slotu (4096 B) jest wielokrotnością minUniformBufferOffsetAlignment.
    constexpr int kMaxBones = 64;
    VkDeviceSize boneSize = kMaxBones * sizeof(Matrix4);
    vk::createBuffer(dev, renderer->physicalDevice(), boneSize * kBoneSlots,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     boneBuffer_, boneMemory_);
    vkMapMemory(dev, boneMemory_, 0, boneSize * kBoneSlots, 0, &boneMapped_);
    std::memset(boneMapped_, 0, static_cast<size_t>(boneSize * kBoneSlots));

    // Pula deskryptorów tej instancji
    VkDescriptorPoolSize bonePoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kBoneSlots};
    VkDescriptorPoolCreateInfo bonePoolInfo{};
    bonePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    bonePoolInfo.maxSets = kBoneSlots;
    bonePoolInfo.poolSizeCount = 1;
    bonePoolInfo.pPoolSizes = &bonePoolSize;
    if (vkCreateDescriptorPool(dev, &bonePoolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    // Układ zestawu deskryptorów
    VkDescriptorSetLayoutBinding boneBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo boneLayoutInfo{};
    boneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    boneLayoutInfo.bindingCount = 1;
    boneLayoutInfo.pBindings = &boneBinding;
    if (vkCreateDescriptorSetLayout(dev, &boneLayoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        return false;
    }

    // Jeden descriptor set na slot — każdy wskazuje na swój fragment bufora kości.
    VkDescriptorSetLayout layouts[kBoneSlots];
    std::fill(std::begin(layouts), std::end(layouts), descriptorSetLayout_);
    VkDescriptorSetAllocateInfo boneAlloc{};
    boneAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    boneAlloc.descriptorPool = descriptorPool_;
    boneAlloc.descriptorSetCount = kBoneSlots;
    boneAlloc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(dev, &boneAlloc, boneDescriptorSets_) != VK_SUCCESS) {
        return false;
    }

    for (uint32_t slot = 0; slot < kBoneSlots; ++slot) {
        VkDescriptorBufferInfo boneBufInfo{boneBuffer_, slot * boneSize, boneSize};
        VkWriteDescriptorSet boneWrite{};
        boneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        boneWrite.dstSet = boneDescriptorSets_[slot];
        boneWrite.dstBinding = 0;
        boneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        boneWrite.descriptorCount = 1;
        boneWrite.pBufferInfo = &boneBufInfo;
        vkUpdateDescriptorSets(dev, 1, &boneWrite, 0, nullptr);
    }
    boneSlot_ = 0;

    return true;
}

Entity* GltfSkinnedInstance::instantiate(Scene* scene, const std::string& namePrefix) {
    if (!model_) return nullptr;
    rootEntity_ = scene->createEntity(namePrefix + "_root");
    
    const auto& prims = model_->getPrimitives();
    for (size_t i = 0; i < prims.size(); ++i) {
        Entity* part = scene->createEntity(namePrefix + "_part_" + std::to_string(i));
        scene->setParent(part, rootEntity_);
        
        auto* mc = part->addComponent<MeshComponent>(prims[i].meshName);
        mc->materialHandle = prims[i].materialHandle;
        mc->skinVertexBuffer = prims[i].skinVertexBuffer;
        mc->boneDescriptorSet = boneDescriptorSets_[boneSlot_];
        
        partEntities_.push_back(part);
    }
    return rootEntity_;
}

void GltfSkinnedInstance::update(float dt, int animIdx, float speed) {
    if (!model_ || !renderer_) return;
    
    animTime_ += dt * speed;
    float dur = getAnimationDuration(model_->getModel(), animIdx);
    if (dur > 0.001f) {
        animTime_ = fmodf(animTime_, dur);
    }
    
    std::vector<Matrix4> jointMats;
    evaluateAnimation(model_->getModel(), animIdx, animTime_, jointMats);
    
    if (!jointMats.empty() && boneMapped_) {
        // Piszemy do kolejnego slotu — poprzednie mogą być jeszcze czytane przez klatki w locie.
        constexpr size_t kSlotSize = 64 * sizeof(Matrix4);
        boneSlot_ = (boneSlot_ + 1) % kBoneSlots;
        size_t copySize = std::min(jointMats.size(), (size_t)64) * sizeof(Matrix4);
        memcpy(static_cast<uint8_t*>(boneMapped_) + boneSlot_ * kSlotSize, jointMats.data(), copySize);

        // Paczki renderowania tej klatki muszą wskazywać na właśnie zapisany slot.
        for (Entity* part : partEntities_) {
            if (auto* mc = part->getComponent<MeshComponent>()) {
                mc->boneDescriptorSet = boneDescriptorSets_[boneSlot_];
            }
        }
    }
}

void GltfSkinnedInstance::cleanup() {
    if (renderer_) {
        VkDevice dev = renderer_->device();
        if (boneBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, boneBuffer_, nullptr);
            boneBuffer_ = VK_NULL_HANDLE;
        }
        if (boneMemory_ != VK_NULL_HANDLE) {
            if (boneMapped_) {
                vkUnmapMemory(dev, boneMemory_);
                boneMapped_ = nullptr;
            }
            vkFreeMemory(dev, boneMemory_, nullptr);
            boneMemory_ = VK_NULL_HANDLE;
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
            std::fill(std::begin(boneDescriptorSets_), std::end(boneDescriptorSets_), VK_NULL_HANDLE);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(dev, descriptorSetLayout_, nullptr);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }
        renderer_ = nullptr;
    }
    model_ = nullptr;
    rootEntity_ = nullptr;
    partEntities_.clear();
    animTime_ = 0.0f;
}

} // namespace engine3d

