#include "app/systems/SceneIoHost.h"
#include "framework/test_common.h"
#include "scene/SceneSerializer.h"

#include <chrono>
#include <filesystem>

namespace
{
struct SceneIoFixture
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("bighero_scene_io_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    BigHero::Scene::EcsScene ecs;
    std::vector<BigHero::Scene::SceneObject> objects;
    BigHero::LightParams light;
    std::vector<BigHero::PointLightParams> pointLights;
    BigHero::OrbitCamera camera;
    bool hasTorus = false;
    int selected = 3;
    int repacks = 0;
    int triangleUpdates = 0;
    int physicsUpdates = 0;
    BigHero::SceneIoHost host{ecs,    objects,  light,    pointLights,
                              camera, hasTorus, selected, (root / "scene.json").string()};

    SceneIoFixture()
    {
        std::error_code ec;
        REQUIRE(std::filesystem::create_directory(root, ec));
        REQUIRE(!ec);
        host.SetLoadHooks(
            [this]
            {
                objects = ecs.BuildPacket();
                ++repacks;
            },
            [this] { ++triangleUpdates; }, [this] { ++physicsUpdates; });
    }

    ~SceneIoFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
} // namespace

TEST_CASE("SceneIo.MissingMeshPreservesEntitiesAndHierarchy")
{
    SceneIoFixture f;
    BigHero::Scene::SceneData input;
    input.cameraFov = 73.0f;
    input.light.exposure = 2.5f;
    input.pointLights.push_back({});
    input.objects.resize(5);
    for (size_t i = 0; i < input.objects.size(); ++i)
    {
        input.objects[i].meshId = static_cast<uint32_t>(i);
        input.objects[i].emissive = {static_cast<float>(i) + 0.5f, 2.0f, 3.0f};
    }
    // 父节点在子节点之后；缺失圆环也是父节点。读档不能改变稳定序。
    input.objects[0].parentIndex = 4;
    input.objects[2].parentIndex = 1;
    input.objects[3].parentIndex = 2;
    REQUIRE(BigHero::Scene::SaveSceneToFile(input, (f.root / "scene.json").string()));
    f.host.Load();
    REQUIRE(f.objects.size() == input.objects.size());
    CHECK_EQ(f.ecs.ObjectCount(), input.objects.size());
    for (size_t i = 0; i < input.objects.size(); ++i)
    {
        CHECK_EQ(f.objects[i].meshId, input.objects[i].meshId);
        CHECK_EQ(f.objects[i].parentIndex, input.objects[i].parentIndex);
        CHECK(f.objects[i].emissive == input.objects[i].emissive);
    }
    CHECK_NEAR(f.camera.fovDegrees_, 73.0f, 1e-4f);
    CHECK_NEAR(f.light.exposure, 2.5f, 1e-4f);
    CHECK_EQ(f.pointLights.size(), 1u);
    CHECK_EQ(f.selected, -1);
    CHECK_EQ(f.repacks, 1);
    CHECK_EQ(f.triangleUpdates, 1);
    CHECK_EQ(f.physicsUpdates, 1);

    // 资源未恢复时再次保存也不能丢实体、父子关系和自发光。
    f.host.Save();
    BigHero::Scene::SceneData saved;
    REQUIRE(BigHero::Scene::LoadSceneFromFile((f.root / "scene.json").string(), saved));
    REQUIRE(saved.objects.size() == input.objects.size());
    CHECK_EQ(saved.objects[2].parentIndex, 1);
    CHECK(saved.objects[3].emissive == input.objects[3].emissive);
}

TEST_CASE("SceneIo.InvalidLoadKeepsLiveState")
{
    SceneIoFixture f;
    BigHero::Scene::SceneObject initial;
    initial.position = {7.0f, 8.0f, 9.0f};
    (void)f.ecs.CreateObject(initial);
    f.objects = f.ecs.BuildPacket();
    f.light.exposure = 4.0f;
    f.camera.fovDegrees_ = 65.0f;
    f.pointLights.push_back({});

    auto checkUnchanged = [&f, &initial]
    {
        CHECK_EQ(f.ecs.ObjectCount(), 1u);
        REQUIRE(f.objects.size() == 1);
        CHECK(f.objects[0].position == initial.position);
        CHECK_NEAR(f.light.exposure, 4.0f, 1e-4f);
        CHECK_NEAR(f.camera.fovDegrees_, 65.0f, 1e-4f);
        CHECK_EQ(f.pointLights.size(), 1u);
        CHECK_EQ(f.selected, 3);
        CHECK_EQ(f.repacks, 0);
        CHECK_EQ(f.triangleUpdates, 0);
        CHECK_EQ(f.physicsUpdates, 0);
    };
    f.host.Load(); // 文件不存在
    checkUnchanged();
    {
        std::ofstream out(f.root / "scene.json");
        out << "{\"light\":{\"exposure\":99},\"objects\":[";
    }
    f.host.Load(); // 已读到部分字段后解析失败
    checkUnchanged();
    BigHero::Scene::SceneData future;
    future.version = BigHero::Scene::kCurrentSceneVersion + 1;
    REQUIRE(BigHero::Scene::SaveSceneToFile(future, (f.root / "scene.json").string()));
    f.host.Load();
    checkUnchanged();
}
