#pragma once
// 资产数据库（AssetDatabase）：在 AssetGuid（路径 ↔ GUID）之上补齐「引用解析 + 断链检测 +
// 索引持久化」，构成完整的 Unity 式资产管线（U2-A2 后半）。
// 纯 CPU、仅标准库 + core/ 既有工具，不依赖渲染/窗口；可离线单测（用临时目录做文件 IO）。
//
// 背景与动机：
//   AssetGuid 解决了「身份与路径解耦」，但还差临门一脚：谁来告诉数据库「这个材质引用了
//   哪张贴图」？没有这一步，引用图永远是空的，断链检测也就无从谈起。
//   本模块补上这一环——导入资产时扫描其内容，抽出被引用的资产并**立刻解析成 GUID 记住**，
//   于是「移动/重命名资产不再拉断引用」这句承诺才真正成立。
//
// 关键设计：引用一旦解析成功就**只记 GUID，不再记路径**。
//   若引用仍以路径字符串保存，被引用的资产一改路径，所有引用者立刻全部断链——那等于
//   白做了 GUID 层。本模块的 RefEntry 保存 (原始路径写法, 已解析 GUID)：解析成功的永远用
//   GUID 反查当前路径（自然跟随移动），只有从未解析成功的才留作断链，并在目标出现时愈合。
//
// 引用抽取策略（务实取舍）：
//   不解析每种格式的完整语法（那需要为 glTF/OBJ/MTL/JSON 各写一个解析器，且随格式演进而腐化），
//   而是做**键值扫描**：找出形如 `key: value` 或 `"key": "value"` 的片段，
//   其中 value 带已知资产扩展名即视为引用。好处是与格式无关、不会随版本腐化；
//   代价是可能把注释里的路径也算进来——但这只会让引用图偏大（多报依赖），
//   不会漏报，而漏报才是断链检测失效的致命情形。
//
// 契约：
//   - 所有对外路径一律为**相对于 Root() 的相对路径**，分隔符统一 '/'（经 NormalizePath）。
//   - Import 是幂等的：重复导入同一路径不会重复登记，只刷新引用与体积。
//   - 被引用但尚未导入的资产记为「断链」（BrokenReference），导入后自动愈合。
//   - Move 只改映射，不动磁盘文件本体（文件移动由调用方先行完成）。
//   - 未做线程同步：导入约定在主线程（编辑器线程）完成。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <queue>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/AssetGuid.h"
#include "core/FileSystemUtils.h"
#include "core/PathUtils.h"

namespace BigHero::Core
{
// 资产类别（按扩展名推断；用于资产浏览器的图标与筛选）。
enum class AssetKind : uint8_t
{
    Unknown = 0,
    Texture,
    Mesh,
    Material,
    Shader,
    Scene,
    Audio,
    Font,
    Animation
};

[[nodiscard]] inline const char* AssetKindName(AssetKind k)
{
    switch (k)
    {
    case AssetKind::Texture:
        return "Texture";
    case AssetKind::Mesh:
        return "Mesh";
    case AssetKind::Material:
        return "Material";
    case AssetKind::Shader:
        return "Shader";
    case AssetKind::Scene:
        return "Scene";
    case AssetKind::Audio:
        return "Audio";
    case AssetKind::Font:
        return "Font";
    case AssetKind::Animation:
        return "Animation";
    default:
        return "Unknown";
    }
}

// 资产引用扫描器：从文件文本中抽取被引用的资产相对路径。
class AssetRefScanner
{
  public:
    // 已知资产扩展名（小写，不含点）。命中即认为该字符串是一条资产引用。
    [[nodiscard]] static bool IsAssetExtension(std::string_view ext)
    {
        static const std::unordered_set<std::string> kExts = {
            "png",  "jpg",   "jpeg",   "tga", "bmp",  "hdr", "ktx", "dds", // 纹理
            "gltf", "glb",   "obj",    "fbx",                              // 网格
            "mat",  "mtl",                                                 // 材质
            "vert", "frag",  "glsl",   "spv", "comp",                      // 着色器
            "json", "scene", "prefab",                                     // 场景/预制体
            "wav",  "mp3",   "ogg",                                        // 音频
            "ttf",  "otf",                                                 // 字体
            "anim"                                                         // 动画
        };
        std::string lower(ext);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return kExts.count(lower) != 0;
    }

    // 扫描文本，返回所有形如 key: value / "key": "value" 且 value 带资产扩展名的路径。
    // 结果已去重（保持首次出现顺序），并做引号剥离与空白裁剪。
    [[nodiscard]] static std::vector<std::string> Scan(const std::string& text)
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;

        const auto consider = [&](std::string value)
        {
            value = StripQuotes(Trim(value));
            if (value.empty())
                return;
            const std::string ext = GetExtension(value, false);
            if (!IsAssetExtension(ext))
                return;
            const std::string normalized = NormalizePath(value);
            if (seen.insert(normalized).second)
                out.push_back(normalized);
        };

        // 逐个冒号向后扫描：冒号左边回溯出键、右边取出到行尾/逗号/引号闭合的值。
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] != ':')
                continue;
            // 跳过作用域符号（"::" 的两个冒号）与 URL 协议（"http://"）
            if (i > 0 && text[i - 1] == ':')
                continue;
            if (i + 1 < text.size() &&
                (text[i + 1] == ':' || (text[i + 1] == '/' && i + 2 < text.size() && text[i + 2] == '/')))
                continue;

            // 左侧：跳过空白与闭合引号后回溯键名；没有键名就不是键值结构。
            size_t keyEnd = i;
            while (keyEnd > 0 && (text[keyEnd - 1] == ' ' || text[keyEnd - 1] == '\t'))
                --keyEnd;
            if (keyEnd > 0 && text[keyEnd - 1] == '"')
                --keyEnd; // JSON 的 "key":
            const size_t keyStart = keyEnd;
            while (keyEnd > 0 && IsKeyChar(text[keyEnd - 1]))
                --keyEnd;
            if (keyEnd == keyStart)
                continue; // 冒号左边没有键名

            // 右侧：跳空白 → 取一段候选值
            size_t j = i + 1;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t'))
                ++j;
            if (j >= text.size())
                continue;

            std::string value;
            if (text[j] == '"')
            {
                size_t k = j + 1;
                while (k < text.size() && text[k] != '"' && text[k] != '\r' && text[k] != '\n')
                    ++k;
                value = text.substr(j + 1, k - j - 1);
            }
            else
            {
                size_t k = j;
                while (k < text.size() && text[k] != '\r' && text[k] != '\n' && text[k] != ',' && text[k] != ';')
                    ++k;
                value = text.substr(j, k - j);
            }
            consider(value);
        }
        return out;
    }

    // 读取文件后扫描；文件不存在或读失败返回空。
    [[nodiscard]] static std::vector<std::string> ScanFile(const std::string& path)
    {
        std::string text;
        if (!FileSystem::ReadText(path, text))
            return {};
        return Scan(text);
    }

  private:
    [[nodiscard]] static bool IsKeyChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }
    [[nodiscard]] static std::string Trim(std::string_view s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
            ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
            --e;
        return std::string(s.substr(b, e - b));
    }
    [[nodiscard]] static std::string StripQuotes(std::string s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
        return s;
    }
};

