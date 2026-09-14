#pragma once
// 阶段 3b：场景序列化子系统（自 Application 拆出）。
// 职责：Save/Load scene.json（场景物体/方向光/点光源/相机 FOV 的序列化与回写）。
// 构造注入 Application 的场景状态引用（ECS 权威存储 + 投影包 + 光照 + 相机）；
// 加载后的联动（重投影包/三角形重算/物理重建/清除选中）经回调或引用交还 Application。

#include "editor/EditorPanel.h" // LightParams/PointLightParams/kMaxPointLights
#include "scene/Camera.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"

#include <functional>
#include <vector>

namespace BigHero
{
class SceneIoHost
{
  public:
    SceneIoHost(Scene::EcsScene& ecsScene, std::vector<Scene::SceneObject>& scene, LightParams& light,
                std::vector<PointLightParams>& pointLights, OrbitCamera& camera, const bool& hasTorus,
                int& selectedObject)
        : ecsScene_(ecsScene), scene_(scene), light_(light), pointLights_(pointLights), camera_(camera),
          hasTorus_(hasTorus), selectedObject_(selectedObject)
    {
    }

    SceneIoHost(const SceneIoHost&) = delete;
    SceneIoHost& operator=(const SceneIoHost&) = delete;

    // 加载成功后的联动回调（Application 侧：RepackScene / RecalculateTriangleCount / physicsHost_.RebuildBodies）
    void SetLoadHooks(std::function<void()> repackScene, std::function<void()> recalcTriangles,
                      std::function<void()> rebuildPhysics)
    {
        repackScene_ = std::move(repackScene);
        recalcTriangles_ = std::move(recalcTriangles);
        rebuildPhysics_ = std::move(rebuildPhysics);
    }

    // 保存场景到 scene.json（物体/方向光/点光源/相机 FOV）
    void Save();

    // 从 scene.json 加载（文件缺失/解析失败保持当前场景）；成功时回写光照/相机并触发联动回调
    void Load();

  private:
    static constexpr const char* kScenePath = "scene.json";

    Scene::EcsScene& ecsScene_;
    std::vector<Scene::SceneObject>& scene_;
    LightParams& light_;
    std::vector<PointLightParams>& pointLights_;
    OrbitCamera& camera_;
    const bool& hasTorus_;   // torus 模型未加载时过滤其物体（meshId != 0）
    int& selectedObject_;    // 加载后索引失效，清除选中

    std::function<void()> repackScene_;
    std::function<void()> recalcTriangles_;
    std::function<void()> rebuildPhysics_;
};
} // namespace BigHero
