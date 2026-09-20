// 资产 GUID 数据库单元测试：Guid 值类型 / .meta 旁车持久化 / 双向映射 / 引用追踪 / 断链检测。
// 纯 CPU + 临时目录文件 IO，无 GPU/窗口依赖。
#include "framework/test_common.h"

#include "core/AssetGuid.h"

#include <filesystem>

using namespace BigHero;

TEST_CASE("Assets.Guid")
{
    using Core::Guid;

    // 默认构造 = 空引用哨兵
    CHECK(!Guid{}.IsValid());
    CHECK((Guid{} == Guid{0, 0}));

    // 生成有效且互异
    const Guid a = Guid::Generate();
    const Guid b = Guid::Generate();
    CHECK(a.IsValid());
    CHECK(b.IsValid());
    CHECK(a != b);

    // 字符串往返（32 小写 hex）
    const std::string s = a.ToString();
    CHECK(s.size() == 32);
    Guid parsed;
    CHECK(Guid::TryParse(s, parsed));
    CHECK(parsed == a);

    // 已知值往返（解析接受大小写，输出归一小写）
    Guid known;
    CHECK(Guid::TryParse("0123456789ABCDEF0123456789abcdef", known));
    CHECK(known.Hi() == 0x0123456789ABCDEFULL);
    CHECK(known.Lo() == 0x0123456789abcdefULL);
    CHECK(known.ToString() == "0123456789abcdef0123456789abcdef");

    // 非法输入一律拒绝且不写 out
    Guid guard = a;
    CHECK(!Guid::TryParse("", guard));
    CHECK(!Guid::TryParse("0123", guard));                               // 太短
    CHECK(!Guid::TryParse("0123456789abcdef0123456789abcdef00", guard)); // 太长（34）
    CHECK(!Guid::TryParse("0123456789abcdef0123456789abcdeg", guard));   // 非 hex
    CHECK(!Guid::TryParse("01234567-89ab-cdef-0123-456789abcdef", guard)); // 连字符不受理
    CHECK(guard == a);

    // 比较与排序（Guid{x,y} 花括号含逗号，宏参数需外层圆括号保护）
    CHECK((Guid{1, 0} < Guid{2, 0}));
    CHECK((Guid{1, 5} < Guid{1, 6}));
    CHECK((!(Guid{2, 0} < Guid{1, 9})));

    // 纯函数 noexcept 契约
    static_assert(noexcept(Guid{}.IsValid()));
    static_assert(noexcept(Guid{1, 2} == Guid{1, 2}));
    static_assert(noexcept(Core::GuidHash{}(a)));
}

