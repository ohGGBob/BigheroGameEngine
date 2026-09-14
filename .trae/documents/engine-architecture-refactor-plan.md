# 引擎架构重构执行蓝图

## Context（背景）

外部架构审计指出 5 项问题，已逐项核实属实（部分数字过时已修正）：

1. **上帝对象**：Application.cpp 2458 行 / Renderer.cpp 1399 行，主循环、物理、粒子、导航、序列化、动画、相机、拾取、全部录制路径混在单文件。
2. **重构半成品**：src/app/systems/ 15 个骨架文件从未接入构建——已核实 PhysicsSystem.h 同类重复定义 Name()/Priority()（编译不过）、关键方法全是空壳注释体，实际逻辑都在 Application.cpp。
3. **多套并行**：Buffer.cpp:33 / Image.cpp:50 / ubo_buffer.h:150 直呼 vkAllocateMemory，已实现且有单测的 GpuAllocator 被旁路（TransientAllocator 独立设计，本轮不动）。
4. **文档滞后**：README 特性章节已同步（前一轮），但架构树/工程化章节未反映本次重构。
5. **core/ 膨胀**：679 个头文件，引擎实际引用仅 11 个、测试额外引用 24 个，其余 ~644 个仅被 BigHeroHeaderCheck 编译；混入 SkyboxRenderer/TemporalAA/UI 系列/Tilemap/Terrain 等错位空壳类与零引用 _v2 文件。

**用户已拍板**：① 删除 systems/ 骨架、重构时新建真实子系统；② core/ 最小清理（删除错位空壳 + 零引用 _v2，保留 bighero:: 工具库原地）；③ 本轮执行推荐子集（骨架删除 → GpuAllocator 收敛 → Application 拆分 → Renderer 拆分 → core 最小清理 → README）。ECS 收敛（EcsScene 唯一权威存储）本轮不做。

**总原则**：每阶段独立可构建，ctest 全绿后进下一阶段；纯重构不改行为；中文注释带"阶段 N"标记。

**前置**：当前工作区有 52 个未提交文件（TAA 等特性）。开工前先请用户确认提交基线，保证每阶段可独立回滚。

---

## 阶段 1：删除 src/app/systems/ 骨架（15 文件）

- 前置校验：Grep `app/systems/` 于全库（含 CMakeLists.txt/ci.yml）确认零引用（CMakeLists 中 src/app 逐文件列出，无 systems glob）。
- 删除 15 个 .h：ISubSystem/SystemManager/TimeManager/CameraController/SceneManager/PhysicsSystem/ParticleSystem/NavigationSystem/RenderPipelineManager/AnimationSystem/GizmoSystem/UndoRedoManager/PostProcessManager/EditorIntegration/AudioSystem。
- 验证：全量构建 + ctest。

## 阶段 2：GpuAllocator 收敛（消除裸 vkAllocateMemory）

### 2.0 设计决策
- **分配器放 Context**：Buffer/Image 的 `Create(const Context&, ...)` 约 40 个调用点签名不变、零改动。生命期安全：Application 成员逆序析构，所有持 Buffer/Image 的对象先于 ctx_ 释放。
- **陷阱**：Context 成员析构晚于 `~Context` 函数体（晚于 vkDestroyDevice）→ 必须在 `~Context` 函数体内显式 `pools_.reset()`。
- **双池门面**（GpuAllocator 单实例只支持一种内存类型）：新增 `Render::MemoryPools`，持两个 GpuAllocator：
  - deviceLocal：blockSize 256MB，maxBlocks 8（上限 2GB）
  - hostVisible：blockSize 64MB，maxBlocks 8（上限 512MB），属性 HOST_VISIBLE|HOST_COHERENT，块级持久映射（建块时 vkMapMemory 一次，缓存 `unordered_map<uint32_t, void*>`）
