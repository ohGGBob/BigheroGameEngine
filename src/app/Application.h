#pragma once
#include "app/systems/AnimationHost.h"
#include "app/systems/NavHost.h"
#include "app/systems/ParticleHost.h"
#include "app/systems/PhysicsHost.h"
#include "app/systems/PostProcessSync.h"
#include "app/systems/SceneIoHost.h"
#include "app/systems/ShowcaseHost.h"
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
#include "editor/ProjectPanel.h"
#include "editor/ScriptFieldUndo.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/CubeShadowMap.h"
#include "render/EnvironmentLighting.h"
#include "render/Frustum.h"
#include "render/InstanceBuffer.h"
#include "render/LightmapBaker.h"
#include "render/LodGroup.h"
#include "render/Mesh.h"
#include "render/ReflectionCapture.h"
#include "render/ReflectionProbe.h"
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
#include "scene/EcsScene.h"
#include "scene/FirstPersonCamera.h"
#include "scene/ObjModel.h"
#include "scene/PersonHost.h"
#include "scene/Picking.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/Terrain.h"
#include "script/CSharpHost.h"
#include "showcase/CyberCity.h"
#include "ui/UiRuntime.h"
#include "voxel/VoxelWorld.h"

#include "game/CommandStack.h"
#include "game/FpController.h"
#include "game/PlayMode.h"
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
        // --no-ui：跳过编辑器覆盖层录制（成像回归基线用）。截图为纯场景，
        // 基线从此不受编辑器 UI 迭代影响；headless 模式行为不变（本就无覆盖层）
        bool noUi = false;
        // 启动即开启后处理（等价于编辑器勾选后处理；供命令行自动化验收）
        bool postProcess = false;
        // 启动相机模式："orbit"（默认）或 "fp"（第一人称漫游）
        std::string cameraMode = "orbit";
        // 启动场景："default"（默认演示场景）或 "slice"（垂直切片场景 samples/vertical_slice：
        // 1200 实体 + 95% 静止 + 50 条父子链）或 "openworld"（开放世界模板 samples/open_world：
        // 320×320 m 分块 + 距离分层密度，~8K 实体，95% 静止 + 视锥剔除 + 层级缓存增量测试）
        std::string sceneKind = "default";
        // 冒烟验收钩子：启动时在场景中生成一个人物（供 --screenshot 自动化验证球/胶囊渲染接入）
        bool demoPerson = false;
        // 动画事件演示钩子（--demo-events）：默认 glTF 道具无动画时，向模型注入一条 2s 循环
        // 程序化 clip，并把内置轨事件名设为 "click"、自动进入 Play 态，使 AnimationEventPlayer
        // 在运行期推进时间轴并经 DrainFiredEvents() 派发事件（日志 + SfxId::Click 路径可见）。
        bool demoEvents = false;
        // 启动曝光（--exposure <f>）：等价编辑器"光照"面板曝光滑条；未提供时保持默认（1.0）
        std::optional<float> exposure;
        // C# 脚本（--scripts <dir>）：用户脚本工程目录（含 .csproj）。非空时启用脚本系统，
        // 失败优雅降级（引擎正常跑，日志 WARN）。默认空 = 不启用（与既往行为逐位一致）
        std::string scriptsDir;
        // 第二张截图（--screenshot2 <p>）：--screenshot2-delay <sec>（默认 3.0s）后再截一张，
        // 供时序/脚本热重载对比（如 C# Spinner 旋转前后姿态差异）
        std::string screenshot2Path;
        float screenshot2DelaySeconds = 3.0f;
        // --ui-demo：运行时 UI 演示画布（面板+标题+按钮，叠加场景之上；按钮走编辑器物体增删路径）
        bool uiDemo = false;
        // --editor-ui：方块世界（--scene voxel）下也保留编辑器面板。
        // 默认收起（纯游戏模式：只留准星 + 方块世界 HUD），运行期按 F1 来回切。
        bool editorUiInVoxel = false;
        // --bake-probes / --bake-occlusion：启动即触发光照探针 / 遮挡剔除烘焙
        // （等价编辑器面板按钮；供命令行自动化做"烘焙前后"性能对比，未指定时行为与既往一致）
        bool bakeProbes = false;
        bool bakeOcclusion = false;
        // --bake-lightmap [p]：启动即触发光照贴图烘焙（U2-L1 接线 2a，离线管道验收钩子）——
        // 主循环前同步兑现（纯 CPU：chartless 直接光 + 天光 AO = render/LightmapBaker.h），
        // 结果写盘到 lightmapPath 后立即退出（0=成功 1=失败）。当前覆盖场景内 meshId 0
        // 静态立方体几何；渲染端采样接线与编辑器面板按钮留待接线 2b。
        bool bakeLightmap = false;
        std::string lightmapPath = "lightmap.lm";
        // --no-reflection-probes：禁用 U2-L2 反射探针烘焙（A/B 对照验证与性能对比用；
        // 未指定时场景加载即烘焙，行为与接线默认一致）
        bool noReflectionProbes = false;
        // --no-probe-capture：旁路 U2-L2 GPU 立方图捕获（探针回退解析 SH 口径；A/B 用）
        bool noProbeCapture = false;
        // --no-lightmap：禁用静态光照贴图烘焙与批次绘制（U2-L1 渲染接线 A/B 对照用；
        // 未指定时场景加载即烘焙，前向主通道以批次替代实时立方体光照）
        bool noLightmap = false;
        // --deferred：启动即以延迟渲染通道运行（等价编辑器"渲染统计"面板勾选；U2-L1 批次
        // 延迟门控的回归验证入口，补上长期欠账的 CLI 开关）
        bool deferred = false;
        // --bench-frames <N>：基准模式——渲染 N 帧后打印平均/最差帧耗时 +
        // 各阶段 CPU 平均耗时（FrameProfiler::BuildSummary 聚合）到 stdout 并退出。
        // 0 = 禁用（默认，行为与既往一致）。用于脚本化性能对比。
        uint32_t benchFrames = 0;
        // --lod-off: bypass sphere/capsule (meshId 3/4) LOD selection -- force all high-detail
        // buckets and skip LOD culling. Does NOT affect frustum culling or PVS occlusion.
        // Default false (bitwise-identical to prior behavior). For quantifying LOD gain.
        bool lodOff = false;
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

    // 反射探针捕获顶点推送：捕获面的视图投影（capture.vert.glsl 消费）
    struct PushCaptureVP
    {
        glm::mat4 faceVP;
    };
    static_assert(sizeof(PushCaptureVP) == 64, "PushCaptureVP must be exactly 64 bytes");

    // 粒子公告板推送常量：阶段 3e 移入 ParticleHost::PushParticle

    // 逐材质推送常量：纹理池槽位 + 透明/自发光参数（与着色器 ObjectPush 布局逐字节一致）
    struct PushObject
    {
        int32_t texIndex = 0;           // 反照率贴图槽（0=全局 tiles）
        int32_t normalIndex = 1;        // 法线贴图槽（1=全局 tiles_normal）
        int32_t mrIndex = 2;            // metallicRoughness 贴图槽（2=纯白透传因子）
        int32_t emissiveIndex = -1;     // 自发光贴图槽（-1=无贴图，emissiveFactor 原样生效）
        glm::vec3 emissiveFactor{0.0f}; // 自发光倍率（线性 HDR）
        float alphaCutoff = 0.0f;       // MASK 裁剪阈值（<=0 视为不透明）
        int32_t mode = 0;               // 0=OPAQUE 1=MASK 2=BLEND 3=EMISSIVE_ONLY（延迟自发光叠加）
        int32_t outputTarget = 0;       // 0=片元内 ACES 直通交换链（后处理关）；1=输出线性 HDR（合成端统一 ACES）
    };
    static_assert(sizeof(PushObject) == 40, "PushObject 须为 40 字节（与着色器 ObjectPush 布局一致）");
    static_assert(__builtin_offsetof(PushObject, emissiveFactor) == 16, "emissiveFactor 偏移须为 16");
    static_assert(__builtin_offsetof(PushObject, alphaCutoff) == 28, "alphaCutoff 偏移须为 28");
    static_assert(__builtin_offsetof(PushObject, mode) == 32, "mode 偏移须为 32");
    static_assert(__builtin_offsetof(PushObject, outputTarget) == 36, "outputTarget 偏移须为 36");

    // ---- 场景快照（撤销/重做命令用，定义见 game/SceneCommand.h） ----
    // SceneSnapshot / SceneSnapshotCommand / SceneSnapshotTarget 已抽到独立纯逻辑头文件，
    // Application 实现 SceneSnapshotTarget 接口（见 Snapshot / RestoreScene）。

    // ---- 初始化 ----
    void InitResources();
    void CreatePipelines();
    void SetupCallbacks();
    void InitScene();
    // 构建并加载指定场景（初始启动与编辑器"场景"下拉框切换共用）：物体构建 / glTF 演示 /
    // 灯光 / 取景 / 物理重建 / 实例容量统一处理，避免启动与切换行为漂移。
    void BuildAndLoadScene(const std::string& kind);
    // 按场景重传地面实例数据（赛博城市湿滑沥青 vs 其余场景）
    void UploadGround(bool cyberCity);

    // ---- 每帧更新 ----
    void UpdateTime();
    void SyncSceneEdits(); // ECS 场景实体化：包 -> ECS 写回（编辑器/Gizmo 编辑持久化）
    void RepackScene();    // ECS 场景实体化：ECS -> 包投影（自转角/物理位置输出到渲染数据）
    void UpdateCamera();
    // 第一人称陆行分支（重力/跳跃/蹲伏/冲刺 + 场景碰撞滑动）：由 UpdateCamera 在 FP 模式调用
    void UpdateFirstPersonMovement();
    void UpdateGizmo();
    // ---- 展示厅（--scene cybercity）：展台交互 / 特性开关 / 时段切换 ----
    // ---- 方块世界：初始化 / 每帧更新 / 网格重建 / 挖掘与放置交互 ----
    void InitVoxelWorld();
    void UpdateVoxelWorld();
    void RebuildVoxelMeshes();                  // 同步区块 + 按帧预算重建（分帧，避免卡顿）
    void SyncVoxelChunks();                     // 按世界已加载区块同步渲染槽（增删）
    void RebuildPendingVoxelChunks(int budget); // 重建最多 budget 个未上传区块
    void MarkVoxelChunkDirty(int cx, int cz);   // 标记区块待重建（编辑相邻面时用）
    void HandleVoxelInteraction();
    // ---- 地形（--scene terrain）：高度场构建 + 分块网格上传；主通道/阴影绘制辅助 ----
    void InitTerrainScene();
    void DrawTerrainChunks(VkCommandBuffer cmd); // 调用方已绑定管线/描述符/推送常量
    [[nodiscard]] std::vector<Scene::Vertex> BuildTerrainChunkVertices(int cx, int cz) const;
    void UpdateShowcase();
    void InitCyberCity();                        // 场景装配（灯光/展台/出生点/时段）
    void ApplyAtmosphere();                      // 氛围落地：连续昼夜插值 × 画面风格（光/曝光/天空/雾/调色/泛光/霓虹）
    void UpdateNeonPulse();                      // 霓虹呼吸：每帧按确定性脉冲系数调制八盏点光源强度
    void ToggleFeature(int featureId);           // 切换引擎特性（展台交互与数字键共用）
    void TriggerShowcaseParticles();             // 粒子展台：在注视点触发一次爆发
    void SpawnShowcaseCube(const glm::vec3& at); // 物理展台：生成一个自发光动态立方体（可撤销）
    void DrawShowcaseHud();                      // 展示厅 HUD（准星 / 交互提示 / 特性清单 / 帮助）
    void DrawVoxelHud();                         // 方块世界 HUD（准星 / 操作 / 手持方块 / 状态）
    void UpdateUi();                             // U1-UI：运行时 UI 每帧更新（输入喂入/命中/按钮状态机/顶点展开）
    void InitUiRuntime(); // U1-UI：UI 系统初始化（headless/--no-ui 停用；--ui-demo 构建演示画布）
    void HandleUiDemoClick(const Ui::UiEvent& ev); // U1-UI：演示按钮点击 -> 真实场景操作
    void UpdateRenderables();                      // ECS 渲染收敛：单趟直读 ECS（剔除 + 按 meshId 批次化 + 上传登记）
    void EnsureInstanceCapacities();               // 场景实体增删后按需扩容实例缓冲（防 Upload 静默裁剪）
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
    void InitGameSystems();                                             // 导航/粒子子系统初始化
    void HandlePropertyEditUndo(const Game::SceneSnapshot& frameStart); // 升级 20：基于编辑器交互手势提交属性编辑命令
    [[nodiscard]] Game::SceneSnapshot Snapshot() const override;        // 抓取当前场景可还原快照
    void RestoreScene(const Game::SceneSnapshot& snap) override;        // 还原快照（命令栈 Do/Undo 用）

    // ---- U1-E3 Play Mode（编辑态/运行态分离，状态机见 game/PlayMode.h） ----
    void UpdatePlayModeRequests(); // 面板播放/暂停/停止按钮 + Ctrl+P 边沿消费（先于仿真门）
    void EnterPlayMode();          // 进入运行态：拍编辑态底稿快照存入 playMode_
    void StopPlayMode();           // 停止：底稿 LoadPacket 全量还原（不入撤销栈），回到编辑态
    // 场景编辑命令统一收口：仅编辑态入撤销栈（U1-E3 撤销栈边界——运行/暂停期间的
    // 场景变化是运行时状态，变化本身照常生效但不留编辑历史）
    void ExecuteEditCommand(std::unique_ptr<Game::Command> cmd);

    // ---- 场景序列化：阶段 3b 移入 SceneIoHost 子系统（sceneIo_.Save/Load） ----

    // ---- 录制回调 ----
    void RecordScene(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void RecordUi(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);
    // 兑现待处理的烘焙请求（面板按钮与命令行 --bake-* 共用）：只有 Application
    // 持有场景数据，面板不直接读场景。Bake* 内部清除请求标志，重复调用零成本。
    void RunPendingBakes();
    // 光照贴图离线烘焙（U2-L1 接线 2a）：场景静态几何 → BakeLightmap → SaveLightmap + 统计。
    void BakeLightmapOffline();
    // 反射探针烘焙（U2-L2 接线 v1）：解析环境（太阳瓣 + 天光 + 点光源瓣）逐探针投影成 SH，
    // 场景加载后调用一次；reflectProbeDirty_ 触发 UpdateUniforms 打包上传。
    void BakeReflectionProbes();
    // 静态光照贴图运行时接线（U2-L1 v1）：烘焙 → 合并批次（顶点带图集 UV）→ 图集上传，
    // 前向主通道以批次替代实时立方体光照。
    void BuildStaticLightmapRuntime();
    // 烘焙共用采集：静态几何（meshId 0 立方体 + 父链）/ 太阳与点光源。
    void CollectStaticLightmapTris(std::vector<Render::LightmapTri>& tris, int& objectCount);
    void CollectBakeLights(std::vector<Render::DirectionalLightDesc>& dirs,
                           std::vector<Render::PointLightDesc>& points) const;
    // 反射探针 GPU 捕获（U2-L2 v1）：探针位 6 面渲染天空+静态几何进彩色立方图，
    // 每帧在 RecordPrePass 末尾执行；完成后 probeCaptureValid_ 置位供着色器走捕获路径。
    void RecordProbeCapture(VkCommandBuffer cmd, uint32_t frameIndex);
    // 后处理参数/相机环境/雾阴影资源每帧同步进 PostProcessor（RecordUi 全路径与 --no-ui 共用）
    void SyncPostProcessFrameState(uint32_t imageIndex, VkExtent2D extent);
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
    // 光照贴图烘焙请求（--bake-lightmap 经 RunPendingBakes 兑现后自清；2a CLI 专属）
    bool lightmapBakeRequested_ = false;
    bool lightmapBakeFailed_ = false;
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
    // 延迟光照逐片元探针辐照度体（set1 binding10）；未烘焙时 probeCount=0，片元回退单探针。
    std::vector<Render::UboBuffer<Render::ProbeUBO>> probeUbos_;
    // 探针脏标记：烘焙后置 true，下次 UpdateUniforms 重新打包并上传全部 UBO 槽后清 false。
    // 探针烘焙后静态不变，未烘焙/未重新烘焙时恒 false，UpdateUniforms 零开销。
    bool probeDirty_ = false;

    // ---- 反射探针（U2-L2 接线 v1）：局部环境烘焙镜面，打包进 set1 binding11 的
    // ReflectProbeUBO（前向/延迟 specular 共用）。场景加载即烘焙（解析环境，见
    // BakeReflectionProbes），脏标记延迟上传（4 槽上限，见 ShaderBindings 常量）。----
    Render::ReflectionProbeSet reflectionProbes_;
    std::vector<Render::UboBuffer<Render::ReflectProbeUBO>> reflectProbeUbos_;
    bool reflectProbeDirty_ = true;
    // ---- 反射探针 GPU 捕获（U2-L2 v1，单探针=probes[0]）：每帧 6 面渲染真实场景
    // （天空+静态几何）进彩色立方图（RGBA16F + mip 链），binding12 采样；
    // --no-probe-capture 旁路（A/B：解析 SH vs 真实捕获）。----
    ReflectionCapture probeCapture_;
    std::optional<Render::GraphicsPipeline> capturePipeline_;    // capture.vert + frag.glsl
    std::optional<Render::GraphicsPipeline> captureSkyPipeline_; // skybox 双着色器 + 捕获通道
    bool probeCaptureValid_ = false;                             // 首帧录制完成后置 true（UBO flag 消费）

    // ---- 静态光照贴图（U2-L1 渲染接线 v1）：运行时烘焙 → 合并批次（顶点带图集 UV）→
    // 前向主通道以批次替代共享立方体实例的光照；0.22.37 起延迟模式同样接管
    // （批次进 GBuffer 第 4 附件写烘焙辐射度，延迟光照 Pass 按标记直出）。----
    Render::Mesh staticLmMesh_;                                        // 合并批次网格（LightmapBatchVertex，世界空间）
    std::shared_ptr<Texture> lightmapAtlas_;                           // 图集（RGBE 解码 → RGBA16F，binding13）
    std::optional<Render::GraphicsPipeline> staticLmPipeline_;         // static_lm 双着色器
    std::optional<Render::GraphicsPipeline> staticLmDeferredPipeline_; // 延迟 GBuffer 变体（4 MRT）
    bool lightmapBatchReady_ = false;                                  // 批次就绪（场景加载后烘焙一次）
    std::vector<glm::mat4> staticLightMatrices_; // 相位 0 世界矩阵（批次就绪时阴影 caster 同源打光）

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
    Render::Mesh sphereLodMesh_;  // LOD 低模球（8×4 细分）
    Render::Mesh capsuleLodMesh_; // LOD 低模胶囊（8×3 细分）
    Render::LodGroup lodGroup_;   // 生产用 LOD 选档组（每帧从 ProjectPanel 参数同步）
    // ---- 方块世界（--scene voxel）：区块化体素地形 ----
    // 逐区块合并网格（顶点直接是世界坐标），配一个单位矩阵实例即可绘制；
    // 顶点色承载「方块基色 × AO」，故无需贴图即可有立体阴影感。
    // 网格重建按帧预算分摊：跨区块时一次只重建少量区块，避免卡顿尖峰
    struct VoxelChunkGpu
    {
        int cx = 0;
        int cz = 0;
        bool uploaded = false;  // 网格是否已生成（false = 等待分帧重建）
        glm::vec3 center{0.0f}; // 包围球中心（视锥剔除用）
        float radius = 0.0f;    // 包围球半径
        Render::Mesh mesh;
    };
    Sample::Voxel::VoxelWorld voxelWorld_;
    std::vector<VoxelChunkGpu> voxelChunks_;
    Render::InstanceBuffer voxelInstances_;
    bool voxelMode_ = false; // 当前场景是否为方块世界
    // 「纯游戏模式」：方块世界下收起编辑器面板，只留准星 + 方块世界 HUD。
    // 默认开（--editor-ui 可让启动即展开）；F1 运行期切换。
    bool voxelPlayMode_ = true;
    bool terrainFpTelemetry_ = false; // 地形 FP 落地遥测（一次性，首帧后置位）
    bool voxelPanelKeyHeld_ = false;  // F1 边沿检测（编辑器面板显隐）
    bool voxelLockKeyHeld_ = false;   // F 边沿检测（光标锁定开关）
    bool voxelReady_ = false;         // 世界与网格是否已完成首次构建
    bool voxelMeshesDirty_ = true;    // 有区块变化，需要重建网格
    int voxelLastChunkX_ = 0;
    int voxelLastChunkZ_ = 0;
    bool voxelLeftHeld_ = false;  // 左键边沿检测（挖掘）
    bool voxelRightHeld_ = false; // 右键边沿检测（放置）
    Sample::Voxel::BlockType voxelPlaceBlock_ = Sample::Voxel::BlockType::Stone;
    // 准星瞄准结果（每帧一次射线，供高亮描边与挖掘/放置共用，避免重复求交）
    Sample::Voxel::VoxelHit voxelAim_{};
    bool voxelAimValid_ = false;
    bool voxelPlaceBlocked_ = false; // 上一帧放置被玩家自身体积挡下（HUD 提示用）
    bool voxelInWater_ = false;      // 头部所在格是否为水体（游泳手感 + 水下雾）
    bool voxelViewUpHeld_ = false;   // '[' / ']' 视距调整的边沿检测
    bool voxelViewDownHeld_ = false;
    float voxelMineTimer_ = 0.0f; // 按住左键连挖的重复倒计时（秒）
    // ---- 方块世界时段（T 键循环）：目标值 + 每帧平滑逼近，避免硬跳变 ----
    int voxelDayIndex_ = 0;
    bool voxelDayHeld_ = false; // T 键边沿检测
    LightParams voxelTargetLight_{};
    glm::vec4 voxelTargetSky_{0.55f, 0.72f, 1.0f, 1.0f};
    glm::vec3 voxelTargetFog_{0.72f, 0.82f, 1.0f};
    glm::vec3 voxelFogTintLand_{0.72f, 0.82f, 1.0f}; // 陆地雾色随时段插值
    [[nodiscard]] const char* VoxelDayName() const noexcept;
    void ApplyVoxelDayTime(bool immediate);

    // ---- 地形场景（--scene terrain，U2-T1 接线 v1）：高度场 + 分块网格 + 顶点色 splat ----
    // 网格顶点即世界坐标、恒等模型矩阵直接绘制（无实例缓冲）；绘制路径复用主通道
    // 默认推送常量（tiles 反照率 × 顶点色 splat），阴影投靠 CSM/立方体阴影 casters。
    Scene::TerrainHeightmap terrainHeightmap_;
    std::vector<Render::Mesh> terrainChunkMeshes_; // 分块网格（确定性构建、仅首次上传）
    Render::InstanceBuffer terrainInstances_;      // 恒等实例缓冲（顶点着色器绑定1逐实例属性必需）
    Scene::TerrainSplatRule terrainSplatRule_;
    int terrainChunkQuads_ = 32; // 每块方格数（4×4 块 = 128×128 格 × 2m = 256×256m）
    int terrainGridSize_ = 129;  // 高度场每边顶点数
    bool terrainMode_ = false;   // 当前场景是否为地形（决定地面/阴影是否被地形替代）

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
    void RegisterMeshAsset(const std::string& name, const std::string& path, const std::vector<Scene::Vertex>& verts,
                           const std::vector<uint32_t>& indices, bighero::AssetMetadata::LoadState state,
                           uint64_t fileSize);
    // 把一张贴图登记进资源注册表（类型=Texture）
    void RegisterTextureAsset(const std::string& name, const std::string& path, bighero::AssetMetadata::LoadState state,
                              uint64_t fileSize);

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
    Render::GraphicsPipelineConfig gltfBlendConfig_;     // 前向 BLEND：标准 Alpha 混合，不写深度
    Render::GraphicsPipelineConfig transBlendConfig_;    // 延迟透明叠加 BLEND
    Render::GraphicsPipelineConfig transEmissiveConfig_; // 延迟透明叠加加性自发光（ONE/ONE）

    // GraphicsPipeline 无默认构造，用 optional 在 Init 阶段原位构造
    std::optional<Render::GraphicsPipeline> pipeline_;
    std::optional<Render::GraphicsPipeline> shadowPipeline_;
    std::optional<Render::GraphicsPipeline> cubeShadowPipeline_;
    std::optional<Render::GraphicsPipeline> skyboxPipeline_;
    std::optional<Render::GraphicsPipeline> gbufferPipeline_;
    std::optional<Render::GraphicsPipeline> lightingPipeline_;
    std::optional<Render::GraphicsPipeline> gltfBlendPipeline_;     // 前向 BLEND
    std::optional<Render::GraphicsPipeline> transBlendPipeline_;    // 延迟透明叠加 BLEND
    std::optional<Render::GraphicsPipeline> transEmissivePipeline_; // 延迟透明叠加加性自发光

    EditorOverlay editorOverlay_;
    // U1-UI 运行时 UI 系统（持 GPU 资源；声明于 editorOverlay_ 之后、renderer_/ctx_ 之后，
    // 析构逆序保证其 Vulkan 资源先于覆盖层与 Context 释放）
    Ui::UiRuntime uiRuntime_;
    bool uiClickForward_ = false; // 本帧单击是否转发引擎拾取（UI 启用时由 UpdateUi 统一消费判定）
    EditorPanel editorPanel_;
    BigHero::Editor::ProjectPanel projectPanel_;
    // 遮挡烘焙：只有达到该尺度的静态体才算遮挡体（小道具挡不住东西，只会拖慢烘焙）
    static constexpr float kOccluderMinScale = 2.0f;
    LightParams lightParams_;
    // 天空盒调色（时段/氛围预设）：rgb = 颜色乘数，w = 强度；默认 (1,1,1,1) 与原样一致
    glm::vec4 skyTint_{1.0f, 1.0f, 1.0f, 1.0f};
    // 第一人称陆行控制器（重力/跳跃/蹲伏/碰撞滑动；仅 FP 漫游模式启用）
    Game::FpController fpController_;
    std::vector<Game::BoxCollider> fpColliders_; // FP 碰撞体（展示厅场景导出；空=仅地面）
    ShowcaseHost showcase_;                      // 展示厅运行时（展台/时段/已体验打点）
    Sample::Showcase::CyberCityBuild city_;      // 赛博城市场景数据（--scene cybercity）
    bool fpFlyMode_ = false;                     // V 键：飞行俯瞰（关闭重力与碰撞）
    bool fpJumpHeld_ = false;                    // 空格边沿检测缓存（跳跃）
    bool fpKeyHeld_ = false;                     // V 键边沿检测缓存
    bool tKeyHeld_ = false;                      // T 键（时段）边沿检测缓存
    bool gKeyHeld_ = false;                      // G 键（画面风格）边沿检测缓存
    bool oKeyHeld_ = false;                      // O 键（自动昼夜循环）边沿检测缓存
    float showcaseClock_ = 0.0f;                 // 展示厅运行秒表（霓虹脉冲/呼吸动画相位）
    bool hKeyHeld_ = false;                      // H 键（帮助）边沿检测缓存
    bool eKeyHeld_ = false;                      // E 键（交互）边沿检测缓存
    bool rKeyHeld_ = false;                      // R 键（回出生点）边沿检测缓存
    bool escHeld_ = false;                       // Esc 键（释放光标）边沿检测缓存
    bool helpVisible_ = true;                    // HUD 按键帮助是否显示
    bool hudEnabled_ = true;                     // 展示厅 HUD 开关（--no-ui 下自动关闭）
    float welcomeTimer_ = 0.0f;                  // 欢迎卡倒计时（秒）
    std::array<bool, 10> digitHeld_{};           // 数字键 1-9 边沿检测缓存

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
    std::string currentSceneKind_; // 当前已加载场景（BuildAndLoadScene 维护，避免下拉框重复触发）
    std::string pendingSceneKind_; // 编辑器"场景"下拉框请求切换的目标场景（主循环消费）

    // 相机模式切换（编辑器面板或 Tab 键触发）
    enum class CameraMode
    {
        Orbit,
        FirstPerson
    };
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
    bool screenshot2Issued_ = false; // 第二张截图（--screenshot2，时序/脚本对比用）
    float runTimeSeconds_ = 0.0f;    // 进入主循环后的累计时长（截图2延时基准）

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
    Render::InstanceBuffer sphereLodInstances_;
    Render::InstanceBuffer capsuleLodInstances_;
    // ECS 渲染收敛：单趟批次化的逐桶暂存（遍历前 clear，遍历后逐桶登记上传）
    std::vector<Render::InstanceData> cubeScratch_;
    std::vector<Render::InstanceData> torusScratch_;
    std::vector<Render::InstanceData> sphereScratch_;
    std::vector<Render::InstanceData> capsuleScratch_;
    std::vector<Render::InstanceData> sphereLodScratch_;
    std::vector<Render::InstanceData> capsuleLodScratch_;
    std::vector<std::vector<Render::InstanceData>> gltfPrimScratch_; // 与 gltfPrims_ 一一对应
    uint32_t cubeInstanceCount_ = 0;
    uint32_t torusInstanceCount_ = 0;
    uint32_t sphereInstanceCount_ = 0;
    uint32_t capsuleInstanceCount_ = 0;
    uint32_t sphereLodInstanceCount_ = 0;
    uint32_t capsuleLodInstanceCount_ = 0;
    glm::mat4 firstGltfModel_{1.0f}; // 本帧首个可见 glTF 实体的模型矩阵（透明批次排序基准）

    // ---- 帧瞬态上传（FrameStaging）：替代逐帧 staging Buffer 创建/销毁 + 一次性提交 ----
    std::vector<Render::FrameStaging::StagedUpload> pendingUploads_; // 更新阶段登记，录制阶段消费
    std::vector<Render::InstanceData> instanceUploadStash_;          // 桶数据的本帧稳定副本（登记指针指向此处）

    // ---- 可见性统计 ----
    uint32_t culledCount_ = 0;
    uint32_t pvsCulledCount_ = 0; // 本帧被烘焙式 PVS（遮挡剔除）跳过的实体数；未烘焙时恒为 0

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
    // GPU 整帧耗时环形历史（与 CPU 帧耗时历史同尺寸，供 Stats HUD 叠加同轴曲线对比）。
    // 设备不支持时间戳时持续写入 0；RecordGpuFrameHistory() 每帧推进，
    // GetGpuHistoryChronological() 按最旧->最新顺序拷出供 ImGui::PlotLines 使用。
    std::array<float, Core::FrameProfiler::kHistorySize> gpuHistory_{};
    // 时序拷贝缓冲（最旧->最新），供 EditorStats.fpsHistory 指针指向，与 fpsHistoryChrono_ 同理。
    std::array<float, Core::FrameProfiler::kHistorySize> gpuHistoryChrono_{};
    size_t gpuHistoryIndex_ = 0;
    size_t gpuHistoryCount_ = 0;
    void RecordGpuFrameHistory(float ms)
    {
        gpuHistory_[gpuHistoryIndex_] = ms;
        gpuHistoryIndex_ = (gpuHistoryIndex_ + 1) % Core::FrameProfiler::kHistorySize;
        if (gpuHistoryCount_ < Core::FrameProfiler::kHistorySize)
            ++gpuHistoryCount_;
    }
    size_t GetGpuHistoryChronological(float* out, size_t maxCount) const
    {
        const size_t n = std::min(maxCount, gpuHistoryCount_);
        for (size_t i = 0; i < n; ++i)
            out[i] = gpuHistory_[(gpuHistoryIndex_ + i) % Core::FrameProfiler::kHistorySize];
        return n;
    }

    // ---- 基准模式（--bench-frames N）：帧耗时统计 + 各阶段 CPU 平均耗时 ----
    // 跳过前 kBenchWarmupFrames 帧（TAA/曝光收敛、动画/相机稳定）后开始累计；
    // 退出时打印 avg/min/max 帧耗时与 FrameProfiler 各作用域平均耗时（stdout）。
    static constexpr uint32_t kBenchWarmupFrames = 30;
    uint32_t benchCount_ = 0; // 已计入统计的帧数
    double benchSumMs_ = 0.0; // 累计帧耗时（毫秒）
    float benchMinMs_ = 0.0f;
    float benchMaxMs_ = 0.0f;
    // name -> (累计毫秒, 帧数)：各 FrameProfiler 作用域跨帧聚合（BuildSummary 仅看单帧）
    std::unordered_map<std::string, std::pair<double, uint32_t>> benchScopeAccum_;
    // 基准模式退出时打印统计到 stdout（FrameProfiler::Scope 数据已跨帧聚合）
    void PrintBenchSummary();

    // ---- 场景序列化快捷键边沿检测 ----
    bool saveKeyHeld_ = false;
    bool loadKeyHeld_ = false;
    bool f7KeyHeld_ = false; // F7 边沿状态（工程面板：资产数据库）
    bool f8KeyHeld_ = false; // F8 边沿状态（工程面板：LOD/探针/遮挡）

    // 阶段 3d：导航子系统（A* 网格 + AI 巡逻代理，调试数据经公有字段供录制消费）
    NavHost navHost_;

    // 阶段 3e：粒子子系统（模拟 + 实例缓冲 + 公告板管线，持 GPU 资源须在 ctx_ 之后析构）
    ParticleHost particleHost_;

    // 阶段 3f：人物子系统（Q 版人物 = 球/胶囊 ECS 部件骨骼树）
    Scene::PersonHost personHost_{ecsScene_};

    // C# 脚本宿主（--scripts 启用；引用 ecsScene_，声明在其后保证先于场景析构）。
    // 未启用时为默认构造（Enabled()==false，Update no-op）
    Script::CSharpHost scripts_;

    // ---- 玩法系统：撤销/重做 ----
    Game::CommandStack commandStack_;
    bool undoKeyHeld_ = false; // Ctrl+Z 边沿检测
    bool redoKeyHeld_ = false; // Ctrl+Y 边沿检测

    // ---- U1-E3 Play Mode：编辑态/运行态分离状态机（纯逻辑在 game/PlayMode.h） ----
    // 仿真系统推进与否经 playMode_.ShouldSimulate() 门控；编辑命令入栈经
    // playMode_.AllowsSceneEditCommands() 收口；Stop 用编辑态底稿全量还原。
    Game::PlayModeController playMode_;
    bool playKeyHeld_ = false; // Ctrl+P（Play/Stop 切换）边沿检测

    // ---- 玩法系统：属性编辑撤销（升级 20） ----
    std::optional<Game::SceneSnapshot> propertyEditBefore_; // 滑块/调色板手势起始快照（对象数不变）
    bool editGestureActive_ = false;                        // 当前是否有进行中的 ImGui 属性编辑手势
    bool gizmoEditActive_ = false;                          // Gizmo 变换拖拽进行中（非 ImGui item，单独跟踪）
    std::optional<Game::SceneSnapshot> gizmoEditBefore_;    // Gizmo 拖拽起始快照
    bool suppressEditGesture_ = false;                      // 本帧已执行显式命令（增删/撤销/重做），抑制手势记录防重复
    // U1-S1d：脚本字段手势基线（Update 阶段逐帧拉取的绘制前值表；与路径 A 的 frameStart 同语义）
    std::vector<Script::ScriptFieldTable> scriptEditBefore_;
};
} // namespace BigHero
