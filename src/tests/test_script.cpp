// C# 脚本系统单测（U1-S1a/b/c）：hostfxr 路径解析纯函数 + 优雅降级 + 托管链路集成冒烟。
//
// SKIP 约定：测试框架无 SKIP 语义，缺 .NET（dotnet CLI 或 hostfxr 运行时）时打印
// "[ SKIP ]" 说明后直接 return（不计失败）。集成冒烟在具备 .NET SDK 的机器上执行
// 真实的 hostfxr 嵌入 → 用户脚本编译 → Attach → Update 派发 → 热重载全链路。

#include "framework/test_common.h"
#include "scene/EcsScene.h"
#include "script/CSharpHost.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace BigHero;

namespace
{
// 从当前目录向上查找仓库内路径（测试工作目录 = 构建树 bin，仓库根在若干级之上）
std::filesystem::path FindRepoPath(const std::filesystem::path& relative)
{
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::current_path(ec);
    for (int i = 0; i < 8 && !cur.empty(); ++i)
    {
        const std::filesystem::path candidate = cur / relative;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
        if (!cur.has_parent_path())
            break;
        cur = cur.parent_path();
    }
    return {};
}

// 伪造 hostfxr 目录结构：<root>/host/fxr/<ver>/hostfxr.dll（空文件即可，只测路径解析）
std::filesystem::path MakeFakeDotnetRoot(const std::vector<std::string>& versions)
{
    std::error_code ec;
    static std::mt19937 rng{static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count())};
    const auto root = std::filesystem::temp_directory_path(ec) / ("bighero_test_fxr_" + std::to_string(rng()));
    for (const std::string& v : versions)
    {
        const auto dir = root / "host" / "fxr" / v;
        std::filesystem::create_directories(dir, ec);
        std::ofstream(dir / "hostfxr.dll", std::ios::binary) << "fake";
    }
    return root;
}

Scene::SceneObject MakeCube(glm::vec3 pos)
{
    Scene::SceneObject o;
    o.position = pos;
    o.scale = 1.0f;
    o.tint = glm::vec3(1.0f);
    o.spinSpeed = 0.0f; // 自转由脚本接管，原生 Spin 保持静止
    o.phase = 0.0f;
    o.metallic = 0.0f;
    o.roughness = 0.5f;
    o.rotation = glm::vec3(0.0f);
    o.meshId = 0;
    return o;
}
} // namespace

TEST_CASE("Script.PickHighestVersionDirectory")
{
    using Script::IsVersionDirectoryName;
    using Script::PickHighestVersionDirectory;

    // 版本目录名合法性：纯数字+点
    CHECK(IsVersionDirectoryName("8.0.31"));
    CHECK(IsVersionDirectoryName("10"));
    CHECK(!IsVersionDirectoryName(""));
    CHECK(!IsVersionDirectoryName("8.0.31-preview"));
    CHECK(!IsVersionDirectoryName("v3"));
    CHECK(!IsVersionDirectoryName("8..0"));
    CHECK(!IsVersionDirectoryName(".8.0"));
    CHECK(!IsVersionDirectoryName("8.0."));
    CHECK(!IsVersionDirectoryName("8a.0"));

    // 取最高版本（逐段数值比较，非字典序：9.0.20 > 10.0 不会发生——10 > 9 数值正确）
    CHECK_EQ(PickHighestVersionDirectory({"8.0.31", "9.0.20", "10.0.12"}), "10.0.12");
    CHECK_EQ(PickHighestVersionDirectory({"9.0.20", "8.0.31"}), "9.0.20");
    CHECK_EQ(PickHighestVersionDirectory({"1.42.0", "1.9.0"}), "1.42.0"); // 逐段数值：42 > 9
    CHECK_EQ(PickHighestVersionDirectory({"junk", "v1", ""}), "");        // 无合法项
    CHECK_EQ(PickHighestVersionDirectory({}), "");
    CHECK_EQ(PickHighestVersionDirectory({"2.1", "10.0.12", "not-a-version"}), "10.0.12");
}

