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

TEST_CASE("AssetDb.DependencyClosure")
{
    // 传递引用闭包：打包（收集一切依赖）与删除影响面（收集一切引用者）的多级展开。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_closure";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // 传递依赖闭包：场景 → {网格, 材质, 贴图}（跨两级；缺失的 normal 不可解析，不入集）
    const auto deps = db.CollectDependencies("scenes/level.json");
    CHECK_EQ(deps.size(), 3u);
    CHECK(Contains(deps, "models/room.glb"));
    CHECK(Contains(deps, "materials/brick.mat"));
    CHECK(Contains(deps, "textures/brick.png"));
    CHECK(!Contains(deps, "scenes/level.json")); // 不含自身
    // 材质的直接依赖只有 brick.png（normal 断链不入集）
    const auto matDeps = db.CollectDependencies("materials/brick.mat");
    CHECK_EQ(matDeps.size(), 1u);
    CHECK(Contains(matDeps, "textures/brick.png"));

    // 传递引用者闭包：贴图 ← 材质 ← 场景（删除贴图会波及材质与场景）
    const auto users = db.CollectDependents("textures/brick.png");
    CHECK_EQ(users.size(), 2u);
    CHECK(Contains(users, "materials/brick.mat"));
    CHECK(Contains(users, "scenes/level.json"));
    // 叶子资产无人引用
    CHECK(db.CollectDependents("scenes/level.json").empty());

    // 按类别筛选
    const auto textures = db.FindAssetsByKind(Core::AssetKind::Texture);
    CHECK_EQ(textures.size(), 1u);
    CHECK(Contains(textures, "textures/brick.png"));
    CHECK_EQ(db.FindAssetsByKind(Core::AssetKind::Audio).size(), 0u);

    // 循环引用安全：a↔b 收敛、不崩溃
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "a.mat").string(), "ref: materials/b.mat\n"));
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "b.mat").string(), "ref: materials/a.mat\n"));
    CHECK(db.Import("materials/a.mat"));
    CHECK(db.Import("materials/b.mat"));
    const auto aDeps = db.CollectDependencies("materials/a.mat");
    CHECK_EQ(aDeps.size(), 1u); // a → b（b→a 回到起点被去重）
    CHECK(Contains(aDeps, "materials/b.mat"));
    const auto aUsers = db.CollectDependents("materials/a.mat");
    CHECK_EQ(aUsers.size(), 1u);
    CHECK(Contains(aUsers, "materials/b.mat"));

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.ChangeDetection")
{
    // 变更检测：磁盘与索引的三向差异（新增/修改/删除），是热重载「增量刷新」的基础。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_changes";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // 基线干净：刚导入完，磁盘与索引一致 → 无变更
    const auto clean = db.ScanForChanges();
    CHECK(clean.Empty());
    CHECK_EQ(clean.TotalCount(), 0u);

    // 新增一个资产 → added
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "wall.png").string(), "PNG-FAKE"));
    {
        const auto ch = db.ScanForChanges();
        REQUIRE(ch.added.size() == 1u);
        CHECK_EQ(ch.added[0], std::string("textures/wall.png"));
        CHECK(ch.modified.empty());
        CHECK(ch.removed.empty());
    }

    // 修改一个已登记资产（写更长内容：体积与 mtime 都变，双重判定兜底）→ modified
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick.png").string(), "PNG-FAKE-BUT-LONGER-CONTENT"));
    {
        const auto ch = db.ScanForChanges();
        REQUIRE(ch.modified.size() == 1u);
        CHECK_EQ(ch.modified[0], std::string("textures/brick.png"));
        CHECK_EQ(ch.added.size(), 1u); // wall.png 仍未导入，仍是 added
    }

    // 删除一个已登记资产 → removed
    fs::remove(root / "models" / "room.glb", ec);
    CHECK(!ec);
    {
        const auto ch = db.ScanForChanges();
        REQUIRE(ch.removed.size() == 1u);
        CHECK_EQ(ch.removed[0], std::string("models/room.glb"));
    }

    // 热重载循环：added/modified → Import，removed → Remove，处理完索引与磁盘重新一致
    {
        const auto ch = db.ScanForChanges();
        for (const auto& rel : ch.added)
            CHECK(db.Import(rel));
        for (const auto& rel : ch.modified)
            CHECK(db.Import(rel));
        for (const auto& rel : ch.removed)
            CHECK(db.Remove(rel));
    }
    CHECK(db.ScanForChanges().Empty());
    CHECK_EQ(db.Count(), 4u); // 4 + wall - room = 4

    // SnapshotNow：只同步 (mtime, 体积) 基线、不重扫内容；把「当前磁盘状态」声明为新基线
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick.png").string(), "SHORT"));
    CHECK_EQ(db.ScanForChanges().modified.size(), 1u); // 又变了
    db.SnapshotNow();
    CHECK(db.ScanForChanges().Empty()); // 基线已对齐，不再报修改

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.OrphansAndApplyChanges")
{
    // 可达性分析（FindOrphans）+ 一键热重载（ApplyChanges）+ 引用表真·刷新。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_orphans";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // ---- FindOrphans：从场景根做可达性分析 ----
    // 全部 4 资产都可从 level.json 到达（场景→网格+材质→贴图）→ 无孤儿
    CHECK(db.FindOrphans({"scenes/level.json"}).empty());

    // 加入一个无人引用的资产 → 成为孤儿
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "unused.png").string(), "PNG-FAKE"));
    CHECK(db.Import("textures/unused.png"));
    {
        const auto orphans = db.FindOrphans({"scenes/level.json"});
        REQUIRE(orphans.size() == 1u);
        CHECK_EQ(orphans[0], std::string("textures/unused.png"));
    }

    // 换一组根（只看材质）：场景与网格变成孤儿
    {
        const auto orphans = db.FindOrphans({"materials/brick.mat"});
        CHECK_EQ(orphans.size(), 3u); // level.json, room.glb, unused.png
        CHECK(Contains(orphans, "scenes/level.json"));
        CHECK(Contains(orphans, "models/room.glb"));
        CHECK(Contains(orphans, "textures/unused.png"));
    }

    // 空根集 → 一切皆孤儿
    CHECK_EQ(db.FindOrphans({}).size(), 5u);

    // ---- ApplyChanges：一键热重载闭环 ----
    // 磁盘改三处：新增 new.png、修改 brick.mat（删掉 normal 引用）、删除 unused.png
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "new.png").string(), "PNG-FAKE"));
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "brick.mat").string(),
                                        "albedo: textures/brick.png\nroughness: 0.5\n"));
    fs::remove(root / "textures" / "unused.png", ec);
    CHECK(!ec);

    const Core::AssetChangeSet applied = db.ApplyChanges();
    CHECK_EQ(applied.added.size(), 1u);
    CHECK_EQ(applied.modified.size(), 1u);
    CHECK_EQ(applied.removed.size(), 1u);
    CHECK_EQ(applied.TotalCount(), 3u);
    // 处理后磁盘与索引重新一致
    CHECK(db.ScanForChanges().Empty());
    CHECK_EQ(db.Count(), 5u); // 5 + new - unused = 5
    // 引用表真·刷新：brick.mat 的 normal 引用被移除，断链随之消失（不是残留成假断链）
    CHECK_EQ(db.BrokenCount(), 0u);
    CHECK(db.GuidFor("textures/unused.png") == nullptr); // 已注销
    CHECK(db.GuidFor("textures/new.png") != nullptr);    // 已登记

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.LoadOrder")
{
    // 拓扑加载顺序（ComputeLoadOrder）+ 引用环检测。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_loadorder";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // ---- 基本拓扑序：场景根出发，依赖必须排在引用者之前 ----
    {
        const Core::AssetLoadOrder lo = db.ComputeLoadOrder({"scenes/level.json"});
        // 可达 4 资产全部入序；断链的 brick_normal.png 无注册目标，天然不进序
        REQUIRE(lo.order.size() == 4u);
        CHECK(lo.cycles.empty());
        CHECK(!lo.HasCycles());
        // 确定性全序（同层字典序决胜）：room.glb 与 brick.png 同为叶子，m < t 故 room.glb 在前
        CHECK_EQ(lo.order[0], std::string("models/room.glb"));
        CHECK_EQ(lo.order[1], std::string("textures/brick.png"));
        CHECK_EQ(lo.order[2], std::string("materials/brick.mat"));
        CHECK_EQ(lo.order[3], std::string("scenes/level.json"));
        // 不变式：任意资产的依赖都排在它之前
        const auto posOf = [&](const std::string& p)
        { return std::find(lo.order.begin(), lo.order.end(), p) - lo.order.begin(); };
        CHECK(posOf("textures/brick.png") < posOf("materials/brick.mat"));
        CHECK(posOf("materials/brick.mat") < posOf("scenes/level.json"));
        CHECK(posOf("models/room.glb") < posOf("scenes/level.json"));
    }

    // ---- 幂等：同一引用图重复计算，结果逐位一致 ----
    {
        const Core::AssetLoadOrder a = db.ComputeLoadOrder({"scenes/level.json"});
        const Core::AssetLoadOrder b = db.ComputeLoadOrder({"scenes/level.json"});
        CHECK(a.order == b.order);
    }

    // ---- 空根集 / 未登记根：静默跳过 ----
    CHECK(db.ComputeLoadOrder({}).order.empty());
    CHECK(db.ComputeLoadOrder({"ghost.mat"}).order.empty());
    CHECK(db.ComputeLoadOrder({"ghost.mat"}).cycles.empty());

    // ---- 环检测：a.mat ↔ b.mat 互相引用 ----
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "a.mat").string(), "other: materials/b.mat\n"));
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "b.mat").string(), "other: materials/a.mat\n"));
    CHECK(db.Import("materials/a.mat"));
    CHECK(db.Import("materials/b.mat"));
    {
        const Core::AssetLoadOrder lo = db.ComputeLoadOrder({"materials/a.mat"});
        CHECK(lo.order.empty()); // 环上资产没有合法加载顺序
        REQUIRE(lo.cycles.size() == 2u);
        CHECK_EQ(lo.cycles[0], std::string("materials/a.mat")); // 字典序
        CHECK_EQ(lo.cycles[1], std::string("materials/b.mat"));
        CHECK(lo.HasCycles());
    }

    // ---- 自环：自己引用自己也是环 ----
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "self.mat").string(), "me: materials/self.mat\n"));
    CHECK(db.Import("materials/self.mat"));
    {
        const Core::AssetLoadOrder lo = db.ComputeLoadOrder({"materials/self.mat"});
        CHECK(lo.order.empty());
        REQUIRE(lo.cycles.size() == 1u);
        CHECK_EQ(lo.cycles[0], std::string("materials/self.mat"));
    }

    // ---- 混合：干净子图照常入序，环被单独隔离、互不污染 ----
    {
        const Core::AssetLoadOrder lo = db.ComputeLoadOrder({"scenes/level.json", "materials/a.mat"});
        CHECK_EQ(lo.order.size(), 4u);  // 干净 4 资产不受影响
        CHECK_EQ(lo.cycles.size(), 2u); // 只有 a/b 在环上
        CHECK(Contains(lo.order, "scenes/level.json"));
        CHECK(!Contains(lo.order, "materials/a.mat"));
    }

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.BundleAndMetaSweep")
{
    // 打包清单（CollectBundle：枚举+体积统计）+ 孤儿 .meta 清理（Find/SweepStaleMetaFiles）。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_bundle";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // ---- CollectBundle：场景根 → 根自身 + 传递依赖（brick_normal.png 断链，无目标不入清单）----
    {
        const Core::AssetBundle b = db.CollectBundle({"scenes/level.json"});
        REQUIRE(b.entries.size() == 4u);
        CHECK_EQ(b.Count(), 4u);
        // 按路径字典序：materials < models < scenes < textures
        CHECK_EQ(b.entries[0].path, std::string("materials/brick.mat"));
        CHECK_EQ(b.entries[1].path, std::string("models/room.glb"));
        CHECK_EQ(b.entries[2].path, std::string("scenes/level.json"));
        CHECK_EQ(b.entries[3].path, std::string("textures/brick.png"));
        // 体积合计 = 各资产 SizeOf 之和；类别随条目带出
        uint64_t expect = 0;
        for (const char* p : {"materials/brick.mat", "models/room.glb", "scenes/level.json", "textures/brick.png"})
            expect += db.SizeOf(p);
        CHECK_EQ(b.totalBytes, expect);
        CHECK_EQ(int(b.entries[3].kind), int(Core::AssetKind::Texture));
        // 断链的 brick_normal.png 不在清单里
        for (const auto& e : b.entries)
            CHECK(e.path != "textures/brick_normal.png");
    }

    // ---- CollectBundle：子集根 / 空根 / 未登记根 / 幂等 ----
    CHECK_EQ(db.CollectBundle({"materials/brick.mat"}).Count(), 2u); // mat + 其贴图
    CHECK(db.CollectBundle({}).entries.empty());
    CHECK_EQ(db.CollectBundle({}).totalBytes, 0u);
    CHECK(db.CollectBundle({"ghost.mat"}).entries.empty());
    {
        const Core::AssetBundle a = db.CollectBundle({"scenes/level.json"});
        const Core::AssetBundle b2 = db.CollectBundle({"scenes/level.json"});
        CHECK_EQ(a.totalBytes, b2.totalBytes);
        CHECK(a.entries.size() == b2.entries.size());
    }

    // ---- 孤儿 .meta 清理 ----
    // 初始：4 个资产都有配套 .meta，无孤儿
    CHECK(db.FindStaleMetaFiles().empty());
    // 模拟库外删除：直接删主文件，brick.png.meta 残留成孤儿
    fs::remove(root / "textures" / "brick.png", ec);
    CHECK(!ec);
    {
        const auto stale = db.FindStaleMetaFiles();
        REQUIRE(stale.size() == 1u);
        CHECK_EQ(stale[0], std::string("textures/brick.png.meta"));
    }
    // 清理：删除孤儿、返回数量；存活资产的 .meta 不受影响
    CHECK_EQ(db.SweepStaleMetaFiles(), 1u);
    CHECK(db.FindStaleMetaFiles().empty());
    CHECK(!Core::FileSystem::Exists((root / "textures" / "brick.png.meta").string()));
    CHECK(Core::FileSystem::Exists((root / "materials" / "brick.mat.meta").string()));

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.Audit")
{
    // 健康审计：一次调用聚合断链 / 引用环 / 孤儿 .meta / 孤儿资产，按严重度分级排序。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_audit";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // ---- 初始：仅 brick_normal.png 一条断链（Error），其余干净 ----
    {
        const auto issues = db.Audit();
        REQUIRE(issues.size() == 1u);
        CHECK(issues[0].severity == Core::AssetIssue::Severity::Error);
        CHECK_EQ(issues[0].category, std::string("broken-ref"));
        CHECK_EQ(issues[0].path, std::string("materials/brick.mat"));
        CHECK_EQ(issues[0].detail, std::string("textures/brick_normal.png"));
    }

    // ---- 修复断链 → 审计清空（健康工程）----
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick_normal.png").string(), "PNG-NORMAL"));
    CHECK(db.Import("textures/brick_normal.png")); // re-resolve 愈合断链
    CHECK(db.Audit().empty());

    // ---- 制造四类问题并验证聚合与排序 ----
    // 孤儿资产：库外新增贴图（场景够不着）
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "unused.png").string(), "PNG-UNUSED"));
    CHECK(db.Import("textures/unused.png"));
    // 引用环：a.mat ↔ b.mat 互引（同时成为孤儿）
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "a.mat").string(), "other: materials/b.mat\n"));
    REQUIRE(Core::FileSystem::WriteText((root / "materials" / "b.mat").string(), "other: materials/a.mat\n"));
    CHECK(db.Import("materials/a.mat"));
    CHECK(db.Import("materials/b.mat"));
    // 断链：删掉 brick.png（库内 Remove，使 brick.mat 的 albedo 断链）
    CHECK(db.Remove("textures/brick.png"));
    // 孤儿 .meta：库外直接删 brick_normal.png，旁车残留
    fs::remove(root / "textures" / "brick_normal.png", ec);
    CHECK(!ec);

    const auto issues = db.Audit();
    // 期望共 7 条：
    //   Error×3   = 断链(brick.mat→brick.png) + 环(a.mat) + 环(b.mat)
    //   Warning×4 = 孤儿.meta(brick_normal.png.meta) + 孤儿(a.mat) + 孤儿(b.mat) + 孤儿(unused.png)
    // 注意 brick_normal.png 不是孤儿资产：brick.mat 仍引用它，GUID 仍解析到已登记路径——文件丢失
    // 不影响可达性判定（它体现为 stale-meta，而非 orphan）。故孤儿只有 a/b/unused 三个。
    REQUIRE(issues.size() == 7u);
    // 前 3 条必须全是 Error，后 4 条必须全是 Warning（严重度排序）
    for (size_t i = 0; i < 3u; ++i)
        CHECK(issues[i].severity == Core::AssetIssue::Severity::Error);
    for (size_t i = 3u; i < 7u; ++i)
        CHECK(issues[i].severity == Core::AssetIssue::Severity::Warning);
    // 类别归类正确
    size_t nBroken = 0, nCycle = 0, nStale = 0, nOrphan = 0;
    for (const auto& it : issues)
    {
        if (it.category == "broken-ref")
            ++nBroken;
        else if (it.category == "cycle")
            ++nCycle;
        else if (it.category == "stale-meta")
            ++nStale;
        else if (it.category == "orphan")
            ++nOrphan;
    }
    CHECK_EQ(nBroken, 1u);
    CHECK_EQ(nCycle, 2u);
    CHECK_EQ(nStale, 1u);
    CHECK_EQ(nOrphan, 3u);
    // 关键条目内容核对
    CHECK_EQ(issues[0].category, std::string("broken-ref")); // Error 里 broken-ref 字典序先于 cycle
    CHECK_EQ(issues[0].detail, std::string("textures/brick.png"));
    CHECK(Contains(std::vector<std::string>{issues[1].path, issues[2].path}, "materials/a.mat"));
    CHECK(Contains(std::vector<std::string>{issues[1].path, issues[2].path}, "materials/b.mat"));

    // ---- 确定性：同状态重复审计逐位一致 ----
    {
        const auto a = db.Audit();
        const auto b = db.Audit();
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].category == b[i].category);
            CHECK(a[i].path == b[i].path);
            CHECK(a[i].detail == b[i].detail);
            CHECK(a[i].severity == b[i].severity);
        }
    }

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.DuplicateGuids")
{
    // GUID 完整性：检测并确定性修复「复制粘贴导致多资产共享同一 GUID」的经典损坏。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_dupguid";
    REQUIRE(MakeProject(root, ec));

    Core::AssetDatabase db;
    db.SetRoot(root.string());
    CHECK_EQ(db.ImportAll(), 4u);

    // 初始：无撞车
    CHECK(db.FindDuplicateGuids().empty());

    // 模拟 Explorer 复制粘贴：brick.png → brick2.png，连同 .meta（GUID 被一起复制）
    const Core::Guid* g0 = db.GuidFor("textures/brick.png");
    REQUIRE(g0 != nullptr);
    const Core::Guid sharedGuid = *g0;
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick2.png").string(), "PNG-COPY"));
    REQUIRE(Core::AssetGuidMeta::Write((root / "textures" / "brick2.png").string(), sharedGuid));

    // 检测：一组、两路径、字典序（'.' < '2'，原文件在前）
    {
        const auto groups = db.FindDuplicateGuids();
        REQUIRE(groups.size() == 1u);
        CHECK(groups[0].guid == sharedGuid);
        REQUIRE(groups[0].paths.size() == 2u);
        CHECK_EQ(groups[0].paths[0], std::string("textures/brick.png"));
        CHECK_EQ(groups[0].paths[1], std::string("textures/brick2.png"));
    }

    // 审计接入：duplicate-guid 为 Error，两路径各一条；总计 = 既有断链 1 + 撞车 2
    {
        const auto issues = db.Audit();
        CHECK_EQ(issues.size(), 3u);
        size_t nDup = 0;
        for (const auto& it : issues)
        {
            if (it.category == "duplicate-guid")
            {
                ++nDup;
                CHECK(it.severity == Core::AssetIssue::Severity::Error);
                CHECK_EQ(it.detail, sharedGuid.ToString());
            }
        }
        CHECK_EQ(nDup, 2u);
    }

    // 修复：1 个资产被重分配身份；原文件保住 GUID（既有引用不断），副本得到新身份
    CHECK_EQ(db.RepairDuplicateGuids(), 1u);
    CHECK(db.FindDuplicateGuids().empty());
    const Core::Guid* g1 = db.GuidFor("textures/brick.png");
    REQUIRE(g1 != nullptr);
    CHECK(*g1 == sharedGuid);
    const Core::Guid* g2 = db.GuidFor("textures/brick2.png");
    REQUIRE(g2 != nullptr);
    CHECK(g2->IsValid());
    CHECK(*g2 != sharedGuid);
    CHECK_EQ(db.Count(), 5u);
    // brick.mat → brick.png 引用完好（GUID 未漂）
    CHECK(Contains(db.DependenciesOf("materials/brick.mat"), "textures/brick.png"));
    // 审计：撞车消失；brick2 未被任何场景引用 → 转为孤儿 Warning
    {
        const auto issues = db.Audit();
        for (const auto& it : issues)
            CHECK(it.category != "duplicate-guid");
        CHECK_EQ(issues.size(), 2u); // 断链(brick_normal) + 孤儿(brick2)
    }

    // 三方撞车：brick3/brick4 也带同一 GUID → 一组三个，修复 2 个，原文件身份依旧
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick3.png").string(), "PNG-C3"));
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick4.png").string(), "PNG-C4"));
    REQUIRE(Core::AssetGuidMeta::Write((root / "textures" / "brick3.png").string(), sharedGuid));
    REQUIRE(Core::AssetGuidMeta::Write((root / "textures" / "brick4.png").string(), sharedGuid));
    {
        const auto groups = db.FindDuplicateGuids();
        REQUIRE(groups.size() == 1u);
        CHECK_EQ(groups[0].paths.size(), 3u);
    }
    CHECK_EQ(db.RepairDuplicateGuids(), 2u);
    CHECK(db.FindDuplicateGuids().empty());
    CHECK_EQ(db.Count(), 7u);
    {
        const Core::Guid* gk = db.GuidFor("textures/brick.png");
        REQUIRE(gk != nullptr);
        CHECK(*gk == sharedGuid);
    }

    fs::remove_all(root, ec);
}

