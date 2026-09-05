#pragma once
// Wavefront .mtl 材质库解析器（纯 CPU、仅标准库，可离线单测）。
// 配套 OBJ 加载：解析 mtllib 引用的 .mtl 文件，并把 usemtl 前面按材质分组为子网格。
//
// 支持的 .mtl 指令：
//   newmtl <name>     开始新材质
//   Ka  r g b         环境色（默认 0.2,0.2,0.2）
//   Kd  r g b         漫反射/反照率色（默认 0.8,0.8,0.8）
//   Ks  r g b         镜面色（默认 0.0,0.0,0.0）
//   Ns  <exp>         镜面高光指数（默认 32）
//   d   <alpha>       透明度（1=不透明）
//   Tr  <alpha>       透明度（1-d）
//   illum <n>         光照模型编号（0..10）
//   map_Kd <path>     漫反射纹理路径
//   map_Ks <path>     镜面纹理路径
//   map_Bump/bump <p> 法线/凹凸纹理路径
// 其余指令忽略。

#include <glm/glm.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace BigHero::Scene
{
struct MtlMaterial
{
    std::string name;         // newmtl 名称
    glm::vec3 ambient{0.2f};  // Ka
    glm::vec3 diffuse{0.8f};  // Kd
    glm::vec3 specular{0.0f}; // Ks
    float shininess = 32.0f;  // Ns（Phong指数）
    float opacity = 1.0f;     // d / (1-Tr)
    int illum = 2;            // illum 光照模型
    std::string mapKd;        // map_Kd 纹理路径
    std::string mapKs;        // map_Ks 纹理路径
    std::string mapBump;      // map_Bump / bump 纹理路径

    [[nodiscard]] bool HasMapKd() const noexcept { return !mapKd.empty(); }
};

// 从内存文本解析 .mtl，返回材质列表（按出现顺序）。
// 空/纯注释文件返回空 vector（不抛异常）。
inline std::vector<MtlMaterial> ParseMtl(const std::string& text)
{
    std::vector<MtlMaterial> materials;
    MtlMaterial* current = nullptr;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        // 去行尾回车
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t nonSpace = line.find_first_not_of(" \t");
        if (nonSpace == std::string::npos)
            continue; // 空行
        const size_t keyStart = nonSpace;
        // 指令关键字到首个空白
        size_t keyEnd = line.find_first_of(" \t", keyStart);
        const std::string key = line.substr(keyStart, keyEnd - keyStart);
        // 参数区（去前导空白）
        std::string args;
        if (keyEnd != std::string::npos)
        {
            const size_t argStart = line.find_first_not_of(" \t", keyEnd);
            args = (argStart != std::string::npos) ? line.substr(argStart) : std::string();
        }

        if (key == "#" || key.empty())
            continue;

        if (key == "newmtl")
        {
            materials.emplace_back();
            current = &materials.back();
            current->name = args;
            continue;
        }

        if (current == nullptr)
            continue; // 材质指令出现在 newmtl 之前，忽略

        std::istringstream as(args);
        auto read3 = [&](glm::vec3& out)
        {
            float x, y, z;
            if (as >> x >> y >> z)
                out = glm::vec3(x, y, z);
        };

        if (key == "Ka")
        {
            read3(current->ambient);
        }
        else if (key == "Kd")
        {
            read3(current->diffuse);
        }
        else if (key == "Ks")
        {
            read3(current->specular);
        }
        else if (key == "Ns")
        {
            as >> current->shininess;
        }
        else if (key == "d")
        {
            as >> current->opacity;
        }
        else if (key == "Tr")
        {
            float t;
            if (as >> t)
                current->opacity = 1.0f - t;
        }
        else if (key == "illum")
        {
            as >> current->illum;
        }
        else if (key == "map_Kd")
        {
            current->mapKd = args;
        }
        else if (key == "map_Ks")
        {
            current->mapKs = args;
        }
        else if (key == "map_Bump" || key == "bump")
        {
            current->mapBump = args;
        }
        // 其余指令忽略
    }
    return materials;
}

// 按 usemtl 前面分组：给定“面所属材质名列表”与材质库，
// 把连续使用同一材质的面段聚合为子网格（SubMesh：材质索引 + 顶点/索引区间）。
struct SubMesh
{
    int32_t materialIndex = -1; // 在材质库中的索引；-1 表示未指定材质
    uint32_t firstIndex = 0;    // 该子网格在网格索引缓冲中的起始偏移
    uint32_t indexCount = 0;    // 该子网格的索引数量（3 的倍数）
};

// faceMaterials：每条面（索引三角形三元组起始）对应的材质名（可为空）。
// 返回连续段聚合的子网格列表。空输入返回空。
inline std::vector<SubMesh> GroupFacesByMaterial(const std::vector<std::string>& faceMaterials,
                                                 const std::vector<MtlMaterial>& library)
{
    std::vector<SubMesh> result;
    auto matIndex = [&](const std::string& name) -> int32_t
    {
        if (name.empty())
            return -1;
        for (size_t i = 0; i < library.size(); ++i)
            if (library[i].name == name)
                return static_cast<int32_t>(i);
        return -1; // 未找到：视为未指定材质
    };

    for (size_t i = 0; i < faceMaterials.size(); ++i)
    {
        const int32_t mi = matIndex(faceMaterials[i]);
        // 与上一个子网格同材质则合并（连续段）
        if (!result.empty() && result.back().materialIndex == mi)
        {
            result.back().indexCount += 3;
        }
        else
        {
            SubMesh sm;
            sm.materialIndex = mi;
            sm.firstIndex = static_cast<uint32_t>(i) * 3;
            sm.indexCount = 3;
            result.push_back(sm);
        }
    }
    return result;
}
} // namespace BigHero::Scene
