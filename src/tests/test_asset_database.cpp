// 资产数据库（core/AssetDatabase.h）单元测试：引用解析 / 断链检测 / 移动不断链 /
// 索引持久化。纯 CPU + 临时目录文件 IO，无 GPU/窗口依赖。
#include "framework/test_common.h"

#include "core/AssetDatabase.h"

#include <filesystem>

using namespace BigHero;
namespace fs = std::filesystem;

namespace
{
// 建一个小型资产工程：一张贴图 + 一个引用它的材质 + 一个引用网格的场景。
// 材质还引用一张并不存在的法线贴图，用于验证断链检测。
bool MakeProject(const fs::path& root, std::error_code& ec)
{
    fs::remove_all(root, ec);
    if (!fs::create_directories(root / "textures", ec))
        return false;
    if (!fs::create_directories(root / "materials", ec))
        return false;
    if (!fs::create_directories(root / "models", ec))
        return false;
    if (!fs::create_directories(root / "scenes", ec))
        return false;

    // 贴图（内容随意，导入只关心存在与体积）
    if (!Core::FileSystem::WriteText((root / "textures" / "brick.png").string(), "PNG-FAKE"))
        return false;
    // 材质：键值写法
    if (!Core::FileSystem::WriteText((root / "materials" / "brick.mat").string(),
                                     "# brick material\n"
                                     "albedo: textures/brick.png\n"
                                     "normal: textures/brick_normal.png\n"))
        return false;
    // 场景：JSON 写法
    if (!Core::FileSystem::WriteText((root / "scenes" / "level.json").string(),
                                     "{\n"
                                     "  \"mesh\": \"models/room.glb\",\n"
                                     "  \"material\": \"materials/brick.mat\"\n"
                                     "}\n"))
        return false;
    // 网格（叶子资产，无人引用它之外的东西）
    if (!Core::FileSystem::WriteText((root / "models" / "room.glb").string(), "GLB-FAKE"))
        return false;
    return true;
}

bool Contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

TEST_CASE("AssetDb.RefScanner")
{
    // 键值写法（YAML / MTL 风格）
    const auto kv = Core::AssetRefScanner::Scan("albedo: textures/a.png\nroughness: 0.5\nnormal:  b/c.tga\n");
    CHECK_EQ(kv.size(), 2u);
    CHECK(Contains(kv, "textures/a.png"));
    CHECK(Contains(kv, "b/c.tga"));

    // JSON 写法（带引号）
    const auto js = Core::AssetRefScanner::Scan("{\"mesh\": \"models/m.glb\", \"count\": 3, \"mat\": \"m.mat\"}");
    CHECK_EQ(js.size(), 2u);
    CHECK(Contains(js, "models/m.glb"));
    CHECK(Contains(js, "m.mat"));

    // 非资产扩展名不算引用；数字值不算引用
    const auto none = Core::AssetRefScanner::Scan("count: 5\nname: hello\nnote: \"plain text\"");
    CHECK_EQ(none.size(), 0u);

    // 去重（保持首次出现顺序）
    const auto dup = Core::AssetRefScanner::Scan("a: t.png\nb: t.png\n");
    CHECK_EQ(dup.size(), 1u);

    // 分隔符与大小写归一
    const auto mixed = Core::AssetRefScanner::Scan(R"(path: tex\Sub\A.PNG)");
    CHECK_EQ(mixed.size(), 1u);
    CHECK_EQ(mixed[0], std::string("tex/Sub/A.PNG"));

    // 冒号的误伤防护：作用域符与 URL 不应被当成键值
    const auto tricky = Core::AssetRefScanner::Scan("ns::value: t.png\nurl: http://x/y.png\nok: real.png");
    CHECK(Contains(tricky, "t.png")); // ns::value 的键值对仍要被识别
    CHECK(Contains(tricky, "real.png"));
    CHECK_EQ(tricky.size(), 3u); // 第三条是把 URL 里的 .png 当成了引用（宁可多报，不可漏报）
    // "::" 不得产生形如 ":value: t.png" 的畸形条目
    for (const auto& s : tricky)
        CHECK(!s.empty() && s[0] != ':');

    // 扩展名判定
    CHECK(Core::AssetRefScanner::IsAssetExtension("PNG"));
    CHECK(Core::AssetRefScanner::IsAssetExtension("glb"));
    CHECK(!Core::AssetRefScanner::IsAssetExtension("txt"));

    // 文件不存在时返回空
    CHECK_EQ(Core::AssetRefScanner::ScanFile("no/such/file.mat").size(), 0u);
}

TEST_CASE("AssetDb.ImportAndDependencies")
{
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_deps";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    const size_t n = db.ImportAll();
    CHECK_EQ(n, 4u); // 4 个资产（.meta 旁车不算）
    CHECK_EQ(db.Count(), 4u);

    // 类别推断
    CHECK_EQ(int(db.KindOf("textures/brick.png")), int(Core::AssetKind::Texture));
    CHECK_EQ(int(db.KindOf("materials/brick.mat")), int(Core::AssetKind::Material));
    CHECK_EQ(int(db.KindOf("models/room.glb")), int(Core::AssetKind::Mesh));
    CHECK_EQ(int(db.KindOf("scenes/level.json")), int(Core::AssetKind::Scene));
    CHECK_EQ(int(db.KindOf("nothing.xxx")), int(Core::AssetKind::Unknown));
    CHECK_GT(db.SizeOf("textures/brick.png"), 0u);

    // GUID 分配且唯一、可正反查
    const Core::Guid* gBrick = db.GuidFor("textures/brick.png");
    REQUIRE(gBrick != nullptr);
    CHECK(gBrick->IsValid());
    const std::string* back = db.PathFor(*gBrick);
    REQUIRE(back != nullptr);
    CHECK_EQ(*back, std::string("textures/brick.png"));

    // 依赖 / 反向依赖
    const auto deps = db.DependenciesOf("materials/brick.mat");
    CHECK(Contains(deps, "textures/brick.png"));
    const auto dependents = db.DependentsOf("textures/brick.png");
    CHECK_EQ(dependents.size(), 1u);
    CHECK_EQ(dependents[0], std::string("materials/brick.mat"));

    // 场景 → 材质 → 贴图 的两级引用链
    CHECK(Contains(db.DependenciesOf("scenes/level.json"), "materials/brick.mat"));
    CHECK(Contains(db.DependenciesOf("scenes/level.json"), "models/room.glb"));

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.BrokenReferences")
{
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_broken";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // 材质引用了不存在的 brick_normal.png → 1 条断链
    const auto broken = db.BrokenReferences();
    CHECK_EQ(broken.size(), 1u);
    CHECK_EQ(broken[0].ownerPath, std::string("materials/brick.mat"));
    CHECK_EQ(broken[0].referencePath, std::string("textures/brick_normal.png"));
    CHECK_EQ(db.BrokenCount(), 1u);

    // 把缺失的贴图补上 → 断链自动愈合（这正是「引用追踪」的价值）
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick_normal.png").string(), "PNG-FAKE"));
    CHECK(db.Import("textures/brick_normal.png"));
    CHECK_EQ(db.BrokenCount(), 0u);
    CHECK(Contains(db.DependenciesOf("materials/brick.mat"), "textures/brick_normal.png"));

    // 删除被引用的贴图：GUID 仍在但路径已消失 → 同样必须显影为断链
    CHECK(db.Remove("textures/brick.png"));
    const auto afterRemove = db.BrokenReferences();
    CHECK_EQ(afterRemove.size(), 1u);
    if (afterRemove.size() == 1u)
    {
        CHECK_EQ(afterRemove[0].ownerPath, std::string("materials/brick.mat"));
        CHECK_EQ(afterRemove[0].referencePath, std::string("textures/brick.png"));
    }

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.MoveKeepsReferences")
{
    // 核心承诺：移动/重命名资产不拉断引用（因为引用记的是 GUID，不是路径）。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_move";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    const Core::Guid* before = db.GuidFor("textures/brick.png");
    REQUIRE(before != nullptr);
    const Core::Guid guidBefore = *before;

    // 磁盘上先把文件搬好，再通知数据库
    fs::rename(root / "textures" / "brick.png", root / "textures" / "wall.png", ec);
    CHECK(!ec);
    CHECK(db.Move("textures/brick.png", "textures/wall.png"));

    // GUID 不变
    const Core::Guid* after = db.GuidFor("textures/wall.png");
    REQUIRE(after != nullptr);
    CHECK(*after == guidBefore);
    CHECK(db.GuidFor("textures/brick.png") == nullptr);

    // 引用自动跟随：材质依旧依赖它（路径已是新路径）
    CHECK(Contains(db.DependenciesOf("materials/brick.mat"), "textures/wall.png"));
    CHECK_EQ(db.DependentsOf("textures/wall.png").size(), 1u);

    // 移动到不存在 / 目标被占用的情形
    CHECK(!db.Move("textures/wall.png", "textures/wall.png")); // 同源
    CHECK(!db.Move("no/such.png", "textures/x.png"));          // 源未登记
    CHECK(!db.Move("textures/wall.png", ""));                  // 空目标

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.IndexPersistence")
{
    // 索引文件让 GUID 在「没有 .meta 旁车」的场景（打包产物 / 只读目录）也能保持稳定。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_index";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    const Core::Guid guidBrick = *db.GuidFor("textures/brick.png");
    const fs::path indexFile = fs::temp_directory_path() / "bighero_assetdb_index.txt";
    CHECK(db.SaveIndex(indexFile.string()));
    CHECK(fs::exists(indexFile, ec));

    // 新库载入索引：即使删掉所有 .meta，GUID 也必须复原
    for (const auto& e : fs::recursive_directory_iterator(root, ec))
    {
        if (e.path().extension() == ".meta")
            fs::remove(e.path(), ec);
    }
    Core::AssetDatabase db2;
    db2.SetRoot(root.string());
    CHECK(db2.LoadIndex(indexFile.string()));
    CHECK_EQ(db2.ImportAll(), 4u);
    const Core::Guid* restored = db2.GuidFor("textures/brick.png");
    REQUIRE(restored != nullptr);
    CHECK(*restored == guidBrick);

    // 引用图同样被重建
    CHECK(Contains(db2.DependenciesOf("materials/brick.mat"), "textures/brick.png"));

    // 载入不存在的索引 → false，且不破坏现有状态
    CHECK(!db2.LoadIndex((fs::temp_directory_path() / "definitely_missing.txt").string()));
    CHECK_EQ(db2.Count(), 4u);

    fs::remove(indexFile, ec);
    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.RootAndPathHandling")
{
    Core::AssetDatabase db;
    db.SetRoot("D:/proj/assets/"); // 末尾斜杠应被吃掉
    CHECK_EQ(db.Root(), std::string("D:/proj/assets"));
    CHECK_EQ(db.AbsoluteOf("a/b.png"), std::string("D:/proj/assets/a/b.png"));
    CHECK_EQ(db.AbsoluteOf("/a/b.png"), std::string("D:/proj/assets/a/b.png"));

    // 反斜杠统一为 '/'
    db.SetRoot("D:\\proj\\assets");
    CHECK_EQ(db.Root(), std::string("D:/proj/assets"));
    CHECK_EQ(db.AbsoluteOf("sub\\x.png"), std::string("D:/proj/assets/sub/x.png"));

    // 未 Root 时相对路径即绝对路径
    Core::AssetDatabase bare;
    CHECK_EQ(bare.AbsoluteOf("a.png"), std::string("a.png"));

    // 不存在的路径导入失败
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_root";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Core::AssetDatabase db3;
    db3.SetRoot(root.string());
    CHECK(!db3.Import("missing.png"));
    CHECK(!db3.Import(""));
    CHECK_EQ(db3.Count(), 0u);
    CHECK_EQ(db3.BrokenCount(), 0u);
    CHECK_EQ(db3.DependenciesOf("whatever").size(), 0u);
    CHECK_EQ(db3.DependentsOf("whatever").size(), 0u);
    db3.Clear();
    CHECK_EQ(db3.Count(), 0u);

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.KindNames")
{
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Texture)), std::string("Texture"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Mesh)), std::string("Mesh"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Material)), std::string("Material"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Shader)), std::string("Shader"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Scene)), std::string("Scene"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Audio)), std::string("Audio"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Font)), std::string("Font"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Animation)), std::string("Animation"));
    CHECK_EQ(std::string(Core::AssetKindName(Core::AssetKind::Unknown)), std::string("Unknown"));
}
