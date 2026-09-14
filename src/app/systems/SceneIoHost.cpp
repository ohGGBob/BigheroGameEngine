#include "app/systems/SceneIoHost.h"

#include "core/Log.h"
#include "scene/SceneSerializer.h"

namespace BigHero
{
void SceneIoHost::Save()
{
    Scene::SceneData data;
    data.version = 1;
    data.cameraFov = camera_.fovDegrees_;

    // 方向光
    data.light.direction = light_.direction;
    data.light.color = light_.color;
    data.light.intensity = light_.intensity;
    data.light.ambient = light_.ambient;
    data.light.shadowStrength = light_.shadowStrength;
    data.light.shadowBias = light_.shadowBias;
    data.light.iblStrength = light_.iblStrength;
    data.light.exposure = light_.exposure;

    // 点光源
    data.pointLights.reserve(pointLights_.size());
    for (const auto& pl : pointLights_)
    {
        Scene::SerializablePointLight spl;
        spl.position = pl.position;
        spl.color = pl.color;
        spl.intensity = pl.intensity;
        spl.radius = pl.radius;
        spl.castsShadow = pl.castsShadow;
        data.pointLights.push_back(spl);
    }

    // 场景物体
    data.objects = scene_;

    if (Scene::SaveSceneToFile(data, kScenePath))
        LOG_INFO("场景已保存: " << kScenePath << "（" << data.objects.size() << "物体 / " << data.pointLights.size()
                                << "灯）");
    else
        LOG_ERROR("场景保存失败: " << kScenePath);
}

void SceneIoHost::Load()
{
    Scene::SceneData data;
    if (!Scene::LoadSceneFromFile(kScenePath, data))
    {
        LOG_WARN("场景文件不存在或解析失败: " << kScenePath << "，保持当前场景");
        return;
    }

    // 方向光
    light_.direction = data.light.direction;
    light_.color = data.light.color;
    light_.intensity = data.light.intensity;
    light_.ambient = data.light.ambient;
    light_.shadowStrength = data.light.shadowStrength;
    light_.shadowBias = data.light.shadowBias;
    light_.iblStrength = data.light.iblStrength;
    light_.exposure = data.light.exposure;

    // 点光源（上限 kMaxPointLights）
    pointLights_.clear();
    for (size_t i = 0; i < data.pointLights.size() && i < EditorPanel::kMaxPointLights; ++i)
    {
        PointLightParams pl;
        pl.position = data.pointLights[i].position;
        pl.color = data.pointLights[i].color;
        pl.intensity = data.pointLights[i].intensity;
        pl.radius = data.pointLights[i].radius;
        pl.castsShadow = data.pointLights[i].castsShadow;
        pointLights_.push_back(pl);
    }

    // 场景物体（过滤掉 torus 物体如果 torus 模型未加载）：ECS 全量重建，自转角重置为 phase
    std::vector<Scene::SceneObject> objs;
    for (const auto& obj : data.objects)
    {
        if (obj.meshId != 0 && !hasTorus_)
            continue;
        objs.push_back(obj);
    }
    ecsScene_.LoadPacket(objs);
    if (repackScene_)
        repackScene_();

    // 相机 FOV
    camera_.fovDegrees_ = data.cameraFov;

    // 取消选中（索引可能失效）
    selectedObject_ = -1;

    // 重算三角形数
    if (recalcTriangles_)
        recalcTriangles_();

    // 重建物理刚体
    if (rebuildPhysics_)
        rebuildPhysics_();

    LOG_INFO("场景已加载: " << kScenePath << "（" << scene_.size() << "物体 / " << pointLights_.size() << "灯）");
}
} // namespace BigHero