- **minAlign** = max(bufferImageGranularity, 256)（GpuBlockAllocator 的 minAlign 同时取整大小与偏移，使 buffer/image 安全同块）。
- **dedicated 回退**：请求 > blockSize、memoryTypeBits 不含池类型、host 池不可用 → 返回 invalid，调用方走原独占路径。
- 单线程假设（分配仅发生在初始化/加载/重建期），注释注明；GpuAllocator.h 本体不改（保住 test_render_logic.cpp L184-274 单测）。

### 2.1 步骤 A：MemoryPools + Context 集成
- 新增 `src/render/MemoryPools.h/.cpp`（render 目录 glob 自动入库）；接口：`Alloc(requiredProps, memReq)` / `Free(a)` / `MemoryOf(a)` / `MappedOf(a)` / 统计日志。
- `Context.h/.cpp`：成员 `std::unique_ptr<MemoryPools> pools_`（unique_ptr::get() const 返回 T*，const 方法可安全暴露 `Pools()`）；`createLogicalDevice()` 成功后创建；`~Context()` 在 vkDestroyDevice 前 reset。
- 验证：构建 + ctest + 启动冒烟（无人用池，仅构造/析构路径干净）。

### 2.2 步骤 B：Buffer 收敛（Buffer.h/.cpp，调用点零改动）
- 新成员：`MemoryPools* pools_` / `GpuAllocation alloc_` / `void* persistent_`。
- Create：requirements → Alloc；valid → MemoryOf + vkBindBufferMemory(offset)，host 可见时 persistent_ = MappedOf；invalid → 原独占路径保留。
- UploadData 三分支：persistent_ 直接 memcpy（HOST_COHERENT 免 flush）/ 独占 host-visible 原 map/unmap / staging 路径不变。
- Destroy：池句柄 Free 或 vkFreeMemory。**MoveFrom 补转移 pools_/alloc_/persistent_（漏转 double-free，重点自查）**。
- 验证：构建 + ctest + 冒烟（网格/纹理/阴影全走 Buffer）。

### 2.3 步骤 C：Image 收敛（Image.h/.cpp）
- 仅改 Create（同 Buffer 模式）；`CreateBound` 一字不动；MoveFrom 补转移。
- 收益：交换链重建时 GBuffer/MSAA/后处理十几张全屏图反复 Create/Destroy，块内复用。
- 验证：构建 + ctest + 冒烟 + 拉拽窗口触发 resize，看显存用量日志不持续增长。

### 2.4 步骤 D：UboBuffer 收敛（ubo_buffer.h + Application.cpp L612-614 三行 emplace）
- 构造改 `UboBuffer(const Context&, uint32_t queueFamilyIndex)`；AllocateMemory/MapHostMemory 换池调用；Release 改 Free；invalid 回退。
- 收尾检查：Grep `vkAllocateMemory` 于 src/render 仅剩 TransientAllocator + 回退分支（≤4 处）。

## 阶段 3：Application.cpp 拆分（2458 行 → 目标 ~1100）

**形态**：普通类 + 构造注入依赖引用，不引入 ISubSystem/SystemManager；编辑器可调参数用公有字段（EditorPanel 本体签名不动，调用点 `&gradeSaturation_` → `&postProcessSync_.gradeSaturation` 机械替换）；不建 FrameContext 大结构，方法参数传每帧量（dt/extent）。

**新文件（6 个子系统，src/app/systems/，h/cpp 成对，两处 CMake 必改：Android add_library L356-358 + 桌面 add_executable L383-385）**：

