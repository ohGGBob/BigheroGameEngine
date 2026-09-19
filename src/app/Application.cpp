#include "app/Application.h"

#include "core/Time.h"
#include "core/VkCheck.h"
#include "scene/GltfLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

namespace BigHero
{
// 升级 20：场景快照命令已抽到 game/SceneCommand.h（纯逻辑、可单测），此处沿用短名。
using Game::SceneSnapshot;
using Game::SceneSnapshotCommand;
using Game::SceneSnapshotTarget;
namespace
{
// 按运行模式构造窗口/上下文/渲染器（跨平台：窗口经 Window 抽象工厂创建）
std::unique_ptr<Window> MakeWindow(const Application::AppConfig& c)
{
    if (c.headless || c.validateOnly)
        return Window::CreateHeadless(); // headless：不创建窗口
    return Window::Create(c.width, c.height, c.title.c_str(), true);
}

Context MakeContext(const Application::AppConfig& c, Window& window, bool enableValidation)
{
    if (c.headless || c.validateOnly)
        return Context(true); // headless：跳过窗口表面
    return Context(window, enableValidation);
}

Renderer MakeRenderer(const Application::AppConfig& c, const Context& ctx, Window& window)
{
    if (c.headless || c.validateOnly)
        return Renderer(ctx); // headless：渲染器跳过交换链
    return Renderer(ctx, window);
}
} // namespace

Application::Application() : Application(AppConfig{}) // 委托构造，避免重复初始化逻辑
{
}

Application::Application(const AppConfig& config)
    : config_(config), // config_ 是首个声明成员，此处读取安全
      window_(MakeWindow(config_)),
      ctx_(MakeContext(config_, *window_, kEnableValidation)), // 声明序保证 window_ 已构造
      renderer_(MakeRenderer(config_, ctx_, *window_)),
      // 阶段 3b：场景序列化子系统（引用绑定 + 加载联动回调交还 Application）
      sceneIo_(ecsScene_, scene_, lightParams_, pointLights_, camera_, hasTorus_, selectedObject_),
      // 阶段 3f：物理子系统（刚体/关节/角色控制器）
      physicsHost_(ecsScene_, scene_, camera_, *window_)
{
    sceneIo_.SetLoadHooks([this] { RepackScene(); }, [this] { RecalculateTriangleCount(); },
                          [this] { physicsHost_.RebuildBodies(); });
}

Application::~Application() = default;

int Application::ValidateOnly()
{
    try
    {
        LOG_INFO("Headless validation mode: checking shader files and SPIR-V...");
        const std::vector<std::string> requiredShaders = {"shaders/vert.spv",
                                                          "shaders/frag.spv",
                                                          "shaders/shadow.vert.spv",
                                                          "shaders/shadow.frag.spv",
                                                          "shaders/shadow_cube.vert.spv",
                                                          "shaders/shadow_cube.frag.spv",
                                                          "shaders/skybox.vert.spv",
                                                          "shaders/skybox.frag.spv",
                                                          "shaders/particle.vert.spv",
                                                          "shaders/particle.frag.spv",
                                                          "shaders/deferred_light.vert.spv",
                                                          "shaders/deferred_light.frag.spv",
                                                          "shaders/gbuffer.frag.spv",
                                                          "shaders/pp_bright.frag.spv",
                                                          "shaders/pp_blur.frag.spv",
                                                          "shaders/pp_composite.frag.spv",
                                                          "shaders/pp_depth_linearize.frag.spv",
                                                          "shaders/pp_dof.frag.spv",
                                                          "shaders/pp_motion_blur.frag.spv",
                                                          "shaders/ssao.frag.spv",
                                                          "shaders/ssr_ray.frag.spv",
                                                          "shaders/ssr_blur.frag.spv",
                                                          "shaders/irradiance.frag.spv",
                                                          "shaders/prefilter.frag.spv",
                                                          "shaders/brdf_lut.frag.spv"};

        for (const auto& path : requiredShaders)
        {
            if (!std::filesystem::exists(path))
            {
                LOG_ERROR("Missing shader: " << path);
                return EXIT_FAILURE;
            }
            LOG_INFO("Shader found: " << path);
        }

        LOG_INFO("Validation successful: all shader SPIR-V files present");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Validation failed: " << e.what());
        return EXIT_FAILURE;
    }
}

// ========================================================================
// 主入口
// ========================================================================

int Application::Run()
{
    try
    {
        InitResources();
        CreatePipelines();
        SetupCallbacks();
        InitScene();
        InitGameSystems();

        lastTime_ = Time::NowSeconds();
        LOG_INFO("进入主循环（左键拖拽旋转 / 滚轮缩放 / WASD+QE平移）");

        while (!window_->ShouldClose())
        {
            frameProfiler_.BeginFrame();

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "PollEvents");
                window_->PollEvents();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Update");
                UpdateTime();
                UpdateCamera();
                UpdateGizmo();
                SyncSceneEdits(); // ECS：包 -> ECS 写回（编辑器/Gizmo 修改持久化）
                physicsHost_.Update(deltaTime_);
                RepackScene(); // ECS：ECS -> 包投影（自转角/物理位置输出到渲染数据）
                // 动画状态机（编辑器面板可暂停/拖动时间轴）：解析角色输入参数后交子系统推进
                {
                    AnimationHost::FrameInput animInput;
                    animInput.characterActive =
                        physicsHost_.characterEnabled && physicsHost_.characterBodyId != UINT32_MAX;
                    if (animInput.characterActive)
                    {
                        const glm::vec3 vel = physicsHost_.engine.GetBodyLinearVelocity(physicsHost_.characterBodyId);
                        animInput.speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                        animInput.grounded = physicsHost_.characterGrounded;
                    }
                    animationHost_.Update(deltaTime_, animInput, gltfModel_, hasGltf_);
                }
                UpdateRenderables(); // ECS 渲染收敛：单趟直读 ECS（剔除 + 批次化 + 上传登记）
                particleHost_.Update(deltaTime_);
                // 粒子：登记到帧瞬态上传（scratch 成员在录制前稳定）
                if (particleHost_.enabled && !particleHost_.scratch.empty())
                    AppendUpload(particleHost_.buffer.Get(), particleHost_.scratch.data(),
                                 static_cast<VkDeviceSize>(particleHost_.scratch.size()) *
                                     sizeof(Render::ParticleInstance));
                navHost_.UpdateAgent(deltaTime_);
                UpdateUniforms();
                UpdateFpsTitle();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Picking");
                HandlePicking();
                UpdateDeferredState();
            }

            // 场景序列化快捷键：F5 保存，F9 加载（边沿检测，避免按住重复触发）
            const bool f5Down = window_->IsKeyDown(Window::kKeyF5);
            const bool f9Down = window_->IsKeyDown(Window::kKeyF9);
            if ((f5Down && !saveKeyHeld_) || editorPanel_.saveRequested)
                sceneIo_.Save();
            if ((f9Down && !loadKeyHeld_) || editorPanel_.loadRequested)
                sceneIo_.Load();
            saveKeyHeld_ = f5Down;
            loadKeyHeld_ = f9Down;
            editorPanel_.saveRequested = false;
            editorPanel_.loadRequested = false;

            // 导航网格：启用状态切换时重算 A* 路径（阶段 3d：状态在 NavHost 子系统）
            if (navHost_.enabled != navHost_.prevEnabled)
            {
                navHost_.prevEnabled = navHost_.enabled;
                if (navHost_.enabled)
                    navHost_.UpdatePath();
            }

            // 撤销/重做：Ctrl+Z / Ctrl+Y（边沿触发，避免按住每帧重复）
            const bool ctrlDown =
                window_->IsKeyDown(Window::kKeyLeftControl) || window_->IsKeyDown(Window::kKeyRightControl);
            const bool zDown = window_->IsKeyDown(Window::kKeyZ);
            const bool yDown = window_->IsKeyDown(Window::kKeyY);
            if (ctrlDown && zDown && !undoKeyHeld_)
            {
                commandStack_.Undo();
                suppressEditGesture_ = true; // 显式命令，抑制本帧手势记录防重复
                LOG_INFO("撤销: 重做栈顶 = " << commandStack_.TopRedoName());
            }
            undoKeyHeld_ = ctrlDown && zDown;
            if (ctrlDown && yDown && !redoKeyHeld_)
            {
                commandStack_.Redo();
                suppressEditGesture_ = true;
                LOG_INFO("重做: 撤销栈顶 = " << commandStack_.TopUndoName());
            }
            redoKeyHeld_ = ctrlDown && yDown;

            // 粒子爆发：P 键（边沿触发，阶段 3e：状态在 ParticleHost 子系统）
            const bool pDown = window_->IsKeyDown(Window::kKeyP);
            if (pDown && !particleHost_.keyHeld)
                particleHost_.EmitBurst(camera_.Target());
            particleHost_.keyHeld = pDown;

            // 编辑器物体增删请求
            if (editorPanel_.addObjectRequested)
            {
                const SceneSnapshot before = Snapshot();
                Scene::SceneObject obj;
                obj.position =
                    glm::vec3(static_cast<float>(rand() % 7) - 3.5f, 0.5f, static_cast<float>(rand() % 7) - 3.5f);
                obj.scale = 1.0f;
                obj.tint = glm::vec3(0.8f, 0.8f, 0.8f);
                obj.spinSpeed = 30.0f;
                obj.phase = 0.0f;
                obj.meshId = 0;
                obj.metallic = 0.1f;
                obj.roughness = 0.7f;
                obj.rotation = glm::vec3(0.0f);
                ecsScene_.CreateObject(obj);
                RepackScene();
                RecalculateTriangleCount();
                physicsHost_.RebuildBodies();
                const SceneSnapshot after = Snapshot();
                suppressEditGesture_ = true;
                commandStack_.Execute(std::make_unique<SceneSnapshotCommand>(this, before, after, "添加物体"));
                editorPanel_.addObjectRequested = false;
                LOG_INFO("添加物体: 总计 " << scene_.size() << " 个（可 Ctrl+Z 撤销）");
            }
            if (editorPanel_.deleteObjectRequested && selectedObject_ >= 0 &&
                selectedObject_ < static_cast<int>(scene_.size()))
            {
                const SceneSnapshot before = Snapshot();
                ecsScene_.DestroyAt(static_cast<size_t>(selectedObject_));
                selectedObject_ = -1;
                RepackScene();
                RecalculateTriangleCount();
                physicsHost_.RebuildBodies();
                const SceneSnapshot after = Snapshot();
                suppressEditGesture_ = true;
                commandStack_.Execute(std::make_unique<SceneSnapshotCommand>(this, before, after, "删除物体"));
                editorPanel_.deleteObjectRequested = false;
                LOG_INFO("删除物体: 剩余 " << scene_.size() << " 个（可 Ctrl+Z 撤销）");
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Render");
                renderer_.SetSSAOCamera(camera_.Proj() * camera_.View(), camera_.Position());
                renderer_.SetSSRCamera(camera_.Proj() * camera_.View(), camera_.Position());
                // 曝光：deferred 链末端统一乘（P0-3 Commit3；与 lightUbo.exposure 同源）
                renderer_.SetExposure(lightParams_.exposure);
                // 升级 22：每帧把相机近/远平面交给后处理，供景深还原线性深度
                renderer_.SetPostProcessingCamera(camera_.nearZ_, camera_.farZ_);
                // 升级 23：计算当前帧视图投影，并把"上一帧→当前帧"重投影交给运动模糊
                postProcessSync_.currViewProj = camera_.Proj() * camera_.View();
                renderer_.SetMotionBlurCamera(postProcessSync_.prevViewProj, postProcessSync_.currViewProj);
                // 升级 28：TAA 重投影（双方均为带抖动的 VP）+ 当前帧抖动量
                if (Render::PostProcessor* pp = renderer_.GetPostProcessor(); pp)
                {
                    pp->SetTaaCamera(postProcessSync_.prevViewProj, postProcessSync_.currViewProj);
                    pp->SetTaaJitter(postProcessSync_.taaJitterX, postProcessSync_.taaJitterY);
                }
                postProcessSync_.prevViewProj = postProcessSync_.currViewProj;
                renderer_.DrawFrame(
                    [this](VkCommandBuffer cmd, uint32_t fi, VkExtent2D ext) { RecordScene(cmd, fi, ext); },
                    [this](VkCommandBuffer cmd, uint32_t ii, VkExtent2D ext) { RecordUi(cmd, ii, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, VkExtent2D ext) { RecordPrePass(cmd, fi, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordLighting(cmd, fi, ii, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordTransparent(cmd, fi, ii, ext); },
                    [this](Render::ParallelCommandRecorder& rec, uint32_t fi) { RecordParallelCubeShadow(rec, fi); });
            }

            frameProfiler_.EndFrame();
        }

        ctx_.WaitIdle();
        LOG_INFO("渲染循环结束，资源由RAII自动释放");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("引擎异常退出: " << e.what());
        return EXIT_FAILURE;
    }
}

// ========================================================================
// 初始化
// ========================================================================

// 资产登记 / glTF 装配 / 批次绘制辅助：已拆至 Application_Assets.cpp

void Application::InitResources()
{
    // ---- 阴影与环境光 ----
    shadowMap_.Create(ctx_);
    cubeShadowMap_.Create(ctx_, 1024);
    
    // 使用HDR环境贴图创建环境光照
    const std::string envHdrPath = "assets/env/env_sunset.hdr";
    if (std::filesystem::exists(envHdrPath))
    {
        LOG_INFO("加载HDR环境贴图: " << envHdrPath);
        if (!envLighting_.CreateFromFile(ctx_, envHdrPath))
        {
            LOG_ERROR("加载HDR环境贴图失败: " << envHdrPath);
            envLighting_.Create(ctx_); // 回退到默认环境光
        }
        else
        {
            LOG_INFO("HDR环境贴图加载成功");
        }
    }
    else
    {
        LOG_INFO("HDR环境贴图不存在，使用默认环境光: " << envHdrPath);
        envLighting_.Create(ctx_);
    }
    // ---- 描述符与每帧 UBO（双帧并行，各自独立缓冲与描述符集） ----
    descManager_.Init(ctx_.Device());
    descManager_.AllocateSets(Renderer::MaxFramesInFlight());

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    cameraUbos_.reserve(kFrameCount);
    lightUbos_.reserve(kFrameCount);
    pointShadowUbos_.reserve(kFrameCount);
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        cameraUbos_.emplace_back(ctx_, ctx_.GraphicsFamily());
        lightUbos_.emplace_back(ctx_, ctx_.GraphicsFamily());
        pointShadowUbos_.emplace_back(ctx_, ctx_.GraphicsFamily());
    }

    // ---- 纹理：通过 AssetManager 统一缓存（LRU + 引用计数），缺失时程序化回退 ----
    // 键名约定：含 "normal" 或 "_mr" 视为线性数据贴图（UNORM），其余按 sRGB 反照率加载
    assetManager_.Cache<Texture>(16,
                                 [this](const std::string& key) -> std::shared_ptr<Texture>
                                 {
                                     auto tex = std::make_shared<Texture>();
                                     if (key == "checkerboard")
                                         tex->CreateCheckerboard(ctx_);
                                     else if (key == "flat_normal")
                                         tex->CreateFlatNormal(ctx_);
                                     else if (key == "white")
                                         tex->CreateSolid(ctx_, 255, 255, 255, /*sRGB=*/false);
                                     else if (std::filesystem::exists(key))
                                         tex->CreateFromFile(ctx_, key.c_str(),
                                                             key.find("normal") == std::string::npos &&
                                                                 key.find("_mr") == std::string::npos);
                                     else
                                         return nullptr;
                                     return tex->IsValid() ? tex : nullptr;
                                 });

    texture_ = assetManager_.Load<Texture>(kDefaultTexturePath);
    if (!texture_)
    {
        LOG_WARN("未找到 " << kDefaultTexturePath << "，使用程序化棋盘格纹理");
        texture_ = assetManager_.Load<Texture>("checkerboard");
    }

    normalTexture_ = assetManager_.Load<Texture>(kNormalMapPath);
    if (!normalTexture_)
    {
        LOG_WARN("未找到 " << kNormalMapPath << "，使用平坦法线");
        normalTexture_ = assetManager_.Load<Texture>("flat_normal");
    }

    // ---- 逐物体纹理池（set1 binding9）：0=全局反照率 1=全局法线 2=中性白，其余回退到反照率 ----
    texturePool_.assign(Render::kObjectTextureSlots, texture_);
    texturePool_[1] = normalTexture_;
    if (auto white = assetManager_.Load<Texture>("white"))
        texturePool_[2] = white;

    // ---- 绑定描述符：set0 相机 / set1 光照+纹理 / set2 点光源立方体阴影矩阵 ----
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        using RDS = Render::FrameDescriptorSet;
        descManager_.UpdateSet(Render::FrameSetIndex(i, RDS::Camera), 0, cameraUbos_[i]);
        descManager_.UpdateSet(Render::FrameSetIndex(i, RDS::Light), 0, lightUbos_[i]);
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 1, texture_->View(), texture_->Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 2, normalTexture_->View(),
                                    normalTexture_->Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 3, shadowMap_.View(), shadowMap_.Sampler(),
                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 4, envLighting_.EnvView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 5, envLighting_.IrradianceView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 6, envLighting_.PrefilteredView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 7, envLighting_.BrdfLutView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 8, cubeShadowMap_.View(),
                                    cubeShadowMap_.Sampler(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        descManager_.UpdateSet(Render::FrameSetIndex(i, RDS::PointShadow), 0, pointShadowUbos_[i]);
    }

    // ---- 逐物体纹理池描述符（set1 binding9 数组，glTF 贴图加载后会再刷新） ----
    UpdateObjectTextureDescriptors();

    // ---- 延迟渲染：GBuffer 输入附件描述符集（每交换链图像一组） ----
    descManager_.AllocateGBufferSets(renderer_.GetSwapchain().ImageCount());

    // ---- 场景几何：立方体+地面组合网格 ----
    const std::vector<Scene::Vertex> vertices = Scene::BuildSceneVertices();
    const std::vector<uint32_t> indices = Scene::BuildSceneIndices();
    sceneMesh_.Create(ctx_, vertices, indices);
    RegisterMeshAsset("builtin:scene", "<procedural>", vertices, indices, bighero::AssetMetadata::LoadState::Loaded, 0);

    // ---- 外部模型：圆环体（OBJ），文件缺失时从场景中剔除 ----
    if (std::filesystem::exists(kTorusModelPath))
    {
        const Scene::MeshData torusData = Scene::LoadObjModel(kTorusModelPath);
        torusMesh_.Create(ctx_, torusData.vertices, torusData.indices);
        hasTorus_ = true;
        RegisterMeshAsset("torus", kTorusModelPath, torusData.vertices, torusData.indices,
                          bighero::AssetMetadata::LoadState::Loaded,
                          static_cast<uint64_t>(std::filesystem::file_size(kTorusModelPath)));
        LOG_INFO("圆环体模型加载成功: " << torusData.vertices.size() << "顶点 / " << torusData.indices.size() / 3
                                        << "三角形");
    }
    else
    {
        RegisterMeshAsset("torus", kTorusModelPath, {}, {}, bighero::AssetMetadata::LoadState::Failed, 0);
        LOG_WARN("未找到 " << kTorusModelPath << "，场景不含外部模型");
    }

    // ---- 音频系统：初始化设备 + 尝试加载背景音乐 ----
    if (audioEngine_.IsValid())
    {
        audioEngine_.SetMasterVolume(0.5f);
        const char* kBgmPath = "assets/audio/bgm.wav";
        if (std::filesystem::exists(kBgmPath))
        {
            if (bgm_.Load(audioEngine_, kBgmPath, /*looping=*/true))
            {
                bgm_.SetVolume(0.4f);
                bgm_.Play();
                LOG_INFO("背景音乐已播放: " << kBgmPath);
            }
            else
            {
                LOG_WARN("背景音乐加载失败: " << kBgmPath);
            }
        }
        else
        {
            LOG_INFO("未找到背景音乐 " << kBgmPath << "，音频系统已就绪（放入文件即可自动播放）");
        }
    }
    else
    {
        LOG_WARN("音频设备初始化失败，音频功能已禁用");
    }

    // ---- 物理引擎（阶段 3f：移入 PhysicsHost 子系统） ----
    physicsHost_.Init();
}

// 管线创建：已拆至 Application_Pipelines.cpp

void Application::SetupCallbacks()
{
    renderer_.SetRenderPassRecreateCallback(
        [this]()
        {
            RebuildMainPipelines();
            RebuildDeferredPipelines();
            if (renderer_.IsDeferred())
                UpdateGBufferSets();
        });

    // headless 模式（CI/校验）：无窗口表面与交换链，ImGui 的 GLFW 后端要求有效窗口句柄，
    // 直接 Init 会以 NULL window 崩溃（ImGui_ImplGlfw_InitForVulkan）；此处跳过，使 headless
    // 渲染路径可独立运行（RecordUi 已按 IsInitialized() 跳帧）。
    if (!ctx_.IsHeadless())
        editorOverlay_.Init(ctx_, *window_, renderer_.GetSwapchain());
    renderer_.SetResizeCallback(
        [this]()
        {
            editorOverlay_.RecreateFramebuffers(renderer_.GetSwapchain());
            if (renderer_.IsDeferred())
                UpdateGBufferSets();
        });

    // GBuffer 图像走 transient 池延迟绑定：视图在 bind 之后才有效。此处注册回调，
    // 在 Renderer 完成绑定的那一刻把 GBuffer 视图写入描述符集，避免较早写入 VK_NULL_HANDLE
    // （严格满足 VUID-01020；UpdateDeferredState 中的调用仍保留作快速路径）。
    renderer_.SetTransientBoundCallback(
        [this]()
        {
            if (renderer_.IsDeferred())
                UpdateGBufferSets();
        });
}

void Application::InitScene()
{
    // ECS 场景实体化：先组装物体列表，再一次性灌入 ECS 权威存储
    std::vector<Scene::SceneObject> objs = Scene::BuildDefaultScene();
    if (!hasTorus_)
    {
        objs.erase(
            std::remove_if(objs.begin(), objs.end(), [](const Scene::SceneObject& obj) { return obj.meshId != 0; }),
            objs.end());
    }

    // ---- glTF 模型 + PBR 贴图映射（meshId=2）：网格/材质/纹理池，决定 hasGltf_ ----
    // 实例缓冲容量：默认场景 + glTF 演示物体 + 余量
    const uint32_t kMaxInstances = static_cast<uint32_t>(objs.size()) + 3;
    LoadGltfAsset(kMaxInstances);

    // ---- glTF 演示物体：模型加载成功后自动入场景，展示材质贴图映射效果 ----
    if (hasGltf_)
    {
        Scene::SceneObject demo;
        demo.position = glm::vec3(0.0f, 1.5f, 0.0f);
        demo.scale = 1.0f;
        demo.tint = glm::vec3(1.0f);
        demo.meshId = 2;
        demo.metallic = 1.0f;
        demo.roughness = 1.0f;
        demo.spinSpeed = 45.0f;
        demo.phase = 0.0f;
        objs.push_back(demo);
        LOG_INFO("glTF 演示物体已加入场景（原点上方旋转）");
    }

    ecsScene_.LoadPacket(objs); // 自转角初始化为 phase
    RepackScene();

    pointLights_ = BuildDefaultPointLights();
    if (!pointLights_.empty())
        pointLights_[0].castsShadow = true; // 演示：默认启用 1 号灯投影阴影

    // 三角形总数（含圆环体/glTF 模型实际入场景的物体）
    RecalculateTriangleCount();

    // 实例缓冲：立方体/圆环/地面三份（glTF 逐 primitive 份在 LoadGltfAsset 内创建）
    cubeInstances_.Create(ctx_, kMaxInstances);
    torusInstances_.Create(ctx_, kMaxInstances);
    groundInstances_.Create(ctx_, kMaxInstances);

    // 地面实例数据恒定（恒等模型 + 固定材质），初始化上传一次，不再逐帧中转
    Render::InstanceData ground{};
    ground.tint = glm::vec4(1.0f);
    ground.metallic = 0.0f;
    ground.roughness = 0.9f;
    groundInstances_.Upload(ctx_, &ground, 1);

    physicsHost_.RebuildBodies();
}

// ========================================================================
// 玩法系统初始化（升级 17：导航 / 粒子 / 撤销重做）
// ========================================================================

void Application::InitGameSystems()
{
    // 阶段 3d：导航网格与 AI 代理（A* 演示网格构建 + 代理巡逻绑定）移入 NavHost 子系统
    navHost_.Init();

    // 阶段 3e：粒子系统（预设/发射器/GPU 实例缓冲）移入 ParticleHost 子系统
    particleHost_.Init(ctx_);
}

Game::SceneSnapshot Application::Snapshot() const
{
    SceneSnapshot s;
    s.objects = scene_;
    s.spins = spinAngles_;
    return s;
}

void Application::RestoreScene(const Game::SceneSnapshot& snap)
{
    // ECS 全量重建（物体与自转角一并恢复），包并行数组随之重投影；
    // 可见性为每帧渲染派生值（UpdateRenderables 直算），不在快照内
    ecsScene_.LoadPacket(snap.objects, &snap.spins);
    scene_ = snap.objects;
    spinAngles_ = snap.spins;
    // 选中索引可能失效
    if (selectedObject_ >= static_cast<int>(scene_.size()))
        selectedObject_ = -1;
    RecalculateTriangleCount();
    physicsHost_.RebuildBodies();
}

void Application::HandlePropertyEditUndo(const SceneSnapshot& frameStart)
{
    // 本帧已执行显式命令（增删/撤销/重做/右键生成）：放弃手势记录，避免与显式命令重复
    if (suppressEditGesture_)
    {
        editGestureActive_ = false;
        propertyEditBefore_.reset();
        gizmoEditActive_ = false;
        gizmoEditBefore_.reset();
        suppressEditGesture_ = false; // 每帧消费一次
        return;
    }

    // 路径 A：ImGui 属性编辑手势（滑块/调色板）。
    // 拖拽期间 IsAnyItemActive 持续为真，松手当帧变为假即提交；一次完整拖拽 = 一个撤销步。
    const bool active = ImGui::IsAnyItemActive();
    if (active && !editGestureActive_)
    {
        // 手势开始：用本帧编辑交互前的快照作为 before（ImGui 在 Draw 内已改写场景）
        editGestureActive_ = true;
        propertyEditBefore_ = frameStart;
    }
    else if (!active && editGestureActive_)
    {
        editGestureActive_ = false;
        const SceneSnapshot after = Snapshot();
        // 仅当对象数不变（排除增删）且快照确有差异时，才压入属性编辑命令
        if (propertyEditBefore_.has_value() && after.objects.size() == propertyEditBefore_->objects.size() &&
            Game::SceneSnapshotsDiffer(propertyEditBefore_.value(), after))
        {
            commandStack_.Execute(
                std::make_unique<SceneSnapshotCommand>(this, propertyEditBefore_.value(), after, "编辑物体属性"));
        }
        propertyEditBefore_.reset();
    }

    // 路径 B：Gizmo 变换拖拽（屏幕手柄位移/旋转，非 ImGui widget，单独跟踪）。
    // 拖拽起始在 UpdateGizmo 内已抓 gizmoEditBefore_；此处检测"拖拽结束"边沿（gizmoDragging_ 落回 false），
    // 比对起始与当前快照，对象数不变且确有差异则作为一个撤销步压入命令栈。
    if (gizmoEditActive_ && !gizmoDragging_)
    {
        gizmoEditActive_ = false;
        const SceneSnapshot after = Snapshot();
        if (gizmoEditBefore_.has_value() && after.objects.size() == gizmoEditBefore_->objects.size() &&
            Game::SceneSnapshotsDiffer(gizmoEditBefore_.value(), after))
        {
            commandStack_.Execute(
                std::make_unique<SceneSnapshotCommand>(this, gizmoEditBefore_.value(), after, "编辑物体属性"));
        }
        gizmoEditBefore_.reset();
    }
}

// ========================================================================
// 每帧更新
// ========================================================================

void Application::UpdateTime()
{
    const double now = Time::NowSeconds();
    deltaTime_ = static_cast<float>(now - lastTime_);
    lastTime_ = now;

    // ECS 组件化自转系统：Spin.angle += speed*dt（包投影由 RepackScene 统一输出）
    ecsScene_.UpdateSpins(deltaTime_);
}

// ECS 场景实体化：包 -> ECS 写回。上一帧 UI/Gizmo/物理对 scene_ 包的修改持久化到组件，
// 使 ECS 始终是场景物体的权威存储（自转角 angle 为运行时状态，不参与写回）。
void Application::SyncSceneEdits()
{
    ecsScene_.SyncFromPacket(scene_);
}

// ECS 场景实体化：ECS -> 包投影。scene_/spinAngles_ 为编辑器数据模型的兼容层
// （Gizmo/拾取/撤销快照/序列化/物理关节锚点/编辑器面板消费）；渲染已直读 ECS 组件
// （UpdateRenderables），不再依赖本包。每帧仍重建：物理/自转每帧改写 ECS，
// Gizmo 与拾取需要当前世界坐标。
void Application::RepackScene()
{
    scene_ = ecsScene_.BuildPacket(&spinAngles_);
}

void Application::UpdateCamera()
{
    const auto [dx, dy] = window_->GetCursorDelta();
    if (window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !gizmoDragging_)
        camera_.Orbit(static_cast<float>(dx), static_cast<float>(dy));
    camera_.Zoom(window_->ConsumeScrollDelta());

    const float panStep = kPanSpeed * deltaTime_;
    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (window_->IsKeyDown(Window::kKeyW))
        forward += panStep;
    if (window_->IsKeyDown(Window::kKeyS))
        forward -= panStep;
    if (window_->IsKeyDown(Window::kKeyD))
        right += panStep;
    if (window_->IsKeyDown(Window::kKeyA))
        right -= panStep;
    if (window_->IsKeyDown(Window::kKeyE))
        up += panStep;
    if (window_->IsKeyDown(Window::kKeyQ))
        up -= panStep;
    if (forward != 0.0f || right != 0.0f || up != 0.0f)
        camera_.Pan(forward, right, up);

    const VkExtent2D frameExtent = renderer_.Extent();
    const float aspect =
        frameExtent.height > 0 ? static_cast<float>(frameExtent.width) / static_cast<float>(frameExtent.height) : 1.0f;

    // 升级 28：TAA Halton 抖动推进（阶段 3a 移入 PostProcessSync::AdvanceJitter）
    const Render::PostProcessor* pp = renderer_.GetPostProcessor();
    postProcessSync_.AdvanceJitter(postProcessSync_.taaEnabled && pp && pp->UseMsaa(), frameExtent, camera_);

    camera_.Update(aspect);
}

void Application::UpdateGizmo()
{
    const auto [fbw, fbh] = window_->GetFramebufferSize();
    const glm::vec2 gizmoVp(static_cast<float>(fbw), static_cast<float>(fbh));
    const auto [mxp, myp] = window_->GetCursorPos();
    const glm::vec2 mousePx(static_cast<float>(mxp), static_cast<float>(myp));
    const bool leftDown = window_->IsMouseButtonDown(Window::kMouseButtonLeft);

    // 左键松开 -> 结束拖拽
    if (gizmoDragging_ && !leftDown)
    {
        gizmoDragging_ = false;
        gizmoDragAxis_ = Editor::GizmoAxis::None;
    }

    const glm::mat4 camViewProj = camera_.Proj() * camera_.View();

    // 左键按下且未命中 UI：尝试拾取最近手柄轴
    if (selectedObject_ >= 0 && gizmoMode_ != Editor::GizmoMode::None && !ImGui::GetIO().WantCaptureMouse && leftDown &&
        !gizmoDragging_)
    {
        Scene::SceneObject& obj = scene_[static_cast<size_t>(selectedObject_)];
        const auto axis =
            Editor::PickAxis(obj.position, camViewProj, gizmoVp, mousePx, kGizmoPickRadius, kGizmoAxisLength);
        if (axis != Editor::GizmoAxis::None)
        {
            gizmoDragging_ = true;
            gizmoDragAxis_ = axis;
            gizmoLastMouse_ = mousePx;
            gizmoSuppressClick_ = true;
            // 升级20：拖拽起始即抓取场景快照（此刻对象尚未被本帧位移改写），作为属性编辑 before
            gizmoEditBefore_ = Snapshot();
            gizmoEditActive_ = true;
        }
    }

    // 拖拽中：把屏幕鼠标位移换算为世界平移/旋转增量
    if (gizmoDragging_ && gizmoDragAxis_ != Editor::GizmoAxis::None && leftDown)
    {
        Scene::SceneObject& obj = scene_[static_cast<size_t>(selectedObject_)];
        const glm::vec2 delta = mousePx - gizmoLastMouse_;
        if (gizmoMode_ == Editor::GizmoMode::Translate)
        {
            const float worldDelta =
                Editor::TranslateDragDelta(obj.position, gizmoDragAxis_, camViewProj, gizmoVp, delta);
            obj.position += Editor::GizmoAxisVector(gizmoDragAxis_) * worldDelta;
        }
        else // Rotate
        {
            const glm::vec2 center = Editor::ProjectWorldToScreen(obj.position, camViewProj, gizmoVp);
            const float ang = Editor::RotateDragAngle(center, gizmoLastMouse_, mousePx);
            if (gizmoDragAxis_ == Editor::GizmoAxis::X)
                obj.rotation.x += glm::degrees(ang);
            else if (gizmoDragAxis_ == Editor::GizmoAxis::Y)
                obj.rotation.y += glm::degrees(ang);
            else
                obj.rotation.z += glm::degrees(ang);
        }
        gizmoLastMouse_ = mousePx;
    }
}

// ECS 渲染收敛：单趟直读 ECS 渲染三元组（Transform/Renderable/Spin）完成视锥剔除 +
// 按 meshId 批次化 + 帧瞬态上传登记，替代旧的 UpdateVisibility（包剔除）与
// FillInstanceBuffers/FillMeshInstances/FillGltfPrimInstances（逐网格 O(网格×对象) 双重遍历）。
// 桶内实例序 = 稳定序（与旧包过滤序一致），剔除球参数与统计口径同旧实现，行为零变化。
void Application::UpdateRenderables()
{
    // 帧瞬态上传：清空上一帧登记（录制阶段已在首个 pass 内消费完毕）
    pendingUploads_.clear();
    instanceUploadStash_.clear();
    // 登记指针指向 instanceUploadStash_ 内部：先按上界预留，防遍历后逐桶 insert realloc 悬垂
    instanceUploadStash_.reserve(ecsScene_.ObjectCount() * (2 + gltfPrims_.size()));
    if (gltfPrimScratch_.size() != gltfPrimInstances_.size())
        gltfPrimScratch_.resize(gltfPrimInstances_.size());
    for (auto& bucket : gltfPrimScratch_)
        bucket.clear();
    cubeScratch_.clear();
    torusScratch_.clear();
    firstGltfModel_ = glm::mat4(1.0f);

    // 预计算常量包围球参数（同旧 UpdateVisibility 口径：hasTorus_/hasGltf_ 关闭时回退立方体球）
    const glm::mat4 camViewProj = camera_.Proj() * camera_.View();
    const Render::Frustum frustum = Render::Frustum::FromViewProj(camViewProj);
    const float cubeRadius = Scene::kCubeBoundingRadius * kCullMargin;
    const glm::vec3 torusCenterOffset = hasTorus_ ? torusMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float torusRadius = hasTorus_ ? torusMesh_.BoundingRadius() * kCullMargin : 0.0f;
    const glm::vec3 gltfCenterOffset = hasGltf_ ? gltfMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float gltfRadius = hasGltf_ ? gltfMesh_.BoundingRadius() * kCullMargin : 0.0f;

    uint32_t visibleCount = 0;
    bool gltfModelSet = false;
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            // 视锥剔除（每实体一次；球心/半径与旧实现逐项一致）
            const bool isTorus = (r.meshId == 1) && hasTorus_;
            const bool isGltf = (r.meshId == 2) && hasGltf_;
            const glm::vec3 centerOffset = isTorus ? torusCenterOffset : (isGltf ? gltfCenterOffset : glm::vec3(0.0f));
            const float boundsRadius = isTorus ? torusRadius : (isGltf ? gltfRadius : cubeRadius);
            const glm::vec3 center = t.position + t.scale * centerOffset;
            const float radius = t.scale * boundsRadius;
            if (!frustum.IntersectsSphere(center, radius))
                return;
            ++visibleCount;

            const glm::mat4 model = world; // 层级世界矩阵（无 Parent 时等价 ComputeEntityModelMatrix）
            Render::InstanceData d{};
            if (r.meshId == 0)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                cubeScratch_.push_back(d);
            }
            else if (r.meshId == 1)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                torusScratch_.push_back(d);
            }
            else if (r.meshId == 2 && hasGltf_)
            {
                // 首个可见 glTF 实体的模型矩阵：透明批次排序基准（等价旧 SortedGltfBlendPrims 扫描）
                if (!gltfModelSet)
                {
                    firstGltfModel_ = model;
                    gltfModelSet = true;
                }
                // 所有 primitive 批次共享同一实例集合：每 prim 各一条，材质因子逐 prim 取
                const glm::mat4 animModel = animationHost_.GltfOffset() * model;
                for (size_t p = 0; p < gltfPrims_.size(); ++p)
                {
                    const GltfPrimMaterial& pm = gltfPrims_[p];
                    d.model = animModel;
                    // tint = 编辑器色调 × glTF baseColorFactor（alpha 经 tint.w 随顶点色下传）
                    d.tint = glm::vec4(r.tint * glm::vec3(pm.baseColorFactor), pm.baseColorFactor.a);
                    d.metallic = pm.metallicFactor;
                    d.roughness = pm.roughnessFactor;
                    gltfPrimScratch_[p].push_back(d);
                }
            }
        });
    culledCount_ = static_cast<uint32_t>(ecsScene_.ObjectCount()) - visibleCount;

    // 逐桶登记上传（计数从桶大小取，空桶登记为 0 与旧 Fill 语义一致）
    cubeInstanceCount_ = static_cast<uint32_t>(cubeScratch_.size());
    AppendInstanceUpload(cubeInstances_, cubeScratch_);
    torusInstanceCount_ = static_cast<uint32_t>(torusScratch_.size());
    AppendInstanceUpload(torusInstances_, torusScratch_);
    for (size_t p = 0; p < gltfPrimScratch_.size(); ++p)
    {
        gltfPrimCounts_[p] = static_cast<uint32_t>(gltfPrimScratch_[p].size());
        AppendInstanceUpload(gltfPrimInstances_[p], gltfPrimScratch_[p]);
    }
    // 地面：实例数据恒定，已在初始化时一次性上传
}

