#pragma once
// 地形核心（Terrain，U2-T1 的可离线部分）：高度场数据 + 双线性采样 + 笔刷编辑 +
// 分块网格化（解析法线 / 块边界缝合）+ 四通道 splat 混合规则 + 纯文本序列化。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   U2-T1（地形：高度图 / splat / 植被实例化 / 笔刷）的商业化定义里，真正需要在
//   GPU/窗口里验证的是渲染接线与笔刷交互体验；而「高度场本身」是一套纯数据与采样
//   数学——本模块即该核心（对照 U2-L1/L2 同样「先落地核心、后接线」的节奏）。
//   参照 VoxelWorld 先例：区块化重建 + 脏矩形增量，运行时只重建被编辑波及的块。
//
// 高度场约定：
//   规则网格 nx × nz 个**顶点**（各维 >= 2），行主序 heights_[z*nx + x]，
//   顶点世界坐标 = origin + (x*cellSize, h, z*cellSize)。世界→网格按 x/z 平面
//   双线性采样；网格内坐标越界按边界钳制（clamp-to-edge，全程良定义）。
//   法线取高度场隐式曲面 F = h(x,z) - y = 0 的梯度：normal = normalize(-dh/dx, 1, -dh/dz)，
//   中心差分（边界退化为单侧差分）。对线性斜坡该式给出精确解析法线（单测锁定）。
//
// 编辑契约：
//   笔刷抬升/压平/平滑/整平全部经内部 SetHeight 统一走「脏矩形」登记；编辑只影响
//   矩形内高度，矩形外逐位不变。抬升/压平峰值恰为 maxRise（t=0 处衰减 = 1）、
//   沿半径方向单调衰减、半径外不变——笔刷手感由这三条不变量保证。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/FileSystemUtils.h"

namespace BigHero::Scene
{
// 高度场：查询 / 编辑 / 脏区域 / 序列化。
class TerrainHeightmap
{
  public:
    // 重建网格（失败保留原状态）。初始高度统一 initialHeight。
    bool Resize(int nx, int nz, float cellSize, const glm::vec3& origin, float initialHeight = 0.0f)
    {
        if (nx < 2 || nz < 2 || !(cellSize > 0.0f))
            return false;
        nx_ = nx;
        nz_ = nz;
        cellSize_ = cellSize;
        origin_ = origin;
        heights_.assign(static_cast<size_t>(nx_) * static_cast<size_t>(nz_), initialHeight);
        dirtyMin_.x = nx_;
        dirtyMin_.y = nz_;
        dirtyMax_.x = -1;
        dirtyMax_.y = -1;
        return true;
    }

    [[nodiscard]] int Nx() const { return nx_; }
    [[nodiscard]] int Nz() const { return nz_; }
    [[nodiscard]] float CellSize() const { return cellSize_; }
    [[nodiscard]] const glm::vec3& Origin() const { return origin_; }

    // ---- 查询 ----
    // 网格内精确取高（x/z 必须在 [0, N) 内，契约由调用方保证；调试用 ClampedHeight）。
    [[nodiscard]] float Height(int x, int z) const
    {
        return heights_[static_cast<size_t>(z) * static_cast<size_t>(nx_) + static_cast<size_t>(x)];
    }
    [[nodiscard]] float ClampedHeight(int x, int z) const { return Height(ClampX(x), ClampZ(z)); }
    [[nodiscard]] glm::vec3 VertexWorld(int x, int z) const
    {
        return glm::vec3(origin_.x + static_cast<float>(x) * cellSize_, Height(x, z),
                         origin_.z + static_cast<float>(z) * cellSize_);
    }

    // 世界坐标 → 网格连续坐标（x/z 平面），越界按边界钳制。
    void WorldToGrid(const glm::vec3& world, float& gx, float& gz) const
    {
        const float fx = (world.x - origin_.x) / cellSize_;
        const float fz = (world.z - origin_.z) / cellSize_;
        gx = std::clamp(fx, 0.0f, static_cast<float>(nx_ - 1));
        gz = std::clamp(fz, 0.0f, static_cast<float>(nz_ - 1));
    }

