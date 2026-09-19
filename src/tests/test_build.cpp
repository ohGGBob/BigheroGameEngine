// 构建设置单元测试 —— Unity 对标清单 U1-B1。
// 覆盖验收线：
//   1. 时间戳目录名格式（YYYYMMDD_HHMMSS 零填充，输出 = builds/<时间戳>/）；
//   2. 清单生成纯函数：文件列表正确（exe/shaders/assets/场景，目录递归升序）、
//      重复场景输入去重、缺失项（目录/场景文件/exe）产生告警而非条目；
//   3. 执行器：对临时目录做真实小拷贝往返（exe 用假文件代替），产物自包含
//      （逐文件存在且内容一致、嵌套子目录保留），重复构建覆盖幂等；
//      来源缺失时逐文件失败报告正确（单条失败不中断其余拷贝）。
#include "framework/test_common.h"

#include "core/FileSystemUtils.h"
#include "editor/BuildExecutor.h"
#include "editor/BuildSettingsModel.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace BigHero;
using namespace BigHero::Editor::BuildSettings;

namespace
{
// 临时目录 RAII：析构时整体删除（假工作目录与产物一并清理）
struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        static int counter = 0;
        ++counter;
        path = std::filesystem::temp_directory_path() /
               ("bh_build_test_" + std::to_string(counter) + "_" + CurrentTimestampDirName());
        std::filesystem::remove_all(path); // 清理同名残留
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// 在 root 下写一个假文件（含内容），自动创建父目录
void WriteFileAt(const std::filesystem::path& root, const std::string& rel, const std::string& content)
{
    const std::filesystem::path full = root / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full, std::ios::binary | std::ios::trunc);
    f << content;
}

// 读回产物内容（缺失/不可读返回空串，由断言比对失败暴露）
std::string ReadAt(const std::filesystem::path& p)
{
    std::string out;
    Core::FileSystem::ReadText(p.string(), out);
    return out;
}

// 组装假工作目录：假 exe + shaders/（含子目录）+ assets/（含子目录）+ scene.json
void MakeFakeWorkDir(const std::filesystem::path& root)
{
    WriteFileAt(root, "mygame.exe", "FAKE-EXE-BYTES");
    WriteFileAt(root, "shaders/vert.spv", "V");
    WriteFileAt(root, "shaders/frag.spv", "F");
    WriteFileAt(root, "shaders/include/common.glsl", "C");
    WriteFileAt(root, "assets/tiles.png", "PNG");
    WriteFileAt(root, "assets/models/m.gltf", "GLTF");
    WriteFileAt(root, "scene.json", "{}");
}

// 断言辅助：清单中存在（source→destination, kind）条目
bool HasEntry(const BuildManifest& m, const std::string& src, const std::string& dst, const char* kind)
{
    for (const CopyEntry& e : m.entries)
        if (e.source == src && e.destination == dst && e.kind == kind)
            return true;
    return false;
}
} // namespace

// ---------------------------------------------------------------------------
// 1. 时间戳目录名格式
// ---------------------------------------------------------------------------

TEST_CASE("Build.TimestampDirNameFormat")
{
    CHECK_EQ(TimestampDirName(2026, 9, 19, 8, 5, 3), std::string("20260919_080503"));
    CHECK_EQ(TimestampDirName(1999, 12, 31, 23, 59, 59), std::string("19991231_235959"));
    const std::string ts = TimestampDirName(1, 2, 3, 4, 5, 6);
    CHECK_EQ(ts.size(), size_t(15)); // 8 位日期 + '_' + 6 位时间
    CHECK_EQ(ts[8], '_');
    // 当前时间入口：格式与纯函数一致（15 字符，'_' 固定位）
    const std::string now = CurrentTimestampDirName();
    CHECK_EQ(now.size(), size_t(15));
    CHECK_EQ(now[8], '_');
}

// ---------------------------------------------------------------------------
// 2. 清单生成：文件列表正确
// ---------------------------------------------------------------------------