// 一次变更检测的结果：相对 Root 的三组相对路径（各自字典序、互不重叠）。
struct AssetChangeSet
{
    std::vector<std::string> added;    // 磁盘上有、索引里没有 → 需要 Import
    std::vector<std::string> modified; // 两边都在、但文件（mtime/体积）变了 → 需要重新 Import
    std::vector<std::string> removed;  // 索引里有、磁盘上没了 → 需要 Remove

    [[nodiscard]] bool Empty() const { return added.empty() && modified.empty() && removed.empty(); }
    [[nodiscard]] size_t TotalCount() const { return added.size() + modified.size() + removed.size(); }
};

// 一条断链记录：owner 引用了一个当前无法解析的资产。
struct BrokenReference
{
    std::string ownerPath;     // 引用者（相对 Root）
    std::string referencePath; // 被引用的原始路径写法（相对 Root，提示用户去哪儿找）
};

// 一次拓扑加载顺序计算的结果（见 AssetDatabase::ComputeLoadOrder）。
struct AssetLoadOrder
{
    std::vector<std::string> order;  // 加载顺序：每个资产的依赖都排在它之前（同层按字典序，可重现）
    std::vector<std::string> cycles; // 处于引用环上的资产（不存在合法加载顺序），字典序

    [[nodiscard]] bool HasCycles() const { return !cycles.empty(); }
};

// 一个待打包资产条目：相对路径 + 类别 + 体积（字节）。
struct AssetBundleEntry
{
    std::string path;
    AssetKind kind = AssetKind::Unknown;
    uint64_t size = 0;
};

// 一份打包清单：根资产及其**传递依赖**的完整集合（含根自身），按路径字典序（见 CollectBundle）。
struct AssetBundle
{
    std::vector<AssetBundleEntry> entries; // 含根自身 + 全部传递依赖
    uint64_t totalBytes = 0;               // entries 体积合计（打包前的体积预估）

    [[nodiscard]] size_t Count() const { return entries.size(); }
};

// 一条资产健康审计发现的问题。
struct AssetIssue
{
    enum class Severity
    {
        Error,   // 阻断性：断链、引用环
        Warning, // 建议处理：孤儿资产、孤儿 .meta
        Info,    // 提示性：暂未使用
    };
    Severity severity = Severity::Info;
    std::string category; // "broken-ref" | "cycle" | "duplicate-guid" | "orphan" | "stale-meta"
    std::string path;     // 涉及的主资产相对路径（断链为引用者）
    std::string detail;   // 补充说明（断链为被引用的原始写法）
};

// 一组共享同一 GUID 的资产（≥2 个路径，字典序）：Explorer 连同 .meta 复制粘贴的典型后果。
struct DuplicateGuidGroup
{
    Guid guid;                      // 被共享的身份
    std::vector<std::string> paths; // 共享它的资产相对路径（≥2，字典序；paths[0] 为修复保留者）
};

// 资产数据库：路径 ↔ GUID + 引用图 + 断链检测 + 索引持久化。
class AssetDatabase
{
  public:
    void SetRoot(std::string root)
    {
        root_ = NormalizePath(root);
        while (!root_.empty() && root_.back() == '/')
            root_.pop_back();
        relCache_.clear();
    }
    [[nodiscard]] const std::string& Root() const { return root_; }

    // 磁盘绝对路径 = Root + 相对路径。
    [[nodiscard]] std::string AbsoluteOf(const std::string& relativePath) const
    {
        const std::string rel = NormalizePath(relativePath);
        if (rel.empty())
            return root_;
        if (!root_.empty())
            return NormalizePath(JoinPath(root_, rel));
        return rel;
    }

    // ---- 导入 ----
    // 导入单个资产：登记 GUID（复用 .meta）+ 扫描引用 + 解析引用 + 愈合历史断链。
    // 幂等；返回 false 表示文件不存在（或路径为空）。
    bool Import(const std::string& relativePath)
    {
        if (!Register(relativePath))
            return false;
        ReresolveAll();
        return true;
    }

