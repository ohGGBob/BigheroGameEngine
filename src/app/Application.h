#pragma once
#include "audio/AudioEngine.h"
#include "audio/Sound.h"
#include "core/AssetCache.h"
#include "core/FrameProfiler.h"
#include "core/Log.h"
#include "editor/EditorOverlay.h"
#include "editor/EditorPanel.h"
#include "editor/Gizmo.h"
#include "physics/PhysicsEngine.h"
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
#include "scene/AnimationStateMachine.h"
#include "scene/Camera.h"
#include "scene/CubeMesh.h"
#include "scene/ObjModel.h"
#include "scene/Picking.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include "game/CommandStack.h"
#include "game/EmitterPresets.h"
#include "game/NavAgent.h"
#include "game/NavGrid.h"
#include "game/ParticleSystem.h"
#include "game/SceneCommand.h"
#include "render/ParticleBuffer.h"

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
class Application : public Game::SceneSnapshotTarget
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

    // 粒子公告板推送常量：视图投影矩阵 + 相机世界右/上轴（用于面向相机展开四边形）
    struct PushParticle
    {
        glm::mat4 viewProj;
        glm::vec3 camRight;
        float _p0 = 0.0f;
        glm::vec3 camUp;
        float _p1 = 0.0f;
    };
    static_assert(sizeof(PushParticle) == 96, "PushParticle 须为 96 字节（mat4 + 2*vec3+pad）");

    // ---- 场景快照（撤销/重做命令用，定义见 game/SceneCommand.h） ----
    // SceneSnapshot / SceneSnapshotCommand / SceneSnapshotTarget 已抽到独立纯逻辑头文件，
    // Application 实现 SceneSnapshotTarget 接口（见 Snapshot / RestoreScene）。

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
    void RecalculateTriangleCount();
    void UpdatePhysics();
    void SyncPhysicsBodies();
    void RebuildPhysicsBodies();
    void UpdateCharacter();
    void InitAnimationStateMachine();
    void UpdateAnimationStateMachine(float dt);

    // ---- 玩法系统（升级 17–19：导航 / 粒子 / 撤销重做 / 粒子编辑器） ----
    void InitGameSystems();     // 导航网格/粒子发射器/实例缓冲初始化
    void UpdateParticles();     // 每帧推进粒子模拟并上传 GPU 实例缓冲
    void UpdateNavPath();       // 重算 A* 路径（状态变化时）
    void EmitParticleBurst();   // 在相机注视点触发粒子爆发
    void UpdateNavAgent();      // 升级 18：每帧推进 AI 导航代理（沿路径移动 + 环形巡逻）
    void ApplyParticleConfig(); // 升级 19：将编辑器粒子配置写入 ParticleSystem（每帧/预设切换时）
    void HandlePropertyEditUndo(const Game::SceneSnapshot& frameStart); // 升级 20：基于编辑器交互手势提交属性编辑命令
    [[nodiscard]] Game::SceneSnapshot Snapshot() const override;        // 抓取当前场景可还原快照
    void RestoreScene(const Game::SceneSnapshot& snap) override;        // 还原快照（命令栈 Do/Undo 用）

    // ---- 场景序列化 ----
    void SaveScene();
    void LoadScene();

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
    static constexpr const char* kScenePath = "scene.json";

#ifdef NDEBUG
    static constexpr bool kEnableValidation = false;
#else
    static constexpr bool kEnableValidation = true;