void Application::AppendUpload(VkBuffer dst, const void* data, VkDeviceSize bytes)
{
    if (dst != VK_NULL_HANDLE && data != nullptr && bytes > 0)
        pendingUploads_.push_back({dst, data, bytes});
}

void Application::AppendInstanceUpload(Render::InstanceBuffer& buffer, const std::vector<Render::InstanceData>& data)
{
    if (data.empty())
        return;
    // 桶 scratch 会被下一帧 UpdateRenderables 覆写，先拷入本帧 stash 再登记（录制前稳定）
    const size_t offset = instanceUploadStash_.size();
    instanceUploadStash_.insert(instanceUploadStash_.end(), data.begin(), data.end());
    AppendUpload(buffer.Get(), instanceUploadStash_.data() + offset,
                 static_cast<VkDeviceSize>(data.size()) * sizeof(Render::InstanceData));
}

void Application::UpdateUniforms()
{
    // CSM 级联矩阵每帧计算一次：UpdateUniforms 刷新缓存，RecordPrePass 录制时消费
    cascadeMatrices_ = ComputeCascadeMatrices(cascadeSplits_);

    Render::PointShadowUBO pointShadowData{};
    FillPointShadowMatrices(pointShadowData);

    const glm::vec3 cameraForward = glm::normalize(camera_.Target() - camera_.Position());

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        Render::CameraUBO camData{};
        camData.view = camera_.View();
        camData.proj = camera_.Proj();
        cameraUbos_[i].Update(camData);

        Render::LightUBO lightData{};
        lightData.lightDir = lightParams_.direction;
        lightData.dirIntensity = lightParams_.intensity;
        lightData.lightColor = lightParams_.color;
        lightData.ambientFactor = lightParams_.ambient;
        lightData.cameraPos = camera_.Position();
        lightData.pointLightCount = static_cast<float>(pointLights_.size());
        lightData.shadowStrength = lightParams_.shadowStrength;
        lightData.shadowBias = lightParams_.shadowBias;
        lightData.iblStrength = lightParams_.iblStrength;
        lightData.exposure = lightParams_.exposure;
        for (uint32_t c = 0; c < Render::kMaxCascades; ++c)
            lightData.lightSpaceMatrices[c] = cascadeMatrices_[c];
        lightData.cascadeSplits = cascadeSplits_;
        lightData.cameraForward = glm::vec4(cameraForward, kShadowDrawDistance);
        for (uint32_t li = 0; li < Render::kMaxPointLights; ++li)
        {
            lightData.lights[li] = Render::GpuPointLight{};
            if (li < pointLights_.size())
            {
                lightData.lights[li].position = pointLights_[li].position;
                lightData.lights[li].intensity = pointLights_[li].intensity;
                lightData.lights[li].color = pointLights_[li].color;
                lightData.lights[li].radius = pointLights_[li].radius;
                lightData.lights[li].castsShadow = pointLights_[li].castsShadow ? 1.0f : 0.0f;
            }
        }
        lightUbos_[i].Update(lightData);
        pointShadowUbos_[i].Update(pointShadowData);
    }
}

