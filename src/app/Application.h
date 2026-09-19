#pragma once
#include "app/systems/AnimationHost.h"
#include "app/systems/NavHost.h"
#include "app/systems/ParticleHost.h"
#include "app/systems/PhysicsHost.h"
#include "app/systems/PostProcessSync.h"
#include "app/systems/SceneIoHost.h"
#include "audio/AudioEngine.h"
#include "audio/Sound.h"
#include "core/AssetCache.h"
#include "core/AssetMetadata.h"
#include "core/AssetRegistry.h"
#include "core/FrameProfiler.h"
#include "core/Log.h"
#include "core/MeshResource.h"
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
#include "scene/AnimationStateMachine.h"
#include "scene/Camera.h"
#include "scene/FirstPersonCamera.h"
#include "scene/CubeMesh.h"
#include "scene/EcsScene.h"
#include "scene/ObjModel.h"
#include "scene/PersonHost.h"
#include "scene/Picking.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include "game/CommandStack.h"
#include "game/SceneCommand.h"

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
    struct AppConfig
    {
        bool headless = false;
        bool validateOnly = false;
        uint32_t width = 1600;
        uint32_t height = 900;
        std::string title = "BigHero Engine - Vulkan";
        // 截图：非空时渲染若干帧稳定后请求引擎截图（可配合 --post-process 做 PP 开/关对比）
        std::string screenshotPath;
        // 启动即开启后处理（等价于编辑器勾选后处理；供命令行自动化验收）
        bool postProcess = false;
        // 启动相机模式："orbit"（默认）或 "fp"（第一人称漫游）
        std::string cameraMode = "orbit";
        // 启动场景："default"（默认演示场景）或 "slice"（垂直切片场景 samples/vertical_slice：
        // 1200 实体 + 95% 静止 + 50 条父子链，供层级增量收益帧计时与 CI 成像回归使用）
        std::string sceneKind = "default";
        // 冒烟验收钩子：启动时在场景中生成一个人物（供 --screenshot 自动化验证球/胶囊渲染接入）
        bool demoPerson = false;
        // 启动曝光（--exposure <f>）：等价编辑器"光照"面板曝光滑条；未提供时保持默认（1.0）
        std::optional<float> exposure;
    };

    Application();
    Application(const AppConfig& config);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // 初始化全部资源并进入主循环，返回进程退出码
    int Run();

    // 仅验证 Shader 文件和 SPIR-V 存在性（用于 CI headless 测试）
    int ValidateOnly();

    // ---- 资源登记（bighero:: 积木块接入真实加载路径，供编辑器/统计查询） ----
    [[nodiscard]] const bighero::AssetRegistry& Assets() const noexcept { return assetRegistry_; }
    [[nodiscard]] const bighero::MeshResource* FindMeshResource(const std::string& name) const;

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
        float tonemapDirect; // 1=片元内 ACES 直通交换链（后处理关）；0=输出线性 HDR（合成端统一 ACES）
    };

    // 粒子公告板推送常量：阶段 3e 移入 ParticleHost::PushParticle

    // 逐材质推送常量：纹理池槽位 + 透明/自发光参数（与着色器 ObjectPush 布局逐字节一致）
    struct PushObject
    {
        int32_t texIndex = 0;       // 反照率贴图槽（0=全局 tiles）
        int32_t normalIndex = 1;    // 法线贴图槽（1=全局 tiles_normal）
        int32_t mrIndex = 2;        // metallicRoughness 贴图槽（2=纯白透传因子）
        int32_t emissiveIndex = -1; // 自发光贴图槽（-1=无贴图，emissiveFactor 原样生效）
        glm::vec3 emissiveFactor{0.0f}; // 自发光倍率（线性 HDR）
        float alphaCutoff = 0.0f;   // MASK 裁剪阈值（<=0 视为不透明）
        int32_t mode = 0;           // 0=OPAQUE 1=MASK 2=BLEND 3=EMISSIVE_ONLY（延迟自发光叠加）
        int32_t outputTarget = 0;   // 0=片元内 ACES 直通交换链（后处理关）；1=输出线性 HDR（合成端统一 ACES）
    };
    static_assert(sizeof(PushObject) == 40, "PushObject 须为 40 字节（与着色器 ObjectPush 布局一致）");
    static_assert(offsetof(PushObject, emissiveFactor) == 16, "emissiveFactor 偏移须为 16");
    static_assert(offsetof(PushObject, alphaCutoff) == 28, "alphaCutoff 偏移须为 28");
    static_assert(offsetof(PushObject, mode) == 32, "mode 偏移须为 32");
    static_assert(offsetof(PushObject, outputTarget) == 36, "outputTarget 偏移须为 36");

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
    void SyncSceneEdits();  // ECS 场景实体化：包 -> ECS 写回（编辑器/Gizmo 编辑持久化）
    void RepackScene();     // ECS 场景实体化：ECS -> 包投影（自转角/物理位置输出到渲染数据）
    void UpdateCamera();
    void UpdateGizmo();
    void UpdateRenderables(); // ECS 渲染收敛：单趟直读 ECS（剔除 + 按 meshId 批次化 + 上传登记）
    void EnsureInstanceCapacities(); // 场景实体增删后按需扩容实例缓冲（防 Upload 静默裁剪）
    void UpdateUniforms();
    void UpdateFpsTitle();
    // ---- 帧瞬态上传（FrameStaging）：更新阶段登记 → 录制阶段首个 pass 内拷入设备本地缓冲 ----
    void AppendUpload(VkBuffer dst, const void* data, VkDeviceSize bytes);
    // 把桶 scratch 拷入 instanceUploadStash_（登记指针须跨录制稳定）并登记上传
    void AppendInstanceUpload(Render::InstanceBuffer& buffer, const std::vector<Render::InstanceData>& data);
    void HandlePicking();
    void UpdateDeferredState();
    void RecalculateTriangleCount();
    // 物理系统：阶段 3f 移入 PhysicsHost 子系统（physicsHost_.Init/RebuildBodies/Update）

    // 动画状态机：阶段 3c 移入 AnimationHost 子系统（animationHost_.Init/Update）

    // ---- 玩法系统（升级 17-20）：导航/粒子子系统（阶段 3d/3e 移入 NavHost/ParticleHost）；撤销重做留主类 ----
    void InitGameSystems(); // 导航/粒子子系统初始化
    void HandlePropertyEditUndo(const Game::SceneSnapshot& frameStart); // 升级 20：基于编辑器交互手势提交属性编辑命令
    [[nodiscard]] Game::SceneSnapshot Snapshot() const override;        // 抓取当前场景可还原快照
    void RestoreScene(const Game::SceneSnapshot& snap) override;        // 还原快照（命令栈 Do/Undo 用）

    // ---- 场景序列化：阶段 3b 移入 SceneIoHost 子系统（sceneIo_.Save/Load） ----

    // ---- 录制回调 ----
    void RecordScene(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void RecordUi(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent);
    void RecordPrePass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    // 多线程命令录制：点光源立方体阴影 6 面并行录制到独立 command buffer
    void RecordParallelCubeShadow(Render::ParallelCommandRecorder& recorder, uint32_t frameIndex);
    void RecordLighting(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);
    // 延迟透明叠加通道：BLEND（Alpha 混合）+ 自发光（加性）批次，深度只读测试
    void RecordTransparent(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);

    // ---- 管线重建（交换链格式变化时回调） ----
    void RebuildMainPipelines();
    void RebuildDeferredPipelines();
    void UpdateGBufferSets();

    // ---- 辅助 ----
    // 级联阴影：实用分割法求轴向视深边界 + 逐级联视锥切片拟合光视正交矩阵（含纹素对齐防闪烁）
    [[nodiscard]] std::array<glm::mat4, Render::kMaxCascades> ComputeCascadeMatrices(glm::vec4& outSplits) const;
    void FillPointShadowMatrices(Render::PointShadowUBO& out) const;
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
    static constexpr const char* kGltfModelPath = "assets/models/model.gltf";
    static constexpr float kPanSpeed = 4.0f;
    static constexpr float kCullMargin = 1.05f;
    static constexpr float kShadowDrawDistance = 100.0f; // CSM 阴影最远绘制距离（截断相机 farZ）
    static constexpr float kCascadeSplitLambda = 0.75f;  // 实用分割法对数/线性混合系数
    static constexpr float kCascadeZPad = 10.0f;         // 级联光空间 Z 向外扩（切片外高物投影进深）
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
    // config_ 必须最先声明：构造函数初始化列表用它初始化 window_/ctx_
    AppConfig config_;
    // 跨平台窗口抽象（桌面=GLFW / Android=native_app_glue），经 Window::Create 工厂构造
    std::unique_ptr<Window> window_;
    Context ctx_;
    Renderer renderer_;

    // 音频引擎必须先于 Sound 成员初始化、后于 Sound 成员析构
    Audio::AudioEngine audioEngine_;
    Audio::Sound bgm_;

    ShadowMap shadowMap_;
    CubeShadowMap cubeShadowMap_;
    EnvironmentLighting envLighting_;

    // CSM 级联缓存：UpdateUniforms 每帧刷新，RecordPrePass 录制阴影深度时消费
    std::array<glm::mat4, Render::kMaxCascades> cascadeMatrices_{};
    glm::vec4 cascadeSplits_{1.0f};

    Render::DescriptorManager descManager_;
    std::vector<Render::UboBuffer<Render::CameraUBO>> cameraUbos_;
    std::vector<Render::UboBuffer<Render::LightUBO>> lightUbos_;
    std::vector<Render::UboBuffer<Render::PointShadowUBO>> pointShadowUbos_;

    // 资源管理器：统一缓存纹理等 GPU 资源，LRU 淘汰 + 引用计数
    Core::AssetManager assetManager_;

    std::shared_ptr<Texture> texture_;
    std::shared_ptr<Texture> normalTexture_;

    // ---- 逐物体纹理池（set1 binding9，16 槽 combined sampler 数组） ----
    // 0=全局反照率(tiles) 1=全局法线(tiles_normal) 2=中性白(无贴图回退/mr透传) 3+=glTF 贴图
    std::vector<std::shared_ptr<Texture>> texturePool_;
    uint32_t nextTextureSlot_ = 3;

    Render::Mesh sceneMesh_;
    Render::Mesh torusMesh_;
    bool hasTorus_ = false;

    // ---- 人物部件几何：球(眼/头/发) 胶囊(躯干/四肢) ----
    Render::Mesh sphereMesh_;
    Render::Mesh capsuleMesh_;

    // ---- glTF 模型 + PBR 材质贴图映射（meshId=2，逐 primitive 材质） ----
    struct GltfPrimMaterial
    {
        uint32_t firstIndex = 0; // 该 primitive 在 gltfMesh_ 索引缓冲中的起始
        uint32_t indexCount = 0;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        int32_t texSlot = 0;    // 反照率贴图池槽位
        int32_t normalSlot = 1; // 法线贴图池槽位
        int32_t mrSlot = 2;     // metallicRoughness 池槽位
        // 透明/自发光（glTF 2.0 core，与 Scene::GltfMaterial 对应）
        int alphaMode = 0;              // 0=OPAQUE 1=MASK 2=BLEND
        float alphaCutoff = 0.5f;       // MASK 裁剪阈值
        glm::vec3 emissiveFactor{0.0f}; // 自发光倍率（线性 HDR）
        int32_t emissiveSlot = -1;      // 自发光贴图池槽位（-1=无贴图）
        glm::vec3 boundsCenter{0.0f};   // 批次包围盒中心（网格空间，透明批次排序用）
    };
    Render::Mesh gltfMesh_;
    std::vector<GltfPrimMaterial> gltfPrims_;
    std::vector<Render::InstanceBuffer> gltfPrimInstances_; // 每个 primitive 一份实例缓冲
    std::vector<uint32_t> gltfPrimCounts_;
    bool hasGltf_ = false;

    // ---- glTF 动画（升级 24）：模型数据常驻；状态机/采样/根节点增量由 AnimationHost 子系统管理 ----
    Scene::GltfModel gltfModel_;

    // 阶段 3c：动画子系统（状态机构建/每帧推进/glTF 根节点 TRS 增量）
    AnimationHost animationHost_;

    // 资源登记：AssetRegistry 存条目（名字/路径/类型/状态/大小），
    // meshResources_ 按名字存几何元数据（顶点/索引计数 + 包围盒）
    bighero::AssetRegistry assetRegistry_;
    std::unordered_map<std::string, bighero::MeshResource> meshResources_;

    // 把一份网格（程序化或 OBJ 加载）登记进资源注册表；state=Failed 时几何为空
    void RegisterMeshAsset(const std::string& name, const std::string& path,
                           const std::vector<Scene::Vertex>& verts, const std::vector<uint32_t>& indices,
                           bighero::AssetMetadata::LoadState state, uint64_t fileSize);
    // 把一张贴图登记进资源注册表（类型=Texture）
    void RegisterTextureAsset(const std::string& name, const std::string& path,
                              bighero::AssetMetadata::LoadState state, uint64_t fileSize);

    // ---- glTF 加载与纹理池 ----
    // 从 glTF 材质贴图 URI 分配纹理池槽位（相对 glTF 文件目录解析；缺失/加载失败回退 fallbackSlot）
    uint32_t LoadTextureSlot(const std::string& baseDir, const std::string& uri, bool sRGB, uint32_t fallbackSlot,
                             const std::string& debugName);
    // InitScene 末尾调用：加载 assets/models/model.gltf + 贴图 → 纹理池 + 逐 primitive 实例缓冲
    void LoadGltfAsset(uint32_t maxInstances);
    // 把纹理池 16 槽写入每帧 Light 描述符集 binding9
    void UpdateObjectTextureDescriptors();
    // glTF 逐 primitive 实例填充由 UpdateRenderables 单趟批次化（材质因子 + 可见性）

    // ---- glTF 透明/自发光录制辅助 ----
    // 由 primitive 材质构建 36B 推送常量（mode 由调用方指定：0/1/2 或 3=加性自发光叠加）
    [[nodiscard]] PushObject MakeGltfPush(const GltfPrimMaterial& pm, int mode) const;
    // 透明（BLEND）批次排序：按批次包围中心（实例变换后）到相机距离从远到近
    [[nodiscard]] std::vector<uint32_t> SortedGltfBlendPrims() const;
    // 绘制 glTF primitive 批次（须已绑定管线与 set0/1 描述符、设置视口）。
    // filter：0=OPAQUE+MASK（BLEND 批次跳过） 2=BLEND（远到近排序） 3=EMISSIVE_ONLY（延迟自发光补写）
    void DrawGltfPrims(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int filter);

    // 管线配置（保留为成员，供交换链重建时复用）
    Render::GraphicsPipelineConfig pipelineConfig_;
    Render::GraphicsPipelineConfig shadowConfig_;
    Render::GraphicsPipelineConfig cubeShadowConfig_;
    Render::GraphicsPipelineConfig skyboxConfig_;
    Render::GraphicsPipelineConfig gbufferConfig_;
    Render::GraphicsPipelineConfig defLightConfig_;
    // glTF 透明/自发光：前向 BLEND（mainPass）+ 延迟透明叠加（transparentRenderPass_）
    Render::GraphicsPipelineConfig gltfBlendConfig_;      // 前向 BLEND：标准 Alpha 混合，不写深度
    Render::GraphicsPipelineConfig transBlendConfig_;     // 延迟透明叠加 BLEND
    Render::GraphicsPipelineConfig transEmissiveConfig_;  // 延迟透明叠加加性自发光（ONE/ONE）

    // GraphicsPipeline 无默认构造，用 optional 在 Init 阶段原位构造
    std::optional<Render::GraphicsPipeline> pipeline_;
    std::optional<Render::GraphicsPipeline> shadowPipeline_;
    std::optional<Render::GraphicsPipeline> cubeShadowPipeline_;
    std::optional<Render::GraphicsPipeline> skyboxPipeline_;
    std::optional<Render::GraphicsPipeline> gbufferPipeline_;
    std::optional<Render::GraphicsPipeline> lightingPipeline_;
    std::optional<Render::GraphicsPipeline> gltfBlendPipeline_;      // 前向 BLEND
    std::optional<Render::GraphicsPipeline> transBlendPipeline_;     // 延迟透明叠加 BLEND
    std::optional<Render::GraphicsPipeline> transEmissivePipeline_;  // 延迟透明叠加加性自发光

    EditorOverlay editorOverlay_;
    EditorPanel editorPanel_;
    LightParams lightParams_;

    // ---- 场景状态 ----
    // ECS 权威存储：场景物体 = 实体 + 组件（Transform/Renderable/Spin/PhysicsBody/PhysicsRef）。
    // scene_ / spinAngles_ 为每帧投影包（ECS -> SceneObject），供渲染/编辑器/序列化既有路径消费；
    // 编辑器对包的修改经 SyncSceneEdits 写回 ECS。
    Scene::EcsScene ecsScene_;
    std::vector<Scene::SceneObject> scene_;
    std::vector<float> spinAngles_;
    std::vector<PointLightParams> pointLights_;
    OrbitCamera camera_;
    FirstPersonCamera fpCamera_; // 第一人称漫游相机（沉浸式场景内部观察）
    int selectedObject_ = -1;

    // 相机模式切换（编辑器面板或 Tab 键触发）
    enum class CameraMode { Orbit, FirstPerson };
    CameraMode cameraMode_ = CameraMode::Orbit;
    bool prevCameraMode_ = false; // 边沿检测缓存（false=Orbit, true=FirstPerson）

    // ---- 双模式相机统一出口：渲染/后处理/拾取等一律经此处取活跃相机 ----
    [[nodiscard]] const glm::mat4& ActiveView() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.View() : camera_.View();
    }
    [[nodiscard]] const glm::mat4& ActiveProj() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.Proj() : camera_.Proj();
    }
    [[nodiscard]] glm::mat4 ActiveViewProj() const noexcept { return ActiveProj() * ActiveView(); }
    [[nodiscard]] glm::vec3 ActivePosition() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.Position() : camera_.Position();
    }
    // 活跃相机视线前向（世界空间）；Orbit 用 target-position，FP 用内部朝向
    [[nodiscard]] glm::vec3 ActiveForward() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.Forward()
                                                        : glm::normalize(camera_.Target() - camera_.Position());
    }
    [[nodiscard]] float ActiveNear() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.nearZ_ : camera_.nearZ_;
    }
    [[nodiscard]] float ActiveFar() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.farZ_ : camera_.farZ_;
    }
    // 活跃相机"注视点"：Orbit 为目标点，FP 为视线前方 3m 处（粒子爆发/交互定位用）
    [[nodiscard]] glm::vec3 ActiveTarget() const noexcept
    {
        return (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.Position() + fpCamera_.Forward() * 3.0f
                                                        : camera_.Target();
    }

    // 第一人称模式：鼠标指针捕获（隐藏光标、无边界视角旋转）
    bool fpCursorCaptured_ = false;
    float fpWalkSpeed_ = 3.5f; // FP 行走速度（m/s，滚轮调节）

    // 截图模式：渲染稳定帧数后请求截图并退出（供 P0-3 验收做 PP 开/关对比）
    uint64_t frameCounter_ = 0;
    bool screenshotIssued_ = false;

    // 阶段 3a：后处理参数同步子系统（渲染路径开关/色调分级/景深/运动模糊/体积雾/
    // 自动曝光电影化/TAA 抖动/视图投影缓存），SyncToPostProcessor + AdvanceJitter
    PostProcessSync postProcessSync_;

    // 阶段 3b：场景序列化子系统（Save/Load scene.json，构造注入场景状态引用）
    SceneIoHost sceneIo_;

    // 阶段 3f：物理子系统（刚体/关节/角色控制器，构造注入 ECS/场景包/相机/窗口引用）
    PhysicsHost physicsHost_;

    // ---- 动画状态机：阶段 3c 移入 AnimationHost 子系统 ----

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
    Render::InstanceBuffer sphereInstances_;
    Render::InstanceBuffer capsuleInstances_;
    // ECS 渲染收敛：单趟批次化的逐桶暂存（遍历前 clear，遍历后逐桶登记上传）
    std::vector<Render::InstanceData> cubeScratch_;
    std::vector<Render::InstanceData> torusScratch_;
    std::vector<Render::InstanceData> sphereScratch_;
    std::vector<Render::InstanceData> capsuleScratch_;
    std::vector<std::vector<Render::InstanceData>> gltfPrimScratch_; // 与 gltfPrims_ 一一对应
    uint32_t cubeInstanceCount_ = 0;
    uint32_t torusInstanceCount_ = 0;
    uint32_t sphereInstanceCount_ = 0;
    uint32_t capsuleInstanceCount_ = 0;
    glm::mat4 firstGltfModel_{1.0f}; // 本帧首个可见 glTF 实体的模型矩阵（透明批次排序基准）

    // ---- 帧瞬态上传（FrameStaging）：替代逐帧 staging Buffer 创建/销毁 + 一次性提交 ----
    std::vector<Render::FrameStaging::StagedUpload> pendingUploads_; // 更新阶段登记，录制阶段消费
    std::vector<Render::InstanceData> instanceUploadStash_;          // 桶数据的本帧稳定副本（登记指针指向此处）

    // ---- 可见性统计 ----
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

    // 阶段 3d：导航子系统（A* 网格 + AI 巡逻代理，调试数据经公有字段供录制消费）
    NavHost navHost_;

    // 阶段 3e：粒子子系统（模拟 + 实例缓冲 + 公告板管线，持 GPU 资源须在 ctx_ 之后析构）
    ParticleHost particleHost_;

    // 阶段 3f：人物子系统（Q 版人物 = 球/胶囊 ECS 部件骨骼树）
    Scene::PersonHost personHost_{ecsScene_};

    // ---- 玩法系统：撤销/重做 ----
    Game::CommandStack commandStack_;
    bool undoKeyHeld_ = false; // Ctrl+Z 边沿检测
    bool redoKeyHeld_ = false; // Ctrl+Y 边沿检测

    // ---- 玩法系统：属性编辑撤销（升级 20） ----
    std::optional<Game::SceneSnapshot> propertyEditBefore_; // 滑块/调色板手势起始快照（对象数不变）
    bool editGestureActive_ = false;                        // 当前是否有进行中的 ImGui 属性编辑手势
    bool gizmoEditActive_ = false;                          // Gizmo 变换拖拽进行中（非 ImGui item，单独跟踪）
    std::optional<Game::SceneSnapshot> gizmoEditBefore_;    // Gizmo 拖拽起始快照
    bool suppressEditGesture_ = false;                      // 本帧已执行显式命令（增删/撤销/重做），抑制手势记录防重复
};
} // namespace BigHero