    // 递归导入 Root 下所有文件（.meta 旁车跳过）。返回导入数量。
    // 先全量登记再统一解析一次，因此**导入顺序无关**（先导入材质也不会留下假断链）。
    size_t ImportAll()
    {
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root_, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            files.push_back(entry.path().string());
        }
        size_t n = 0;
        for (const std::string& abs : files)
        {
            const std::string rel = RelativeOf(abs);
            if (rel.empty() || IsMetaPath(rel))
                continue;
            if (Register(rel))
                ++n;
        }
        ReresolveAll();
        return n;
    }

    // ---- 移动 / 删除 ----
    // 移动资产（调用方需先把文件搬好）：GUID 不变，因此所有已解析的引用自动跟随。
    bool Move(const std::string& from, const std::string& to)
    {
        const std::string oldRel = NormalizePath(from);
        const std::string newRel = NormalizePath(to);
        if (oldRel.empty() || newRel.empty() || oldRel == newRel)
            return false;
        const std::string oldAbs = AbsoluteOf(oldRel);
        const std::string newAbs = AbsoluteOf(newRel);
        if (!guidDb_.MoveAsset(oldAbs, newAbs))
            return false;

        if (auto it = kinds_.find(oldAbs); it != kinds_.end())
        {
            kinds_[newAbs] = it->second;
            kinds_.erase(it);
        }
        if (auto it = sizes_.find(oldAbs); it != sizes_.end())
        {
            sizes_[newAbs] = it->second;
            sizes_.erase(it);
        }
        if (auto it = mtimes_.find(oldAbs); it != mtimes_.end())
        {
            mtimes_[newAbs] = it->second;
            mtimes_.erase(it);
        }
        if (auto it = refs_.find(oldRel); it != refs_.end())
        {
            refs_[newRel] = std::move(it->second);
            refs_.erase(it);
        }
        relCache_.clear();
        ReresolveAll();
        return true;
    }

    bool Remove(const std::string& relativePath)
    {
        const std::string rel = NormalizePath(relativePath);
        const std::string abs = AbsoluteOf(rel);
        if (!guidDb_.RemoveAsset(abs))
            return false;
        kinds_.erase(abs);
        sizes_.erase(abs);
        mtimes_.erase(abs);
        refs_.erase(rel);
        relCache_.clear();
        return true;
    }

    // ---- 查询 ----
    [[nodiscard]] size_t Count() const { return guidDb_.Count(); }

    [[nodiscard]] const Guid* GuidFor(const std::string& relativePath) const
    {
        return guidDb_.Find(AbsoluteOf(relativePath));
    }
    [[nodiscard]] const std::string* PathFor(const Guid& guid) const
    {
        const std::string* abs = guidDb_.FindPath(guid);
        if (abs == nullptr)
            return nullptr;
        const auto it = relCache_.find(*abs);
        if (it != relCache_.end())
            return &it->second;
        return &relCache_.emplace(*abs, RelativeOf(*abs)).first->second;
    }
    [[nodiscard]] AssetKind KindOf(const std::string& relativePath) const
    {
        const auto it = kinds_.find(AbsoluteOf(relativePath));
        return it != kinds_.end() ? it->second : AssetKind::Unknown;
    }
    [[nodiscard]] uint64_t SizeOf(const std::string& relativePath) const
    {
        const auto it = sizes_.find(AbsoluteOf(relativePath));
        return it != sizes_.end() ? it->second : 0;
    }

    // 全部已登记资产的相对路径（按字典序，供资产浏览器列表使用）。
    [[nodiscard]] std::vector<std::string> AllPaths() const
    {
        std::vector<std::string> out;
        out.reserve(kinds_.size());
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            out.push_back(RelativeOf(abs));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // 我引用了谁：按**当前路径**返回（跟随被引用者的移动而更新）。
    [[nodiscard]] std::vector<std::string> DependenciesOf(const std::string& relativePath) const
    {
        std::vector<std::string> out;
        const auto it = refs_.find(NormalizePath(relativePath));
        if (it == refs_.end())
            return out;
        for (const RefEntry& e : it->second)
        {
            if (!e.guid.IsValid())
                continue;
            if (const std::string* p = PathFor(e.guid); p != nullptr)
                out.push_back(*p);
        }
        return out;
    }

    // 谁引用了我（删除前的影响面评估）。
    [[nodiscard]] std::vector<std::string> DependentsOf(const std::string& relativePath) const
    {
        const Guid* g = GuidFor(relativePath);
        if (g == nullptr)
            return {};
        std::vector<std::string> out;
        for (const std::string& abs : guidDb_.DependentsOf(*g))
            out.push_back(RelativeOf(abs));
        return out;
    }

    // 断链：引用了当前无法解析的资产。
    [[nodiscard]] std::vector<BrokenReference> BrokenReferences() const
    {
        std::vector<BrokenReference> out;
        for (const auto& [rel, entries] : refs_)
        {
            for (const RefEntry& e : entries)
            {
                // 两种情形都算断链：① 从未解析成功；② 解析成功过但目标已被删除
                // （GUID 还在、路径没了——这正是「引用追踪」要显影给用户的东西）。
                const bool resolved = e.guid.IsValid() && PathFor(e.guid) != nullptr;
                if (!resolved)
                    out.push_back(BrokenReference{rel, e.rawPath});
            }
        }
        std::sort(out.begin(), out.end(),
                  [](const BrokenReference& a, const BrokenReference& b)
                  {
                      if (a.ownerPath != b.ownerPath)
                          return a.ownerPath < b.ownerPath;
                      return a.referencePath < b.referencePath;
                  });
        return out;
    }
    [[nodiscard]] size_t BrokenCount() const { return BrokenReferences().size(); }

    // ---- 引用闭包（打包 / 删除影响面）----
    // 递归收集某资产**直接+间接**依赖的全部资产（跨多级），返回相对路径、字典序、不含自身。
    // 典型用途：资产打包时收集一个关卡所需的一切；「本资产到底依赖了什么」的全景。
    // 循环引用安全（visited 去重保证收敛）。只统计已解析的引用——断链目标没有可入集的路径。
    [[nodiscard]] std::vector<std::string> CollectDependencies(const std::string& relativePath) const
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> visited;
        visited.insert(NormalizePath(relativePath));
        CollectDependenciesImpl(NormalizePath(relativePath), visited, out);
        std::sort(out.begin(), out.end());
        return out;
    }

    // 递归收集**直接+间接**引用此资产的全部引用者（删除前的完整影响面评估）。循环安全。
    [[nodiscard]] std::vector<std::string> CollectDependents(const std::string& relativePath) const
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> visited;
        visited.insert(NormalizePath(relativePath));
        CollectDependentsImpl(NormalizePath(relativePath), visited, out);
        std::sort(out.begin(), out.end());
        return out;
    }

    // 按类别筛选已登记资产（资产浏览器用），返回相对路径、字典序。
    [[nodiscard]] std::vector<std::string> FindAssetsByKind(AssetKind kind) const
    {
        std::vector<std::string> out;
        for (const auto& [abs, k] : kinds_)
        {
            if (k == kind)
                out.push_back(RelativeOf(abs));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---- 可达性分析（资产清理 / 打包瘦身）----
    // 从一组「根资产」（通常传入所有场景）出发做传递可达性分析，返回**未被任何根直接或
    // 间接引用**的已登记资产（相对路径、字典序）。用途：找出死资产做清理、或精简打包体积。
    // ⚠️ 盲区提示：被运行时按路径硬编码加载（扫描器看不见的字符串拼接路径）的资产也会被
    //    列为孤儿——删除前需人工确认，本函数只负责「引用图上不可达」这一客观事实。
    [[nodiscard]] std::vector<std::string> FindOrphans(const std::vector<std::string>& roots) const
    {
        std::unordered_set<std::string> reachable;
        for (const std::string& r : roots)
        {
            const std::string rel = NormalizePath(r);
            reachable.insert(rel);
            for (const std::string& dep : CollectDependencies(rel))
                reachable.insert(dep);
        }
        std::vector<std::string> out;
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            const std::string rel = RelativeOf(abs);
            if (reachable.count(rel) == 0)
                out.push_back(rel);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---- 打包清单（发布 / 体积预估）----
    // 给定一组根资产，收集「根自身 + 传递依赖」的完整打包清单（含类别与体积），按路径字典序，
    // 并给出体积合计。用途：打包前的内容枚举与体积预估——回答「这个关卡到底要带哪些资产、
    // 一共多大」。未登记的根静默跳过；断链引用没有可打包的目标，天然不入清单（断链另见
    // BrokenReferences）。与 FindOrphans 互补：一个列「要带什么」，一个列「可以删什么」。
    [[nodiscard]] AssetBundle CollectBundle(const std::vector<std::string>& roots) const
    {
        AssetBundle bundle;
        std::unordered_set<std::string> inSet;
        for (const std::string& r : roots)
        {
            const std::string rel = NormalizePath(r);
            if (GuidFor(rel) == nullptr)
                continue; // 未登记的根不入清单
            if (!inSet.insert(rel).second)
                continue;
            for (const std::string& dep : CollectDependencies(rel))
                inSet.insert(dep);
        }
        bundle.entries.reserve(inSet.size());
        for (const std::string& rel : inSet)
        {
            bundle.entries.push_back(AssetBundleEntry{rel, KindOf(rel), SizeOf(rel)});
            bundle.totalBytes += SizeOf(rel);
        }
        std::sort(bundle.entries.begin(), bundle.entries.end(),
                  [](const AssetBundleEntry& a, const AssetBundleEntry& b) { return a.path < b.path; });
        return bundle;
    }

    // ---- 加载顺序（拓扑排序 / 环检测）----
    // 给定一组根资产，返回其**传递依赖闭包**的拓扑加载顺序：每个资产的所有依赖都排在它之前，
    // 同层之间按字典序决胜——同一引用图必然得到同一顺序，便于测试与问题复现。
    // 典型用途：资源系统按此序加载，材质引用贴图时贴图必然已就绪；打包按此序写入便于流式读取。
    //
    // 环检测：引用环上的资产不存在合法加载顺序（A 等 B、B 等 A），一律不进 order，
    // 单独列入 cycles——环几乎总是内容出错的信号，应当显影给工具链而不是静默跳过。
    // 未登记的根静默跳过；断链引用没有可加载的目标，天然不进序。
    [[nodiscard]] AssetLoadOrder ComputeLoadOrder(const std::vector<std::string>& roots) const
    {
        AssetLoadOrder result;

        // 1. 可达集 = 根 ∪ 根的传递依赖（只含已登记资产；CollectDependencies 自带 visited，环安全）
        std::unordered_set<std::string> inSet;
        for (const std::string& r : roots)
        {
            const std::string rel = NormalizePath(r);
            if (GuidFor(rel) == nullptr)
                continue; // 未登记的根没什么可排序的
            if (inSet.insert(rel).second)
            {
                for (const std::string& dep : CollectDependencies(rel))
                    inSet.insert(dep);
            }
        }

        // 2. 建图：边 dep → owner 表示「dep 必须先于 owner 加载」；indeg[owner] = 集合内依赖数
        std::unordered_map<std::string, size_t> indeg;
        std::unordered_map<std::string, std::vector<std::string>> dependents; // dep → 依赖它的资产们
        for (const std::string& rel : inSet)
            indeg.emplace(rel, 0);
        for (const std::string& owner : inSet)
        {
            for (const std::string& dep : DependenciesOf(owner))
            {
                if (inSet.count(dep) == 0)
                    continue; // 防御：闭包理论上已含全部已解析依赖
                dependents[dep].push_back(owner);
                ++indeg[owner];
            }
        }

        // 3. Kahn：小顶堆保证同层按字典序出队（输出确定性）
        std::priority_queue<std::string, std::vector<std::string>, std::greater<std::string>> ready;
        for (const auto& [rel, d] : indeg)
        {
            if (d == 0)
                ready.push(rel);
        }
        while (!ready.empty())
        {
            const std::string cur = ready.top();
            ready.pop();
            result.order.push_back(cur);
            const auto it = dependents.find(cur);
            if (it == dependents.end())
                continue;
            for (const std::string& nxt : it->second)
            {
                if (--indeg[nxt] == 0)
                    ready.push(nxt);
            }
        }

        // 4. 未能出队的即环上资产（自环同理：自己引用自己时入度永远 ≥1）
        for (const auto& [rel, d] : indeg)
        {
            if (d > 0)
                result.cycles.push_back(rel);
        }
        std::sort(result.cycles.begin(), result.cycles.end());
        return result;
    }

    // ---- 变更检测（热重载的基础）----
    // 扫描磁盘 Root 与内存索引的差异，列出 新增 / 修改 / 删除 三组资产相对路径。
    // 「修改」以 (修改时间, 体积) 双重判定：写入通常会更新 mtime；即便 mtime 粒度太粗，体积
    // 变化也能兜底。只读、不落盘、不改动库状态；调用方拿到结果后自行决定 Import / Remove。
    // 用途：编辑器文件监视器触发后，精准增量刷新而非全量 ImportAll（大型资产工程的关键性能）。
    [[nodiscard]] AssetChangeSet ScanForChanges() const
    {
        AssetChangeSet out;

        // 磁盘现状 → added / modified
        std::error_code ec;
        std::unordered_set<std::string> onDisk;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root_, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string rel = RelativeOf(entry.path().string());
            if (rel.empty() || IsMetaPath(rel))
                continue;
            onDisk.insert(rel);
            const std::string abs = AbsoluteOf(rel);
            if (kinds_.find(abs) == kinds_.end())
            {
                out.added.push_back(rel); // 未登记 → 新增
                continue;
            }
            // 已登记：比对 (mtime, 体积)，任一不同即视为修改
            const auto mit = mtimes_.find(abs);
            const auto sit = sizes_.find(abs);
            const bool mtimeDiff = (mit == mtimes_.end()) || (mit->second != LastWriteTime(abs));
            const bool sizeDiff = (sit == sizes_.end()) || (sit->second != FileSystem::GetSize(abs));
            if (mtimeDiff || sizeDiff)
                out.modified.push_back(rel);
        }

        // 索引有、磁盘没 → removed
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            const std::string rel = RelativeOf(abs);
            if (onDisk.count(rel) == 0)
                out.removed.push_back(rel);
        }

        std::sort(out.added.begin(), out.added.end());
        std::sort(out.modified.begin(), out.modified.end());
        std::sort(out.removed.begin(), out.removed.end());
        return out;
    }

    // 把内存索引的 (mtime, 体积) 基线对齐到磁盘现状，**不重新解析引用**。
    // 与 Import 的区别：Import 会重扫文件内容并刷新引用图（较重），SnapshotNow 只同步
    // 时间戳/体积（轻）。用途：外部已完成批量处理后，把「当前磁盘状态」声明为新基线，
    // 使下一次 ScanForChanges 从干净状态起步。
    void SnapshotNow()
    {
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            mtimes_[abs] = LastWriteTime(abs);
            sizes_[abs] = FileSystem::GetSize(abs);
        }
    }

    // 一键应用 ScanForChanges 的结果：added/modified → Import，removed → Remove，
    // 并返回本次实际处理的变更集。让热重载闭环开箱即用——文件监视器触发后一句
    // `db.ApplyChanges()` 即可令索引与磁盘重新一致，无需手写三分支循环。
    AssetChangeSet ApplyChanges()
    {
        const AssetChangeSet changes = ScanForChanges();
        for (const std::string& rel : changes.added)
            Import(rel);
        for (const std::string& rel : changes.modified)
            Import(rel);
        for (const std::string& rel : changes.removed)
            Remove(rel);
        return changes;
    }

    // ---- 孤儿 .meta 清理（维护侧）----
    // 扫描磁盘 Root，找出「主资产已不存在」的孤儿 .meta 旁车（相对路径、字典序）。只读、不删。
    // 触发场景：资产在库外被直接删除/移动（未经 Remove/Move），.meta 旁车残留成孤儿——它们
    // 不参与索引（ImportAll/ScanForChanges 跳过 .meta），却会污染版本控制与打包目录。
    [[nodiscard]] std::vector<std::string> FindStaleMetaFiles() const
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root_, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string rel = RelativeOf(entry.path().string());
            if (!IsMetaPath(rel))
                continue;
            // 主资产路径 = 去掉 .meta 后缀
            const std::string owner = rel.substr(0, rel.size() - 5);
            if (!FileSystem::Exists(AbsoluteOf(owner)))
                out.push_back(rel);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // 删除全部孤儿 .meta，返回删除数量。与 FindStaleMetaFiles 同一次扫描口径，先报后删。
    size_t SweepStaleMetaFiles()
    {
        const std::vector<std::string> stale = FindStaleMetaFiles();
        size_t n = 0;
        for (const std::string& rel : stale)
        {
            if (FileSystem::Remove(AbsoluteOf(rel)))
                ++n;
        }
        return n;
    }

    // ---- GUID 完整性（复制粘贴撞车的检测与确定性修复）----
    // Explorer / 编辑器外「复制-粘贴」资产会连同 .meta 一起复制 → 两个文件共享同一 GUID。
    // GuidForPath 的撞车自愈虽会在导入时为后登记者重新生成身份，但「谁保住原 GUID」取决于
    // 导入顺序——原文件的既有引用可能被静默转移到副本上。此处提供不依赖导入状态的磁盘级
    // 检测与确定性修复。
    [[nodiscard]] std::vector<DuplicateGuidGroup> FindDuplicateGuids() const
    {
        std::unordered_map<Guid, std::vector<std::string>, GuidHash> byGuid;
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root_, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;
            const std::string rel = RelativeOf(entry.path().string());
            if (!IsMetaPath(rel))
                continue;
            const std::string owner = rel.substr(0, rel.size() - 5);
            Guid g;
            if (!AssetGuidMeta::Read(AbsoluteOf(owner), g) || !g.IsValid())
                continue; // 缺失/损坏的 .meta 由导入自愈负责；此处只统计有效 GUID 的撞车
            byGuid[g].push_back(owner);
        }
        std::vector<DuplicateGuidGroup> out;
        for (auto& [g, paths] : byGuid)
        {
            if (paths.size() < 2)
                continue;
            std::sort(paths.begin(), paths.end());
            out.push_back(DuplicateGuidGroup{g, std::move(paths)});
        }
        std::sort(out.begin(), out.end(), [](const DuplicateGuidGroup& a, const DuplicateGuidGroup& b)
                  { return a.paths.front() < b.paths.front(); });
        return out;
    }

    // 确定性修复：每组保留**字典序最小路径**的 GUID（复制场景下通常即原文件），其余资产
    // 重新生成身份并重新导入，返回被重分配身份的资产数。顺序设计：先摘除副本登记（连带删其
    // 共享 .meta），再兜底导入保留者——如此即使「保留者未登记而副本已登记」，身份归属依旧
    // 确定。已登记身份永不被剥夺；保留者 GUID 不变 ⇒ 指向它的既有引用零影响。
    size_t RepairDuplicateGuids()
    {
        size_t repaired = 0;
        for (const DuplicateGuidGroup& group : FindDuplicateGuids())
        {
            for (size_t i = 1; i < group.paths.size(); ++i)
                guidDb_.RemoveAsset(AbsoluteOf(group.paths[i]));
            Import(group.paths.front());
            for (size_t i = 1; i < group.paths.size(); ++i)
            {
                Import(group.paths[i]);
                ++repaired;
            }
        }
        return repaired;
    }

    // ---- 健康审计（聚合既有单点检测，一次调用拿到全部问题）----
    // 汇总五类已验证的检测原语，输出按严重度分级、确定性排序的问题清单。供编辑器「项目健康」
    // 面板或 CI 门禁使用——不必逐个调用 BrokenReferences/ComputeLoadOrder/FindDuplicateGuids/
    // FindOrphans/FindStaleMetaFiles 再自行拼接。聚合规则：
    //   断链（BrokenReferences）          → Error   阻断加载，必须先修
    //   引用环（ComputeLoadOrder 全图）   → Error   无合法加载顺序
    //   重复 GUID（FindDuplicateGuids）   → Error   复制粘贴导致的身份撞车
    //   孤儿 .meta（FindStaleMetaFiles）  → Warning 库外删资产的残留
    //   孤儿资产（FindOrphans，根=全部场景）→ Warning 可清理的冗余内容
    // 排序：severity(Error 先) → category → path → detail，同一库状态必然得到同一份报告。
    // 注意：审计是只读快照（含 FindStaleMetaFiles 的一次磁盘扫描），不做任何修复；修复见各
    // 单点 API（Import 愈断链 / SweepStaleMetaFiles 清孤儿 .meta / Move 保引用等）。
    [[nodiscard]] std::vector<AssetIssue> Audit() const
    {
        std::vector<AssetIssue> issues;

        // 断链（含原始写法，提示用户去哪儿找被引用的文件）
        for (const BrokenReference& b : BrokenReferences())
            issues.push_back(AssetIssue{AssetIssue::Severity::Error, "broken-ref", b.ownerPath, b.referencePath});

        // 引用环：以全部已登记资产为根跑拓扑排序，未出队的即环上成员
        const AssetLoadOrder lo = ComputeLoadOrder(AllPaths());
        for (const std::string& c : lo.cycles)
            issues.push_back(AssetIssue{AssetIssue::Severity::Error, "cycle", c, {}});

        // 重复 GUID：多资产共享同一身份（Explorer 连同 .meta 复制粘贴的典型后果）
        for (const DuplicateGuidGroup& g : FindDuplicateGuids())
        {
            for (const std::string& p : g.paths)
                issues.push_back(AssetIssue{AssetIssue::Severity::Error, "duplicate-guid", p, g.guid.ToString()});
        }

        // 孤儿 .meta：主资产已被库外删除的旁车
        for (const std::string& m : FindStaleMetaFiles())
            issues.push_back(AssetIssue{AssetIssue::Severity::Warning, "stale-meta", m, {}});

        // 孤儿资产：不被任何场景（入口）直接或间接引用
        for (const std::string& o : FindOrphans(FindAssetsByKind(AssetKind::Scene)))
            issues.push_back(AssetIssue{AssetIssue::Severity::Warning, "orphan", o, {}});

        std::sort(issues.begin(), issues.end(),
                  [](const AssetIssue& a, const AssetIssue& b)
                  {
                      const auto sev = [](AssetIssue::Severity s)
                      {
                          switch (s)
                          {
                          case AssetIssue::Severity::Error:
                              return 0;
                          case AssetIssue::Severity::Warning:
                              return 1;
                          default:
                              return 2;
                          }
                      };
                      if (sev(a.severity) != sev(b.severity))
                          return sev(a.severity) < sev(b.severity);
                      if (a.category != b.category)
                          return a.category < b.category;
                      if (a.path != b.path)
                          return a.path < b.path;
                      return a.detail < b.detail;
                  });
        return issues;
    }

    // ---- 快照缓存持久化（热启动提速）----
    // 与 SaveIndex（仅持久化 GUID 映射，供无 .meta 场景）不同，快照缓存持久化**完整内存状态**：
    // kinds_/sizes_/mtimes_/refs_（引用图）。热启动时 LoadCache() 逐行校验文件存在性 +
    // (mtime,size) 一致性，全对则直接重建内存、零内容读取；再配合 ApplyChanges() 即可构成
    // 「读缓存 → 校验 → 增量应用变更」的完整快速启动路径。
    // 纯文本格式（UTF-8，LF）：首行版本头，随后每行一条
    // "guid<TAB>rel<TAB>kind<TAB>size<TAB>mtime<TAB>refCount<TAB>ref..." 引用条目为 "raw<TAB>resolvedHex"，resolved
    // 无效时记为 "-"（断链）。按 GUID 排序保证可重现。
    bool SaveCache(const std::string& path) const
    {
        std::string text = "# BigHero asset cache v1\n";
        // 按 GUID 排序收集所有已登记资产
        std::vector<std::pair<std::string, std::string>> rows; // (guidHex, rel)
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            if (const Guid* g = guidDb_.Find(abs); g != nullptr)
                rows.emplace_back(g->ToString(), RelativeOf(abs));
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& [guidHex, rel] : rows)
        {
            const std::string abs = AbsoluteOf(rel);
            const AssetKind kind = KindOf(rel);
            const uint64_t size = SizeOf(rel);
            const int64_t mtime = [&]() -> int64_t
            {
                const auto it = mtimes_.find(abs);
                return it != mtimes_.end() ? static_cast<int64_t>(it->second.time_since_epoch().count()) : 0;
            }();
            const auto refIt = refs_.find(rel);
            const size_t refCount = refIt != refs_.end() ? refIt->second.size() : 0;

            text += guidHex;
            text += '\t';
            text += rel;
            text += '\t';
            text += std::to_string(static_cast<int>(kind));
            text += '\t';
            text += std::to_string(size);
            text += '\t';
            text += std::to_string(mtime);
            text += '\t';
            text += std::to_string(refCount);
            if (refIt != refs_.end())
            {
                for (const RefEntry& e : refIt->second)
                {
                    text += '\t';
                    text += e.rawPath;
                    text += '\t';
                    text += e.guid.IsValid() ? e.guid.ToString() : "-";
                }
            }
            text += '\n';
        }
        return FileSystem::WriteText(path, text);
    }

    // 从快照缓存热启动：逐行校验文件存在性与 (mtime,size) 一致性。任一条不符即整体回退
    // 返回 false（调用方应降级为 ImportAll() 全量导入）。全部通过则重建内存并返回 true。
    // 注意：本函数不做任何文件内容读取，仅 stat 校验——这是热启动提速的核心。
    bool LoadCache(const std::string& path)
    {
        std::string text;
        if (!FileSystem::ReadText(path, text))
            return false;
        // 先清空，避免半初始化状态
        Clear();
        // 任一校验失败：回到全空状态再返回 false，保证调用方降级 ImportAll 时是干净起点
        const auto fail = [this]()
        {
            Clear();
            return false;
        };
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos)
                eol = text.size();
            std::string line = text.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            pos = eol + 1;
            if (line.empty() || line[0] == '#')
                continue;

            // 解析 6 个固定字段 + 变长引用
            std::vector<std::string> fields;
            size_t fpos = 0;
            while (fpos <= line.size())
            {
                const size_t tab = line.find('\t', fpos);
                if (tab == std::string::npos)
                {
                    fields.push_back(line.substr(fpos));
                    break;
                }
                fields.push_back(line.substr(fpos, tab - fpos));
                fpos = tab + 1;
            }
            if (fields.size() < 6)
                return fail(); // 格式损坏（最少 6 个固定字段：guid/rel/kind/size/mtime/refCount）

            Guid g;
            if (!Guid::TryParse(fields[0], g))
                return fail();
            const std::string rel = NormalizePath(fields[1]);
            const std::string abs = AbsoluteOf(rel);
            const int kindInt = std::atoi(fields[2].c_str());
            const uint64_t size = static_cast<uint64_t>(std::strtoull(fields[3].c_str(), nullptr, 10));
            const int64_t mtimeCount = std::strtoll(fields[4].c_str(), nullptr, 10);
            const size_t refCount = static_cast<size_t>(std::strtoull(fields[5].c_str(), nullptr, 10));

            // 校验：文件必须存在
            if (!FileSystem::Exists(abs))
                return fail();
            // 校验：size 与 mtime 必须与缓存一致
            const uint64_t curSize = static_cast<uint64_t>(FileSystem::GetSize(abs));
            if (curSize != size)
                return fail();
            std::error_code fec;
            const auto ftime = std::filesystem::last_write_time(abs, fec);
            if (fec)
                return fail();
            const int64_t curMtime = static_cast<int64_t>(ftime.time_since_epoch().count());
            if (curMtime != mtimeCount)
                return fail();

            // 校验通过：重建内存
            kinds_[abs] = static_cast<AssetKind>(kindInt);
            sizes_[abs] = size;
            mtimes_[abs] = ftime;
            // 重建引用表（raw 与 resolved；resolved 无效则留空待 ReresolveAll 自愈）
            if (refCount > 0 && fields.size() >= 6 + refCount * 2)
            {
                std::vector<RefEntry> entries;
                std::vector<Guid> resolved;
                entries.reserve(refCount);
                resolved.reserve(refCount);
                for (size_t i = 0; i < refCount; ++i)
                {
                    const std::string& raw = fields[5 + i * 2 + 1];
                    const std::string& resHex = fields[5 + i * 2 + 2];
                    Guid rg;
                    const bool valid = resHex != "-" && Guid::TryParse(resHex, rg);
                    entries.push_back(RefEntry{raw, valid ? rg : Guid{}});
                    resolved.push_back(valid ? rg : Guid{});
                }
                refs_[rel] = std::move(entries);
                guidDb_.SetReferences(abs, resolved);
            }
            // 登记到 guidDb：读现有 .meta 登记（零副作用），并校验身份与缓存一致——
            // 不一致说明 .meta 在库外被改写，缓存不可信，整体回退。
            const Guid actual = guidDb_.GuidForPath(abs);
            if (!(actual == g))
                return fail();
        }
        // 缓存加载完成：引用中可能存在断链（目标文件后来被删），重解析一次自愈
        ReresolveAll();
        return true;
    }

    // 便捷启动路径：先尝试 LoadCache，失败则降级 ImportAll。返回 true 表示走了缓存（热启动）。
    bool WarmStart(const std::string& cachePath)
    {
        if (LoadCache(cachePath))
            return true;
        ImportAll();
        return false;
    }

    // ---- 索引持久化 ----
    // 纯文本格式（UTF-8，LF）："guid<TAB>relativePath" 每行一条，按 GUID 排序保证可重现。
    // 用于在「没有 .meta 旁车」的场景（打包产物、只读资源目录）也能保持 GUID 稳定。
    bool SaveIndex(const std::string& path) const
    {
        std::string text = "# BigHero asset index v1\n";
        std::vector<std::pair<std::string, std::string>> rows; // (guid, rel)
        for (const auto& [abs, kind] : kinds_)
        {
            (void)kind;
            if (const Guid* g = guidDb_.Find(abs); g != nullptr)
                rows.emplace_back(g->ToString(), RelativeOf(abs));
        }
        std::sort(rows.begin(), rows.end());
        for (const auto& [guid, rel] : rows)
        {
            text += guid;
            text += '\t';
            text += rel;
            text += '\n';
        }
        return FileSystem::WriteText(path, text);
    }

    // 载入索引：把索引里的 (guid, rel) 覆写回 .meta，保证跨机器/跨打包的身份一致。
    // 索引中的相对路径若当前不存在，仅跳过（保留 .meta 原值，待文件回来时自动愈合）。
    bool LoadIndex(const std::string& path)
    {
        std::string text;
        if (!FileSystem::ReadText(path, text))
            return false;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos)
                eol = text.size();
            std::string line = text.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            pos = eol + 1;
            if (line.empty() || line[0] == '#')
                continue;
            const size_t tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            Guid g;
            if (!Guid::TryParse(std::string_view(line).substr(0, tab), g))
                continue;
            const std::string abs = AbsoluteOf(NormalizePath(std::string_view(line).substr(tab + 1)));
            if (FileSystem::Exists(abs))
            {
                AssetGuidMeta::Write(abs, g); // 以索引为准
                guidDb_.GuidForPath(abs);
            }
        }
        return true;
    }

    void Clear()
    {
        guidDb_.Clear();
        kinds_.clear();
        sizes_.clear();
        mtimes_.clear();
        refs_.clear();
        relCache_.clear();
    }

  private:
    // 一条引用记录：原始路径写法 + 解析结果。解析成功后只认 GUID（见文件头「关键设计」）。
    struct RefEntry
    {
        std::string rawPath;
        Guid guid{};
    };

    [[nodiscard]] static bool IsMetaPath(const std::string& rel)
    {
        return rel.size() >= 5 && rel.compare(rel.size() - 5, 5, ".meta") == 0;
    }

    // 文件最后修改时间。读取失败返回零值，使比对必然视为「不同」（保守：宁可误判为已修改）。
    [[nodiscard]] static std::filesystem::file_time_type LastWriteTime(const std::string& abs)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(abs, ec);
        return ec ? std::filesystem::file_time_type{} : t;
    }

    [[nodiscard]] std::string RelativeOf(const std::string& absPath) const
    {
        const std::string norm = NormalizePath(absPath);
        if (root_.empty())
            return norm;
        if (norm.size() > root_.size() && norm.compare(0, root_.size(), root_) == 0 && norm[root_.size()] == '/')
            return norm.substr(root_.size() + 1);
        if (norm == root_)
            return {};
        return norm;
    }

    // 登记一个资产：GUID（复用 .meta）+ 类别 + 体积 + 扫描出的原始引用（不做解析）。
    bool Register(const std::string& relativePath)
    {
        const std::string rel = NormalizePath(relativePath);
        if (rel.empty())
            return false;
        const std::string abs = AbsoluteOf(rel);
        if (!FileSystem::Exists(abs) || FileSystem::IsDirectory(abs))
            return false;

        guidDb_.GuidForPath(abs);
        kinds_[abs] = KindFromPath(rel);
        sizes_[abs] = FileSystem::GetSize(abs);
        mtimes_[abs] = LastWriteTime(abs); // 记录导入时刻的修改时间，供 ScanForChanges 判定「修改」

        // 以当前文件内容为准**真·刷新**引用表（re-import 语义）：同写法的既有条目保留其已解析
        // GUID（移动后不退化成断链），文件中已不存在的旧引用则丢弃——否则热重载删掉一行引用，
        // 旧引用会残留成永远的假断链。
        std::vector<RefEntry> oldEntries;
        const auto it = refs_.find(rel);
        if (it != refs_.end())
            oldEntries = std::move(it->second);
        std::vector<RefEntry> merged;
        for (const std::string& raw : AssetRefScanner::ScanFile(abs))
        {
            Guid resolved{};
            const auto oldIt =
                std::find_if(oldEntries.begin(), oldEntries.end(), [&](const RefEntry& e) { return e.rawPath == raw; });
            if (oldIt != oldEntries.end())
                resolved = oldIt->guid; // 同一引用写法，保留已解析的 GUID
            merged.push_back(RefEntry{raw, resolved});
        }
        if (merged.empty())
            refs_.erase(rel);
        else
            refs_[rel] = std::move(merged);
        return true;
    }

    // 重新解析所有**尚未解析成功**的引用（已解析的一律保持，这是引用跟随移动的关键）。
    void ReresolveAll()
    {
        for (auto& [rel, entries] : refs_)
        {
            std::vector<Guid> resolved;
            for (RefEntry& e : entries)
            {
                if (!e.guid.IsValid())
                {
                    const std::string candidate = ResolveReference(rel, e.rawPath);
                    if (const Guid* g = GuidFor(candidate); g != nullptr)
                        e.guid = *g;
                    else if (FileSystem::Exists(AbsoluteOf(candidate)))
                        e.guid = guidDb_.GuidForPath(AbsoluteOf(candidate));
                }
                resolved.push_back(e.guid);
            }
            guidDb_.SetReferences(AbsoluteOf(rel), resolved);
        }
    }

    // 引用路径解析：优先按引用者所在目录，其次相对 Root；都不通则原样返回（记为断链）。
    [[nodiscard]] std::string ResolveReference(const std::string& ownerRel, const std::string& raw) const
    {
        const std::string parent = GetParentDir(ownerRel);
        const std::string sibling = parent.empty() ? NormalizePath(raw) : NormalizePath(JoinPath(parent, raw));
        const std::string rootBased = NormalizePath(raw);
        if (GuidFor(sibling) != nullptr)
            return sibling;
        if (GuidFor(rootBased) != nullptr)
            return rootBased;
        if (FileSystem::Exists(AbsoluteOf(sibling)))
            return sibling;
        if (FileSystem::Exists(AbsoluteOf(rootBased)))
            return rootBased;
        return sibling; // 都不通：返回按引用者目录的写法，供断链报告显示
    }

    // CollectDependencies 的深度优先递归（visited 以相对路径为键；路径 ↔ GUID 一一对应）。
    void CollectDependenciesImpl(const std::string& rel, std::unordered_set<std::string>& visited,
                                 std::vector<std::string>& out) const
    {
        const auto it = refs_.find(NormalizePath(rel));
        if (it == refs_.end())
            return;
        for (const RefEntry& e : it->second)
        {
            if (!e.guid.IsValid())
                continue;
            const std::string* p = PathFor(e.guid);
            if (p == nullptr)
                continue; // 断链目标不入集
            // 立即拷贝：PathFor 可能向 relCache_ 插入新条目触发 rehash，令裸指针悬空（A2 契约）。
            const std::string depPath = *p;
            if (visited.insert(depPath).second)
            {
                out.push_back(depPath);
                CollectDependenciesImpl(depPath, visited, out);
            }
        }
    }

    // CollectDependents 的深度优先递归。
    void CollectDependentsImpl(const std::string& rel, std::unordered_set<std::string>& visited,
                               std::vector<std::string>& out) const
    {
        const Guid* g = GuidFor(rel);
        if (g == nullptr)
            return;
        const Guid target = *g; // 立即拷贝，规避裸指针生命周期
        for (const std::string& abs : guidDb_.DependentsOf(target))
        {
            const std::string dep = RelativeOf(abs);
            if (visited.insert(dep).second)
            {
                out.push_back(dep);
                CollectDependentsImpl(dep, visited, out);
            }
        }
    }

    [[nodiscard]] static AssetKind KindFromPath(const std::string& rel)
    {
        const std::string ext = GetExtension(rel, false);
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp" || ext == "hdr" ||
            ext == "ktx" || ext == "dds")
            return AssetKind::Texture;
        if (ext == "gltf" || ext == "glb" || ext == "obj" || ext == "fbx")
            return AssetKind::Mesh;
        if (ext == "mat" || ext == "mtl")
            return AssetKind::Material;
        if (ext == "vert" || ext == "frag" || ext == "glsl" || ext == "spv" || ext == "comp")
            return AssetKind::Shader;
        if (ext == "json" || ext == "scene" || ext == "prefab")
            return AssetKind::Scene;
        if (ext == "wav" || ext == "mp3" || ext == "ogg")
            return AssetKind::Audio;
        if (ext == "ttf" || ext == "otf")
            return AssetKind::Font;
        if (ext == "anim")
            return AssetKind::Animation;
        return AssetKind::Unknown;
    }

    std::string root_;
    AssetGuidDatabase guidDb_;
    std::unordered_map<std::string, AssetKind> kinds_;                        // abs → kind
    std::unordered_map<std::string, uint64_t> sizes_;                         // abs → size
    std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_; // abs → 导入时的文件修改时间
    std::unordered_map<std::string, std::vector<RefEntry>> refs_;             // rel → 引用表
    mutable std::unordered_map<std::string, std::string> relCache_;           // abs → rel（PathFor 返回引用用）
};
} // namespace BigHero::Core