    // 双线性采样（x/z 平面）。
    [[nodiscard]] float SampleWorld(const glm::vec3& world) const
    {
        float gx = 0.0f;
        float gz = 0.0f;
        WorldToGrid(world, gx, gz);
        const int x0 = static_cast<int>(std::floor(gx));
        const int z0 = static_cast<int>(std::floor(gz));
        const int x1 = std::min(x0 + 1, nx_ - 1);
        const int z1 = std::min(z0 + 1, nz_ - 1);
        const float fx = gx - static_cast<float>(x0);
        const float fz = gz - static_cast<float>(z0);
        const float h00 = Height(x0, z0);
        const float h10 = Height(x1, z0);
        const float h01 = Height(x0, z1);
        const float h11 = Height(x1, z1);
        return h00 * (1.0f - fx) * (1.0f - fz) + h10 * fx * (1.0f - fz) + h01 * (1.0f - fx) * fz + h11 * fx * fz;
    }

    // 解析法线（隐式曲面梯度，中心差分；边界退化为单侧差分）。
    [[nodiscard]] glm::vec3 NormalAt(int x, int z) const
    {
        const int xm = (x > 0) ? x - 1 : x;
        const int xp = (x < nx_ - 1) ? x + 1 : x;
        const int zm = (z > 0) ? z - 1 : z;
        const int zp = (z < nz_ - 1) ? z + 1 : z;
        const float stepU = (xp == xm) ? 1.0f : static_cast<float>(xp - xm); // 防御（已被 xm/xp 保证 >=1）
        const float dHdx = (Height(xp, z) - Height(xm, z)) / (stepU * cellSize_);
        const float stepV = (zp == zm) ? 1.0f : static_cast<float>(zp - zm);
        const float dHdz = (Height(x, zp) - Height(x, zm)) / (stepV * cellSize_);
        return glm::normalize(glm::vec3(-dHdx, 1.0f, -dHdz));
    }

    // 坡度 = 高度对水平距离的变化率 = sqrt((dh/dx)² + (dh/dz)²)（中心差分）。
    [[nodiscard]] float SlopeAt(int x, int z) const
    {
        const int xm = (x > 0) ? x - 1 : x;
        const int xp = (x < nx_ - 1) ? x + 1 : x;
        const int zm = (z > 0) ? z - 1 : z;
        const int zp = (z < nz_ - 1) ? z + 1 : z;
        const float dHdx = (Height(xp, z) - Height(xm, z)) / (static_cast<float>(xp - xm) * cellSize_);
        const float dHdz = (Height(x, zp) - Height(x, zm)) / (static_cast<float>(zp - zm) * cellSize_);
        return std::sqrt(dHdx * dHdx + dHdz * dHdz);
    }

    [[nodiscard]] float MinHeight() const { return *std::min_element(heights_.begin(), heights_.end()); }
    [[nodiscard]] float MaxHeight() const { return *std::max_element(heights_.begin(), heights_.end()); }

    // ---- 编辑（经 SetHeight 登记脏矩形） ----
    // 直写某格高度（高度图导入/测试用）：下标越界按边界钳制，仅登记脏区域。
    void SetHeight(int x, int z, float h)
    {
        const int cx = ClampX(x);
        const int cz = ClampZ(z);
        heights_[CellIndex(cx, cz)] = h;
        MarkDirty(cx, cx, cz, cz);
    }

