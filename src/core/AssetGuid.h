#pragma once
// 资产 GUID 数据库（AssetGuid）：把「资产身份」与「文件路径」解耦的持久化标识层。
// 纯 CPU、仅标准库、仅头文件；不依赖渲染/窗口，可离线单测。
//
// 背景与动机：
//   现有资产管线（AssetCache / AssetRegistry）一律以字符串路径为键。「路径即身份」意味着
//   重命名/移动资产会静默拉断所有引用（贴图换目录后材质断链、场景找不到网格）。
//   Unity/Unreal 的解法是同一套：为每个资产分配一个永不复用的 128 位 GUID，资产间引用
//   一律记 GUID，路径只是 GUID 的当前投影——本模块即该机制的最小实现（U2-A2 前半：
//   GUID + 引用追踪）。
//
// 提供：
//   - Guid：128 位值类型（Generate / ToString / TryParse / 比较 / GuidHash 散列）。
//   - AssetGuidMeta：.meta 旁车文件读写（Unity 惯例：asset.png → asset.png.meta，
//     内容一行 "guid: <32hex>"；资产入库/移动/删除时随主文件一起走）。
//   - AssetGuidDatabase：路径 ↔ GUID 双向映射（路径经 NormalizePath 归一化）、
//     GuidForPath 幂等登记（已有 .meta 读旧值，无/损坏/撞车则生成并写盘自愈）、
//     MoveAsset 保证 GUID 稳定、引用追踪（SetReferences / ReferencesOf / DependentsOf）
//     与断链检测（FindBrokenReferences）、ScanDirectory 批量导入。
//
// 契约：
//   - 全零 Guid 表示「空引用」哨兵（IsValid()==false），不计入断链。
//   - 本库不做线程同步：资产导入/登记约定在主线程（编辑器线程）完成。
//   - 只管理映射与 .meta 旁车，不搬动资产文件本体（文件移动由调用方先行完成）。
//   - 与现有 AssetCache / AssetRegistry 正交：不改其接口，路径键语义保持不变。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/FileSystemUtils.h"
#include "core/PathUtils.h"

namespace BigHero::Core
{
// ---- 128 位资产全局唯一标识 ----
class Guid
{
  public:
    constexpr Guid() noexcept = default;
    constexpr Guid(uint64_t hi, uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

    // 生成随机 GUID（random_device 播种的 mt19937_64）。128 位空间的生日碰撞界约 2^64 次
    // 生成，单项目资产规模（远小于 2^32）下碰撞概率可忽略。非线程安全（主线程约定，见文件头）。
    [[nodiscard]] static Guid Generate()
    {
        static std::mt19937_64 s_rng{std::random_device{}()};
        return Guid{s_rng(), s_rng()};
    }

    // 32 个小写十六进制字符（无连字符，与 Unity .meta 的 guid 字段同形）。
    [[nodiscard]] std::string ToString() const
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(32);
        const uint64_t words[2] = {hi_, lo_};
        for (uint64_t w : words)
        {
            for (int shift = 60; shift >= 0; shift -= 4)
                out += kHex[(w >> shift) & 0xF];
        }
        return out;
    }

    // 解析恰好 32 个十六进制字符（大小写均可）。非法输入返回 false 且不写 out。
    [[nodiscard]] static bool TryParse(std::string_view text, Guid& out)
    {
        if (text.size() != 32)
            return false;
        uint64_t words[2] = {0, 0};
        for (size_t i = 0; i < 32; ++i)
        {
            const char c = text[i];
            uint64_t digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<uint64_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<uint64_t>(c - 'A' + 10);
            else
                return false;
            words[i / 16] = (words[i / 16] << 4) | digit;
        }
        out = Guid{words[0], words[1]};
        return true;
    }

    // 全零为「空引用」哨兵（不指向任何资产，断链检测不计）。
    [[nodiscard]] constexpr bool IsValid() const noexcept { return hi_ != 0 || lo_ != 0; }

    [[nodiscard]] constexpr uint64_t Hi() const noexcept { return hi_; }
    [[nodiscard]] constexpr uint64_t Lo() const noexcept { return lo_; }

    friend constexpr bool operator==(const Guid& a, const Guid& b) noexcept;
    friend constexpr bool operator!=(const Guid& a, const Guid& b) noexcept;
    friend constexpr bool operator<(const Guid& a, const Guid& b) noexcept;

