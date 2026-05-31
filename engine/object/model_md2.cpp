#include "core/math.h"
#include "object/model_md2.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace engine3d {
namespace {

constexpr int kMd2Ident = 844121161;
constexpr int kMd2Version = 8;

int readInt(const std::vector<char>& buf, int off) {
    const auto* p = reinterpret_cast<const unsigned char*>(buf.data()) + off;
    return int(p[0]) | (int(p[1]) << 8) | (int(p[2]) << 16) | (int(p[3]) << 24);
}

short readShort(const std::vector<char>& buf, int off) {
    const auto* p = reinterpret_cast<const unsigned char*>(buf.data()) + off;
    return short(p[0]) | (short(p[1]) << 8);
}

float readFloat(const std::vector<char>& buf, int off) {
    int i = readInt(buf, off);
    float v;
    std::memcpy(&v, &i, 4);
    return v;
}

struct Tri {
    unsigned short vi[3];
    unsigned short ti[3];
};

void computeNormals(std::vector<Vec3>& normals, const std::vector<Vec3>& verts,
                    const std::vector<Tri>& tris) {
    normals.assign(verts.size(), {0, 0, 0});
    int vc = (int)verts.size();
    for (const auto& t : tris) {
        int i0 = t.vi[0], i1 = t.vi[2], i2 = t.vi[1];
        if (i0 < 0 || i0 >= vc || i1 < 0 || i1 >= vc || i2 < 0 || i2 >= vc) continue;
        Vec3 a = subtract(verts[i1], verts[i0]);
        Vec3 b = subtract(verts[i2], verts[i0]);
        Vec3 n = normalize(cross(a, b));
        normals[i0] = add(normals[i0], n);
        normals[i1] = add(normals[i1], n);
        normals[i2] = add(normals[i2], n);
    }
    for (auto& n : normals) n = normalize(n);
}

} // namespace

Md2ModelData loadMd2Model(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Cannot open MD2: " + path);

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<char> buf(len);
    fread(buf.data(), 1, len, f);
    fclose(f);

    int ident = readInt(buf, 0);
    if (ident != kMd2Ident) throw std::runtime_error("Not an MD2: " + path);
    int version = readInt(buf, 4);
    if (version != kMd2Version) throw std::runtime_error("Bad MD2 version: " + path);

    int frameSize    = readInt(buf, 16);
    int numVertices  = readInt(buf, 24);
    int numTexCoords = readInt(buf, 28);
    int numTriangles = readInt(buf, 32);
    int numFrames    = readInt(buf, 40);
    int offTex       = readInt(buf, 48);
    int offTris      = readInt(buf, 52);
    int offFrames    = readInt(buf, 56);

    Md2ModelData model;

    model.texCoords.resize(numTexCoords);
    float maxS = 0, maxT = 0;
    for (int i = 0; i < numTexCoords; ++i) {
        float s = (float)readShort(buf, offTex + i * 4);
        float t = (float)readShort(buf, offTex + i * 4 + 2);
        if (s > maxS) maxS = s;
        if (t > maxT) maxT = t;
        model.texCoords[i] = {s, t};
    }
    if (maxS > 0.0001f) for (auto& st : model.texCoords) st[0] /= maxS;
    if (maxT > 0.0001f) for (auto& st : model.texCoords) st[1] = 1.0f - st[1] / maxT;

    std::vector<Tri> tris(numTriangles);
    model.tris.resize(numTriangles);
    for (int i = 0; i < numTriangles; ++i) {
        int o = offTris + i * 12;
        tris[i].vi[0] = (unsigned short)readShort(buf, o);
        tris[i].vi[1] = (unsigned short)readShort(buf, o + 2);
        tris[i].vi[2] = (unsigned short)readShort(buf, o + 4);
        tris[i].ti[0] = (unsigned short)readShort(buf, o + 6);
        tris[i].ti[1] = (unsigned short)readShort(buf, o + 8);
        tris[i].ti[2] = (unsigned short)readShort(buf, o + 10);
        model.tris[i] = {tris[i].vi[0], tris[i].vi[1], tris[i].vi[2],
                         tris[i].ti[0], tris[i].ti[1], tris[i].ti[2]};
    }

    float maxDist = 0;
    model.frames.resize(numFrames);
    for (int f = 0; f < numFrames; ++f) {
        int fo = offFrames + frameSize * f;
        float sx = readFloat(buf, fo);
        float sy = readFloat(buf, fo + 4);
        float sz = readFloat(buf, fo + 8);
        float tx = readFloat(buf, fo + 12);
        float ty = readFloat(buf, fo + 16);
        float tz = readFloat(buf, fo + 20);

        auto& frame = model.frames[f];
        frame.vertices.resize(numVertices);
        int vo = fo + 40;
        for (int i = 0; i < numVertices; ++i) {
            int o = vo + i * 4;
            float x = (float)(unsigned char)buf[o]     * sx + tx;
            float y = (float)(unsigned char)buf[o + 1] * sy + ty;
            float z = (float)(unsigned char)buf[o + 2] * sz + tz;
            frame.vertices[i] = {x, y, z};
            float d = x * x + y * y + z * z;
            if (d > maxDist) maxDist = d;
        }
        computeNormals(frame.normals, frame.vertices, tris);
    }
    model.radius = std::sqrt(maxDist);

    auto& mesh = model.triangles;
    mesh.vertices.resize(numTriangles * 3);
    mesh.indices.resize(numTriangles * 3);
    for (int i = 0; i < numTriangles; ++i) {
        uint32_t b = (uint32_t)i * 3;
        mesh.indices[b + 0] = b + 0;
        mesh.indices[b + 1] = b + 1;
        mesh.indices[b + 2] = b + 2;
    }

    return model;
}