TEST_CASE("AssetDb.CachePersistence")
{
    // 快照缓存持久化：SaveCache → 新实例 LoadCache → 校验通过则零内容读取重建内存。
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "bighero_assetdb_cache";
    REQUIRE(MakeProject(root, ec));

    // 冷启动：全量导入并保存缓存
    Core::AssetDatabase db1;
    db1.SetRoot(root.string());
    CHECK_EQ(db1.ImportAll(), 4u);
    // 缓存文件放在资产根之外：ImportAll 会把根内一切非 .meta 文件登记为资产，
    // 缓存若落进根目录会污染导入计数。
    const fs::path cachePath = fs::temp_directory_path() / "bighero_assetdb_cache_file.txt";
    CHECK(db1.SaveCache(cachePath.string()));

    // 热启动：新实例 LoadCache，文件未变 → 校验通过、内存重建、引用图完整
    Core::AssetDatabase db2;
    db2.SetRoot(root.string());
    CHECK(db2.LoadCache(cachePath.string()));
    CHECK_EQ(db2.Count(), 4u);
    // 引用图与依赖关系完整恢复
    CHECK(Contains(db2.DependenciesOf("scenes/level.json"), "materials/brick.mat"));
    CHECK(Contains(db2.DependenciesOf("materials/brick.mat"), "textures/brick.png"));
    // 类别与体积正确
    CHECK_EQ(int(db2.KindOf("textures/brick.png")), int(Core::AssetKind::Texture));
    CHECK(db2.SizeOf("textures/brick.png") > 0u);

    // 缓存失效场景 1：文件被修改（mtime 变）→ LoadCache 拒绝，调用方降级 ImportAll
    REQUIRE(Core::FileSystem::WriteText((root / "textures" / "brick.png").string(), "PNG-MODIFIED"));
    Core::AssetDatabase db3;
    db3.SetRoot(root.string());
    CHECK(!db3.LoadCache(cachePath.string())); // 校验失败
    CHECK_EQ(db3.Count(), 0u);                 // 已 Clear
    CHECK_EQ(db3.ImportAll(), 4u);             // 降级全量导入

    // 缓存失效场景 2：文件被删除 → LoadCache 拒绝
    fs::remove(root / "models" / "room.glb", ec);
    CHECK(!ec);
    Core::AssetDatabase db4;
    db4.SetRoot(root.string());
    CHECK(!db4.LoadCache(cachePath.string()));
    CHECK_EQ(db4.Count(), 0u);

    // WarmStart：有缓存且有效 → true；无缓存或失效 → false 且已全量导入
    fs::remove(cachePath, ec);
    Core::AssetDatabase db5;
    db5.SetRoot(root.string());
    CHECK(!db5.WarmStart(cachePath.string())); // 无缓存 → 降级
    CHECK_EQ(db5.Count(), 3u);                 // room.glb 已在上一步删除

    fs::remove_all(root, ec);
    fs::remove(cachePath, ec);
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
