#pragma once
#include "scene/CubeMesh.h"
#include "scene/MtlMaterial.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace BigHero::Scene
{
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // 材质（可选）：mtllib 引用的材质库 + 按 usemtl 聚合的子网格区间。
    // 未使用材质时二者为空，加载行为与旧版完全一致。
    std::vector<MtlMaterial> materials;
    std::vector<SubMesh> subMeshes;
};

// Lengyel法从几何与UV累计顶点切线；退化UV三角形跳过，无切线数据时回退+X
inline void ComputeTangents(MeshData& mesh)
{
    std::vector<glm::vec3> accumulated(mesh.vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint32_t i0 = mesh.indices[i];
        const uint32_t i1 = mesh.indices[i + 1];
        const uint32_t i2 = mesh.indices[i + 2];
        const Vertex& v0 = mesh.vertices[i0];
        const Vertex& v1 = mesh.vertices[i1];
        const Vertex& v2 = mesh.vertices[i2];

        const glm::vec3 e1 = v1.pos - v0.pos;
        const glm::vec3 e2 = v2.pos - v0.pos;
        const glm::vec2 duv1 = v1.uv - v0.uv;
        const glm::vec2 duv2 = v2.uv - v0.uv;

        const float det = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::fabs(det) < 1e-8f)
            continue; // UV退化，切线无意义

        const float invDet = 1.0f / det;
        const glm::vec3 tangent = (e1 * duv2.y - e2 * duv1.x) * invDet;

        accumulated[i0] += tangent;
        accumulated[i1] += tangent;
        accumulated[i2] += tangent;
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const glm::vec3& normal = mesh.vertices[i].normal;
        glm::vec3 t = accumulated[i];
        // Gram-Schmidt正交化，剔除法线分量；退化时回退+X
        t -= normal * glm::dot(normal, t);
        mesh.vertices[i].tangent = (glm::dot(t, t) > 1e-12f) ? glm::normalize(t) : glm::vec3(1.0f, 0.0f, 0.0f);
    }
}

// 极简OBJ加载器：支持 v/vt/vn 与多边形面（扇形三角化），负索引按OBJ规范相对引用
// 顶点色统一置白，实际颜色由实例tint控制；重复的"v/vt/vn"角点组合按字符串键去重
inline MeshData LoadObjModel(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("ObjModel: 无法打开 " + path);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;
    MeshData mesh;
    std::unordered_map<std::string, uint32_t> uniqueVertices;

    // 材质：mtllib 收集的材质库文件 + 当前材质名（usemtl）+ 每三角形所属材质名
    std::vector<std::string> mtlLibs;
    std::vector<std::string> faceMaterials; // 每 3 个索引一条三角形记录
    std::string currentMaterial;

    // 解析单个角点 "v[/vt][/vn]"，负索引按相对引用换算
    const auto appendCorner = [&](const std::string& corner) -> uint32_t
    {
        const auto it = uniqueVertices.find(corner);
        if (it != uniqueVertices.end())
            return it->second;

        long long vi = 0, vti = 0, vni = 0;
        const size_t slash1 = corner.find('/');
        if (slash1 == std::string::npos)
        {
            vi = std::atoll(corner.c_str());
        }
        else
        {
            vi = std::atoll(corner.substr(0, slash1).c_str());
            const size_t slash2 = corner.find('/', slash1 + 1);
            if (slash2 == std::string::npos)
            {
                vti = std::atoll(corner.substr(slash1 + 1).c_str());
            }
            else
            {
                if (slash2 > slash1 + 1)
                    vti = std::atoll(corner.substr(slash1 + 1, slash2 - slash1 - 1).c_str());
                vni = std::atoll(corner.substr(slash2 + 1).c_str());
            }
        }

        const auto resolve = [](long long index, size_t count) -> size_t
        {
            if (index > 0)
                return static_cast<size_t>(index - 1);
            return static_cast<size_t>(static_cast<long long>(count) + index);
        };

        Vertex vert{};
        vert.pos = positions.at(resolve(vi, positions.size()));
        vert.uv = (vti != 0) ? uvs.at(resolve(vti, uvs.size())) : glm::vec2(0.0f);
        vert.normal = (vni != 0) ? normals.at(resolve(vni, normals.size())) : glm::vec3(0.0f, 1.0f, 0.0f);
        vert.color = glm::vec3(1.0f);

        const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(vert);
        uniqueVertices.emplace(corner, index);
        return index;
    };

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v")
        {
            glm::vec3 p{};
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (tag == "vt")
        {
            glm::vec2 t{};
            ss >> t.x >> t.y;
            uvs.push_back(t);
        }
        else if (tag == "vn")
        {
            glm::vec3 n{};
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (tag == "f")
        {
            std::vector<uint32_t> face;
            std::string corner;
            while (ss >> corner)
                face.push_back(appendCorner(corner));

            // 扇形三角化，保持原始绕序；每个三角形记录当前材质名
            for (size_t k = 2; k < face.size(); ++k)
            {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[k - 1]);
                mesh.indices.push_back(face[k]);
                faceMaterials.push_back(currentMaterial);
            }
        }
        else if (tag == "usemtl")
        {
            // 后续面切换到该材质
            std::string mtlName;
            std::getline(ss, mtlName);
            // 去首尾空白
            const auto trim = [](std::string s)
            {
                const auto b = s.find_first_not_of(" \t\r");
                if (b == std::string::npos)
                    return std::string();
                const auto e = s.find_last_not_of(" \t\r");
                return s.substr(b, e - b + 1);
            };
            currentMaterial = trim(mtlName);
        }
        else if (tag == "mtllib")
        {
            // 收集材质库文件名（可能多个，空格分隔）
            std::string lib;
            while (ss >> lib)
                mtlLibs.push_back(lib);
        }
    }

    if (mesh.indices.empty())
        throw std::runtime_error("ObjModel: 文件中没有可用的面数据 " + path);

    // 解析 mtllib 引用的材质库；任一可读则合并进材质库，缺失/不可读时忽略（优雅降级）
    for (const std::string& lib : mtlLibs)
    {
        std::string mtlPath = lib;
        // 相对路径：优先相对于 OBJ 所在目录
        const size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos && lib.find('/') == std::string::npos && lib.find('\\') == std::string::npos)
            mtlPath = path.substr(0, slash + 1) + lib;
        std::ifstream mf(mtlPath);
        if (!mf.is_open())
            continue;
        std::stringstream buffer;
        buffer << mf.rdbuf();
        const std::vector<MtlMaterial> parsed = ParseMtl(buffer.str());
        mesh.materials.insert(mesh.materials.end(), parsed.begin(), parsed.end());
    }

    // 按 usemtl 前面聚合子网格（无材质则 subMeshes 为空）
    mesh.subMeshes = GroupFacesByMaterial(faceMaterials, mesh.materials);

    ComputeTangents(mesh);
    return mesh;
}
} // namespace BigHero::Scene