TEST_CASE("Build.ManifestFileList")
{
    TempDir work;
    MakeFakeWorkDir(work.path);
    const BuildConfig config; // 全开默认（含 scene.json）
    const BuildManifest m = GenerateManifest(config, work.path, work.path / "mygame.exe", "20260919_080503");

    // 输出目录 = 工作目录/builds/<时间戳>
    const std::filesystem::path out(m.outputDirectory);
    CHECK(out.is_absolute());
    CHECK_EQ(out.filename().string(), std::string("20260919_080503"));
    CHECK_EQ(out.parent_path().filename().string(), std::string("builds"));

    // 7 个文件：exe + 3 着色器（含嵌套子目录）+ 2 资产 + 1 场景；无告警
    CHECK(m.warnings.empty());
    CHECK_EQ(m.entries.size(), size_t(7));
    // exe 条目的 source 为绝对路径（自我复制来源独立于工作目录），destination 为文件名
    const std::string exeSrc = (work.path / "mygame.exe").lexically_normal().generic_string();
    CHECK(HasEntry(m, exeSrc, "mygame.exe", "exe"));
    CHECK(HasEntry(m, "shaders/vert.spv", "shaders/vert.spv", "shaders"));
    CHECK(HasEntry(m, "shaders/frag.spv", "shaders/frag.spv", "shaders"));
    CHECK(HasEntry(m, "shaders/include/common.glsl", "shaders/include/common.glsl", "shaders"));
    CHECK(HasEntry(m, "assets/tiles.png", "assets/tiles.png", "assets"));
    CHECK(HasEntry(m, "assets/models/m.gltf", "assets/models/m.gltf", "assets"));
    CHECK(HasEntry(m, "scene.json", "scene.json", "scene"));

    // 关闭全部开关 → 仅剩场景文件条目
    BuildConfig trimmed;
    trimmed.copyExecutable = false;
    trimmed.copyShaders = false;
    trimmed.packageAssets = false;
    const BuildManifest m2 = GenerateManifest(trimmed, work.path, work.path / "mygame.exe", "20260919_080503");
    CHECK_EQ(m2.entries.size(), size_t(1));
    CHECK_EQ(m2.entries[0].kind, std::string("scene"));
}

// ---------------------------------------------------------------------------
// 3. 清单生成：去重（同文件多路径写法 / 重复条目）
// ---------------------------------------------------------------------------

TEST_CASE("Build.ManifestDedup")
{
    TempDir work;
    MakeFakeWorkDir(work.path);
    BuildConfig config;
    config.sceneFiles = {"scene.json", "scene.json", "./scene.json", "assets/../scene.json"};
    const BuildManifest m = GenerateManifest(config, work.path, work.path / "mygame.exe", "20260919_080503");

    // 4 份等价场景输入去重为 1 条；总条目数与单场景清单一致
    CHECK_EQ(m.entries.size(), size_t(7));
    int sceneCount = 0;
    for (const CopyEntry& e : m.entries)
        if (e.kind == "scene")
            ++sceneCount;
    CHECK_EQ(sceneCount, 1);
    CHECK(m.warnings.empty());

    // 幂等：同一配置重复生成，条目列表逐项一致（确定性顺序）
    const BuildManifest again = GenerateManifest(config, work.path, work.path / "mygame.exe", "20260919_080503");
    REQUIRE(again.entries.size() == m.entries.size());
    for (size_t i = 0; i < m.entries.size(); ++i)
    {
        CHECK_EQ(again.entries[i].source, m.entries[i].source);
        CHECK_EQ(again.entries[i].destination, m.entries[i].destination);
        CHECK_EQ(again.entries[i].kind, m.entries[i].kind);
    }
}

// ---------------------------------------------------------------------------
// 4. 清单生成：缺失告警（目录 / 场景文件 / exe）
// ---------------------------------------------------------------------------

TEST_CASE("Build.ManifestMissingWarnings")
{
    TempDir work;
    WriteFileAt(work.path, "mygame.exe", "EXE"); // 仅有 exe：shaders/assets/scene.json 全缺

    const BuildConfig config;
    const BuildManifest m = GenerateManifest(config, work.path, work.path / "mygame.exe", "20260919_080503");
    // 仅 exe 条目；3 条缺失告警（shaders / assets / scene.json）
    CHECK_EQ(m.entries.size(), size_t(1));
    CHECK_EQ(m.entries[0].kind, std::string("exe"));
    CHECK_EQ(m.warnings.size(), size_t(3));

    // 关闭 shaders/assets 打包 → 告警只剩场景缺失
    BuildConfig trimmed;
    trimmed.copyShaders = false;
    trimmed.packageAssets = false;
    const BuildManifest m2 = GenerateManifest(trimmed, work.path, work.path / "mygame.exe", "20260919_080503");
    CHECK_EQ(m2.warnings.size(), size_t(1));
    CHECK_EQ(m2.entries.size(), size_t(1));

    // exe 路径未知（空）→ 告警且无 exe 条目
    const BuildManifest m3 = GenerateManifest(config, work.path, {}, "20260919_080503");
    CHECK_EQ(m3.entries.size(), size_t(0));
    CHECK_EQ(m3.warnings.size(), size_t(4));

    // exe 路径不存在 → 告警且无 exe 条目
    const BuildManifest m4 = GenerateManifest(config, work.path, work.path / "missing.exe", "20260919_080503");
    CHECK_EQ(m4.entries.size(), size_t(0));
    CHECK_EQ(m4.warnings.size(), size_t(4));
}

