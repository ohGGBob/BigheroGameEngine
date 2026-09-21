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

// 一条断链记录：owner 引用了一个当前无法解析的资产。
struct BrokenReference
{
    std::string ownerPath;     // 引用者（相对 Root）
    std::string referencePath; // 被引用的原始路径写法（相对 Root，提示用户去哪儿找）
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

        // 已有的解析结果要保留（移动后不能退化成断链），只把新扫描到的原始路径补进来。
        std::vector<RefEntry> merged;
        const auto it = refs_.find(rel);
        if (it != refs_.end())
            merged = std::move(it->second);
        for (const std::string& raw : AssetRefScanner::ScanFile(abs))
        {
            if (std::any_of(merged.begin(), merged.end(), [&](const RefEntry& e) { return e.rawPath == raw; }))
                continue;
            merged.push_back(RefEntry{raw, Guid{}});
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
    std::unordered_map<std::string, AssetKind> kinds_;              // abs → kind
    std::unordered_map<std::string, uint64_t> sizes_;               // abs → size
    std::unordered_map<std::string, std::vector<RefEntry>> refs_;   // rel → 引用表
    mutable std::unordered_map<std::string, std::string> relCache_; // abs → rel（PathFor 返回引用用）
};
} // namespace BigHero::Core
