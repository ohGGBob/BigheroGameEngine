#pragma once
#include "scene/CubeMesh.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <fstream>
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
    };

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

                // 扇形三角化，保持原始绕序
                for (size_t k = 2; k < face.size(); ++k)
                {
                    mesh.indices.push_back(face[0]);
                    mesh.indices.push_back(face[k - 1]);
                    mesh.indices.push_back(face[k]);
                }
            }
        }

        if (mesh.indices.empty())
            throw std::runtime_error("ObjModel: 文件中没有可用的面数据 " + path);
        return mesh;
    }
}