    // 抬升/压平笔刷：centerWorld 为笔刷中心（仅 x/z 使用），radiusWorld 为半径、
    // maxRise 为笔刷中心抬升量（压平传负值）。峰值衰减 = (1 - (d/r)²)²（d<=r），
    // 半径外不变。
    void Stamp(const glm::vec3& centerWorld, float radiusWorld, float maxRise)
    {
        if (!(radiusWorld > 0.0f))
            return;
        float gx = 0.0f;
        float gz = 0.0f;
        WorldToGrid(centerWorld, gx, gz);
        const int reach = static_cast<int>(std::ceil(radiusWorld / cellSize_)) + 1;
        const int x0 = std::max(0, static_cast<int>(std::floor(gx)) - reach);
        const int x1 = std::min(nx_ - 1, static_cast<int>(std::ceil(gx)) + reach);
        const int z0 = std::max(0, static_cast<int>(std::floor(gz)) - reach);
        const int z1 = std::min(nz_ - 1, static_cast<int>(std::ceil(gz)) + reach);
        const float r2 = radiusWorld * radiusWorld;
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec3 v = VertexWorld(x, z);
                const float dx = v.x - centerWorld.x;
                const float dz = v.z - centerWorld.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 > r2)
                    continue;
                const float t = std::sqrt(d2) / radiusWorld; // ∈ [0,1]
                const float fall = (1.0f - t * t) * (1.0f - t * t);
                SetHeight(x, z, Height(x, z) + maxRise * fall);
            }
        }
    }

    // 平滑：单趟四邻域平均逼近，strength ∈ [0,1]（1 = 一步全平均）。
    void Smooth(const glm::vec3& centerWorld, float radiusWorld, float strength)
    {
        if (!(radiusWorld > 0.0f))
            return;
        const float s = std::clamp(strength, 0.0f, 1.0f);
        float gx = 0.0f;
        float gz = 0.0f;
        WorldToGrid(centerWorld, gx, gz);
        const int reach = static_cast<int>(std::ceil(radiusWorld / cellSize_)) + 1;
        const int x0 = std::max(0, static_cast<int>(std::floor(gx)) - reach);
        const int x1 = std::min(nx_ - 1, static_cast<int>(std::ceil(gx)) + reach);
        const int z0 = std::max(0, static_cast<int>(std::floor(gz)) - reach);
        const int z1 = std::min(nz_ - 1, static_cast<int>(std::ceil(gz)) + reach);
        const float r2 = radiusWorld * radiusWorld;

        std::vector<float> next = heights_; // 同步更新（不逐点交叉污染）
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec3 v = VertexWorld(x, z);
                const float dx = v.x - centerWorld.x;
                const float dz = v.z - centerWorld.z;
                if (dx * dx + dz * dz > r2)
                    continue;
                const float sum = Height(std::max(0, x - 1), z) + Height(std::min(nx_ - 1, x + 1), z) +
                                  Height(x, std::max(0, z - 1)) + Height(x, std::min(nz_ - 1, z + 1));
                next[static_cast<size_t>(z) * static_cast<size_t>(nx_) + static_cast<size_t>(x)] =
                    Height(x, z) * (1.0f - s) + (sum * 0.25f) * s;
            }
        }
        heights_.swap(next);
        MarkDirty(x0, x1, z0, z1);
    }

    // 整平：半径内高度全部设为 targetHeight（硬边缘）。
    void FlattenTo(const glm::vec3& centerWorld, float radiusWorld, float targetHeight)
    {
        if (!(radiusWorld > 0.0f))
            return;
        float gx = 0.0f;
        float gz = 0.0f;
        WorldToGrid(centerWorld, gx, gz);
        const int reach = static_cast<int>(std::ceil(radiusWorld / cellSize_)) + 1;
        const int x0 = std::max(0, static_cast<int>(std::floor(gx)) - reach);
        const int x1 = std::min(nx_ - 1, static_cast<int>(std::ceil(gx)) + reach);
        const int z0 = std::max(0, static_cast<int>(std::floor(gz)) - reach);
        const int z1 = std::min(nz_ - 1, static_cast<int>(std::ceil(gz)) + reach);
        const float r2 = radiusWorld * radiusWorld;
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const glm::vec3 v = VertexWorld(x, z);
                const float dx = v.x - centerWorld.x;
                const float dz = v.z - centerWorld.z;
                if (dx * dx + dz * dz <= r2)
                    SetHeight(x, z, targetHeight);
            }
        }
    }

    // ---- 脏区域（分块重建用） ----
    [[nodiscard]] bool ChunkDirty(int cx, int cz, int chunkQuads) const
    {
        if (dirtyMin_.x > dirtyMax_.x)
            return false; // 干净
        const int x0 = cx * chunkQuads;
        const int z0 = cz * chunkQuads;
        const int x1 = x0 + chunkQuads;
        const int z1 = z0 + chunkQuads;
        return x1 >= dirtyMin_.x && x0 <= dirtyMax_.x && z1 >= dirtyMin_.y && z0 <= dirtyMax_.y;
    }
    void ClearDirty()
    {
        dirtyMin_ = glm::ivec2(nx_, nz_);
        dirtyMax_ = glm::ivec2(-1, -1);
    }

    // ---- 序列化（纯文本快照，可重现） ----
    friend bool SaveTerrainHeightmap(const TerrainHeightmap&, const std::string&);
    friend bool LoadTerrainHeightmap(const std::string&, TerrainHeightmap&);

  private:
    int ClampX(int x) const { return std::clamp(x, 0, nx_ - 1); }
    int ClampZ(int z) const { return std::clamp(z, 0, nz_ - 1); }
    size_t CellIndex(int x, int z) const
    {
        return static_cast<size_t>(z) * static_cast<size_t>(nx_) + static_cast<size_t>(x);
    }

    void MarkDirty(int x0, int x1, int z0, int z1)
    {
        dirtyMin_.x = std::min(dirtyMin_.x, x0);
        dirtyMin_.y = std::min(dirtyMin_.y, z0);
        dirtyMax_.x = std::max(dirtyMax_.x, x1);
        dirtyMax_.y = std::max(dirtyMax_.y, z1);
    }

    int nx_ = 0;
    int nz_ = 0;
    float cellSize_ = 1.0f;
    glm::vec3 origin_{0.0f};
    std::vector<float> heights_;
    glm::ivec2 dirtyMin_{0, 0};
    glm::ivec2 dirtyMax_{-1, -1};
};