void Application::UpdateFpsTitle()
{
    fpsTimer_ += deltaTime_;
    ++fpsFrames_;
    lastFrameMs_ = lastFrameMs_ * 0.9f + deltaTime_ * 1000.0f;
    if (fpsTimer_ >= 0.5)
    {
        lastFps_ = static_cast<uint32_t>(std::lround(fpsFrames_ / fpsTimer_));
        window_->SetTitle(baseTitle_ + "  |  FPS: " + std::to_string(lastFps_) + "  |  MSAA " +
                          std::to_string(static_cast<uint32_t>(renderer_.SampleCount())) + "x");
        fpsTimer_ = 0.0;
        fpsFrames_ = 0;
    }
}

void Application::HandlePicking()
{
    bool leftClicked = window_->ConsumeClick();
    bool rightClicked = window_->ConsumeRightClick();
    if (rightClicked)
        selectedObject_ = -1;
    if (gizmoSuppressClick_)
    {
        gizmoSuppressClick_ = false;
        leftClicked = false;
    }

    if ((leftClicked || rightClicked) && !ImGui::GetIO().WantCaptureMouse)
    {
        const auto [cx, cy] = window_->GetCursorPos();
        const auto [fw, fh] = window_->GetFramebufferSize();
        if (fh <= 0)
            return;

        const glm::mat4 invViewProj = glm::inverse(camera_.Proj() * camera_.View());
        const float ndcX = 2.0f * static_cast<float>(cx) / static_cast<float>(fw) - 1.0f;
        const float ndcY = 1.0f - 2.0f * static_cast<float>(cy) / static_cast<float>(fh);
        const glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) / farPoint.w - camera_.Position());
        const glm::vec3 rayOrigin = camera_.Position();

        // 物理射线检测（优先），未命中物理体时回退到 AABB 拾取（阶段 3f：引擎在 PhysicsHost）
        Physics::RaycastHit hit{};
        if (physicsHost_.enabled)
            hit = physicsHost_.engine.Raycast(rayOrigin, rayDir, 200.0f);

        if (leftClicked)
        {
            if (hit.hit && hit.userTag != UINT32_MAX && hit.userTag < scene_.size())
                selectedObject_ = static_cast<int>(hit.userTag);
            else
                selectedObject_ = Scene::PickObject(rayOrigin, rayDir, scene_);
        }
        else if (rightClicked && physicsHost_.enabled && hit.hit)
        {
            // 右键：在命中点上方生成一个动态立方体（物理交互 demo）
            const SceneSnapshot before = Snapshot();
            Scene::SceneObject ball;
            ball.position = hit.point + hit.normal * 0.6f;
            ball.scale = 0.4f;
            ball.tint = glm::vec3(1.0f, 0.8f, 0.3f);
            ball.spinSpeed = 0.0f;
            ball.phase = 0.0f;
            ball.meshId = 0;
            ball.metallic = 0.0f;
            ball.roughness = 0.6f;
            ball.rotation = glm::vec3(0.0f);
            ball.physicsType = Physics::BodyType::Dynamic;
            ball.physicsShape = Physics::ShapeType::Box;
            ball.physicsMass = 1.0f;
            ball.physicsFriction = 0.5f;
            ball.physicsRestitution = 0.3f;
            ecsScene_.CreateObject(ball);
            RepackScene();
            RecalculateTriangleCount();
            physicsHost_.RebuildBodies();
            const SceneSnapshot after = Snapshot();
            suppressEditGesture_ = true;
            commandStack_.Execute(std::make_unique<SceneSnapshotCommand>(this, before, after, "生成物理立方体"));
        }
    }
}

