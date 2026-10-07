// Application 地形场景翻译单元（U2-T1 接线 v1）：从 Application 主文件分立的
// 地形职责——高度场地貌构建 / 分块网格打包上传 / 顶点色 splat 装箱 / 绘制辅助。
// 地形几何直接以 Scene::Vertex 格式上传（位置/法线/UV/顶点色/切线五属性与主管线一致），
// 主通道用默认推送常量（tiles 反照率 × 顶点色），零 shader / 零描述符改动。
#include "app/Application.h"

#include "core/Log.h"
#include "scene/Terrain.h"

namespace BigHero
{
// 构建并上传地形分块网格（幂等：重复进入 --scene terrain 不再重建）。
void Application::InitTerrainScene()
{
    if (!terrainChunkMeshes_.empty())
        return;

    // 地貌：一座主峰 + 两座次峰 + 一处洼地，两轮全域轻平滑后压一块平坦台地——
    // 笔刷四件套（Stamp/Smooth/FlattenTo）作为场景资产来源（对照笔刷交互面板 v2）。
    terrainHeightmap_.Resize(terrainGridSize_, terrainGridSize_, 2.0f, glm::vec3(-128.0f, 0.0f, -128.0f), 0.5f);
    terrainHeightmap_.Stamp(glm::vec3(-60.0f, 0.0f, -60.0f), 55.0f, 14.0f);
    terrainHeightmap_.Stamp(glm::vec3(55.0f, 0.0f, 45.0f), 38.0f, 9.0f);
    terrainHeightmap_.Stamp(glm::vec3(20.0f, 0.0f, -70.0f), 30.0f, 5.5f);
    terrainHeightmap_.Stamp(glm::vec3(30.0f, 0.0f, 40.0f), 45.0f, -6.0f);
    terrainHeightmap_.Smooth(glm::vec3(0.0f, 0.0f, 0.0f), 120.0f, 0.35f);
    terrainHeightmap_.Smooth(glm::vec3(0.0f, 0.0f, 0.0f), 120.0f, 0.35f);
    terrainHeightmap_.FlattenTo(glm::vec3(-70.0f, 0.0f, 70.0f), 16.0f, 2.0f);

    const int chunksPerAxis = (terrainGridSize_ - 1 + terrainChunkQuads_ - 1) / terrainChunkQuads_;
    terrainChunkMeshes_.resize(static_cast<size_t>(chunksPerAxis) * static_cast<size_t>(chunksPerAxis));
    uint32_t totalTris = 0;
    for (int cz = 0; cz < chunksPerAxis; ++cz)
    {
        for (int cx = 0; cx < chunksPerAxis; ++cx)
        {
            const std::vector<Scene::Vertex> verts = BuildTerrainChunkVertices(cx, cz);
            Scene::TerrainChunkMesh chunkData;
            Scene::BuildTerrainChunkMesh(terrainHeightmap_, chunkData, cx, cz, terrainChunkQuads_, terrainSplatRule_);
            if (verts.empty() || chunkData.indices.empty())
                continue;
            Render::Mesh& mesh = terrainChunkMeshes_[(static_cast<size_t>(cz) * static_cast<size_t>(chunksPerAxis)) +
                                                     static_cast<size_t>(cx)];
            mesh.Create(ctx_, verts, chunkData.indices);
            totalTris += static_cast<uint32_t>(chunkData.indices.size() / 3u);
        }
    }
    LOG_INFO("地形场景构建完成: " << terrainGridSize_ << "×" << terrainGridSize_ << " 顶点场 / " << chunksPerAxis << "×"
                                  << chunksPerAxis << " 块 / " << totalTris << " 三角形");

    // 恒等实例数据：主管线顶点着色器逐实例属性（binding1：模型矩阵/材质）必须绑定，
    // 与地面路径（groundInstances_）同构。探针辐照度保持 0（v1：地形不参与 LightProbe 包装，
    // 环境光走 lightParams_.ambient 常量通道）。
    Render::InstanceData terrainId{};
    terrainId.tint = glm::vec4(1.0f);
    terrainId.metallic = 0.0f;
    terrainId.roughness = 1.0f;
    terrainInstances_.Upload(ctx_, &terrainId, 1);
}

// 打包第 (cx, cz) 块的顶点（与 Scene::BuildTerrainChunkMesh 同一遍历序：z 外循环、
// x 内循环、行长度为 x1-x0+1——索引缓冲可直接复用核心构建器的输出）。
std::vector<Scene::Vertex> Application::BuildTerrainChunkVertices(int cx, int cz) const
{
    std::vector<Scene::Vertex> verts;
    const int nx = terrainHeightmap_.Nx();
    const int nz = terrainHeightmap_.Nz();
    if (nx < 2 || nz < 2)
        return verts;
    const int x0 = std::min(cx * terrainChunkQuads_, nx - 1);
    const int z0 = std::min(cz * terrainChunkQuads_, nz - 1);
    const int x1 = std::min(x0 + terrainChunkQuads_, nx - 1);
    const int z1 = std::min(z0 + terrainChunkQuads_, nz - 1);
    const int vw = x1 - x0 + 1;
    const int vh = z1 - z0 + 1;
    const float cell = terrainHeightmap_.CellSize();
    constexpr float kUvTiles = 16.0f;

    verts.reserve(static_cast<size_t>(vw) * static_cast<size_t>(vh));
    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const float h = terrainHeightmap_.Height(x, z);
            const int xp = std::min(x + 1, nx - 1);
            const int xm = std::max(x - 1, 0);
            const float dHdx = (terrainHeightmap_.Height(xp, z) - terrainHeightmap_.Height(xm, z)) /
                               (static_cast<float>(xp - xm) * cell);

            Scene::Vertex v{};
            v.pos = terrainHeightmap_.VertexWorld(x, z);
            v.normal = terrainHeightmap_.NormalAt(x, z);
            v.uv = glm::vec2(static_cast<float>(x) / kUvTiles, static_cast<float>(z) / kUvTiles);
            const glm::vec4 w = Scene::TerrainSplatWeights(h, terrainHeightmap_.SlopeAt(x, z), terrainSplatRule_);
            v.color = w.x * terrainSplatRule_.grassColor + w.y * terrainSplatRule_.rockColor +
                      w.z * terrainSplatRule_.sandColor + w.w * terrainSplatRule_.snowColor;
            // 切线：沿 +X 的坡度切线（法线贴图 TBN 用；tiles_normal 在坡面上依赖正确切线）
            const float tl = std::sqrt(1.0f + (dHdx * dHdx));
            v.tangent = glm::vec3(1.0f / tl, dHdx / tl, 0.0f);
            verts.push_back(v);
        }
    }
    return verts;
}

// 绘制全部分块（调用方已绑定管线 / 描述符 / 推送常量；恒等模型矩阵 = 顶点即世界坐标）。
// 与地面同构：绑定恒等实例缓冲后 DrawIndexedInstanced(1)——顶点着色器 binding1 逐实例
// 属性（模型矩阵/材质）在主管线是必需绑定项。
void Application::DrawTerrainChunks(VkCommandBuffer cmd)
{
    if (terrainChunkMeshes_.empty())
        return;
    terrainInstances_.Bind(cmd);
    for (Render::Mesh& mesh : terrainChunkMeshes_)
    {
        mesh.Bind(cmd);
        mesh.DrawIndexedInstanced(cmd, mesh.IndexCount(), 0, 1);
    }
}
} // namespace BigHero