// splat 混合规则（四通道：grass/rock/sand/snow，层次混合，权重恒和为 1）。
struct TerrainSplatRule
{
    glm::vec3 grassColor{0.34f, 0.55f, 0.24f};
    glm::vec3 rockColor{0.45f, 0.44f, 0.42f};
    glm::vec3 sandColor{0.78f, 0.72f, 0.50f};
    glm::vec3 snowColor{0.92f, 0.94f, 0.96f};
    float rockSlopeStart = 0.6f; // 坡度超过此值开始岩石化
    float rockSlopeEnd = 1.2f;   // 坡度超过此值完全岩石
    float sandLevel = 0.35f;     // 低于此高度为暖沙地带
    float sandBlend = 1.5f;      // 沙→草过渡带高度
    float snowLevel = 8.0f;      // 高于此高度为雪
    float snowBlend = 3.0f;      // 草→雪过渡带高度
};

namespace detail
{
inline float SmoothStep01(float lo, float hi, float v)
{
    if (hi <= lo)
        return v >= hi ? 1.0f : 0.0f;
    const float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace detail

// 层次混合：雪带 > 沙带 > 草/岩按坡度过渡；构造保证权重组非负且和为 1。
[[nodiscard]] inline glm::vec4 TerrainSplatWeights(float height, float slope, const TerrainSplatRule& r)
{
    const float wSnow = detail::SmoothStep01(r.snowLevel - r.snowBlend, r.snowLevel, height);
    const float wSand = (1.0f - wSnow) * (1.0f - detail::SmoothStep01(r.sandLevel, r.sandLevel + r.sandBlend, height));
    const float wRock = (1.0f - wSnow - wSand) * detail::SmoothStep01(r.rockSlopeStart, r.rockSlopeEnd, slope);
    const float wGrass = 1.0f - wSnow - wSand - wRock;
    return glm::vec4(wGrass, wRock, wSand, wSnow);
}

// 分块网格：顶点（位置/法线/UV/splat 权重）+ 三角形索引（CCW，从 +Y 看逆时针）。
struct TerrainChunkMesh
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> splatWeights;
    std::vector<uint32_t> indices;
};

// 构建第 (cx, cz) 块的网格（chunkQuads×chunkQuads 个方格，顶点数 ≤ (q+1)²，
// 边界块按地形边界截断）。块间共享同一高度场网格顶点 → 邻块边界位置逐位一致
// （缝合无需后期处理）。UV = 网格坐标 / 16（每 16 米重复一次）。
inline void BuildTerrainChunkMesh(const TerrainHeightmap& hm, TerrainChunkMesh& out, int cx, int cz, int chunkQuads,
                                  const TerrainSplatRule& rule)
{
    out = TerrainChunkMesh{};
    if (chunkQuads < 1)
        return;
    const int x0 = std::min(cx * chunkQuads, hm.Nx() - 1);
    const int z0 = std::min(cz * chunkQuads, hm.Nz() - 1);
    const int x1 = std::min(x0 + chunkQuads, hm.Nx() - 1);
    const int z1 = std::min(z0 + chunkQuads, hm.Nz() - 1);
    const int vw = x1 - x0 + 1;
    const int vh = z1 - z0 + 1;

    constexpr float kUvTiles = 16.0f;
    out.positions.reserve(static_cast<size_t>(vw) * static_cast<size_t>(vh));
    out.normals.reserve(out.positions.capacity());
    out.uvs.reserve(out.positions.capacity());
    out.splatWeights.reserve(out.positions.capacity());
    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            out.positions.push_back(hm.VertexWorld(x, z));
            out.normals.push_back(hm.NormalAt(x, z));
            out.uvs.emplace_back(static_cast<float>(x) / kUvTiles, static_cast<float>(z) / kUvTiles);
            out.splatWeights.push_back(TerrainSplatWeights(hm.Height(x, z), hm.SlopeAt(x, z), rule));
        }
    }
    out.indices.reserve(static_cast<size_t>(x1 - x0) * static_cast<size_t>(z1 - z0) * 6u);
    for (int z = z0; z < z1; ++z)
    {
        for (int x = x0; x < x1; ++x)
        {
            const uint32_t i00 = static_cast<uint32_t>((z - z0) * vw + (x - x0));
            const uint32_t i10 = i00 + 1u;
            const uint32_t i01 = i00 + static_cast<uint32_t>(vw);
            const uint32_t i11 = i01 + 1u;
            out.indices.push_back(i00);
            out.indices.push_back(i01);
            out.indices.push_back(i10);
            out.indices.push_back(i10);
            out.indices.push_back(i01);
            out.indices.push_back(i11);
        }
    }
}

