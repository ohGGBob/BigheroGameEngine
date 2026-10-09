// 无窗口、无 Vulkan 上下文的独立脚本部署验证；初始化失败必须非零退出，不能 SKIP。
#include "scene/EcsScene.h"
#include "script/CSharpHost.h"

#include <cmath>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: BigHeroScriptCheck <scripts-directory>\n");
        return 2;
    }
    BigHero::Scene::EcsScene scene;
    (void)scene.CreateObject(BigHero::Scene::SceneObject{});
    BigHero::Script::CSharpHost host;
    if (!host.Init(&scene, argv[1]) || host.AttachToOrderIndex(0, "MyGame.Spinner") < 0)
        return 1;
    const auto entity = scene.At(0);
    const float before = scene.Registry().Get<BigHero::Scene::ecs::Transform>(entity).rotation.y;
    for (int i = 0; i < 3; ++i)
        host.Update(1.0f / 60.0f);
    const float after = scene.Registry().Get<BigHero::Scene::ecs::Transform>(entity).rotation.y;
    host.Shutdown();
    if (!std::isfinite(after) || std::fabs(after - before) < 0.01f)
        return 1;
    std::printf("Standalone script dispatch passed: %.3f -> %.3f degrees\n", before, after);
    return 0;
}