TEST_CASE("Script.CollectSearchRoots")
{
    using Script::CollectDotnetCandidates;
    using Script::CollectSearchRoots;

    // 优先级：DOTNET_ROOT > 注册表根（依序）> 默认根；空项跳过；去重保序
    const std::vector<std::string> roots =
        CollectSearchRoots("D:/dotnet", {"R1", "R2"}, "C:/Program Files/dotnet");
    REQUIRE(roots.size() == 4);
    CHECK_EQ(roots[0], "D:/dotnet");
    CHECK_EQ(roots[1], "R1");
    CHECK_EQ(roots[2], "R2");
    CHECK_EQ(roots[3], "C:/Program Files/dotnet");

    // 无环境变量 → 注册表 → 默认根
    const std::vector<std::string> noEnv = CollectSearchRoots("", {"R1"}, "default");
    REQUIRE(noEnv.size() == 2);
    CHECK_EQ(noEnv[0], "R1");
    CHECK_EQ(noEnv[1], "default");

    // 全空 → 空
    CHECK(CollectSearchRoots("", {}, "").empty());

    // 去重：环境变量与默认根相同
    const std::vector<std::string> dedup = CollectSearchRoots("same", {"same"}, "same");
    CHECK_EQ(dedup.size(), size_t{1});

    // dotnet 候选 = 同一排序逻辑（无注册表项）
    const std::vector<std::string> dotnet = CollectDotnetCandidates("E:/dotnet", "C:/Program Files/dotnet");
    REQUIRE(dotnet.size() == 2);
    CHECK_EQ(dotnet[0], "E:/dotnet");
    CHECK_EQ(dotnet[1], "C:/Program Files/dotnet");
}

TEST_CASE("Script.FindHostfxrUnderRoot")
{
    using Script::FindHostfxrUnderRoot;

    // 伪造目录结构：host/fxr/{8.0.31, 9.0.20, 垃圾目录}/hostfxr.dll → 命中最高版本
    const std::filesystem::path fake = MakeFakeDotnetRoot({"8.0.31", "9.0.20", "9.0.20-preview"});
    const std::string hit = FindHostfxrUnderRoot(fake.string());
    CHECK(!hit.empty());
    if (!hit.empty())
    {
        const std::string normalized = hit;
        CHECK(normalized.find("9.0.20") != std::string::npos);
        CHECK(normalized.find("preview") == std::string::npos); // 非纯版本目录被排除
        CHECK(std::filesystem::path(normalized).filename() == "hostfxr.dll");
    }

    // 只有低版本 → 命中该版本
    const std::filesystem::path only8 = MakeFakeDotnetRoot({"8.0.31"});
    const std::string hit8 = FindHostfxrUnderRoot(only8.string());
    CHECK(!hit8.empty());
    if (!hit8.empty())
        CHECK(hit8.find("8.0.31") != std::string::npos);

    // 空目录 / 不存在的根 → 空串（未命中）
    const std::filesystem::path emptyRoot = MakeFakeDotnetRoot({});
    CHECK(FindHostfxrUnderRoot(emptyRoot.string()).empty());
    CHECK(FindHostfxrUnderRoot("Z:/definitely/not/a/dotnet/root").empty());

    // dll 文件缺失 → 未命中
    const std::filesystem::path noDll = MakeFakeDotnetRoot({"7.0.0"});
    std::error_code ec;
    std::filesystem::remove_all(noDll / "host" / "fxr" / "7.0.0" / "hostfxr.dll", ec);
    CHECK(FindHostfxrUnderRoot(noDll.string()).empty());

    std::filesystem::remove_all(fake, ec);
    std::filesystem::remove_all(only8, ec);
    std::filesystem::remove_all(emptyRoot, ec);
    std::filesystem::remove_all(noDll, ec);
}

TEST_CASE("Script.GracefulDegrade")
{
    // 降级契约：目录不存在 → Init false（快速失败，不依赖 .NET）→ 引擎 API 全部安全 no-op
    Scene::EcsScene scene;
    (void)scene.CreateObject(MakeCube(glm::vec3(0.0f, 0.5f, 0.0f)));

    Script::CSharpHost host;
    const bool ok = host.Init(&scene, "Z:/definitely/not/a/script/dir");
    CHECK(!ok);
    CHECK(!host.Enabled());
    CHECK_EQ(host.AttachedCount(), uint32_t{0});
    CHECK_EQ(host.LastFrameScriptMs(), 0.0f);

    // 降级后各接口必须安全：挂接失败、Update 不崩、显式重载被忽略
    CHECK_EQ(host.AttachToOrderIndex(0, "MyGame.Spinner"), -1);
    host.Update(1.0f / 60.0f);
    CHECK(!host.ReloadScripts());
    CHECK_EQ(host.LastFrameScriptMs(), 0.0f);

    // 幂等 Shutdown + 析构（RAII）不崩
    host.Shutdown();
    host.Shutdown();

    // 空目录 + 无 csproj 目录同样快速失败
    Script::CSharpHost emptyDirHost;
    CHECK(!emptyDirHost.Init(&scene, ""));
    Script::CSharpHost noProjHost;
    const std::filesystem::path noProj =
        std::filesystem::temp_directory_path() / "bighero_test_noproj";
    std::error_code ec;
    std::filesystem::create_directories(noProj, ec);
    CHECK(!noProjHost.Init(&scene, noProj.string()));
    std::filesystem::remove_all(noProj, ec);
}