void animateMd2(const Md2ModelData& model, Md2AnimationState& state, float dt, MeshData& outMesh) {
    int nf = (int)model.frames.size();
    if (nf == 0) return;

    state.startFrame = std::max(0, std::min(state.startFrame, nf - 1));
    state.endFrame   = std::max(state.startFrame + 1, std::min(state.endFrame, nf - 1));

    state.interpol += state.speed * dt;

    if (state.currentFrame < 0) { state.currentFrame = state.startFrame; state.nextFrame = state.currentFrame + 1; }

    while (state.interpol >= 1.0f) {
        state.interpol -= 1.0f;
        state.currentFrame++;
        if (state.currentFrame > state.endFrame) state.currentFrame = state.startFrame;
        state.nextFrame = state.currentFrame + 1;
        if (state.nextFrame > state.endFrame) state.nextFrame = state.startFrame;
    }

    const auto& fc = model.frames[state.currentFrame];
    const auto& fn = model.frames[state.nextFrame];
    float t = state.interpol;

    int triCount = (int)model.tris.size();
    outMesh.vertices.resize(triCount * 3);
    outMesh.indices.resize(triCount * 3);
    for (int i = 0; i < triCount; ++i) {
        outMesh.indices[i * 3 + 0] = (uint32_t)(i * 3 + 0);
        outMesh.indices[i * 3 + 1] = (uint32_t)(i * 3 + 1);
        outMesh.indices[i * 3 + 2] = (uint32_t)(i * 3 + 2);
    }

    for (int i = 0; i < triCount; ++i) {
        const auto& tr = model.tris[i];
        int vi0 = tr.vi[0], vi1 = tr.vi[2], vi2 = tr.vi[1];
        int vc = (int)fc.vertices.size();
        if (vi0 < 0 || vi0 >= vc || vi1 < 0 || vi1 >= vc || vi2 < 0 || vi2 >= vc) continue;
        int ti0 = tr.ti[0] < (int)model.texCoords.size() ? tr.ti[0] : 0;
        int ti1 = tr.ti[2] < (int)model.texCoords.size() ? tr.ti[2] : 0;
        int ti2 = tr.ti[1] < (int)model.texCoords.size() ? tr.ti[1] : 0;

        auto lerpV = [&](const Vec3& c, const Vec3& n) -> Vec3 {
            return {c.x + t * (n.x - c.x), c.y + t * (n.y - c.y), c.z + t * (n.z - c.z)};
        };
        auto lerpN = [&](const Vec3& c, const Vec3& n) -> Vec3 {
            Vec3 r{c.x + t * (n.x - c.x), c.y + t * (n.y - c.y), c.z + t * (n.z - c.z)};
            return normalize(r);
        };

        Vec3 v0 = lerpV(fc.vertices[vi0], fn.vertices[vi0]);
        Vec3 v1 = lerpV(fc.vertices[vi1], fn.vertices[vi1]);
        Vec3 v2 = lerpV(fc.vertices[vi2], fn.vertices[vi2]);
        Vec3 n0 = lerpN(fc.normals[vi0], fn.normals[vi0]);
        Vec3 n1 = lerpN(fc.normals[vi1], fn.normals[vi1]);
        Vec3 n2 = lerpN(fc.normals[vi2], fn.normals[vi2]);

        int b = i * 3;
        outMesh.vertices[b + 0] = {{v0.x, v0.y, v0.z}, {n0.x, n0.y, n0.z}, {1, 1, 1}, model.texCoords[ti0]};
        outMesh.vertices[b + 1] = {{v1.x, v1.y, v1.z}, {n1.x, n1.y, n1.z}, {1, 1, 1}, model.texCoords[ti1]};
        outMesh.vertices[b + 2] = {{v2.x, v2.y, v2.z}, {n2.x, n2.y, n2.z}, {1, 1, 1}, model.texCoords[ti2]};
    }
}

} // namespace engine3d