// ---------------------------------------------------------------------------
// 5. 执行器：临时目录真实拷贝往返（产物自包含 + 重复构建幂等）
// ---------------------------------------------------------------------------

TEST_CASE("Build.ExecutorRoundTrip")
{
    TempDir work;
    MakeFakeWorkDir(work.path);
    const BuildConfig config;
    const BuildManifest m = GenerateManifest(config, work.path, work.path / "mygame.exe", "20260919_080503");
    REQUIRE(m.entries.size() == 7);

    const ExecuteReport r = ExecuteManifest(m, work.path);
    CHECK(r.fatalError.empty());
    CHECK(r.AllSucceeded());
    CHECK_EQ(r.results.size(), size_t(7));
    CHECK_EQ(r.SuccessCount(), size_t(7));
    CHECK_EQ(r.FailureCount(), size_t(0));

    // 产物自包含：逐文件存在且内容一致，嵌套子目录保留
    const std::filesystem::path out = work.path / "builds" / "20260919_080503";
    CHECK_EQ(ReadAt(out / "mygame.exe"), std::string("FAKE-EXE-BYTES"));
    CHECK_EQ(ReadAt(out / "shaders" / "vert.spv"), std::string("V"));
    CHECK_EQ(ReadAt(out / "shaders" / "frag.spv"), std::string("F"));
    CHECK_EQ(ReadAt(out / "shaders" / "include" / "common.glsl"), std::string("C"));
    CHECK_EQ(ReadAt(out / "assets" / "tiles.png"), std::string("PNG"));
    CHECK_EQ(ReadAt(out / "assets" / "models" / "m.gltf"), std::string("GLTF"));
    CHECK_EQ(ReadAt(out / "scene.json"), std::string("{}"));

    // 重复构建（覆盖已存在文件）仍然全成功
    const ExecuteReport again = ExecuteManifest(m, work.path);
    CHECK(again.fatalError.empty());
    CHECK(again.AllSucceeded());
}

// ---------------------------------------------------------------------------
// 6. 执行器：逐文件失败报告（来源缺失不中断其余拷贝 + 嵌套输出目录创建）
// ---------------------------------------------------------------------------

TEST_CASE("Build.ExecutorPerFileFailure")
{
    TempDir work;
    WriteFileAt(work.path, "a.txt", "A");
    WriteFileAt(work.path, "b.txt", "B");

    BuildManifest m;
    m.outputDirectory = (work.path / "out" / "deep" / "builds").generic_string(); // 嵌套输出目录
    m.entries.push_back({"a.txt", "a.txt", "scene"});
    m.entries.push_back({"missing.bin", "missing.bin", "assets"});
    m.entries.push_back({"b.txt", "sub/b.txt", "scene"});

    const ExecuteReport r = ExecuteManifest(m, work.path);
    CHECK(r.fatalError.empty());
    CHECK(!r.AllSucceeded());
    CHECK_EQ(r.results.size(), size_t(3));
    CHECK_EQ(r.SuccessCount(), size_t(2));
    CHECK_EQ(r.FailureCount(), size_t(1));

    // 失败项可定位（错误信息非空、目的地正确），成功项无错误信息
    const CopyResult* failed = nullptr;
    for (const CopyResult& res : r.results)
        if (!res.success)
            failed = &res;
    REQUIRE(failed != nullptr);
    CHECK_EQ(failed->destination, std::string("missing.bin"));
    CHECK(!failed->error.empty());
    for (const CopyResult& res : r.results)
        if (res.success)
            CHECK(res.error.empty());

    // 成功项落盘（含子目录目的地）+ 嵌套输出目录被创建
    CHECK_EQ(ReadAt(work.path / "out" / "deep" / "builds" / "a.txt"), std::string("A"));
    CHECK_EQ(ReadAt(work.path / "out" / "deep" / "builds" / "sub" / "b.txt"), std::string("B"));
    CHECK(!std::filesystem::exists(work.path / "out" / "deep" / "builds" / "missing.bin"));
}