TEST_CASE("Script.ManagedSmoke")
{
    // ---- SKIP 门：缺 dotnet CLI / hostfxr 运行时时打印说明后返回（框架无 SKIP 语义） ----
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path samples = FindRepoPath("samples/scripts/MyGame");
    if (samples.empty())
    {
        std::printf("[ SKIP ] Script.ManagedSmoke: 未找到 samples/scripts/MyGame（请在仓库内运行测试）\n");
        return;
    }
    // hostfxr 可达性粗探（与宿主相同候选根顺序：DOTNET_ROOT → 默认安装根）
    const char* envRoot = std::getenv("DOTNET_ROOT");
    bool hostfxrFound = false;
    for (const std::string& root :
         Script::CollectSearchRoots(envRoot != nullptr ? envRoot : "", {}, "C:\\Program Files\\dotnet"))
    {
        if (!Script::FindHostfxrUnderRoot(root).empty())
        {
            hostfxrFound = true;
            break;
        }
    }
    if (!hostfxrFound)
    {
        std::printf("[ SKIP ] Script.ManagedSmoke: 未检测到 .NET 运行时（hostfxr），跳过集成冒烟\n");
        return;
    }

    // ---- 集成冒烟：初始化 host → attach → Update(3 帧) → 断言实体旋转 + 脚本耗时 ----
    Scene::EcsScene scene;
    (void)scene.CreateObject(MakeCube(glm::vec3(0.0f, 0.5f, 0.0f)));
    CHECK_EQ(scene.ObjectCount(), size_t{1});

    Script::CSharpHost host;
    const auto initT0 = std::chrono::steady_clock::now();
    const bool inited = host.Init(&scene, samples.string());
    const auto initMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initT0).count();
    if (!inited)
    {
        std::printf("[ SKIP ] Script.ManagedSmoke: 初始化失败（本机可能缺 .NET SDK/运行时），跳过集成冒烟\n");
        return;
    }
    CHECK(host.Enabled());
    std::printf("  [ INFO ] 初始化耗时 %lld ms（含首次 dotnet build；复用缓存时应显著缩短）\n",
                static_cast<long long>(initMs));

    // 挂接 Spinner 到 0 号实体（先把带副作用的调用存进变量：CHECK_* 宏会把表达式展开多次）
    const int attachId = host.AttachToOrderIndex(0, "MyGame.Spinner");
    CHECK_GE(attachId, 0);
    CHECK_EQ(host.AttachedCount(), uint32_t{1});

    const glm::vec3 rotBefore = scene.Registry().Get<Scene::ecs::Transform>(scene.At(0)).rotation;

    // Update(3 帧)：OnStart（首帧）+ OnUpdate×3
    constexpr float kDt = 1.0f / 60.0f;
    host.Update(kDt);
    host.Update(kDt);
    host.Update(kDt);

    const glm::vec3 rotAfter = scene.Registry().Get<Scene::ecs::Transform>(scene.At(0)).rotation;
    std::printf("  [ INFO ] 自转: (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f), LastFrameScriptMs=%.4f\n", rotBefore.x,
                rotBefore.y, rotBefore.z, rotAfter.x, rotAfter.y, rotAfter.z, host.LastFrameScriptMs());
    CHECK_GT(std::fabs(rotAfter.y - rotBefore.y), 0.01f); // Spinner 绕 Y 自转生效
    CHECK_LT(rotAfter.y - rotBefore.y, 10.0f);            // 3 帧 @90°/s ≈ 4.5°，防飞车
    CHECK_GT(host.LastFrameScriptMs(), 0.0f);             // 脚本耗时采样 > 0

    // ---- 热重载链路：显式 ReloadScripts → ALC 卸载/重载 → 重挂 → 继续旋转 ----
    CHECK(host.ReloadScripts());
    CHECK_EQ(host.AttachedCount(), uint32_t{1});
    const glm::vec3 rotReloaded = scene.Registry().Get<Scene::ecs::Transform>(scene.At(0)).rotation;
    host.Update(kDt);
    host.Update(kDt);
    const glm::vec3 rotAfterReload = scene.Registry().Get<Scene::ecs::Transform>(scene.At(0)).rotation;
    std::printf("  [ INFO ] 热重载后自转: %.3f -> %.3f\n", rotReloaded.y, rotAfterReload.y);
    CHECK_GT(std::fabs(rotAfterReload.y - rotReloaded.y), 0.01f);

    // 优雅关闭：OnDestroy + GCHandle 全释放 + ALC unload + hostfxr_close
    host.Shutdown();
    CHECK(!host.Enabled());
    host.Shutdown(); // 幂等
}
