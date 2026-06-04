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


#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION

#include <cstring>
#include <stdexcept>
#include <string>
#include "object/gltf/tiny_gltf.h"

namespace engine3d {
namespace {

Matrix4 inverseMatrix4(const Matrix4& m) {
    Matrix4 inv = Matrix4::identity();
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            inv.values[c * 4 + r] = m.values[r * 4 + c];
    for (int r = 0; r < 3; r++) {
        float dot = 0;
        for (int c = 0; c < 3; c++)
            dot += inv.values[c * 4 + r] * m.values[3 * 4 + c];
        inv.values[3 * 4 + r] = -dot;
    }
    return inv;
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

} // namespace

GltfModel loadGltfModel(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = false;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) LOG_WARN("glTF warn: {}", warn);
    if (!err.empty()) throw std::runtime_error("glTF error: " + err);
    if (!ok) throw std::runtime_error("Failed to load glTF: " + path);

    std::string baseDir;
    auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) baseDir = path.substr(0, slash);

    GltfModel result;
    std::vector<std::string> tempFiles;

    std::function<Matrix4(int)> nodeWorldMatrix = [&](int nodeIdx) -> Matrix4 {
        if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size()) return Matrix4::identity();
        const auto& node = model.nodes[nodeIdx];
        Matrix4 local = Matrix4::identity();
        if (!node.matrix.empty() && node.matrix.size() == 16) {
            for (int i = 0; i < 16; ++i) local.values[i] = (float)node.matrix[i];
        } else {
            if (!node.rotation.empty()) {
                Quaternion q{(float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]};
                Vec3 v{1,0,0}; v = rotateVector(q, v);
                Vec3 v2{0,1,0}; v2 = rotateVector(q, v2);
                Vec3 v3{0,0,1}; v3 = rotateVector(q, v3);
                Matrix4 r = Matrix4::identity();
                r.values[0] = v.x; r.values[1] = v.y; r.values[2] = v.z;
                r.values[4] = v2.x; r.values[5] = v2.y; r.values[6] = v2.z;
                r.values[8] = v3.x; r.values[9] = v3.y; r.values[10] = v3.z;
                local = multiply(local, r);
            }
            if (!node.translation.empty()) local = multiply(local, translation((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
        }
        for (int child : node.children) {
            if (child == nodeIdx) continue;
        }
        int parentIdx = -1;
        for (int pi = 0; pi < (int)model.nodes.size(); ++pi) {
            for (int c : model.nodes[pi].children) {
                if (c == nodeIdx) { parentIdx = pi; break; }
            }
            if (parentIdx >= 0) break;
        }
        if (parentIdx < 0) return local;
        return multiply(nodeWorldMatrix(parentIdx), local);
    };

    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        const auto& node = model.nodes[ni];
        if (node.mesh < 0 || node.mesh >= (int)model.meshes.size()) continue;

        Matrix4 worldMat = nodeWorldMatrix(ni);
        const auto& mesh = model.meshes[node.mesh];

        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& prim = mesh.primitives[pi];
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) continue;

            GltfPrimitive outPrim;
            outPrim.materialIndex = prim.material;

            if (prim.material >= 0 && prim.material < (int)model.materials.size()) {
                const auto& mat = model.materials[prim.material];
                outPrim.materialName = mat.name;
                auto& pbr = mat.pbrMetallicRoughness;
                if (pbr.baseColorTexture.index >= 0)
                    outPrim.texturePath = texturePath(model, pbr.baseColorTexture.index, baseDir, tempFiles);
                outPrim.baseColor = {(float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                                     (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3]};
                if (pbr.metallicRoughnessTexture.index >= 0)
                    outPrim.ormPath = texturePath(model, pbr.metallicRoughnessTexture.index, baseDir, tempFiles);
                if (mat.normalTexture.index >= 0)
                    outPrim.normalMapPath = texturePath(model, mat.normalTexture.index, baseDir, tempFiles);
                if (mat.occlusionTexture.index >= 0)
                    outPrim.occlusionPath = texturePath(model, mat.occlusionTexture.index, baseDir, tempFiles);
                if (mat.emissiveTexture.index >= 0)
                    outPrim.emissivePath = texturePath(model, mat.emissiveTexture.index, baseDir, tempFiles);
            }

            const float* posData = accessorData<float>(model, prim.attributes.count("POSITION") ? prim.attributes.at("POSITION") : -1);
            const float* nrmData = accessorData<float>(model, prim.attributes.count("NORMAL") ? prim.attributes.at("NORMAL") : -1);
            const float* texData = accessorData<float>(model, prim.attributes.count("TEXCOORD_0") ? prim.attributes.at("TEXCOORD_0") : -1);
            const float* colData = accessorData<float>(model, prim.attributes.count("COLOR_0") ? prim.attributes.at("COLOR_0") : -1);

            int posAcc = prim.attributes.count("POSITION") ? prim.attributes.at("POSITION") : -1;
            int vertexCount = posAcc >= 0 ? (int)model.accessors[posAcc].count : 0;
            if (vertexCount == 0) continue;

            auto indices = readIndices<unsigned char>(model, prim.indices);
            if (indices.empty()) {
                indices.resize(vertexCount);
                for (int i = 0; i < vertexCount; ++i) indices[i] = (uint32_t)i;
            }

            // Check if this primitive is skinned (has joints)
            bool isSkinned = prim.attributes.count("JOINTS_0") > 0;

            auto& meshData = outPrim.mesh;
            meshData.vertices.resize(vertexCount);
            for (int i = 0; i < vertexCount; ++i) {
                auto& v = meshData.vertices[i];
                float px = posData ? posData[i*3] : 0;
                float py = posData ? posData[i*3+1] : 0;
                float pz = posData ? posData[i*3+2] : 0;
                float nx = nrmData ? nrmData[i*3] : 0;
                float ny = nrmData ? nrmData[i*3+1] : 0;
                float nz = nrmData ? nrmData[i*3+2] : 0;

                if (isSkinned) {
                    // For skinned meshes: store raw local-space vertices.
                    // The GPU joint matrices handle all transformation from bind pose.
                    // Pre-transforming by worldMat here causes parts to fly apart.
                    v.position = {px, py, pz};
                    v.normal = {nx, ny, nz};
                } else {
                    // For non-skinned meshes: bake the node world transform into vertices.
                    Matrix4 normalMat = removeTranslation(worldMat);
                    auto& w = worldMat.values;
                    v.position = {px*w[0] + py*w[4] + pz*w[8] + w[12],
                                  px*w[1] + py*w[5] + pz*w[9] + w[13],
                                  px*w[2] + py*w[6] + pz*w[10] + w[14]};
                    auto& nw = normalMat.values;
                    v.normal = {nx*nw[0] + ny*nw[4] + nz*nw[8],
                                nx*nw[1] + ny*nw[5] + nz*nw[9],
                                nx*nw[2] + ny*nw[6] + nz*nw[10]};
                }
                v.color = colData ? std::array<float,3>{colData[i*3], colData[i*3+1], colData[i*3+2]} : std::array<float,3>{1,1,1};
                v.texCoord = texData ? std::array<float,2>{texData[i*2], 1.0f - texData[i*2+1]} : std::array<float,2>{0,0};
            }

            const uint8_t* jointData = accessorData<uint8_t>(model, prim.attributes.count("JOINTS_0") ? prim.attributes.at("JOINTS_0") : -1);
            const float* weightData = accessorData<float>(model, prim.attributes.count("WEIGHTS_0") ? prim.attributes.at("WEIGHTS_0") : -1);
            int jointAcc = prim.attributes.count("JOINTS_0") ? prim.attributes.at("JOINTS_0") : -1;
            if (jointAcc >= 0) {
                const auto& acc = model.accessors[jointAcc];
                int stride = acc.ByteStride(model.bufferViews[acc.bufferView]);
                if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const uint8_t* jointData8 = accessorData<uint8_t>(model, jointAcc);
                    if (jointData8 && weightData) {
                        outPrim.joints.resize(vertexCount);
                        outPrim.weights.resize(vertexCount);
                        for (int i = 0; i < vertexCount; ++i) {
                            const uint8_t* jointPtr = reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(jointData8) + i * stride);
                            for (int j = 0; j < 4; ++j) {
                                outPrim.joints[i][j] = static_cast<uint32_t>(jointPtr[j]);
                                outPrim.weights[i][j] = weightData[i*4 + j];
                            }
                        }
                    }
                } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* jointData16 = accessorData<uint16_t>(model, jointAcc);
                    if (jointData16 && weightData) {
                        outPrim.joints.resize(vertexCount);
                        outPrim.weights.resize(vertexCount);
                        for (int i = 0; i < vertexCount; ++i) {
                            const uint16_t* jointPtr = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(jointData16) + i * stride);
                            for (int j = 0; j < 4; ++j) {
                                outPrim.joints[i][j] = static_cast<uint32_t>(jointPtr[j]);
                                outPrim.weights[i][j] = weightData[i*4 + j];
                            }
                        }
                    }
                } else {
                    LOG_ERROR("Unsupported JOINTS_0 componentType: {}", acc.componentType);
                }
            }

            meshData.indices = std::move(indices);
            result.primitives.push_back(std::move(outPrim));
        }
    }

    result.nodeParents.resize(model.nodes.size(), -1);
    result.nodeTransforms.resize(model.nodes.size(), Matrix4::identity());
    result.nodeLocalTransforms.resize(model.nodes.size(), Matrix4::identity());
    result.nodeTranslations.resize(model.nodes.size(), {0,0,0});
    result.nodeScales.resize(model.nodes.size(), {1,1,1});
    for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
        for (int c : model.nodes[ni].children) result.nodeParents[c] = ni;
        result.nodeTransforms[ni] = nodeWorldMatrix(ni);
        const auto& node = model.nodes[ni];
        Matrix4 local = Matrix4::identity();
        if (!node.matrix.empty() && node.matrix.size() == 16) {
            for (int i = 0; i < 16; ++i) local.values[i] = (float)node.matrix[i];
        } else {
            if (!node.rotation.empty()) {
                Quaternion q{(float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]};
                Vec3 v{1,0,0}; v = rotateVector(q, v);
                Vec3 v2{0,1,0}; v2 = rotateVector(q, v2);
                Vec3 v3{0,0,1}; v3 = rotateVector(q, v3);
                local.values[0] = v.x; local.values[1] = v.y; local.values[2] = v.z;
                local.values[4] = v2.x; local.values[5] = v2.y; local.values[6] = v2.z;
                local.values[8] = v3.x; local.values[9] = v3.y; local.values[10] = v3.z;
            }
            if (!node.scale.empty()) local = multiply(local, scale((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
            if (!node.translation.empty()) local = multiply(translation((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]), local);
        }
        result.nodeLocalTransforms[ni] = local;
        if (!node.translation.empty()) result.nodeTranslations[ni] = {(float)node.translation[0], (float)node.translation[1], (float)node.translation[2]};
        if (!node.scale.empty()) result.nodeScales[ni] = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};
    }

    // Recompute nodeTransforms from nodeLocalTransforms (consistent with worldMats)
    {
        int ncount = (int)result.nodeTransforms.size();
        std::vector<bool> done(ncount, false);
        for (int pass = 0; pass < ncount; ++pass) {
            bool any = false;
            for (int i = 0; i < ncount; ++i) {
                if (done[i]) continue;
                int p = result.nodeParents[i];
                if (p >= 0 && !done[p]) continue;
                result.nodeTransforms[i] = (p >= 0) ? multiply(result.nodeTransforms[p], result.nodeLocalTransforms[i])
                                                     : result.nodeLocalTransforms[i];
                done[i] = true;
                any = true;
            }
            if (!any) break;
        }
    }

    for (const auto& skin : model.skins) {
        GltfSkin gs;
        gs.joints = skin.joints;
        auto* ibmData = accessorData<float>(model, skin.inverseBindMatrices);
        if (ibmData && skin.inverseBindMatrices >= 0) {
            int acc = skin.inverseBindMatrices;
            int count = (int)model.accessors[acc].count;
            gs.inverseBindMatrices.resize(count);
            for (int i = 0; i < count; ++i) {
                for (int j = 0; j < 16; ++j)
                    gs.inverseBindMatrices[i].values[j] = ibmData[i * 16 + j];
            }
        }
        result.skins.push_back(std::move(gs));
    }

    for (const auto& anim : model.animations) {
        GltfAnimation ga;
        ga.name = anim.name;
        for (const auto& ch : anim.channels) {
            GltfAnimChannel gc;
            gc.nodeIndex = ch.target_node;
            gc.path = ch.target_path;
            const auto& samp = anim.samplers[ch.sampler];
            auto* timeData = accessorData<float>(model, samp.input);
            auto* valueData = accessorData<float>(model, samp.output);
            if (timeData && valueData) {
                int tc = (int)model.accessors[samp.input].count;
                int vc = (int)model.accessors[samp.output].count;
                int numComp = 1;
                int accType = model.accessors[samp.output].type;
                if (accType == TINYGLTF_TYPE_VEC3) numComp = 3;
                else if (accType == TINYGLTF_TYPE_VEC4) numComp = 4;
                gc.times.assign(timeData, timeData + tc);
                gc.values.assign(valueData, valueData + vc * numComp);
            }
            ga.channels.push_back(std::move(gc));
        }
        result.animations.push_back(std::move(ga));
    }

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

    // For animated nodes, fill missing TRS from original node data (not decomposed matrix!)
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
        LOG_ERROR("Failed to load GLTF model ({}): {}", path, e.what());
        return false;
    }

    if (model_.primitives.empty()) {
        LOG_WARN("GLTF model has no primitives: {}", path);
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
            mat.textureName = assets->textureName(texHandle).value_or("");
        }

        MaterialHandle handle = materialManager->add(mat);

        primitives_[i].meshName = meshName;
        primitives_[i].materialHandle = handle;

        // Tworzenie bufora wierzchołków dla wag i indeksów kości (Skinning Vertex Buffer)
        if (!prim.joints.empty()) {
            size_t skinSize = prim.joints.size() * (sizeof(uint32_t) * 4 + sizeof(float) * 4);
            vk::createBuffer(renderer->device(), renderer->physicalDevice(), skinSize,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             primitives_[i].skinVertexBuffer, primitives_[i].skinVertexMemory);

            std::vector<uint8_t> skinData(skinSize);
            for (size_t v = 0; v < prim.joints.size(); ++v) {
                size_t off = v * (sizeof(uint32_t) * 4 + sizeof(float) * 4);
                uint32_t jointIndices[4] = {
                    static_cast<uint32_t>(prim.joints[v][0]),
                    static_cast<uint32_t>(prim.joints[v][1]),
                    static_cast<uint32_t>(prim.joints[v][2]),
                    static_cast<uint32_t>(prim.joints[v][3])
                };
                memcpy(skinData.data() + off, jointIndices, sizeof(uint32_t) * 4);
                memcpy(skinData.data() + off + sizeof(uint32_t) * 4, prim.weights[v].data(), sizeof(float) * 4);
            }

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
        descriptorPool_ = other.descriptorPool_;
        descriptorSetLayout_ = other.descriptorSetLayout_;
        boneDescriptorSet_ = other.boneDescriptorSet_;
        rootEntity_ = other.rootEntity_;
        partEntities_ = std::move(other.partEntities_);
        animTime_ = other.animTime_;

        other.renderer_ = nullptr;
        other.model_ = nullptr;
        other.boneBuffer_ = VK_NULL_HANDLE;
        other.boneMemory_ = VK_NULL_HANDLE;
        other.descriptorPool_ = VK_NULL_HANDLE;
        other.descriptorSetLayout_ = VK_NULL_HANDLE;
        other.boneDescriptorSet_ = VK_NULL_HANDLE;
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

    // Create UBO for bone matrices (max 64 bones)
    constexpr int kMaxBones = 64;
    VkDeviceSize boneSize = kMaxBones * sizeof(Matrix4);
    vk::createBuffer(dev, renderer->physicalDevice(), boneSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     boneBuffer_, boneMemory_);

    // Create descriptor pool for this instance
    VkDescriptorPoolSize bonePoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo bonePoolInfo{};
    bonePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    bonePoolInfo.maxSets = 1;
    bonePoolInfo.poolSizeCount = 1;
    bonePoolInfo.pPoolSizes = &bonePoolSize;
    if (vkCreateDescriptorPool(dev, &bonePoolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding boneBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo boneLayoutInfo{};
    boneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    boneLayoutInfo.bindingCount = 1;
    boneLayoutInfo.pBindings = &boneBinding;
    if (vkCreateDescriptorSetLayout(dev, &boneLayoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo boneAlloc{};
    boneAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    boneAlloc.descriptorPool = descriptorPool_;
    boneAlloc.descriptorSetCount = 1;
    boneAlloc.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(dev, &boneAlloc, &boneDescriptorSet_) != VK_SUCCESS) {
        return false;
    }

    // Update descriptor set
    VkDescriptorBufferInfo boneBufInfo{boneBuffer_, 0, boneSize};
    VkWriteDescriptorSet boneWrite{};
    boneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneWrite.dstSet = boneDescriptorSet_;
    boneWrite.dstBinding = 0;
    boneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boneWrite.descriptorCount = 1;
    boneWrite.pBufferInfo = &boneBufInfo;
    vkUpdateDescriptorSets(dev, 1, &boneWrite, 0, nullptr);

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
        mc->boneDescriptorSet = boneDescriptorSet_;
        
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
    
    if (!jointMats.empty()) {
        size_t copySize = std::min(jointMats.size(), (size_t)64) * sizeof(Matrix4);
        void* mapped;
        vkMapMemory(renderer_->device(), boneMemory_, 0, copySize, 0, &mapped);
        memcpy(mapped, jointMats.data(), copySize);
        vkUnmapMemory(renderer_->device(), boneMemory_);
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
            vkFreeMemory(dev, boneMemory_, nullptr);
            boneMemory_ = VK_NULL_HANDLE;
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
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