void Application::UpdateDeferredState()
{
    if (postProcessSync_.deferred != postProcessSync_.prevDeferred)
    {
        renderer_.SetDeferred(postProcessSync_.deferred);
        if (postProcessSync_.deferred)
            UpdateGBufferSets();
        postProcessSync_.prevDeferred = postProcessSync_.deferred;
    }
    if (postProcessSync_.postProcess != postProcessSync_.prevPostProcess)
    {
        renderer_.SetPostProcessing(postProcessSync_.postProcess);
        postProcessSync_.prevPostProcess = postProcessSync_.postProcess;
    }
    if (postProcessSync_.ssao != postProcessSync_.prevSsao)
    {
        renderer_.SetSSAO(postProcessSync_.ssao);
        postProcessSync_.prevSsao = postProcessSync_.ssao;
    }
    if (postProcessSync_.ssr != postProcessSync_.prevSsr)
    {
        renderer_.SetSSR(postProcessSync_.ssr);
        postProcessSync_.prevSsr = postProcessSync_.ssr;
    }
}

void Application::RecalculateTriangleCount()
{
    triangleCount_ = Scene::kCubeIndexCount / 3 * static_cast<uint32_t>(scene_.size()) + Scene::kGroundIndexCount / 3;
    if (hasTorus_)
    {
        uint32_t torusCount = 0;
        for (const auto& obj : scene_)
            if (obj.meshId == 1)
                ++torusCount;
        triangleCount_ += torusMesh_.IndexCount() / 3 * torusCount;
    }
    if (hasGltf_)
    {
        uint32_t gltfCount = 0;
        for (const auto& obj : scene_)
            if (obj.meshId == 2)
                ++gltfCount;
        triangleCount_ += gltfMesh_.IndexCount() / 3 * gltfCount;
    }
}

