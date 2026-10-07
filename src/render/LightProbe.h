#pragma once
// 光照探针（Light Probes）：把静态场景的间接光预计算成球谐（SH）系数场，
// 运行时按位置插值、按法线求值，让动态物体也能吃到烘焙级环境光（U2-L1 的可离线部分）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   实时阴影（CSM）+ IBL 能覆盖直接光与无限远的天空，但「墙角的反弹光」「室内外的明暗过渡」
//   这类局部间接光算不动——每帧做一次全局光照不现实。业界（Unity Light Probes / Unreal
//   ILC / 绝大多数手游）的通用解法是空间采样：在场景中布一堆探针，离线在每个探针处把
//   四周来光积分成低频球谐，运行时动态物体只需按自己所在位置插值几个探针、按法线求值，
//   成本接近零。本模块即这套机制的完整实现（投影 / 卷积 / 体插值 / 无效探针剔除）。
//
// 球谐约定：
//   使用 L0+L1+L2 共 9 个实球谐基（Ramamoorthi & Hanrahan 的经典三阶截断），
//   索引顺序 [0]=Y00, [1]=Y1-1, [2]=Y10, [3]=Y11, [4]=Y2-2, [5]=Y2-1, [6]=Y20, [7]=Y21, [8]=Y22。
//   基函数归一化到 ∫Y_lm² dω = 1，因此投影为 c_lm = ∫ f(ω) Y_lm(ω) dω，
//   重建为 f(ω) ≈ Σ c_lm Y_lm(ω)。
//
// 存储形态（关键，别搞混）：
//   本模块对外统一以**漫反射辐照度 SH**为准：即已按余弦卷积（A_l = π, 2π/3, π/4）
//   并除以 π 归一。运行期求值 Evaluate(sh, n) 直接返回「乘在 albedo 上的颜色」：
//       outColor = albedo * Evaluate(probeSh, normal)
//   均匀环境（各方向辐射亮度恒为 L）下恒有 Evaluate(...) == L，这条不变量由单测锁死。
//
// 契约：
//   - 体采样越界时按网格边界钳制（clamp-to-edge）：不会出现未定义值，代价是体外仍取边缘光。
//   - 无效探针（埋在实体内部、会漏光的那种）在插值中被剔除并重新归一化权重；
//     若周边全无效则回退到 AmbientFallback（默认全黑，也可设为天光常量）。
//   - 未做线程同步：Bake 为写、Sample 为读，约定分别离线与运行时执行。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Render
{
// 9 项实球谐系数（每通道一套，即 RGB × 9）。
struct Sh9
{
    glm::vec3 c[9]{};

    // ---- 基函数 ----
    // 归一化实球谐（∫Y² dω = 1）。dir 不必归一化（内部会归一化；零向量退化为 0）。
    static void Basis(const glm::vec3& dir, float out[9])
    {
        glm::vec3 d = dir;
        const float len = glm::length(d);
        if (len > 1e-8f)
            d /= len;
        else
            d = glm::vec3(0.0f, 0.0f, 1.0f);
        const float x = d.x, y = d.y, z = d.z;

        out[0] = 0.282095f;                         // Y00 = 1/(2√π)
        out[1] = 0.488603f * y;                     // Y1-1
        out[2] = 0.488603f * z;                     // Y10
        out[3] = 0.488603f * x;                     // Y11
        out[4] = 1.092548f * x * y;                 // Y2-2
        out[5] = 1.092548f * y * z;                 // Y2-1
        out[6] = 0.315392f * (3.0f * z * z - 1.0f); // Y20
        out[7] = 1.092548f * x * z;                 // Y21
        out[8] = 0.546274f * (x * x - y * y);       // Y22
    }

    // ---- 构造 ----
    // 均匀环境（各方向辐射亮度恒为 radiance）的漫反射辐照度 SH。
    // 推导：c00 = L·4π·Y00 = L·2√π；A0/π = 1；重建 Σ c Y = L·2√π·Y00 = L。
    [[nodiscard]] static Sh9 FromAmbient(const glm::vec3& radiance)
    {
        constexpr float kTwoSqrtPi = 3.5449077018110321f; // 2√π = 4π·Y00
        Sh9 s;
        s.c[0] = radiance * kTwoSqrtPi;
        return s;
    }

    // 投影：把方向辐射亮度函数 f(dir) → RGB 积分成**辐射亮度 SH**。
    // 采样用 Fibonacci 球面（近似均匀），每个样本立体角权重 4π/N。
    // sampleCount 越大越准；SH 只保留到 L2，高于此的细节本来也会被丢掉。
    template<class Fn> [[nodiscard]] static Sh9 ProjectRadiance(Fn&& f, int sampleCount = 512)
    {
        const int n = std::max(sampleCount, 8);
        constexpr float kGolden = 2.39996322972865332f; // π(3-√5)
        const float weight = 4.0f * 3.14159265358979323846f / static_cast<float>(n);

        Sh9 s;
        float basis[9];
        for (int i = 0; i < n; ++i)
        {
            // Fibonacci 球面：z 均匀铺开，方位角按黄金角递增。
            const float z = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(n);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = kGolden * static_cast<float>(i);
            const glm::vec3 dir(r * std::cos(phi), r * std::sin(phi), z);

            Basis(dir, basis);
            const glm::vec3 radiance = f(dir);
            for (int k = 0; k < 9; ++k)
                s.c[k] += radiance * (basis[k] * weight);
        }
        return s;
    }

    // 卷积：辐射亮度 SH → 漫反射辐照度 SH（乘 A_l 后除以 π，使 Evaluate 直接可乘 albedo）。
    // A_0 = π, A_1 = 2π/3, A_2 = π/4（余弦核的球谐卷积系数）。
    [[nodiscard]] static Sh9 ToDiffuseIrradiance(const Sh9& radianceSh)
    {
        // A_l / π：l=0 → 1；l=1 → 2/3；l=2 → 1/4。
        static constexpr float kAl[9] = {1.0f, // l=0
                                         2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        Sh9 s;
        for (int k = 0; k < 9; ++k)
            s.c[k] = radianceSh.c[k] * kAl[k];
        return s;
    }

    // ---- 求值 ----
    // Σ c_lm Y_lm(normal)：返回可直接乘到 albedo 上的辐照度颜色（见文件头约定）。
    // 结果按分量钳到非负：SH 三阶截断在强方向光下会出现负瓣，负值在物理上无意义。
    [[nodiscard]] static glm::vec3 Evaluate(const Sh9& sh, const glm::vec3& normal)
    {
        float basis[9];
        Basis(normal, basis);
        glm::vec3 out(0.0f);
        for (int k = 0; k < 9; ++k)
            out += sh.c[k] * basis[k];
        return glm::max(out, glm::vec3(0.0f));
    }

    // L1（前 4 项）截断：去掉方向性细节，只留「平均色 + 线性梯度」。
    // 用于低端档位或与非定向模式对齐（Unity 的 Non-Directional 模式）。
    [[nodiscard]] Sh9 TruncateToL1() const
    {
        Sh9 s;
        for (int k = 0; k < 4; ++k)
            s.c[k] = c[k];
        return s;
    }

    [[nodiscard]] Sh9 operator+(const Sh9& o) const
    {
        Sh9 s;
        for (int k = 0; k < 9; ++k)
            s.c[k] = c[k] + o.c[k];
        return s;
    }
    [[nodiscard]] Sh9 operator*(float s) const
    {
        Sh9 o;
        for (int k = 0; k < 9; ++k)
            o.c[k] = c[k] * s;
        return o;
    }
};

// 线性插值两个 SH（体插值与交叉淡入用）。
[[nodiscard]] inline Sh9 LerpSh(const Sh9& a, const Sh9& b, float t)
{
    const float u = std::clamp(t, 0.0f, 1.0f);
    return a * (1.0f - u) + b * u;
}

// 均匀网格光照探针体。
class LightProbeVolume
{
  public:
    // ---- 网格 ----
    // 重建探针网格：dims 各维 >= 1，spacing 各分量 > 0。失败时保留原状态。
    bool Resize(const glm::ivec3& dims, const glm::vec3& origin, const glm::vec3& spacing)
    {
        if (dims.x < 1 || dims.y < 1 || dims.z < 1)
            return false;
        if (!(spacing.x > 0.0f) || !(spacing.y > 0.0f) || !(spacing.z > 0.0f))
            return false;
        dims_ = dims;
        origin_ = origin;
        spacing_ = spacing;
        const size_t n = static_cast<size_t>(dims.x) * static_cast<size_t>(dims.y) * static_cast<size_t>(dims.z);
        sh_.assign(n, Sh9{});
        valid_.assign(n, 1);
        return true;
    }

    [[nodiscard]] size_t ProbeCount() const { return sh_.size(); }
    [[nodiscard]] const glm::ivec3& Dims() const { return dims_; }
    [[nodiscard]] const glm::vec3& Origin() const { return origin_; }
    [[nodiscard]] const glm::vec3& Spacing() const { return spacing_; }

    [[nodiscard]] glm::vec3 ProbePosition(const glm::ivec3& idx) const { return origin_ + glm::vec3(idx) * spacing_; }
    // 世界坐标 → 网格坐标（连续值，未取整；三线性插值直接用它取小数部分）。
    [[nodiscard]] glm::vec3 ToGridSpace(const glm::vec3& world) const { return (world - origin_) / spacing_; }

    // ---- 数据 ----
    // 写入某探针的辐照度 SH 与有效性（无效 = 埋在实体里，插值时剔除以防漏光）。
    bool SetProbe(const glm::ivec3& idx, const Sh9& irradianceSh, bool valid = true)
    {
        const size_t i = Index(idx);
        if (i >= sh_.size())
            return false;
        sh_[i] = irradianceSh;
        valid_[i] = valid ? 1 : 0;
        return true;
    }
    [[nodiscard]] const Sh9* Probe(const glm::ivec3& idx) const
    {
        const size_t i = Index(idx);
        return i < sh_.size() ? &sh_[i] : nullptr;
    }
    [[nodiscard]] bool IsProbeValid(const glm::ivec3& idx) const
    {
        const size_t i = Index(idx);
        return i < valid_.size() && valid_[i] != 0;
    }

    // 全无效邻域的兜底环境光（默认全黑；室外场景通常设为天空常量）。
    void SetAmbientFallback(const Sh9& sh) { fallback_ = sh; }
    [[nodiscard]] const Sh9& AmbientFallback() const { return fallback_; }

    // ---- 烘焙 ----
    // radianceFn(pos, dir) → 该点朝该方向看到的入射辐射亮度（由烘焙器实现：
    // 天空/面光源/已烘焙的贴图 GI 都行）。内部逐探针做 Fibonacci 球面投影 + 余弦卷积。
    // isValidFn(pos) → 该探针位置是否处在可采样的空气中（false = 埋在几何体内，标记无效）。
    template<class RadianceFn, class ValidFn>
    void Bake(RadianceFn&& radianceFn, ValidFn&& isValidFn, int sampleCount = 256)
    {
        for (int z = 0; z < dims_.z; ++z)
        {
            for (int y = 0; y < dims_.y; ++y)
            {
                for (int x = 0; x < dims_.x; ++x)
                {
                    const glm::ivec3 idx(x, y, z);
                    const glm::vec3 pos = ProbePosition(idx);
                    const bool ok = isValidFn(pos);
                    const Sh9 radiance =
                        Sh9::ProjectRadiance([&](const glm::vec3& dir) { return radianceFn(pos, dir); }, sampleCount);
                    SetProbe(idx, Sh9::ToDiffuseIrradiance(radiance), ok);
                }
            }
        }
    }

    // 只重算有效性（几何改动后无需重跑光照积分时的快速路径）。
    template<class ValidFn> void RefreshValidity(ValidFn&& isValidFn)
    {
        for (size_t i = 0; i < valid_.size(); ++i)
        {
            const int x = static_cast<int>(i % static_cast<size_t>(dims_.x));
            const int y = static_cast<int>((i / static_cast<size_t>(dims_.x)) % static_cast<size_t>(dims_.y));
            const int z = static_cast<int>(i / (static_cast<size_t>(dims_.x) * static_cast<size_t>(dims_.y)));
            valid_[i] = isValidFn(ProbePosition(glm::ivec3(x, y, z))) ? 1 : 0;
        }
    }

    // ---- 采样 ----
    // 三线性插值出该位置的辐照度 SH。无效探针权重置零并重新归一化（防漏光）。
    // 若 8 邻域内没有可用权重（典型：采样点正落在无效探针格上，或邻域全埋在墙里），
    // 退化为「最近的有效探针」——而不是直接跳到 AmbientFallback。
    // 这一点很关键：直接兜底会让站在墙边/贴地的角色忽明忽暗地发黑（Unity 的
    // 「黑探针」artifact 同理），取最近有效值则平滑得多。
    // 全网格无有效探针时才会用到 AmbientFallback。越界坐标按边界钳制。
    [[nodiscard]] Sh9 SampleSh(const glm::vec3& world) const
    {
        if (sh_.empty())
            return fallback_;

        const glm::vec3 g =
            glm::clamp(ToGridSpace(world), glm::vec3(0.0f), glm::vec3(dims_.x - 1, dims_.y - 1, dims_.z - 1));
        const int x0 = static_cast<int>(std::floor(g.x));
        const int y0 = static_cast<int>(std::floor(g.y));
        const int z0 = static_cast<int>(std::floor(g.z));
        const float fx = g.x - static_cast<float>(x0);
        const float fy = g.y - static_cast<float>(y0);
        const float fz = g.z - static_cast<float>(z0);

        Sh9 acc;
        float wsum = 0.0f;
        for (int dz = 0; dz <= 1; ++dz)
        {
            for (int dy = 0; dy <= 1; ++dy)
            {
                for (int dx = 0; dx <= 1; ++dx)
                {
                    const int cx = std::min(x0 + dx, dims_.x - 1);
                    const int cy = std::min(y0 + dy, dims_.y - 1);
                    const int cz = std::min(z0 + dz, dims_.z - 1);
                    const size_t i = Index(glm::ivec3(cx, cy, cz));
                    if (i >= valid_.size() || valid_[i] == 0)
                        continue; // 埋在实体内的探针不参与，避免把墙内黑/亮漏出来
                    const float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy) * (dz ? fz : 1.0f - fz);
                    if (w <= 0.0f)
                        continue;
                    acc = acc + sh_[i] * w;
                    wsum += w;
                }
            }
        }
        if (wsum <= 1e-6f)
            return NearestValidSh(glm::ivec3(x0, y0, z0));
        return acc * (1.0f / wsum);
    }

    // 便捷接口：位置插值 + 法线求值，直接得到可乘 albedo 的颜色。
    [[nodiscard]] glm::vec3 SampleIrradiance(const glm::vec3& world, const glm::vec3& normal) const
    {
        return Sh9::Evaluate(SampleSh(world), normal);
    }

  private:
    // 以 cell 为中心逐环外扩，返回最近的有效探针；全网格无效时返回 AmbientFallback。
    // 环半径上限取网格最大边长，保证一定能扫完整个体（小网格成本可忽略）。
    [[nodiscard]] Sh9 NearestValidSh(const glm::ivec3& cell) const
    {
        const int maxRing = std::max(std::max(dims_.x, dims_.y), dims_.z);
        for (int r = 0; r <= maxRing; ++r)
        {
            Sh9 best;
            float bestD2 = std::numeric_limits<float>::max();
            bool found = false;
            for (int dz = -r; dz <= r; ++dz)
            {
                for (int dy = -r; dy <= r; ++dy)
                {
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        // 只检查本环（切比雪夫距离 == r）的壳层，内部环已查过
                        if (std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz))) != r)
                            continue;
                        const glm::ivec3 idx(cell.x + dx, cell.y + dy, cell.z + dz);
                        const size_t i = Index(idx);
                        if (i >= valid_.size() || valid_[i] == 0)
                            continue;
                        const float d2 = static_cast<float>(dx * dx + dy * dy + dz * dz);
                        if (d2 < bestD2)
                        {
                            bestD2 = d2;
                            best = sh_[i];
                            found = true;
                        }
                    }
                }
            }
            if (found)
                return best;
        }
        return fallback_;
    }

    [[nodiscard]] size_t Index(const glm::ivec3& idx) const
    {
        if (idx.x < 0 || idx.y < 0 || idx.z < 0 || idx.x >= dims_.x || idx.y >= dims_.y || idx.z >= dims_.z)
            return static_cast<size_t>(-1); // 越界哨兵（大于任何合法下标）
        return (static_cast<size_t>(idx.z) * static_cast<size_t>(dims_.y) + static_cast<size_t>(idx.y)) *
                   static_cast<size_t>(dims_.x) +
               static_cast<size_t>(idx.x);
    }

    glm::ivec3 dims_{0, 0, 0};
    glm::vec3 origin_{0.0f};
    glm::vec3 spacing_{1.0f};
    std::vector<Sh9> sh_;
    std::vector<uint8_t> valid_;
    Sh9 fallback_{};
};

