#pragma once
// 场景管理：场景物体、光源、三角形统计、实例缓冲
// 原 Application 中 scene_, spinAngles_, pointLights_, sceneMesh_, torusMesh_ 等逻辑

#include "ISubSystem.h"
#include "scene/Scene.h"
#include "render/Mesh.h"
#include "render/InstanceBuffer.h"
#include "render/Frustum.h"
#include <vector>
#include <filesystem>
#include <glm/glm.hpp>
#include <filesystem>
#include <glm/glm.hpp>

namespace BigHero::App
{

class SceneManager final : public ISubSystem
{
public:
    SceneManager() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "SceneManager"; }
    [[nodiscard]] int Priority() const noexcept override { return -40; }

    void Init(class Context& ctx, class Renderer& renderer) { ctx_ = &ctx; renderer_ = &renderer; }
    void Shutdown() override { sceneMesh_.Destroy(); torusMesh_.Destroy(); cubeInstances_.Destroy(); torusInstances_.Destroy(); groundInstances_.Destroy(); }

    void Update(const FrameContext& frame) override { UpdateSpin(frame.deltaTime); }
    void PreRender(uint32_t frameIndex) override { FillInstanceBuffers(); }

    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    void BuildDefaultScene() { scene_ = Scene::BuildDefaultScene(); UpdateTriangleCount(); }
    void LoadTorusModel(const class Context& ctx) { /* 加载 torus.obj */ }

    void UpdateTriangleCount() { triangleCount_ = Scene::kCubeIndexCount / 3 * static_cast<uint32_t>(scene_.size()) + Scene::kGroundIndexCount / 3; if (hasTorus_) triangleCount_ += static_cast<uint32_t>(torusMesh_.IndexCount() / 3); }

    void CreateMeshResources(const class Context& ctx);
    void CreateInstanceBuffers(const class Context& ctx);
    void FillInstanceBuffers();
    void UpdateSpin(float dt);

    // 点光源管理
    void AddPointLight(const Scene::PointLightParams& light) { if (pointLights_.size() < 8) pointLights_.push_back(light); }
    void RemovePointLight(size_t index) { if (index < pointLights_.size()) pointLights_.erase(pointLights_.begin() + index); }

    // 场景序列化
    void SaveScene() const;
    void LoadScene();

    // 可见性剔除
    void UpdateVisibility(const class Frustum& frustum);

    // 访问器
    [[nodiscard]] const std::vector<Scene::SceneObject>& Scene() const noexcept { return scene_; }
    [[nodiscard]] std::vector<Scene::SceneObject>& Scene() noexcept { return scene_; }
    [[nodiscard]] const std::vector<float>& SpinAngles() const noexcept { return spinAngles_; }
    [[nodiscard]] std::vector<float>& SpinAnglesRef() noexcept { return spinAngles_; }
    [[nodiscard]] const std::vector<Scene::PointLightParams>& PointLights() const noexcept { return pointLights_; }
    [[nodiscard]] std::vector<Scene::PointLightParams>& PointLightsRef() noexcept { return pointLights_; }
    [[nodiscard]] const class Render::Mesh& SceneMesh() const noexcept { return sceneMesh_; }
    [[nodiscard]] const class Render::Mesh& TorusMesh() const noexcept { return torusMesh_; }
    [[nodiscard]] bool HasTorus() const noexcept { return hasTorus_; }
    [[nodiscard]] const std::vector<uint8_t>& Visible() const noexcept { return visible_; }
    [[nodiscard]] std::vector<uint8_t>& Visible() noexcept { return visible_; }
    [[nodiscard]] uint32_t VisibleCount() const noexcept { return visibleCount_; }
    [[nodiscard]] uint32_t CulledCount() const noexcept { return culledCount_; }
    [[nodiscard]] const std::vector<float>& SpinAnglesRef() noexcept { return spinAngles_; }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return triangleCount_; }
    [[nodiscard]] float CameraFOV() const noexcept { return cameraFOV_; }
    void SetCameraFOV(float fov) { cameraFOV_ = fov; }
    [[nodiscard]] const std::vector<Scene::PointLightParams>& PointLightsRef() noexcept { return pointLights_; }
    [[nodiscard]] const std::vector<Render::InstanceData>& InstanceScratch() const noexcept { return instanceScratch_; }
    [[nodiscard]] uint32_t CubeInstanceCount() const noexcept { return cubeInstanceCount_; }
    [[nodiscard]] uint32_t TorusInstanceCount() const noexcept { return torusInstanceCount_; }
    [[nodiscard]] const Render::InstanceBuffer& CubeInstances() const noexcept { return cubeInstances_; }
    [[nodiscard]] const Render::InstanceBuffer& TorusInstances() const noexcept { return torusInstances_; }
    [[nodiscard]] const Render::InstanceBuffer& GroundInstances() const noexcept { return groundInstances_; }

private:
    class Context* ctx_ = nullptr;
    class Renderer* renderer_ = nullptr;

    std::vector<Scene::SceneObject> scene_;
    std::vector<float> spinAngles_;
    std::vector<Scene::PointLightParams> pointLights_;
    Render::Mesh sceneMesh_;
    Render::Mesh torusMesh_;
    bool hasTorus_ = false;
    Render::InstanceBuffer cubeInstances_;
    Render::InstanceBuffer torusInstances_;
    Render::InstanceBuffer groundInstances_;
    std::vector<Render::InstanceData> instanceScratch_;
    std::vector<uint8_t> visible_;
    uint32_t visibleCount_ = 0;
    uint32_t culledCount_ = 0;
    uint32_t cubeInstanceCount_ = 0;
    uint32_t torusInstanceCount_ = 0;
    float cameraFOV_ = 60.0f;
    static constexpr const char* kScenePath = "scene.json";
};

} // namespace BigHero::App