  private:
    uint64_t hi_ = 0;
    uint64_t lo_ = 0;
};

// 类外 constexpr 定义（noexcept 与类内声明一致；类内仅声明，避免 GCC 对类内 friend
// constexpr+noexcept 组合的求值 corner case 导致 noexcept(...) 假阴性）。
constexpr bool operator==(const Guid& a, const Guid& b) noexcept
{
    return a.Hi() == b.Hi() && a.Lo() == b.Lo();
}
constexpr bool operator!=(const Guid& a, const Guid& b) noexcept
{
    return !(a == b);
}
constexpr bool operator<(const Guid& a, const Guid& b) noexcept
{
    return a.Hi() < b.Hi() || (a.Hi() == b.Hi() && a.Lo() < b.Lo());
}

// Guid 散列函子（unordered_map / unordered_set 键用）。
struct GuidHash
{
    size_t operator()(const Guid& g) const noexcept
    {
        // 64 位混合：黄金比例扰动组合（boost::hash_combine 风格）。
        const uint64_t mixed = g.Lo() + 0x9e3779b97f4a7c15ULL + (g.Hi() << 6) + (g.Hi() >> 2);
        return std::hash<uint64_t>{}(g.Hi() ^ mixed);
    }
};

// ---- .meta 旁车文件（asset.png → asset.png.meta）----
namespace AssetGuidMeta
{
// .meta 与主文件同目录同名，仅追加 .meta 后缀（Unity 惯例）。
[[nodiscard]] inline std::string MetaPathFor(const std::string& assetPath)
{
    return assetPath + ".meta";
}

// 读取 .meta 中的 guid 字段；文件缺失、无 guid 行或值非法均返回 false。
inline bool Read(const std::string& assetPath, Guid& out)
{
    std::string text;
    if (!FileSystem::ReadText(MetaPathFor(assetPath), text))
        return false;
    constexpr std::string_view kKey = "guid:";
    const size_t pos = text.find(kKey);
    if (pos == std::string::npos)
        return false;
    size_t begin = pos + kKey.size();
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    size_t end = begin;
    while (end < text.size() && text[end] != '\r' && text[end] != '\n')
        ++end;
    return Guid::TryParse(std::string_view(text).substr(begin, end - begin), out);
}

// 写入 .meta（整文件覆写：当前仅 guid 一行；未来扩展导入器设置时迁移为结构化格式）。
inline bool Write(const std::string& assetPath, const Guid& guid)
{
    return FileSystem::WriteText(MetaPathFor(assetPath), "guid: " + guid.ToString() + "\n");
}

inline bool Remove(const std::string& assetPath)
{
    return FileSystem::Remove(MetaPathFor(assetPath));
}
} // namespace AssetGuidMeta

// ---- 路径 ↔ GUID 双向映射 + 引用追踪 ----
class AssetGuidDatabase
{
  public:
    // 只读查询：未登记返回 nullptr（不触发 IO，不生成）。
    [[nodiscard]] const Guid* Find(const std::string& assetPath) const
    {
        const auto it = byPath_.find(NormalizePath(assetPath));
        return it != byPath_.end() ? &it->second : nullptr;
    }
    [[nodiscard]] const std::string* FindPath(const Guid& guid) const
    {
        const auto it = byGuid_.find(guid);
        return it != byGuid_.end() ? &it->second : nullptr;
    }

    // 幂等登记：已登记 → 返回既有 GUID；未登记 → 优先采用 .meta 持久化值（资产跨重启
    // 保持身份）；无 .meta / 损坏 / 与库内既有 GUID 撞车（如连带 .meta 复制的文件）→
    // 生成新 GUID 并覆写 .meta（自愈）。同一资产反复调用必然返回同一 GUID。
    Guid GuidForPath(const std::string& assetPath)
    {
        const std::string key = NormalizePath(assetPath);
        if (const auto it = byPath_.find(key); it != byPath_.end())
            return it->second;

        Guid guid;
        if (!AssetGuidMeta::Read(key, guid) || !guid.IsValid() || byGuid_.count(guid) != 0)
        {
            do
            {
                guid = Guid::Generate();
            } while (byGuid_.count(guid) != 0); // 撞车防护（概率可忽略，成本为零）
            AssetGuidMeta::Write(key, guid);
        }
        byPath_.emplace(key, guid);
        byGuid_.emplace(guid, key);
        return guid;
    }