#endif

    // ---- 资源（声明顺序 = 初始化顺序，析构逆序释放） ----
    Window window_;
    Context ctx_;
    Renderer renderer_;

    // 音频引擎必须先于 Sound 成员初始化、后于 Sound 成员析构
    Audio::AudioEngine audioEngine_;
    Audio::Sound bgm_;

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
    bool postProcess_ = false;
    bool prevPostProcess_ = false;

    // ---- 后处理：色调分级（升级 21，作用于 PostProcessor 合成阶段） ----
    float gradeSaturation_ = 1.0f;
    float gradeContrast_ = 1.0f;
    float gradeLift_ = 0.0f;
    float gradeGain_ = 1.0f;
    float gradeGamma_ = 1.0f;

    // ---- 后处理：景深（升级 22，作用于独立景深 Pass） ----
    bool dofEnabled_ = false;
    float dofFocusDistance_ = 7.0f;
    float dofAperture_ = 0.03f;
    float dofMaxBlur_ = 0.020f;

    // ---- 后处理：相机运动模糊（升级 23，作用于独立运动模糊 Pass） ----
    bool mbEnabled_ = false;
    float mbStrength_ = 0.5f;    // 拖尾强度 [0,1]
    float mbMaxBlur_ = 0.02f;    // 速度向量长度上限（UV 空间）
    float mbMaxSamples_ = 16.0f; // 沿轨迹采样数
    // 相机视图投影矩阵：prev=上一帧、curr=当前帧，供运动模糊重投影
    glm::mat4 prevViewProj_ = glm::mat4(1.0f);
    glm::mat4 currViewProj_ = glm::mat4(1.0f);
    bool ssao_ = false;
    bool prevSsao_ = false;
    bool ssr_ = false;
    bool prevSsr_ = false;

    // ---- 物理系统 ----
    Physics::PhysicsEngine physicsEngine_;
    std::vector<uint32_t> physicsBodyIds_; // 每个场景物体对应的物理刚体 ID（UINT32_MAX=无）
    bool physicsEnabled_ = true;
    bool physicsDebugDraw_ = false;
    float gravity_ = -9.81f;

    // ---- 关节系统 ----
    std::vector<Physics::SceneJoint> sceneJoints_;
    std::vector<uint32_t> physicsJointIds_; // 每个场景关节对应的物理关节 ID

    // ---- 角色控制器 ----
    uint32_t characterBodyId_ = UINT32_MAX;
    bool characterEnabled_ = false;
    bool prevCharacterEnabled_ = false;
    float characterSpeed_ = 6.0f;     // 移动速度（m/s）
    float characterJumpForce_ = 7.5f; // 跳跃初速度（m/s）
    bool characterGrounded_ = false;
    glm::vec3 characterSpawn_{0.0f, 2.0f, 0.0f};

    // ---- 动画状态机 ----
    Scene::AnimationStateMachine animStateMachine_;
    bool animStateMachineInited_ = false;

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
    float masterVolume_ = 0.5f;
    Core::FrameProfiler frameProfiler_;
    std::array<float, Core::FrameProfiler::kHistorySize> fpsHistoryChrono_{};

    // ---- 场景序列化快捷键边沿检测 ----
    bool saveKeyHeld_ = false;
    bool loadKeyHeld_ = false;

    // ---- 玩法系统：导航网格（A*） ----
    Game::NavGrid navGrid_;
    Game::PathResult navPath_;
    bool navEnabled_ = false;           // 是否在编辑器绘制导航调试线
    bool prevNavEnabled_ = false;       // 边沿检测，启用时重算路径
    int navStartX_ = 1, navStartY_ = 1; // 寻路起点格
    int navGoalX_ = 14, navGoalY_ = 14; // 寻路终点格
    float navCellSize_ = 1.0f;          // 格宽（世界单位）
    glm::vec2 navOrigin_{-8.0f, -8.0f}; // 网格左下角世界坐标

    // ---- 玩法系统：AI 导航代理（升级 18） ----
    Game::NavAgent navAgent_;     // 沿 A* 路径移动、环形巡逻的 AI 代理
    bool navAgentEnabled_ = true; // AI 代理总开关（默认开启，可视化可在编辑器关闭）

    // ---- 玩法系统：粒子 ----
    Game::ParticleSystem particleSystem_;
    bool particleEnabled_ = true; // 粒子系统总开关
    // 升级 19：编辑器可实时调参的发射器配置（每帧写入 particleSystem_）
    Game::Emitter particleEmitterConfig_;
    float particleGravity_ = -4.0f;                            // 模拟重力 Y（编辑器可调）
    float particleDamping_ = 0.4f;                             // 速度阻尼（编辑器可调）
    int emitterPresetIndex_ = 0;                               // 当前预设下标（编辑器下拉框）
    int prevEmitterPresetIndex_ = 0;                           // 边沿检测：切换预设时重建配置
    Render::ParticleBuffer particleBuffer_;                    // GPU 实例缓冲
    std::vector<Render::ParticleInstance> particleScratch_;    // 每帧复用，避免动态分配
    std::optional<Render::GraphicsPipeline> particlePipeline_; // 公告板管线
    Render::GraphicsPipelineConfig particleConfig_;

    // ---- 玩法系统：撤销/重做 ----
    Game::CommandStack commandStack_;
    bool undoKeyHeld_ = false;     // Ctrl+Z 边沿检测
    bool redoKeyHeld_ = false;     // Ctrl+Y 边沿检测
    bool particleKeyHeld_ = false; // P 键爆发边沿检测

    // ---- 玩法系统：属性编辑撤销（升级 20） ----
    std::optional<Game::SceneSnapshot> propertyEditBefore_; // 滑块/调色板手势起始快照（对象数不变）
    bool editGestureActive_ = false;                        // 当前是否有进行中的 ImGui 属性编辑手势
    bool gizmoEditActive_ = false;                          // Gizmo 变换拖拽进行中（非 ImGui item，单独跟踪）
    std::optional<Game::SceneSnapshot> gizmoEditBefore_;    // Gizmo 拖拽起始快照
    bool suppressEditGesture_ = false;                      // 本帧已执行显式命令（增删/撤销/重做），抑制手势记录防重复
};
} // namespace BigHero