// ========================================================================
// 物理系统（阶段 3f：移入 PhysicsHost 子系统，见 app/systems/PhysicsHost.cpp）
// ========================================================================

// ========================================================================
// 动画状态机（阶段 3c：移入 AnimationHost 子系统，见 app/systems/AnimationHost.cpp）
// ========================================================================

// ========================================================================
// 场景序列化（阶段 3b：移入 SceneIoHost 子系统，见 app/systems/SceneIoHost.cpp）
// ========================================================================

// 渲染录制回调（Record*）与阴影批次绘制：已拆至 Application_Record.cpp
// 管线重建 / GBuffer 描述符更新：已拆至 Application_Pipelines.cpp

// ========================================================================
// 辅助
// ========================================================================

std::array<glm::mat4, Render::kMaxCascades> Application::ComputeCascadeMatrices(glm::vec4& outSplits) const
{
    // ---- 1. 实用分割法（Practical Split Scheme）：对数/均匀插值按 λ 混合，
    //      分割边界为轴向视图深度（dot(相机前向, p-相机位置)），覆盖距离截断到阴影绘制距离
    float edges[Render::kMaxCascades + 1];
    const float nearZ = camera_.nearZ_;
    const float farZ = std::min(camera_.farZ_, kShadowDrawDistance);
    edges[0] = nearZ;
    edges[Render::kMaxCascades] = farZ;
    for (uint32_t i = 1; i < Render::kMaxCascades; ++i)
    {
        const float d = static_cast<float>(i) / static_cast<float>(Render::kMaxCascades);
        const float logSplit = nearZ * std::pow(farZ / nearZ, d);
        const float uniSplit = nearZ + (farZ - nearZ) * d;
        edges[i] = glm::mix(uniSplit, logSplit, kCascadeSplitLambda);
    }
    for (uint32_t c = 0; c < Render::kMaxCascades; ++c)
        outSplits[c] = edges[c + 1];

    // ---- 2. 光源视图：与原方向光路径一致的方向基（lookAt 原点沿光传播方向），平移无关（正交投影吸收）
    const glm::vec3 lightDir = glm::normalize(lightParams_.direction);
    const glm::vec3 lightUp =
        (std::fabs(lightParams_.direction.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), lightDir, lightUp);

    const glm::mat4 invViewProj = glm::inverse(camera_.Proj() * camera_.View());
    const float tileTexels = static_cast<float>(shadowMap_.CascadeTileSize());

    std::array<glm::mat4, Render::kMaxCascades> matrices{};
    for (uint32_t c = 0; c < Render::kMaxCascades; ++c)
    {
        // ---- 3a. 深度切片 8 角点（NDC z∈[0,1]）反投影到世界，再变换到光视空间求 AABB
        glm::vec3 minP(std::numeric_limits<float>::max());
        glm::vec3 maxP(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec4 cornerNdc((corner & 4) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f,
                                      (corner & 1) ? 1.0f : 0.0f, 1.0f);
            const glm::vec4 worldW = invViewProj * cornerNdc;
            const glm::vec3 world = glm::vec3(worldW) / worldW.w;
            const glm::vec3 lp = glm::vec3(lightView * glm::vec4(world, 1.0f));
            minP = glm::min(minP, lp);
            maxP = glm::max(maxP, lp);
        }

        // ---- 3b. Z 向外扩：切片外的高物/近物投影进本级联深度范围
        minP.z -= kCascadeZPad;
        maxP.z += kCascadeZPad;

        // ---- 3c. XY 覆盖取方形 + 纹素对齐（平移稳定，抑制相机运动时的阴影边缘闪烁）
        const glm::vec2 extent(maxP.x - minP.x, maxP.y - minP.y);
        const float side = std::max(extent.x, extent.y);
        const float worldPerTexel = std::max(side / tileTexels, 1e-4f);
        glm::vec2 center(minP.x + extent.x * 0.5f, minP.y + extent.y * 0.5f);
        center = glm::floor(center / worldPerTexel + 0.5f) * worldPerTexel;
        const float half = worldPerTexel * tileTexels * 0.5f;

        // 光视空间中可见几何 z<0：ortho near/far = -max.z / -min.z（GLM 零到一深度）
        matrices[c] = glm::ortho(center.x - half, center.x + half, center.y - half, center.y + half, -maxP.z, -minP.z) *
                      lightView;
    }
    return matrices;
}

