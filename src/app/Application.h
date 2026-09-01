#pragma once
#include "core/AssetCache.h"
#include "core/Log.h"
#include "editor/EditorOverlay.h"
#include "editor/EditorPanel.h"
#include "editor/Gizmo.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/CubeShadowMap.h"
#include "render/EnvironmentLighting.h"
#include "render/Frustum.h"
#include "render/InstanceBuffer.h"
#include "render/Mesh.h"
#include "render/Renderer.h"
#include "render/ShadowMap.h"
#include "render/Texture.h"
#include "render/descriptor_set.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include "render/ubo_buffer.h"
#include "render/ubo_structs.h"
#include "scene/Camera.h"
#include "scene/CubeMesh.h"
#include "scene/ObjModel.h"
#include "scene/Picking.h"
#include "scene/Scene.h"

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
// 应用层：装配引擎资源 + 主循环 + 输入/UI/录制回调。
// 把原 main.cpp 的过程式代码封装为类：状态由成员变量管理，
// 各阶段（更新/可见性/UBO/录制）拆分为独立方法，便于维护与扩展。
//
// 成员声明顺序即初始化顺序，析构时逆序释放，保证 Vulkan 资源在
// Context 销毁前全部释放。
class Application
{
  public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // 初始化全部资源并进入主循环，返回进程退出码
    int Run();

  private:
    // ---- 推送常量结构 ----
    struct PushShadow
    {
        glm::mat4 lightSpace;
        glm::mat4 model;
    };
    static_assert(sizeof(PushShadow) == 128, "PushShadow must be exactly 128 bytes (push constant limit)");

    struct PushCubeShadow
    {
        glm::mat4 model;
        glm::vec4 faceIndex; // x = face index (0..5)
    };
    static_assert(sizeof(PushCubeShadow) <= 128, "PushCubeShadow exceeds push constant limit");

    struct PushSky
    {
        glm::mat4 invViewProj;
    };

    // ---- 初始化 ----
    void InitResources();
    void CreatePipelines();
    void SetupCallbacks();
    void InitScene();

    // ---- 每帧更新 ----
    void UpdateTime();
    void UpdateCamera();
    void UpdateGizmo();
    void UpdateVisibility();
    void FillInstanceBuffers();
    void UpdateUniforms();
    void UpdateFpsTitle();
    void HandlePicking();
    void UpdateDeferredState();