    // GUID 稳定移动/重命名：路径映射换键、GUID 不变，该资产的引用记录随路径换键；
    // .meta 写到新路径并清理旧 .meta。
    // 返回 false：oldPath 未登记，或 newPath 已被另一资产占用（冲突，不动任何状态）。
    // 注意：只动映射与 .meta 旁车；资产文件本体的移动由调用方先行完成。
    bool MoveAsset(const std::string& oldPath, const std::string& newPath)
    {
        const std::string oldKey = NormalizePath(oldPath);
        const std::string newKey = NormalizePath(newPath);
        if (oldKey == newKey)
            return Find(oldKey) != nullptr;
        const auto it = byPath_.find(oldKey);
        if (it == byPath_.end() || byPath_.count(newKey) != 0)
            return false;
        const Guid guid = it->second;
        byPath_.erase(it);
        byPath_.emplace(newKey, guid);
        byGuid_.find(guid)->second = newKey;
        if (auto refIt = refs_.find(oldKey); refIt != refs_.end())
        {
            refs_.emplace(newKey, std::move(refIt->second));
            refs_.erase(refIt);
        }
        AssetGuidMeta::Write(newKey, guid);
        AssetGuidMeta::Remove(oldKey);
        return true;
    }

    // 注销资产：清双向映射与其引用记录，并删除 .meta。仍以该资产 GUID 为目标的引用
    // 保留在引用者名下——随后 FindBrokenReferences 会把它们显影为断链（这正是
    // 「引用追踪」的价值：删资产不再静默，断链可枚举）。
    bool RemoveAsset(const std::string& assetPath)
    {
        const std::string key = NormalizePath(assetPath);
        const auto it = byPath_.find(key);
        if (it == byPath_.end())
            return false;
        byGuid_.erase(it->second);
        byPath_.erase(it);
        refs_.erase(key);
        AssetGuidMeta::Remove(key);
        return true;
    }

    // ---- 引用追踪（资产 → 其依赖的 GUID 集合）----
    // 覆盖式登记 assetPath 引用的全部 GUID（空数组 = 清除记录）。
    // 引用来源由上层解析器提供（材质里的贴图 GUID、场景里的网格 GUID），本库只维护图。
    void SetReferences(const std::string& assetPath, std::vector<Guid> refs)
    {
        const std::string key = NormalizePath(assetPath);
        if (refs.empty())
            refs_.erase(key);
        else
            refs_[key] = std::move(refs);
    }

    // 无记录返回 nullptr。
    [[nodiscard]] const std::vector<Guid>* ReferencesOf(const std::string& assetPath) const
    {
        const auto it = refs_.find(NormalizePath(assetPath));
        return it != refs_.end() ? &it->second : nullptr;
    }

    // 反向查询：哪些资产引用了 target GUID（「谁在用我」——删除前的影响面评估）。
    [[nodiscard]] std::vector<std::string> DependentsOf(const Guid& target) const
    {
        std::vector<std::string> out;
        for (const auto& [path, refs] : refs_)
        {
            for (const Guid& g : refs)
            {
                if (g == target)
                {
                    out.push_back(path);
                    break;
                }
            }
        }
        return out;
    }

    // 断链检测：引用表中所有「非空引用但目标未登记」的 (引用者路径, 缺失 GUID) 对。
    // 典型触发：目标资产被 RemoveAsset / 库外删除；或引用了从未导入的 GUID。
    [[nodiscard]] std::vector<std::pair<std::string, Guid>> FindBrokenReferences() const
    {
        std::vector<std::pair<std::string, Guid>> out;
        for (const auto& [path, refs] : refs_)
        {
            for (const Guid& g : refs)
            {
                if (g.IsValid() && byGuid_.count(g) == 0)
                    out.emplace_back(path, g);
            }
        }
        return out;
    }

    // 递归扫描目录：为每个非 .meta 文件登记（有 .meta 读旧值，无则生成写盘）。
    // 返回本次「新建身份」（生成新 GUID）的文件数；复用既有 .meta 的不计入。
    // 目录不存在等 IO 错误按空目录处理（返回 0）。
    size_t ScanDirectory(const std::string& dir)
    {
        size_t created = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string path = NormalizePath(entry.path().string());
            // .meta 旁车不是资产本体，跳过（避免 .meta.meta 套娃）
            if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".meta") == 0)
                continue;
            Guid existing;
            const bool reusable =
                AssetGuidMeta::Read(path, existing) && existing.IsValid() && FindPath(existing) == nullptr;
            GuidForPath(path);
            if (!reusable)
                ++created;
        }
        return created;
    }

    [[nodiscard]] size_t Count() const { return byPath_.size(); }

    void Clear()
    {
        byPath_.clear();
        byGuid_.clear();
        refs_.clear();
    }

  private:
    std::unordered_map<std::string, Guid> byPath_;
    std::unordered_map<Guid, std::string, GuidHash> byGuid_;
    std::unordered_map<std::string, std::vector<Guid>> refs_;
};
} // namespace BigHero::Core