namespace detail
{
inline std::string Fmt(float v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}
} // namespace detail

// 序列化：纯文本快照（%.9g 精确往返）。
inline bool SaveTerrainHeightmap(const TerrainHeightmap& hm, const std::string& path)
{
    std::ostringstream ss;
    ss << "bighero-terrain 1\n";
    ss << "grid " << hm.nx_ << " " << hm.nz_ << "\n";
    ss << "cell " << detail::Fmt(hm.cellSize_) << "\n";
    ss << "origin " << detail::Fmt(hm.origin_.x) << " " << detail::Fmt(hm.origin_.y) << " " << detail::Fmt(hm.origin_.z)
       << "\n";
    ss << "data\n";
    for (const float h : hm.heights_)
        ss << detail::Fmt(h) << " ";
    ss << "\n";
    return Core::FileSystem::WriteText(path, ss.str());
}

inline bool LoadTerrainHeightmap(const std::string& path, TerrainHeightmap& hm)
{
    std::string text;
    if (!Core::FileSystem::ReadText(path, text))
        return false;
    std::istringstream ss(text);
    std::string tag;
    int version = 0;
    if (!(ss >> tag >> version) || tag != "bighero-terrain" || version != 1)
        return false;

    int nx = 0;
    int nz = 0;
    float cell = 0.0f;
    glm::vec3 origin{0.0f};
    bool gridSeen = false;
    bool cellSeen = false;
    bool originSeen = false;
    std::string word;
    while (ss >> word)
    {
        if (word == "grid")
        {
            if (!(ss >> nx >> nz))
                return false;
            gridSeen = true;
        }
        else if (word == "cell")
        {
            if (!(ss >> cell))
                return false;
            cellSeen = true;
        }
        else if (word == "origin")
        {
            if (!(ss >> origin.x >> origin.y >> origin.z))
                return false;
            originSeen = true;
        }
        else if (word == "data")
        {
            if (!gridSeen || !cellSeen || !originSeen || nx < 2 || nz < 2 || !(cell > 0.0f))
                return false;
            TerrainHeightmap parsed;
            if (!parsed.Resize(nx, nz, cell, origin, 0.0f))
                return false;
            const size_t expect = static_cast<size_t>(nx) * static_cast<size_t>(nz);
            for (size_t i = 0; i < expect; ++i)
            {
                float h = 0.0f;
                if (!(ss >> h))
                    return false;
                parsed.heights_[i] = h;
            }
            hm = parsed;
            return true;
        }
        else
        {
            return false; // 未知字段视为损坏
        }
    }
    return false; // 未见 data 段
}

} // namespace BigHero::Scene