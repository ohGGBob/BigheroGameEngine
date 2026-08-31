// BigHero Game Engine —— 纯逻辑单元测试
// 仅覆盖不依赖 GPU / 窗口系统的头文件内联逻辑，运行时无需初始化 Vulkan。
#include "scene/Scene.h"
#include "scene/CubeMesh.h"
#include "render/ubo_structs.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace
{
    int g_failures = 0;

    void check(bool cond, const char* expr, const char* file, int line)
    {
        if (!cond)
        {
            std::printf("FAIL: %s  (%s:%d)\n", expr, file, line);
            ++g_failures;
        }
    }
}

#define CHECK(cond) ::check((cond), #cond, __FILE__, __LINE__)

int main()
{
    using namespace BigHero;

    // ---- 场景 ----
    const std::vector<Scene::SceneObject> scene = Scene::BuildDefaultScene();
    CHECK(scene.size() == 6);
    CHECK(scene.front().meshId == 0);          // 首个为共享立方体
    CHECK(scene.back().meshId == 1);           // 末个为外部圆环体
    for (const Scene::SceneObject& obj : scene)
        CHECK(obj.scale > 0.0f);

    // ---- 网格顶点/索引布局 ----
    const std::vector<Scene::Vertex> cube = Scene::BuildCubeVertices();
    CHECK(cube.size() == Scene::kCubeVertexCount);
    const std::vector<Scene::Vertex> all = Scene::BuildSceneVertices();
    CHECK(all.size() == Scene::kCubeVertexCount + 4);   // 立方体 + 4 顶点地面
    const std::vector<uint32_t> indices = Scene::BuildSceneIndices();
    CHECK(indices.size() == Scene::kCubeIndexCount + Scene::kGroundIndexCount);
    CHECK(Scene::kCubeIndexCount == 36);
    CHECK(Scene::kGroundIndexCount == 6);

    // 顶点输入步幅必须与结构大小一致，否则 GPU 读取错位
    CHECK(Scene::Vertex::getBindingDesc().stride == sizeof(Scene::Vertex));
    {
        const auto attrs = Scene::Vertex::getAttrDesc();
        CHECK(attrs.size() == 5);
        CHECK(attrs[0].location == 0);
        CHECK(attrs[4].location == 4);
    }

    // ---- UBO 布局（std140 严格对齐） ----
    CHECK(sizeof(Render::CameraUBO) == 128);
    // GpuPointLight 必须为 16 的倍数：std140 规则要求"结构体数组"步长=大小向上取整到16，
    // 故元素取 48 字节（位置/强度/颜色/半径/阴影标志 + 填充），CPU 数组步长=GPU 步长。
    CHECK(sizeof(Render::GpuPointLight) == 48);
    CHECK(offsetof(Render::LightUBO, lightSpaceMatrix) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights[1]) - offsetof(Render::LightUBO, lights[0]) == 48);
    // 点光源立方体阴影 UBO：6 个 mat4 紧密数组，每 mat4 64 字节
    CHECK(sizeof(Render::PointShadowUBO) == 6 * 64);
    CHECK(offsetof(Render::PointShadowUBO, faceMatrices[1]) - offsetof(Render::PointShadowUBO, faceMatrices[0]) == 64);

    if (g_failures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test failure(s).\n", g_failures);
    return 1;
}