    // ---- 录制回调 ----
    void RecordScene(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void RecordUi(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent);
    void RecordPrePass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void RecordLighting(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);

    // ---- 管线重建（交换链格式变化时回调） ----
    void RebuildMainPipelines();
    void RebuildDeferredPipelines();
    void UpdateGBufferSets();

    // ---- 辅助 ----
    [[nodiscard]] glm::mat4 ComputeLightSpaceMatrix() const;
    void FillPointShadowMatrices(Render::PointShadowUBO& out) const;
    uint32_t FillMeshInstances(uint32_t meshId, Render::InstanceBuffer& buffer);
    void DrawShadowCasters(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, const glm::mat4& lightSpace);
    void DrawCubeShadowCasters(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int face, uint32_t frameIndex);
    [[nodiscard]] static glm::vec3 GetActiveShadowLight(const std::vector<PointLightParams>& lights);

    // ---- 常量配置 ----
    static constexpr uint32_t kWindowWidth = 1600;
    static constexpr uint32_t kWindowHeight = 900;
    static constexpr const char* kVertSpvPath = "shaders/vert.spv";
    static constexpr const char* kFragSpvPath = "shaders/frag.spv";
    static constexpr const char* kDefaultTexturePath = "assets/tiles.png";
    static constexpr const char* kNormalMapPath = "assets/tiles_normal.png";
    static constexpr const char* kTorusModelPath = "assets/models/torus.obj";
    static constexpr float kPanSpeed = 4.0f;
    static constexpr float kCullMargin = 1.05f;
    static constexpr float kShadowOrthoHalf = 14.0f;
    static constexpr float kShadowNear = 0.5f;
    static constexpr float kShadowFar = 45.0f;
    static constexpr float kShadowEyeDistance = 20.0f;
    static constexpr float kPointShadowNear = 0.1f;
    static constexpr float kPointShadowFar = 50.0f;
    static constexpr float kGizmoPickRadius = 12.0f;
    static constexpr float kGizmoAxisLength = 80.0f;

#ifdef NDEBUG
    static constexpr bool kEnableValidation = false;
#else
    static constexpr bool kEnableValidation = true;
#endif

    // ---- 资源（声明顺序 = 初始化顺序，析构逆序释放） ----
    Window window_;
    Context ctx_;
    Renderer renderer_;

    ShadowMap shadowMap_;
    CubeShadowMap cubeShadowMap_;
    EnvironmentLighting envLighting_;

    Render::DescriptorManager descManager_;
    std::vector<Render::UboBuffer<Render::CameraUBO>> cameraUbos_;
    std::vector<Render::UboBuffer<Render::LightUBO>> lightUbos_;
    std::vector<Render::UboBuffer<Render::PointShadowUBO>> pointShadowUbos_;

    // 资源管理器：统一缓存纹理等 GPU 资源，LRU 淘汰 + 引用计数
    Core::AssetManager assetManager_;

    std::shared_ptr<Texture> texture_;
    std::shared_ptr<Texture> normalTexture_;

    Render::Mesh sceneMesh_;
    Render::Mesh torusMesh_;
    bool hasTorus_ = false;

    // 管线配置（保留为成员，供交换链重建时复用）
    Render::GraphicsPipelineConfig pipelineConfig_;
    Render::GraphicsPipelineConfig shadowConfig_;
    Render::GraphicsPipelineConfig cubeShadowConfig_;
    Render::GraphicsPipelineConfig skyboxConfig_;
    Render::GraphicsPipelineConfig gbufferConfig_;
    Render::GraphicsPipelineConfig defLightConfig_;

    // GraphicsPipeline 无默认构造，用 optional 在 Init 阶段原位构造
    std::optional<Render::GraphicsPipeline> pipeline_;
    std::optional<Render::GraphicsPipeline> shadowPipeline_;
    std::optional<Render::GraphicsPipeline> cubeShadowPipeline_;
    std::optional<Render::GraphicsPipeline> skyboxPipeline_;
    std::optional<Render::GraphicsPipeline> gbufferPipeline_;
    std::optional<Render::GraphicsPipeline> lightingPipeline_;

    EditorOverlay editorOverlay_;
    EditorPanel editorPanel_;
    LightParams lightParams_;

    // ---- 场景状态 ----
    std::vector<Scene::SceneObject> scene_;
    std::vector<float> spinAngles_;
    std::vector<PointLightParams> pointLights_;
    OrbitCamera camera_;
    int selectedObject_ = -1;
    bool deferred_ = false;
    bool prevDeferred_ = false;

    // ---- Gizmo 交互状态 ----
    Editor::GizmoMode gizmoMode_ = Editor::GizmoMode::None;
    Editor::GizmoAxis gizmoDragAxis_ = Editor::GizmoAxis::None;
    bool gizmoDragging_ = false;
    bool gizmoSuppressClick_ = false;
    glm::vec2 gizmoLastMouse_{0.0f};

    // ---- 实例缓冲 ----
    Render::InstanceBuffer cubeInstances_;
    Render::InstanceBuffer torusInstances_;
    Render::InstanceBuffer groundInstances_;
    std::vector<Render::InstanceData> instanceScratch_; // 每帧复用，避免动态分配
    uint32_t cubeInstanceCount_ = 0;
    uint32_t torusInstanceCount_ = 0;

    // ---- 可见性 ----
    std::vector<uint8_t> visible_;
    uint32_t visibleCount_ = 0;
    uint32_t culledCount_ = 0;

    // ---- 计时 ----
    double lastTime_ = 0.0;
    double fpsTimer_ = 0.0;
    uint32_t fpsFrames_ = 0;
    uint32_t lastFps_ = 0;
    float lastFrameMs_ = 0.0f;
    float deltaTime_ = 0.0f;
    const std::string baseTitle_ = "BigHero Engine - Vulkan";

    // ---- 统计 ----
    uint32_t triangleCount_ = 0;
};
} // namespace BigHero
