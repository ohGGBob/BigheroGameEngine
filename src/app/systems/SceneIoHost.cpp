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

    if (Scene::SaveSceneToFile(data, scenePath_))
        LOG_INFO("场景已保存: " << scenePath_ << "（" << data.objects.size() << "物体 / " << data.pointLights.size()
                                << "灯）");
    else
        LOG_ERROR("场景保存失败: " << scenePath_);
}

void SceneIoHost::Load()
{
    Scene::SceneData data;
    if (!Scene::LoadSceneFromFile(scenePath_, data))
    {
        LOG_WARN("场景文件不存在或解析失败: " << scenePath_ << "，保持当前场景");
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

    // 资源缺失不能删除持久化实体：保留稳定序和父索引，资源恢复后仍可渲染。
    // ECS 全量重建，自转角重置为 phase。
    for (const auto& obj : data.objects)
    {
        if (obj.meshId == 1 && !hasTorus_)
        {
            LOG_WARN("圆环模型未加载，保留场景实体及层级，待资源恢复后显示");
            break;
        }
    }
    ecsScene_.LoadPacket(data.objects);
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

    LOG_INFO("场景已加载: " << scenePath_ << "（" << ecsScene_.ObjectCount() << "物体 / " << pointLights_.size()
                            << "灯）");
}
} // namespace BigHero