| 顺序 | 文件 | 搬走的方法（Application::） | 搬走的成员 |
|---|---|---|---|
| 3a | PostProcessSync | RecordUi 内 pp 同步块（L2129-2183 抽为 SyncToPostProcessor）、UpdateCamera 内 Halton 抖动段（AdvanceJitter，静态函数随迁） | grade*/dof*/mb*/fog*/autoExposure_/exposureKeyValue_/adaptationSpeed_/vignette*/filmGrain_/taa*/prevTaa/deferred_/prevDeferred_/postProcess_/prevPostProcess_/ssao_/prevSsao_/ssr_/prevSsr_/prevViewProj_/currViewProj_ |
| 3b | SceneIoHost | SaveScene / LoadScene | kScenePath、saveKeyHeld_/loadKeyHeld_ 留主循环；注入 scene_/ecsScene_/光照参数/相机 |
| 3c | AnimationHost | InitAnimationStateMachine / UpdateAnimationStateMachine / UpdateGltfAnimationPose | animStateMachine_/animStateMachineInited_/animPoseT_R_S_/gltfAnimOffset_ |
| 3d | NavHost | UpdateNavPath / UpdateNavAgent | navGrid_/navPath_/navEnabled_/prevNavEnabled_/nav*/navAgent_ 全组 |
| 3e | ParticleHost | UpdateParticles / EmitParticleBurst / ApplyParticleConfig | particleSystem_/particleEnabled_/particleEmitterConfig_/particleGravity_/particleDamping_/emitterPresetIndex_/prevEmitterPresetIndex_/particleBuffer_/particleScratch_/particlePipeline_/particleConfig_/particleKeyHeld_（持 GPU 资源，声明在 ctx_ 之后；RebuildMainPipelines 补转发调用） |
| 3f | PhysicsHost | UpdatePhysics / SyncPhysicsBodies / RebuildPhysicsBodies / UpdateCharacter | physicsEngine_/physicsEnabled_/physicsDebugDraw_/gravity_/sceneJoints_/physicsJointIds_/character* 全组（耦合最深，最后拆） |

- Application 保留：装配、Run 主循环、六个 Record* 录制方法、Snapshot/RestoreScene（SceneSnapshotTarget 接口）、commandStack_、gizmo、编辑器编排。
- Host 们声明在 ctx_/renderer_/ecsScene_/camera_ 之后（析构逆序自动满足）；Host 内部成员保持原相对顺序。
- 每步验证：构建 + ctest；3a/3e/3f 额外冒烟（后处理全参数拖动、粒子发射、物理/角色、撤销重做往返）。

## 阶段 4：Renderer.cpp 拆分（1399 行）

**方案：纯物理拆 .cpp，不改头文件/类结构/不提 Pass 类**（RenderGraph 已是 pass 抽象层，提类触碰私有成员与 lambda 捕获，风险/收益不成比）。

| 文件 | 承接方法 | 约行数 |
|---|---|---|
| Renderer.cpp（保留） | 构造/析构、initCommon、pickDepthFormat/pickSampleCount、createCommandResources、createDummyWhiteImage、DrawFrame 整函数不拆、logTransientMemoryReport | ~620 |
| Renderer_Deferred.cpp | createDeferredRenderPass/destroy、createLightingRenderPass/destroy、createTransparentRenderPass/destroy、createDeferredFramebuffers/destroy、SetDeferred、createDeferredResources/destroy | ~350 |
| Renderer_PostFx.cpp | SetSSAO/SetSSR/SetPostProcessing、createOffscreenFramebuffer/destroy、createCompositeResources/destroyCompositeResources | ~290 |
| Renderer_FrameResources.cpp | createFrameResources/destroyFrameResources、createSyncObjects/destroySyncObjects、handleResize | ~170 |

- 跨区间共享的 anonymous-namespace 辅助 → 新建 `src/render/Renderer_Internal.h`；琐碎者就地复制注明来源。
- src/render 为 glob + CONFIGURE_DEPENDS，重新 configure 即入库。
- 验证：构建 + ctest + 四种模式组合冒烟（前向/前向+后处理/延迟/延迟+SSAO+SSR）。

## 阶段 5：core/ 最小清理（679 → 预期删 150-300）

**保留白名单**：引擎引用 11（Time/VkCheck/Log/VkUtils/AssetCache/AssetMetadata/AssetRegistry/FrameProfiler/MeshResource/Random/ecs）∪ 测试引用 24（JobSystem/AABB/Base64/BinaryReader/BinaryWriter/BitVector/ColorCurve/CRC32/EasingCurve_v2/FloatCurve/CurveKey/FbmNoise/Mathf/Matrix4/ObjectPool/Plane3_v2/Random/RingBuffer/SeededRandom/Sphere3_v2/Triangle/Vector2/Vector3/Vector3Curve/Vector4）∪ bighero:: 工具库（数学/几何/噪声/序列化/容器/渲染描述符纯数据结构/基础——判定口径：bighero:: 命名空间且属上述类别；同名冲突本轮保留不去重）。