TEST_CASE("Assets.GuidDatabase")
{
    namespace fs = std::filesystem;
    namespace AssetGuidMeta = Core::AssetGuidMeta;
    using Core::AssetGuidDatabase;
    using Core::Guid;

    // 独立临时目录（用例前清干净，避免跨运行污染）
    const fs::path root = fs::temp_directory_path() / "bighero_guid_db_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    REQUIRE(fs::create_directories(root / "textures", ec));
    REQUIRE(fs::create_directories(root / "models", ec));

    const auto touch = [](const std::string& p) { REQUIRE(Core::FileSystem::WriteText(p, "asset-bytes")); };
    const std::string albedo = Core::NormalizePath((root / "textures" / "albedo.png").string());
    const std::string normal = Core::NormalizePath((root / "textures" / "normal.png").string());
    const std::string mesh = Core::NormalizePath((root / "models" / "rock.glb").string());
    touch(albedo);
    touch(normal);
    touch(mesh);

    // ---- 登记：生成 + .meta 落盘 + 幂等 ----
    AssetGuidDatabase db;
    CHECK(db.Count() == 0);
    CHECK(db.Find(albedo) == nullptr); // 只读查询不触发登记
    const Guid gAlbedo = db.GuidForPath(albedo);
    CHECK(gAlbedo.IsValid());
    CHECK(db.Count() == 1);
    CHECK(db.GuidForPath(albedo) == gAlbedo); // 幂等
    CHECK(fs::exists(albedo + ".meta"));
    // 路径形态不同（含 ./ 段）归一化后仍命中同一身份
    CHECK(db.GuidForPath(Core::GetParentDir(albedo) + "/./albedo.png") == gAlbedo);

    // ---- 双向映射 ----
    REQUIRE(db.Find(albedo) != nullptr);
    CHECK(*db.Find(albedo) == gAlbedo);
    REQUIRE(db.FindPath(gAlbedo) != nullptr);
    CHECK(*db.FindPath(gAlbedo) == albedo);
    CHECK(db.FindPath(Guid::Generate()) == nullptr); // 未登记 GUID

    // ---- 持久性：新库实例读同一 .meta 复用同一 GUID ----
    {
        AssetGuidDatabase db2;
        CHECK(db2.GuidForPath(albedo) == gAlbedo);
    }

    // ---- .meta 受损自愈：坏内容 → 重新生成并覆写 ----
    REQUIRE(Core::FileSystem::WriteText(normal + ".meta", "guid: not-a-guid\n"));
    AssetGuidDatabase db3;
    const Guid gNormal = db3.GuidForPath(normal);
    CHECK(gNormal.IsValid());
    Guid reread;
    CHECK(AssetGuidMeta::Read(normal, reread));
    CHECK(reread == gNormal); // 已自愈为合法值

    // ---- GUID 撞车防护：手工把 normal 的 .meta 抄成 albedo 的 GUID → 重新生成 ----
    REQUIRE(Core::FileSystem::WriteText(normal + ".meta", "guid: " + gAlbedo.ToString() + "\n"));
    AssetGuidDatabase db4;
    CHECK(db4.GuidForPath(albedo) == gAlbedo);
    const Guid gNormal2 = db4.GuidForPath(normal);
    CHECK(gNormal2.IsValid());
    CHECK(gNormal2 != gAlbedo);

    // ---- GUID 稳定移动（Unity 语义）----
    AssetGuidDatabase db5;
    const Guid gMesh = db5.GuidForPath(mesh);
    const std::string meshMoved = Core::NormalizePath((root / "models" / "rock_renamed.glb").string());
    fs::rename(mesh, meshMoved, ec); // 调用方先搬文件本体
    REQUIRE(!ec);
    REQUIRE(db5.MoveAsset(mesh, meshMoved));
    CHECK(db5.Find(mesh) == nullptr);
    REQUIRE(db5.Find(meshMoved) != nullptr);
    CHECK(*db5.Find(meshMoved) == gMesh); // GUID 不变
    CHECK(fs::exists(meshMoved + ".meta"));
    CHECK(!fs::exists(mesh + ".meta")); // 旧旁车已清理
    // 冲突 / 未登记场景
    CHECK(!db5.MoveAsset(mesh, meshMoved)); // old 已不存在
    CHECK(!db5.MoveAsset("nonexistent.png", meshMoved));

    // ---- 引用追踪 ----
    AssetGuidDatabase db6;
    const Guid gTex = db6.GuidForPath(albedo);
    const Guid gRock = db6.GuidForPath(mesh); // mesh 文件已移走：纯映射登记（生成新身份）
    const std::string material = Core::NormalizePath((root / "materials" / "rock.mat").string());

    // 登记 / 覆盖 / 读取
    db6.SetReferences(material, {gTex, gRock});
    REQUIRE(db6.ReferencesOf(material) != nullptr);
    CHECK(db6.ReferencesOf(material)->size() == 2);
    CHECK((*db6.ReferencesOf(material))[0] == gTex);
    db6.SetReferences(material, {gRock}); // 覆盖式
    CHECK(db6.ReferencesOf(material)->size() == 1);

    // 反向查询（"谁在用我"）
    CHECK(db6.DependentsOf(gTex).empty());
    const std::vector<std::string> rockUsers = db6.DependentsOf(gRock);
    REQUIRE(rockUsers.size() == 1);
    CHECK(rockUsers[0] == material);

    // 被引资产自身的引用记录随 MoveAsset 换键
    db6.SetReferences(mesh, {gTex});
    const std::string meshV2 = Core::NormalizePath((root / "models" / "rock_v2.glb").string());
    REQUIRE(db6.MoveAsset(mesh, meshV2));
    CHECK(db6.ReferencesOf(mesh) == nullptr);
    REQUIRE(db6.ReferencesOf(meshV2) != nullptr);
    CHECK((*db6.ReferencesOf(meshV2))[0] == gTex);
    // 移动后被引身份不变：material → gRock 仍有效，无断链
    CHECK(db6.FindBrokenReferences().empty());

    // 空引用哨兵（Guid{}）不算断链
    db6.SetReferences(material, {Guid{}, gRock});
    CHECK(db6.FindBrokenReferences().empty());

    // 引用未登记 GUID → 断链显影（引用者路径 + 缺失 GUID 成对返回）
    const Guid ghost = Guid::Generate();
    db6.SetReferences(material, {ghost});
    const std::vector<std::pair<std::string, Guid>> broken = db6.FindBrokenReferences();
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].first == material);
    CHECK(broken[0].second == ghost);

    // 删除被引资产 → 断链显影；删除清映射 + .meta
    db6.SetReferences(material, {gRock});
    CHECK(db6.FindBrokenReferences().empty());
    CHECK(db6.RemoveAsset(meshV2));
    CHECK(db6.FindBrokenReferences().size() == 1);
    CHECK(db6.Find(meshV2) == nullptr);
    CHECK(!fs::exists(meshV2 + ".meta"));

    // 清除引用记录
    db6.SetReferences(material, {});
    CHECK(db6.ReferencesOf(material) == nullptr);
    CHECK(db6.FindBrokenReferences().empty());

    // ---- 目录扫描 ----
    // 追加全新资产：扫描时为其生成身份；其余三个复用既有 .meta
    const std::string extra = Core::NormalizePath((root / "models" / "tree.glb").string());
    touch(extra);
    AssetGuidDatabase db7;
    const size_t created = db7.ScanDirectory(root.string());
    CHECK(created == 1); // 仅 tree.glb 是新建
    CHECK(db7.Count() == 4);
    REQUIRE(db7.Find(albedo) != nullptr);
    CHECK(*db7.Find(albedo) == gAlbedo); // 旧身份不漂
    CHECK(fs::exists(extra + ".meta"));

    // 再扫：零新建（幂等）；.meta 自身不会被登记为资产
    AssetGuidDatabase db8;
    CHECK(db8.ScanDirectory(root.string()) == 0);
    CHECK(db8.Count() == 4);
    CHECK(db8.Find(albedo + ".meta") == nullptr);

    // 清理临时目录
    fs::remove_all(root, ec);
}