// 把探针体按线性顺序打包成延迟光照 UBO 的 probes[] 上传槽（纯 CPU、零 GPU，可离线单测）。
//   每槽 .rgb = 该探针以世界 up 法线预求值的漫反射辐照度（与前向逐实例路径同口径），
//   .a = 有效性（1/0；片元三线性插值时剔除无效探针并重新归一化权重）。
// 线性索引与片元着色器一致：idx = (z*dims.y + y)*dims.x + x（同 LightProbeVolume::Index）。
// 返回实际写入槽数（<= maxCount）；未烘焙（ProbeCount()==0）或参数非法时返回 0（片元回退单探针）。
[[nodiscard]] inline int PackProbeIrradianceUp(const LightProbeVolume& vol, glm::vec4* out, int maxCount)
{
    if (out == nullptr || maxCount <= 0 || vol.ProbeCount() == 0)
        return 0;
    const glm::ivec3 dims = vol.Dims();
    const int count = static_cast<int>(vol.ProbeCount());
    const int n = std::min(count, maxCount);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                const int idx = (z * dims.y + y) * dims.x + x;
                if (idx >= n)
                    return n;
                const glm::ivec3 gidx(x, y, z);
                const Sh9* sh = vol.Probe(gidx);
                const bool valid = vol.IsProbeValid(gidx);
                out[idx] = (sh != nullptr) ? glm::vec4(Sh9::Evaluate(*sh, up), valid ? 1.0f : 0.0f)
                                           : glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }
    return n;
}
} // namespace BigHero::Render