**删除候选**：A 错位 UI/2D（UICanvas 系列/Sprite 系列/Tilemap 系列/Tween 系列/ScreenSpaceOverlay 等）→ B 错位渲染（SkyboxRenderer/TemporalAA/SSAO/ShadowMap/SwapChain/Texture2D/VertexBuffer/UniformBuffer/Skeleton/SkinnedMesh/SkinnedMeshRenderer/Shader 空壳/Semaphore/Spotlight 等）→ C 错位玩法物理（Terrain 系列/SoftBody2D/Verlet/Spring 系列/SweptVolume/SphereCollider 等）→ D 零引用 _v2（**必须保留 EasingCurve_v2/Plane3_v2/Sphere3_v2**）。

**判定与验证流程（每轮）**：
1. 候选 = core/*.h − keep；逐轮 Grep 引用（排除 src/core 内部互相 include）→ 被 keep 文件 include 的候选移入 keep（传递闭包），leaf 优先删，迭代至不动点（预期 3-4 轮）。
2. `git rm` → 构建三目标 BigHeroHeaderCheck + BigHeroTests + BigHeroGameEngine（HeaderCheck glob 在 configure 期自动收缩）→ ctest 全绿。
3. 轮次：Round A（UI/2D ~60）→ Round B（渲染 ~70）→ Round C（玩法/_v2 ~60）→ Round D 闭包扫尾（可选）。

## 阶段 6：README 同步

- 引擎架构树：src/app/systems/ 改为 6 个真实子系统一句话列表；渲染一节补 Renderer 拆分 4 TU 与 MemoryPools。
- 新增"架构重构（2026-09）"小节（性能剖析与工程化之后）：子系统职责表、GpuAllocator 收敛设计（双池/粒度/持久映射/回退）、core 清理统计、ECS 收敛铺垫说明。
- Roadmap：ECS 收敛条目补注"铺垫已就绪"。

---

## 验证（端到端）

每阶段执行后：
1. `cmake --build "D:\BigheroGameEngine\out\build\x64-Debug" --config Debug`（阶段 2A/3/4 需先重新 configure 使 glob 生效）
2. `ctest --test-dir "D:\BigheroGameEngine\out\build\x64-Debug" -C Debug --output-on-failure`（44 用例全绿）
3. 启动 BigHeroGameEngine.exe 冒烟：validation 层零报错、后处理面板调参/粒子/物理/角色/撤销重做/resize 行为不变
4. 阶段 2 收尾：Grep `vkAllocateMemory` 仅剩 TransientAllocator + 回退分支
5. 阶段 5 收尾：core/ 头文件数量与删除清单核对，test_foundation 引用的文件全部保留

## 风险与对策

| 风险 | 对策 |
|---|---|
| Context 成员析构晚于 vkDestroyDevice | ~Context 函数体内显式 pools_.reset()（阶段 2A 硬性要求） |
| MoveFrom 漏转移池句柄 → double-free | 2B/2C 逐字段自查 + validation 冒烟 |
| memoryTypeBits 不兼容池类型 | Alloc 前校验 + dedicated 回退保留 |
| 拆 Host 破坏初始化/析构顺序 | Host 声明在 ctx_ 之后；成员随迁不改相对顺序 |
| core 删除误伤传递依赖 | 传递闭包规则 + 每轮三目标构建 + ctest |
| 新 systems/*.cpp 忘加 Android 目标 | CMakeLists 两处逐文件列表同改（阶段 3 检查项） |
| 工作区 52 个未提交文件污染回滚点 | 开工前请用户确认提交基线；每阶段独立 commit（需用户同意） |

## 执行顺序

阶段 1 → 2A → 2B → 2C → 2D → 3a → 3b → 3c → 3d → 3e → 3f → 4 → 5(Round A→D) → 6，共约 15 个可构建、可回滚的步进。