void Application::FillPointShadowMatrices(Render::PointShadowUBO& out) const
{
    const glm::vec3 shadowLightPos = GetActiveShadowLight(pointLights_);
    const std::array<glm::vec3, 6> faceCenters = {glm::vec3(1, 0, 0),  glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                                  glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),  glm::vec3(0, 0, -1)};
    const std::array<glm::vec3, 6> faceUps = {glm::vec3(0, -1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1),
                                              glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), glm::vec3(0, -1, 0)};
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear, kPointShadowFar);
    for (int f = 0; f < 6; ++f)
    {
        // Vulkan Y 翻转，保持与主相机一致的 NDC 约定
        glm::mat4 viewProj = proj * glm::lookAt(shadowLightPos, shadowLightPos + faceCenters[f], faceUps[f]);
        viewProj[1][1] *= -1.0f;
        out.faceMatrices[f] = viewProj;
    }
}

// 阴影批次绘制（DrawShadowCasters / DrawCubeShadowCasters）：已拆至 Application_Record.cpp

glm::vec3 Application::GetActiveShadowLight(const std::vector<PointLightParams>& lights)
{
    for (const PointLightParams& pl : lights)
        if (pl.castsShadow)
            return pl.position;
    return glm::vec3(0.0f);
}
} // namespace BigHero
