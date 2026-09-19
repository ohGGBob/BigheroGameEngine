#pragma once
// 极简 glTF 2.0 加载器（纯 CPU、可离线单测）—— 公共数据结构与 API 声明。
// 实现体在 GltfLoader.cpp；自带过的精简 JSON 解析器已独立为 core/Json.h（BigHero::Core）。
//
// 定位：与 ObjModel 并列的网格几何加载入口，聚焦几何数据（顶点/索引/子网格）。
// 不依赖外部 JSON 库，支持 glTF 2.0 静态网格核心字段：
//   - asset.version（校验 2.x）
//   - buffers[]（data URI base64 内嵌 或 相对外部 .bin 文件）
//   - bufferViews[]（byteOffset/byteLength/byteStride，索引到具体 buffer）
//   - accessors[]（componentType/type/count/byteOffset）
//   - meshes[].primitives[]（attributes: POSITION/NORMAL/TEXCOORD_0/COLOR_0/TANGENT，
//     indices，mode=4 TRIANGLES）
//   - materials[]（pbrMetallicRoughness.baseColorFactor）
//
// 语义约定：
//   - 仅支持 mode=4（TRIANGLES）；稀疏 accessor（sparse）暂不支持（遇到报错）。
//   - 支持静态网格 + 骨骼蒙皮数据提取（nodes/skins/JOINTS_0/WEIGHTS_0/逆绑定矩阵）。
//   - 支持动画数据提取（animations[]：通道 target.node/path + 采样器 input/output/interpolation），
//     供 AnimationPlayer（Animation.h）做 LINEAR/STEP 插值采样驱动骨骼动画。
//   - 每个 primitive 的顶点按 POSITION 索引去重后追加到模型，并记录子网格区间。
//   - 缺失法线回退 +Y、缺失 UV 用 0、缺失顶点色用 1（白）、缺失切线用 +X，与 OBJ 加载器一致。
//   - base64 解码支持标准与 URL-safe 两种字符集。

#include "scene/CubeMesh.h" // Vertex
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace BigHero::Scene
{
// ---- glTF 子网格（对应 primitive） ----
struct GltfPrimitive
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t materialIndex = -1; // -1 表示未指定材质
};

// ---- glTF 材质（抽取 PBR 基础色/金属度/粗糙度 + 贴图引用） ----
struct GltfMaterial
{
    std::string name;
    glm::vec4 baseColorFactor = glm::vec4(1.0f); // RGBA
    float metallicFactor = 1.0f;                 // glTF 默认 1.0
    float roughnessFactor = 1.0f;                // glTF 默认 1.0

    // 透明与自发光（glTF 2.0 core）：
    //   alphaMode：0=OPAQUE（默认） 1=MASK（alphaCutoff 裁剪） 2=BLEND（Alpha 混合）
    //   emissiveFactor × emissiveTexture 加到最终颜色（不受光照调制）
    int alphaMode = 0;
    float alphaCutoff = 0.5f;
    glm::vec3 emissiveFactor = glm::vec3(0.0f);

    // 贴图引用：解析后的 image URI（材质 texture index -> textures[].source
    // -> images[].uri）。空串 = 未引用，或为内嵌 bufferView（GLB，暂不解引用）。
    std::string baseColorTextureUri;
    std::string metallicRoughnessTextureUri;
    std::string normalTextureUri;
    std::string emissiveTextureUri;
};

// ---- glTF 动画通道（target node + path） ----
struct GltfAnimationChannel
{
    int32_t targetNode = -1; // 目标节点下标
    std::string path;        // "translation" | "rotation" | "scale"
    int sampler = -1;        // 采样器下标
};

// ---- glTF 动画采样器（时间 -> 关键帧值） ----
struct GltfAnimationSampler
{
    std::string interpolation = "LINEAR"; // LINEAR | STEP | CUBICSPLINE
    std::vector<float> times;             // 输入（SCALAR FLOAT）
    std::vector<glm::vec4> values;        // 输出（VEC3 平移/缩放，VEC4 旋转）
};

// ---- glTF 动画 ----
struct GltfAnimation
{
    std::string name;
    std::vector<GltfAnimationChannel> channels;
    std::vector<GltfAnimationSampler> samplers;
};

// ---- glTF 模型结果 ----
struct GltfModel
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfMaterial> materials;

    // ---- 贴图引用表（可选，无 images/textures 时为空） ----
    std::vector<std::string> imageUris;  // images[].uri（内嵌 bufferView 为空串）
    std::vector<int32_t> textureSources; // textures[].source -> image 索引，-1 = 缺失

    // ---- 骨骼/蒙皮（可选，无 skin 时为空，向后兼容） ----
    // 节点层级（扁平数组，index 即 node index）
    std::vector<int32_t> nodeParents;        // 父节点索引，-1=根
    std::vector<glm::vec3> nodeTranslations; // 局部平移
    std::vector<glm::quat> nodeRotations;    // 局部旋转（四元数）
    std::vector<glm::vec3> nodeScales;       // 局部缩放

    // 皮肤：关节（节点索引）+ 逆绑定矩阵（每个关节一个）
    std::vector<int32_t> jointNodes;            // 关节对应的节点索引
    std::vector<glm::mat4> inverseBindMatrices; // 与 jointNodes 一一对应

    // 逐顶点蒙皮权重（可选，与 vertices 一一对应，各至多 4 关节）
    std::vector<glm::u8vec4> jointIndices; // 每顶点 0~4 个关节（u8，值为 jointNodes 下标）
    std::vector<glm::vec4> jointWeights;   // 每顶点 0~4 个权重（和为1）

    // 动画（可选，无 animations 时为空）
    std::vector<GltfAnimation> animations;
};

// 从内存 glTF JSON 文档加载（支持 data URI base64 内嵌缓冲）
GltfModel LoadGltfFromMemory(const std::string& jsonText);

// 从文件加载 glTF（支持内嵌 base64 data URI 的 buffer）。
// 外部相对 .bin buffer：为保持简单与健壮，暂不支持，建议导出为内嵌或后续接入时再扩展。
GltfModel LoadGltf(const std::string& path);
} // namespace BigHero::Scene
