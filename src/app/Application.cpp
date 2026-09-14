#include "app/Application.h"

#include "core/Time.h"
#include "core/VkCheck.h"
#include "scene/GltfLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

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

Application::Application()
    : Application(AppConfig{}) // 委托构造，避免重复初始化逻辑
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
        const std::vector<std::string> requiredShaders = {
            "shaders/vert.spv", "shaders/frag.spv",
            "shaders/shadow.vert.spv", "shaders/shadow.frag.spv",
            "shaders/shadow_cube.vert.spv", "shaders/shadow_cube.frag.spv",
            "shaders/skybox.vert.spv", "shaders/skybox.frag.spv",
            "shaders/particle.vert.spv", "shaders/particle.frag.spv",
            "shaders/deferred_light.vert.spv", "shaders/deferred_light.frag.spv",
            "shaders/gbuffer.frag.spv",
            "shaders/pp_bright.frag.spv", "shaders/pp_blur.frag.spv",
            "shaders/pp_composite.frag.spv",
            "shaders/pp_depth_linearize.frag.spv", "shaders/pp_dof.frag.spv",
            "shaders/pp_motion_blur.frag.spv",
            "shaders/ssao.frag.spv", "shaders/ssr_ray.frag.spv", "shaders/ssr_blur.frag.spv",
            "shaders/irradiance.frag.spv", "shaders/prefilter.frag.spv", "shaders/brdf_lut.frag.spv"
        };

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
                UpdateVisibility();
                FillInstanceBuffers();
                particleHost_.Update(deltaTime_, ctx_);
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
            const bool ctrlDown = window_->IsKeyDown(Window::kKeyLeftControl) || window_->IsKeyDown(Window::kKeyRightControl);
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

void Application::RegisterMeshAsset(const std::string& name, const std::string& path,
                                    const std::vector<Scene::Vertex>& verts,
                                    const std::vector<uint32_t>& indices,
                                    bighero::AssetMetadata::LoadState state, uint64_t fileSize)
{
    bighero::AssetRegistry::Entry e;
    e.name = name;
    e.path = path;
    e.type = static_cast<uint32_t>(bighero::AssetMetadata::AssetType::Mesh);
    e.state = static_cast<uint32_t>(state);
    e.size = fileSize;
    assetRegistry_.Add(e);

    bighero::MeshResource res(name, static_cast<uint32_t>(verts.size()),
                              static_cast<uint32_t>(indices.size()));
    res.SetPrimitiveCount(static_cast<uint32_t>(indices.size() / 3));
    if (!verts.empty())
    {
        glm::vec3 lo = verts[0].pos, hi = verts[0].pos;
        for (const Scene::Vertex& v : verts)
        {
            lo = glm::min(lo, v.pos);
            hi = glm::max(hi, v.pos);
        }
        res.SetBounds(lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
    }
    meshResources_[name] = std::move(res);
}

const bighero::MeshResource* Application::FindMeshResource(const std::string& name) const
{
    const auto it = meshResources_.find(name);
    return it != meshResources_.end() ? &it->second : nullptr;
}

void Application::RegisterTextureAsset(const std::string& name, const std::string& path,
                                       bighero::AssetMetadata::LoadState state, uint64_t fileSize)
{
    bighero::AssetRegistry::Entry e;
    e.name = name;
    e.path = path;
    e.type = static_cast<uint32_t>(bighero::AssetMetadata::AssetType::Texture);
    e.state = static_cast<uint32_t>(state);
    e.size = fileSize;
    assetRegistry_.Add(e);
}

uint32_t Application::LoadTextureSlot(const std::string& baseDir, const std::string& uri, bool sRGB,
                                      uint32_t fallbackSlot, const std::string& debugName)
{
    if (uri.empty() || nextTextureSlot_ >= Render::kObjectTextureSlots)
        return fallbackSlot;

    const std::filesystem::path p = std::filesystem::path(baseDir) / uri;
    if (!std::filesystem::exists(p))
    {
        LOG_WARN("glTF 贴图未找到: " << p.string() << "（" << debugName << " 使用回退槽 " << fallbackSlot << "）");
        RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Failed, 0);
        return fallbackSlot;
    }

    // 经 AssetManager 缓存工厂加载（键名含 normal/_mr 按线性，其余 sRGB）；sRGB 参数保留语义校验用
    auto tex = assetManager_.Load<Texture>(p.string());
    const auto fileSize = static_cast<uint64_t>(std::filesystem::file_size(p));
    if (!tex)
    {
        LOG_WARN("glTF 贴图加载失败: " << p.string() << "（" << debugName << " 使用回退槽 " << fallbackSlot << "）");
        RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Failed, fileSize);
        return fallbackSlot;
    }

    texturePool_[nextTextureSlot_] = std::move(tex);
    RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Loaded, fileSize);
    LOG_INFO("glTF 贴图入池: 槽" << nextTextureSlot_ << " <- " << p.string());
    return nextTextureSlot_++;
}

void Application::UpdateObjectTextureDescriptors()
{
    using RDS = Render::FrameDescriptorSet;
    std::vector<VkDescriptorImageInfo> infos(Render::kObjectTextureSlots);
    for (uint32_t s = 0; s < Render::kObjectTextureSlots; ++s)
    {
        const Texture* tex = texturePool_[s].get();
        if (!tex || !tex->IsValid())
            tex = texture_.get();
        infos[s].sampler = tex->Sampler();
        infos[s].imageView = tex->View();
        infos[s].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    for (uint32_t fi = 0; fi < kFrameCount; ++fi)
        descManager_.UpdateSetImageArray(FrameSetIndex(fi, RDS::Light), 9, infos.data(), Render::kObjectTextureSlots);
}

void Application::LoadGltfAsset(uint32_t maxInstances)
{
    if (!std::filesystem::exists(kGltfModelPath))
    {
        LOG_INFO("未找到 " << kGltfModelPath << "，场景不含 glTF 模型");
        return;
    }

    try
    {
        gltfModel_ = Scene::LoadGltf(kGltfModelPath);
        Scene::GltfModel& gltf = gltfModel_;
        gltfMesh_.Create(ctx_, gltf.vertices, gltf.indices);

        RegisterMeshAsset("gltf:model", kGltfModelPath, gltf.vertices, gltf.indices,
                          bighero::AssetMetadata::LoadState::Loaded,
                          static_cast<uint64_t>(std::filesystem::file_size(kGltfModelPath)));

        // ---- 逐 primitive 材质解析：贴图 URI 相对 glTF 文件目录解引用，入纹理池 ----
        const std::string baseDir = std::filesystem::path(kGltfModelPath).parent_path().string();
        gltfPrims_.reserve(gltf.primitives.size());
        for (const Scene::GltfPrimitive& p : gltf.primitives)
        {
            GltfPrimMaterial pm;
            pm.firstIndex = p.firstIndex;
            pm.indexCount = p.indexCount;
            if (p.materialIndex >= 0 && p.materialIndex < static_cast<int32_t>(gltf.materials.size()))
            {
                const Scene::GltfMaterial& m = gltf.materials[static_cast<size_t>(p.materialIndex)];
                pm.baseColorFactor = m.baseColorFactor;
                pm.metallicFactor = m.metallicFactor;
                pm.roughnessFactor = m.roughnessFactor;
                // 无贴图回退：反照率/mr→纯白(2) 因子透传；法线→平坦(1)
                pm.texSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.baseColorTextureUri, true, 2, "gltf:baseColor#" + m.name));
                pm.normalSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.normalTextureUri, false, 1, "gltf:normal#" + m.name));
                pm.mrSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.metallicRoughnessTextureUri, false, 2, "gltf:mr#" + m.name));
                // 透明/自发光（glTF 2.0 core）：自发光贴图线性色彩空间，无 URI 保持 -1（仅因子生效）
                pm.alphaMode = m.alphaMode;
                pm.alphaCutoff = m.alphaCutoff;
                pm.emissiveFactor = m.emissiveFactor;
                if (!m.emissiveTextureUri.empty())
                    pm.emissiveSlot = static_cast<int32_t>(
                        LoadTextureSlot(baseDir, m.emissiveTextureUri, false, 2, "gltf:emissive#" + m.name));
            }

            // 批次包围盒中心（网格空间）：透明批次按相机深度排序用
            glm::vec3 bmin(1e9f), bmax(-1e9f);
            for (uint32_t i = 0; i < p.indexCount; ++i)
            {
                const glm::vec3 pos = gltf.vertices[gltf.indices[p.firstIndex + i]].pos;
                bmin = glm::min(bmin, pos);
                bmax = glm::max(bmax, pos);
            }
            pm.boundsCenter = (p.indexCount > 0) ? (bmin + bmax) * 0.5f : glm::vec3(0.0f);
            gltfPrims_.push_back(pm);
        }

        // ---- 逐 primitive 实例缓冲（每材质一次 DrawIndexedInstanced） ----
        gltfPrimInstances_.resize(gltfPrims_.size());
        gltfPrimCounts_.assign(gltfPrims_.size(), 0);
        for (Render::InstanceBuffer& ib : gltfPrimInstances_)
            ib.Create(ctx_, maxInstances);

        hasGltf_ = true;
        LOG_INFO("glTF 模型加载成功: " << gltfPrims_.size() << " 个材质批次 / " << gltf.vertices.size() << " 顶点 / "
                                       << gltf.indices.size() / 3 << " 三角形");

        // ---- 动画绑定（升级 24）：模型常驻 + 状态机绑定模型 + 状态绑到实际动画下标 ----
        if (!gltf.animations.empty())
        {
            animationHost_.Init();
            Scene::AnimationStateMachine& sm = animationHost_.StateMachine();
            sm.BindModel(&gltfModel_);
            const int animCount = static_cast<int>(gltf.animations.size());
            for (size_t i = 0; i < sm.StateCount(); ++i)
                sm.SetStateAnimation(static_cast<int>(i), static_cast<int>(i) % animCount);
            LOG_INFO("glTF 动画绑定: " << animCount << " 条动画 -> 状态机状态（前 "
                                      << std::min<size_t>(sm.StateCount(), gltf.animations.size()) << " 个状态）");
        }
    }
    catch (const std::exception& e)
    {
        hasGltf_ = false;
        gltfModel_ = Scene::GltfModel{};
        gltfPrims_.clear();
        gltfPrimInstances_.clear();
        gltfPrimCounts_.clear();
        LOG_ERROR("glTF 模型加载失败: " << e.what());
        RegisterMeshAsset("gltf:model", kGltfModelPath, {}, {}, bighero::AssetMetadata::LoadState::Failed,
                          std::filesystem::exists(kGltfModelPath)
                              ? static_cast<uint64_t>(std::filesystem::file_size(kGltfModelPath))
                              : 0);
    }
}

uint32_t Application::FillGltfPrimInstances(size_t primIndex, Render::InstanceBuffer& buffer)
{
    if (primIndex >= gltfPrims_.size())
        return 0;
    const GltfPrimMaterial& pm = gltfPrims_[primIndex];

    instanceScratch_.clear();
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        const Scene::SceneObject& obj = scene_[i];
        if (obj.meshId != 2 || visible_[i] == 0)
            continue;
        Render::InstanceData d{};
        d.model = animationHost_.GltfOffset() * Scene::ComputeObjectModelMatrix(obj, spinAngles_[i]);
        // tint = 编辑器色调 × glTF baseColorFactor（alpha 经 tint.w 随顶点色下传）
        d.tint = glm::vec4(obj.tint * glm::vec3(pm.baseColorFactor), pm.baseColorFactor.a);
        d.metallic = pm.metallicFactor;
        d.roughness = pm.roughnessFactor;
        instanceScratch_.push_back(d);
    }
    buffer.Upload(ctx_, instanceScratch_.data(), static_cast<uint32_t>(instanceScratch_.size()));
    return static_cast<uint32_t>(instanceScratch_.size());
}

// ========================================================================
// glTF 透明/自发光录制辅助
// ========================================================================

Application::PushObject Application::MakeGltfPush(const GltfPrimMaterial& pm, int mode) const
{
    PushObject po;
    po.texIndex = pm.texSlot;
    po.normalIndex = pm.normalSlot;
    po.mrIndex = pm.mrSlot;
    po.emissiveIndex = pm.emissiveSlot;
    po.emissiveFactor = pm.emissiveFactor;
    // 仅 MASK 模式下传阈值（其余模式不裁剪）
    po.alphaCutoff = (pm.alphaMode == 1) ? pm.alphaCutoff : 0.0f;
    po.mode = mode;
    return po;
}

std::vector<uint32_t> Application::SortedGltfBlendPrims() const
{
    std::vector<uint32_t> order;
    if (!hasGltf_)
        return order;
    for (uint32_t p = 0; p < gltfPrims_.size(); ++p)
        if (gltfPrims_[p].alphaMode == 2 && gltfPrimCounts_[p] > 0)
            order.push_back(p);
    if (order.size() < 2)
        return order;

    // 所有 primitive 批次共享同一实例集合：取第一个 glTF 实例的模型矩阵，
    // 把批次包围中心变换到世界空间后按相机距离从远到近排序（批次级工程折衷）
    glm::mat4 model(1.0f);
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (scene_[i].meshId == 2 && visible_[i] != 0)
        {
            model = Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]);
            break;
        }
    }
    const glm::vec3 camPos = camera_.Position();
    std::sort(order.begin(), order.end(),
              [this, &model, &camPos](uint32_t a, uint32_t b)
              {
                  const glm::vec3 wa = glm::vec3(model * glm::vec4(gltfPrims_[a].boundsCenter, 1.0f));
                  const glm::vec3 wb = glm::vec3(model * glm::vec4(gltfPrims_[b].boundsCenter, 1.0f));
                  return glm::distance(wa, camPos) > glm::distance(wb, camPos);
              });
    return order;
}

void Application::DrawGltfPrims(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int filter)
{
    if (!hasGltf_)
        return;

    // 批次顺序：BLEND 按相机深度远到近，其余保持 natural 顺序
    std::vector<uint32_t> order;
    if (filter == 2)
    {
        order = SortedGltfBlendPrims();
    }
    else
    {
        order.resize(gltfPrims_.size());
        for (uint32_t p = 0; p < gltfPrims_.size(); ++p)
            order[p] = p;
    }

    gltfMesh_.Bind(cmd);
    for (const uint32_t p : order)
    {
        const GltfPrimMaterial& pm = gltfPrims_[p];
        if (gltfPrimCounts_[p] == 0)
            continue;
        // filter：0=OPAQUE+MASK 2=BLEND 3=EMISSIVE_ONLY（有自发光的非 BLEND 批次）
        if (filter == 0 && pm.alphaMode > 1)
            continue;
        if (filter == 2 && pm.alphaMode != 2)
            continue;
        if (filter == 3)
        {
            const bool hasEmissive =
                pm.emissiveSlot >= 0 || glm::any(glm::greaterThan(pm.emissiveFactor, glm::vec3(0.0f)));
            if (pm.alphaMode == 2 || !hasEmissive)
                continue; // BLEND 批次随 mode2 全量着色（含自发光），勿重复叠加
        }

        gltfPrimInstances_[p].Bind(cmd);
        const PushObject po = MakeGltfPush(pm, (filter == 3) ? 3 : pm.alphaMode);
        vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject), &po);
        gltfMesh_.DrawIndexedInstanced(cmd, pm.indexCount, pm.firstIndex, gltfPrimCounts_[p]);
    }
}

void Application::InitResources()
{
    // ---- 阴影与环境光 ----
    shadowMap_.Create(ctx_);
    cubeShadowMap_.Create(ctx_, 1024);
    envLighting_.Create(ctx_);
    LOG_INFO("[DBG] envLighting 完成");

    // ---- 描述符与每帧 UBO（双帧并行，各自独立缓冲与描述符集） ----
    descManager_.Init(ctx_.Device());
    descManager_.AllocateSets(Renderer::MaxFramesInFlight());
    LOG_INFO("[DBG] descManager 完成");

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
    LOG_INFO("[DBG] UBO 完成");

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
    LOG_INFO("[DBG] 主纹理 完成");

    normalTexture_ = assetManager_.Load<Texture>(kNormalMapPath);
    if (!normalTexture_)
    {
        LOG_WARN("未找到 " << kNormalMapPath << "，使用平坦法线");
        normalTexture_ = assetManager_.Load<Texture>("flat_normal");
    }
    LOG_INFO("[DBG] 法线纹理 完成");

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
                                    VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 4, envLighting_.EnvView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 5, envLighting_.IrradianceView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 6, envLighting_.PrefilteredView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 7, envLighting_.BrdfLutView(),
                                    envLighting_.Sampler());
        descManager_.UpdateSetImage(Render::FrameSetIndex(i, RDS::Light), 8, cubeShadowMap_.View(),
                                    cubeShadowMap_.Sampler(), VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
        descManager_.UpdateSet(Render::FrameSetIndex(i, RDS::PointShadow), 0, pointShadowUbos_[i]);
    }

    // ---- 逐物体纹理池描述符（set1 binding9 数组，glTF 贴图加载后会再刷新） ----
    UpdateObjectTextureDescriptors();
    LOG_INFO("[DBG] 物体纹理描述符 完成");

    // ---- 延迟渲染：GBuffer 输入附件描述符集（每交换链图像一组） ----
    descManager_.AllocateGBufferSets(renderer_.GetSwapchain().ImageCount());
    LOG_INFO("[DBG] GBuffer 描述符 完成");

    // ---- 场景几何：立方体+地面组合网格 ----
    const std::vector<Scene::Vertex> vertices = Scene::BuildSceneVertices();
    const std::vector<uint32_t> indices = Scene::BuildSceneIndices();
    sceneMesh_.Create(ctx_, vertices, indices);
    LOG_INFO("[DBG] sceneMesh 完成");
    RegisterMeshAsset("builtin:scene", "<procedural>", vertices, indices,
                      bighero::AssetMetadata::LoadState::Loaded, 0);

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
        RegisterMeshAsset("torus", kTorusModelPath, {}, {},
                          bighero::AssetMetadata::LoadState::Failed, 0);
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

void Application::CreatePipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass mainPass = renderer_.GetRenderPass();
    const VkRenderPass deferredPass = renderer_.GetDeferredRenderPass();
    // 注意：光照管线必须使用独立的 lightingRenderPass_（subpass 0）。
    // deferredRenderPass_ 只有 1 个 subpass，以 subpass=1 创建管线是越界未定义行为（驱动段错误）
    const VkRenderPass lightingPass = renderer_.GetLightingRenderPass();

    const VkVertexInputBindingDescription vertexBinding = Scene::Vertex::getBindingDesc();
    const std::vector<VkVertexInputAttributeDescription> vertexAttributes = Scene::Vertex::getAttrDesc();
    const VkVertexInputBindingDescription instanceBinding = Render::InstanceBuffer::GetBindingDesc();
    const std::vector<VkVertexInputAttributeDescription> instanceAttributes = Render::InstanceBuffer::GetAttrDesc();

    auto mergedAttrs = [&]()
    {
        std::vector<VkVertexInputAttributeDescription> attrs = vertexAttributes;
        attrs.insert(attrs.end(), instanceAttributes.begin(), instanceAttributes.end());
        return attrs;
    };

    // ---- 主场景前向管线 ----
    {
        Render::ShaderModuleHandle vert(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle frag(dev, Render::ReadShaderFile(kFragSpvPath));
        pipelineConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        pipelineConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        pipelineConfig_.vertexBindings = {vertexBinding, instanceBinding};
        pipelineConfig_.vertexAttributes = mergedAttrs();
        pipelineConfig_.rasterSamples = renderer_.SampleCount();
        pipeline_.emplace(dev, mainPass, std::move(vert), std::move(frag), pipelineConfig_);
    }

    // ---- 方向光阴影深度管线 ----
    {
        Render::ShaderModuleHandle sv(dev, Render::ReadShaderFile("shaders/shadow.vert.spv"));
        Render::ShaderModuleHandle sf(dev, Render::ReadShaderFile("shaders/shadow.frag.spv"));
        shadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushShadow)}};
        shadowConfig_.vertexBindings = {vertexBinding};
        shadowConfig_.vertexAttributes = vertexAttributes;
        shadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
        shadowConfig_.depthOnly = true;
        shadowPipeline_.emplace(dev, shadowMap_.GetRenderPass(), std::move(sv), std::move(sf), shadowConfig_);
    }

    // ---- 点光源立方体阴影深度管线 ----
    {
        Render::ShaderModuleHandle cv(dev, Render::ReadShaderFile("shaders/shadow_cube.vert.spv"));
        Render::ShaderModuleHandle cf(dev, Render::ReadShaderFile("shaders/shadow_cube.frag.spv"));
        cubeShadowConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight,
                                        descManager_.layoutCubeShadow}; // 着色器在 set=2 访问 PointShadowUBO
        cubeShadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow)}};
        cubeShadowConfig_.vertexBindings = {vertexBinding};
        cubeShadowConfig_.vertexAttributes = vertexAttributes;
        cubeShadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
        cubeShadowConfig_.depthOnly = true;
        cubeShadowPipeline_.emplace(dev, cubeShadowMap_.GetRenderPass(), std::move(cv), std::move(cf),
                                    cubeShadowConfig_);
    }

    // ---- 天空盒管线 ----
    {
        Render::ShaderModuleHandle kv(dev, Render::ReadShaderFile("shaders/skybox.vert.spv"));
        Render::ShaderModuleHandle kf(dev, Render::ReadShaderFile("shaders/skybox.frag.spv"));
        skyboxConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        skyboxConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushSky)}};
        skyboxConfig_.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        skyboxConfig_.depthWrite = false;
        skyboxConfig_.cullMode = VK_CULL_MODE_NONE;
        skyboxConfig_.rasterSamples = renderer_.SampleCount();
        skyboxPipeline_.emplace(dev, mainPass, std::move(kv), std::move(kf), skyboxConfig_);
    }

    // ---- 延迟渲染：GBuffer 几何管线（MRT 写 3 张） ----
    {
        Render::ShaderModuleHandle gv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle gf(dev, Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
        gbufferConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        gbufferConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        gbufferConfig_.vertexBindings = {vertexBinding, instanceBinding};
        gbufferConfig_.vertexAttributes = mergedAttrs();
        gbufferConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        gbufferConfig_.colorAttachmentCount = 3;
        gbufferConfig_.subpass = 0;
        gbufferConfig_.depthTest = true;
        gbufferConfig_.depthWrite = true;
        gbufferPipeline_.emplace(dev, deferredPass, std::move(gv), std::move(gf), gbufferConfig_);
    }

    // ---- 延迟渲染：全屏延迟光照管线（输入附件） ----
    {
        Render::ShaderModuleHandle lv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
        Render::ShaderModuleHandle lf(dev, Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
        defLightConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight,
                                      descManager_.layoutGBufferInput, descManager_.layoutAO};
        defLightConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
        defLightConfig_.vertexBindings = {};
        defLightConfig_.vertexAttributes = {};
        defLightConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        defLightConfig_.colorAttachmentCount = 1;
        defLightConfig_.subpass = 0; // lightingRenderPass_ 仅有一个 subpass
        defLightConfig_.depthTest = false;
        defLightConfig_.depthWrite = false;
        lightingPipeline_.emplace(dev, lightingPass, std::move(lv), std::move(lf), defLightConfig_);
    }

    // ---- glTF 透明（BLEND）前向管线：标准 Alpha 混合、不写深度（与主管线同 pass） ----
    {
        Render::ShaderModuleHandle bv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle bf(dev, Render::ReadShaderFile(kFragSpvPath));
        gltfBlendConfig_ = pipelineConfig_; // 复制主场景配置（顶点输入/布局/采样数一致）
        gltfBlendConfig_.depthWrite = false;
        gltfBlendConfig_.blendEnable = true;
        gltfBlendPipeline_.emplace(dev, mainPass, std::move(bv), std::move(bf), gltfBlendConfig_);
    }

    // ---- 延迟透明叠加管线（transparentRenderPass_）：BLEND 标准混合，深度只读测试 ----
    {
        const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
        Render::ShaderModuleHandle tv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle tf(dev, Render::ReadShaderFile(kFragSpvPath));
        transBlendConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        transBlendConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        transBlendConfig_.vertexBindings = {vertexBinding, instanceBinding};
        transBlendConfig_.vertexAttributes = mergedAttrs();
        transBlendConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        transBlendConfig_.colorAttachmentCount = 1;
        transBlendConfig_.depthTest = true;
        transBlendConfig_.depthWrite = false;
        transBlendConfig_.blendEnable = true;
        transBlendPipeline_.emplace(dev, transparentPass, std::move(tv), std::move(tf), transBlendConfig_);
    }

    // ---- 延迟加性自发光管线（transparentRenderPass_）：ONE/ONE 加性，补写光照 Pass 无法感知的自发光 ----
    {
        const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
        Render::ShaderModuleHandle ev(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle ef(dev, Render::ReadShaderFile(kFragSpvPath));
        transEmissiveConfig_ = transBlendConfig_; // 与 BLEND 管线同布局/顶点输入/深度状态
        transEmissiveConfig_.blendSrcColor = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendDstColor = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendSrcAlpha = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendDstAlpha = VK_BLEND_FACTOR_ONE;
        transEmissivePipeline_.emplace(dev, transparentPass, std::move(ev), std::move(ef), transEmissiveConfig_);
    }

    // ---- 粒子实例化公告板管线（前向-only，Alpha 混合，不写深度；阶段 3e 移入 ParticleHost） ----
    particleHost_.CreatePipeline(dev, mainPass, renderer_.SampleCount());
}

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

    editorOverlay_.Init(ctx_, *window_, renderer_.GetSwapchain());
    renderer_.SetResizeCallback(
        [this]()
        {
            editorOverlay_.RecreateFramebuffers(renderer_.GetSwapchain());
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
    s.visibility = visible_;
    return s;
}

void Application::RestoreScene(const Game::SceneSnapshot& snap)
{
    // ECS 全量重建（物体与自转角一并恢复），包与可见性并行数组随之重投影
    ecsScene_.LoadPacket(snap.objects, &snap.spins);
    scene_ = snap.objects;
    spinAngles_ = snap.spins;
    visible_ = snap.visibility;
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

// ECS 场景实体化：ECS -> 包投影。scene_/spinAngles_ 为渲染、拾取、编辑器、序列化、
// 撤销快照等既有路径的兼容层；visible_ 与包同长（新物体默认可见，剔除系统每帧覆写）。
void Application::RepackScene()
{
    scene_ = ecsScene_.BuildPacket(&spinAngles_);
    if (visible_.size() != scene_.size())
        visible_.resize(scene_.size(), 1);
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

void Application::UpdateVisibility()
{
    const glm::mat4 camViewProj = camera_.Proj() * camera_.View();
    const Render::Frustum frustum = Render::Frustum::FromViewProj(camViewProj);

    // 预计算常量包围球参数（立方体中心在原点，圆环体中心偏移固定，避免每物体重复查询）
    const float cubeRadius = Scene::kCubeBoundingRadius * kCullMargin;
    const glm::vec3 torusCenterOffset = hasTorus_ ? torusMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float torusRadius = hasTorus_ ? torusMesh_.BoundingRadius() * kCullMargin : 0.0f;
    const glm::vec3 gltfCenterOffset = hasGltf_ ? gltfMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float gltfRadius = hasGltf_ ? gltfMesh_.BoundingRadius() * kCullMargin : 0.0f;

    visibleCount_ = 0;
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        const Scene::SceneObject& obj = scene_[i];
        const bool isTorus = (obj.meshId == 1) && hasTorus_;
        const bool isGltf = (obj.meshId == 2) && hasGltf_;
        const glm::vec3 centerOffset = isTorus ? torusCenterOffset : (isGltf ? gltfCenterOffset : glm::vec3(0.0f));
        const float boundsRadius = isTorus ? torusRadius : (isGltf ? gltfRadius : cubeRadius);
        const glm::vec3 center = obj.position + obj.scale * centerOffset;
        const float radius = obj.scale * boundsRadius;
        visible_[i] = frustum.IntersectsSphere(center, radius) ? 1 : 0;
        if (visible_[i])
            ++visibleCount_;
    }
    culledCount_ = static_cast<uint32_t>(scene_.size()) - visibleCount_;
}

void Application::FillInstanceBuffers()
{
    cubeInstanceCount_ = FillMeshInstances(0, cubeInstances_);
    torusInstanceCount_ = FillMeshInstances(1, torusInstances_);
    for (size_t p = 0; p < gltfPrimInstances_.size(); ++p)
        gltfPrimCounts_[p] = FillGltfPrimInstances(p, gltfPrimInstances_[p]);

    // 地面：恒等模型，单个实例（哑光电介质材质，始终绘制）
    Render::InstanceData ground{};
    ground.tint = glm::vec4(1.0f);
    ground.metallic = 0.0f;
    ground.roughness = 0.9f;
    groundInstances_.Upload(ctx_, &ground, 1);
}

uint32_t Application::FillMeshInstances(uint32_t meshId, Render::InstanceBuffer& buffer)
{
    instanceScratch_.clear();
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        const Scene::SceneObject& obj = scene_[i];
        if (obj.meshId != meshId || visible_[i] == 0)
            continue;
        Render::InstanceData d{};
        d.model = Scene::ComputeObjectModelMatrix(obj, spinAngles_[i]);
        d.tint = glm::vec4(obj.tint, 1.0f);
        d.metallic = obj.metallic;
        d.roughness = obj.roughness;
        instanceScratch_.push_back(d);
    }
    buffer.Upload(ctx_, instanceScratch_.data(), static_cast<uint32_t>(instanceScratch_.size()));
    return static_cast<uint32_t>(instanceScratch_.size());
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

// ========================================================================
// 录制回调
// ========================================================================

void Application::RecordScene(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent)
{
    using RDS = Render::FrameDescriptorSet;
    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();
    const VkDescriptorSet sceneSets[] = {sets[Render::FrameSetIndex(frameIndex, RDS::Camera)],
                                         sets[Render::FrameSetIndex(frameIndex, RDS::Light)]};

    if (renderer_.IsDeferred())
    {
        gbufferPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_->GetLayout(), 0, 2, sceneSets, 0,
                                nullptr);
        // 默认纹理池槽位（立方体/地面/圆环共用：0=全局反照率 1=全局法线 2=纯白mr透传）
        const PushObject defaultPush{};
        vkCmdPushConstants(cmd, gbufferPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject),
                           &defaultPush);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        sceneMesh_.Bind(cmd);
        cubeInstances_.Bind(cmd);
        sceneMesh_.DrawIndexedInstanced(cmd, Scene::kCubeIndexCount, 0, cubeInstanceCount_);

        sceneMesh_.Bind(cmd);
        groundInstances_.Bind(cmd);
        sceneMesh_.DrawIndexedInstanced(cmd, Scene::kGroundIndexCount, Scene::kGroundIndexOffset, 1);

        torusMesh_.Bind(cmd);
        torusInstances_.Bind(cmd);
        torusMesh_.DrawIndexedInstanced(cmd, torusMesh_.IndexCount(), 0, torusInstanceCount_);

        // glTF 模型：不透明 + MASK 批次（BLEND 批次不进 GBuffer，由透明叠加通道处理）
        DrawGltfPrims(cmd, *gbufferPipeline_, 0);
        return;
    }

    // 前向：描述符先行绑定（天空盒与场景共用同一套 set）
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->GetLayout(), 0, 2, sceneSets, 0, nullptr);

    // 天空盒：最先绘制（不写深度，场景覆盖其上）
    skyboxPipeline_->Bind(cmd);
    const PushSky skyPush{glm::inverse(camera_.Proj() * camera_.View())};
    vkCmdPushConstants(cmd, skyboxPipeline_->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushSky), &skyPush);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    pipeline_->Bind(cmd);

    // 默认纹理池槽位（立方体/地面/圆环共用：0=全局反照率 1=全局法线 2=纯白mr透传）
    const PushObject defaultPush{};
    vkCmdPushConstants(cmd, pipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject), &defaultPush);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    sceneMesh_.Bind(cmd);
    cubeInstances_.Bind(cmd);
    sceneMesh_.DrawIndexedInstanced(cmd, Scene::kCubeIndexCount, 0, cubeInstanceCount_);

    sceneMesh_.Bind(cmd);
    groundInstances_.Bind(cmd);
    sceneMesh_.DrawIndexedInstanced(cmd, Scene::kGroundIndexCount, Scene::kGroundIndexOffset, 1);

    torusMesh_.Bind(cmd);
    torusInstances_.Bind(cmd);
    torusMesh_.DrawIndexedInstanced(cmd, torusMesh_.IndexCount(), 0, torusInstanceCount_);

    // glTF 模型：不透明 + MASK 批次（前向 frag 按模式分发；MASK 逐片元 discard）
    DrawGltfPrims(cmd, *pipeline_, 0);

    // glTF 透明（BLEND）批次：不写深度 + 远到近排序，避免与粒子混合次序错乱
    if (hasGltf_ && gltfBlendPipeline_)
    {
        gltfBlendPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gltfBlendPipeline_->GetLayout(), 0, 2, sceneSets,
                                0, nullptr);
        DrawGltfPrims(cmd, *gltfBlendPipeline_, 2);
    }

    // ---- 粒子：前向-only，最后绘制（Alpha 混合，不写深度；阶段 3e 资源在 ParticleHost） ----
    if (particleHost_.enabled && particleHost_.pipeline->IsValid())
    {
        const glm::mat4 viewProj = camera_.Proj() * camera_.View();
        // 由视图矩阵的行向量提取相机世界右/上轴（billboard 展开用）
        const glm::mat4& view = camera_.View();
        const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
        const ParticleHost::PushParticle pp{viewProj, camRight, 0.0f, camUp, 0.0f};
        particleHost_.pipeline->Bind(cmd);
        vkCmdPushConstants(cmd, particleHost_.pipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(ParticleHost::PushParticle), &pp);
        particleHost_.buffer.Bind(cmd);
        const uint32_t alive = static_cast<uint32_t>(particleHost_.scratch.size());
        if (alive > 0)
            vkCmdDraw(cmd, 6, alive, 0, 0);
    }
}

void Application::RecordUi(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent)
{
    editorOverlay_.NewFrame();

    EditorStats stats;
    stats.fps = lastFps_;
    stats.frameMs = lastFrameMs_;
    stats.gpuName = ctx_.PhysicalDeviceName();
    stats.extent = renderer_.Extent();
    stats.msaaSamples = static_cast<uint32_t>(renderer_.SampleCount());
    stats.triangleCount = triangleCount_;
    stats.culledCount = culledCount_;
    uint32_t gltfBatches = 0;
    for (const uint32_t c : gltfPrimCounts_)
        if (c > 0)
            ++gltfBatches;
    stats.batchCount = (cubeInstanceCount_ > 0 ? 1u : 0u) + 1u + (torusInstanceCount_ > 0 ? 1u : 0u) + gltfBatches;
    if (const Render::GpuProfiler* profiler = renderer_.GetProfiler())
    {
        stats.gpuFrameMs = profiler->FrameMs();
        stats.gpuShadowMs = profiler->ShadowMs();
        stats.gpuSceneMs = profiler->SceneMs();
        stats.gpuUiMs = profiler->UiMs();
    }

    // CPU 帧剖析数据
    const auto& cpuRecords = frameProfiler_.Records();
    stats.cpuScopes = reinterpret_cast<const EditorStats::CpuScope*>(cpuRecords.data());
    stats.cpuScopeCount = static_cast<uint32_t>(cpuRecords.size());
    stats.cpuTotalMs = frameProfiler_.TotalMs();
    const size_t histCount = frameProfiler_.GetHistoryChronological(fpsHistoryChrono_.data(), fpsHistoryChrono_.size());
    stats.fpsHistory = fpsHistoryChrono_.data();
    stats.fpsHistoryCount = static_cast<uint32_t>(histCount);

    // 升级20：本帧编辑交互前的场景快照，作为属性编辑手势的"起始 before"（ImGui 在 Draw 内即改场景）
    const SceneSnapshot frameStart = Snapshot();

    editorPanel_.Draw(stats, scene_, lightParams_, camera_.fovDegrees_, pointLights_, selectedObject_,
                      &postProcessSync_.deferred, &gizmoMode_,
                      glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)),
                      &masterVolume_, &postProcessSync_.postProcess, &postProcessSync_.ssao, &postProcessSync_.ssr,
                      &physicsHost_.enabled, &physicsHost_.debugDraw, &physicsHost_.gravity,
                      &physicsHost_.characterEnabled, &physicsHost_.characterSpeed, &physicsHost_.characterJumpForce,
                      &physicsHost_.joints, &animationHost_.StateMachine(), &navHost_.enabled,
                      &particleHost_.enabled, &navHost_.agentEnabled, &particleHost_.emitterConfig,
                      &particleHost_.gravity, &particleHost_.damping, &particleHost_.emitterPresetIndex, &postProcessSync_.gradeSaturation, &postProcessSync_.gradeContrast,
                      &postProcessSync_.gradeLift, &postProcessSync_.gradeGain, &postProcessSync_.gradeGamma,
                      &postProcessSync_.dofEnabled, &postProcessSync_.dofFocusDistance, &postProcessSync_.dofAperture,
                      &postProcessSync_.dofMaxBlur, &postProcessSync_.mbEnabled, &postProcessSync_.mbStrength,
                      &postProcessSync_.mbMaxBlur, &postProcessSync_.mbMaxSamples, &postProcessSync_.fogEnabled,
                      &postProcessSync_.fogDensity, &postProcessSync_.fogHeightFalloff, &postProcessSync_.fogBaseHeight,
                      &postProcessSync_.fogScatter, &postProcessSync_.fogTint, &postProcessSync_.fogShadowEnabled,
                      &postProcessSync_.fogSteps, &postProcessSync_.autoExposure, &postProcessSync_.exposureKeyValue,
                      &postProcessSync_.adaptationSpeed, &postProcessSync_.vignetteIntensity,
                      &postProcessSync_.vignetteRadius, &postProcessSync_.filmGrain, &postProcessSync_.taaEnabled,
                      &postProcessSync_.taaFeedback, &assetRegistry_, &meshResources_);
    audioEngine_.SetMasterVolume(masterVolume_);

    // 阶段 3a：后处理参数/相机环境/雾阴影资源每帧同步进 PostProcessor（子系统封装，升级 21-28）
    postProcessSync_.SyncToPostProcessor(renderer_.GetPostProcessor(), extent,
                                         lightUbos_[imageIndex % lightUbos_.size()].buffer, shadowMap_.View(),
                                         shadowMap_.Sampler(), lightParams_, camera_, deltaTime_);

    // 编辑器撤销/重做按钮（Ctrl+Z/Y 在主循环已处理；此处处理面板按钮）
    if (editorPanel_.undoRequested)
    {
        commandStack_.Undo();
        suppressEditGesture_ = true;
        editorPanel_.undoRequested = false;
    }
    if (editorPanel_.redoRequested)
    {
        commandStack_.Redo();
        suppressEditGesture_ = true;
        editorPanel_.redoRequested = false;
    }

    // 物理属性变更 -> 重建刚体
    if (editorPanel_.physicsRebuildRequested)
    {
        physicsHost_.RebuildBodies();
        editorPanel_.physicsRebuildRequested = false;
    }

    // 关节创建请求
    if (editorPanel_.jointCreateRequested)
    {
        editorPanel_.jointCreateRequested = false;
        const int target = editorPanel_.jointTargetObject;
        if (selectedObject_ >= 0 && target >= 0 && target != selectedObject_ &&
            target < static_cast<int>(scene_.size()))
        {
            Physics::SceneJoint sj;
            sj.objectA = static_cast<uint32_t>(selectedObject_);
            sj.objectB = static_cast<uint32_t>(target);
            sj.type = static_cast<Physics::JointType>(editorPanel_.jointType);
            sj.axis = glm::vec3(0.0f, 1.0f, 0.0f);
            physicsHost_.joints.push_back(sj);
            physicsHost_.RebuildBodies();
            LOG_INFO("创建关节: #" << sj.objectA << " <-> #" << sj.objectB);
        }
    }

    // 关节删除请求
    if (editorPanel_.jointDeleteRequested)
    {
        editorPanel_.jointDeleteRequested = false;
        const int idx = editorPanel_.jointDeleteIndex;
        if (idx >= 0 && idx < static_cast<int>(physicsHost_.joints.size()))
        {
            physicsHost_.joints.erase(physicsHost_.joints.begin() + idx);
            physicsHost_.RebuildBodies();
            LOG_INFO("删除关节: #" << idx);
        }
    }
    physicsHost_.engine.SetGravity(glm::vec3(0.0f, physicsHost_.gravity, 0.0f));

    // ---- Gizmo 屏幕手柄 ----
    if (selectedObject_ >= 0 && selectedObject_ < static_cast<int>(scene_.size()))
    {
        const glm::mat4 gvp = camera_.Proj() * camera_.View();
        const glm::vec2 gvpSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
        const glm::vec3 origin = scene_[static_cast<size_t>(selectedObject_)].position;
        const glm::vec2 o = Editor::ProjectWorldToScreen(origin, gvp, gvpSize);
        if (o.x > -1e8f)
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const glm::vec3 axesW[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            const ImU32 cols[3] = {IM_COL32(232, 72, 72, 255), IM_COL32(72, 210, 96, 255), IM_COL32(80, 130, 240, 255)};
            const ImVec2 oPx(o.x, o.y);
            for (int a = 0; a < 3; ++a)
            {
                const glm::vec2 tip = Editor::ProjectWorldToScreen(origin + axesW[a], gvp, gvpSize);
                if (tip.x < -1e8f)
                    continue;
                const bool active = gizmoDragging_ && (static_cast<Editor::GizmoAxis>(a) == gizmoDragAxis_);
                dl->AddLine(oPx, ImVec2(tip.x, tip.y), cols[a], active ? 4.0f : 2.5f);
                dl->AddCircleFilled(ImVec2(tip.x, tip.y), active ? 8.0f : 5.0f, cols[a]);
            }
            dl->AddCircleFilled(oPx, 4.0f, IM_COL32(235, 235, 235, 255));
        }
    }

    // ---- 物理调试线框（阶段 3f：引擎与关节在 PhysicsHost） ----
    if (physicsHost_.debugDraw)
    {
        const glm::mat4 gvp = camera_.Proj() * camera_.View();
        const glm::vec2 gvpSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
        const auto debugLines = physicsHost_.engine.GetDebugLines();
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        for (const auto& line : debugLines)
        {
            const glm::vec2 p1 = Editor::ProjectWorldToScreen(line.a, gvp, gvpSize);
            const glm::vec2 p2 = Editor::ProjectWorldToScreen(line.b, gvp, gvpSize);
            if (p1.x < -1e8f || p2.x < -1e8f)
                continue;
            const ImU32 col = IM_COL32(static_cast<int>(line.color.r * 255), static_cast<int>(line.color.g * 255),
                                       static_cast<int>(line.color.b * 255), 200);
            dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col, 1.5f);
        }

        // 关节调试线：连接两物体 + 锚点 + 轴
        for (size_t i = 0; i < physicsHost_.joints.size(); ++i)
        {
            const Physics::SceneJoint& sj = physicsHost_.joints[i];
            if (sj.objectA >= scene_.size() || sj.objectB >= scene_.size())
                continue;
            const glm::vec3 posA = scene_[sj.objectA].position;
            const glm::vec3 posB = scene_[sj.objectB].position;
            const glm::vec3 anchor = (posA + posB) * 0.5f;

            const glm::vec2 spA = Editor::ProjectWorldToScreen(posA, gvp, gvpSize);
            const glm::vec2 spB = Editor::ProjectWorldToScreen(posB, gvp, gvpSize);
            const glm::vec2 spAnchor = Editor::ProjectWorldToScreen(anchor, gvp, gvpSize);
            if (spA.x < -1e8f || spB.x < -1e8f)
                continue;

            // 关节颜色：固定=灰，铰链=青，球窝=品红，滑块=橙
            const ImU32 jointCols[] = {
                IM_COL32(180, 180, 180, 220), // Fixed
                IM_COL32(0, 220, 220, 220),   // Hinge
                IM_COL32(220, 0, 220, 220),   // BallAndSocket
                IM_COL32(255, 165, 0, 220),   // Slider
            };
            const ImU32 jcol = jointCols[static_cast<int>(sj.type)];
            dl->AddLine(ImVec2(spA.x, spA.y), ImVec2(spB.x, spB.y), jcol, 2.0f);
            dl->AddCircleFilled(ImVec2(spAnchor.x, spAnchor.y), 5.0f, jcol);

            // 铰链/滑块：画轴方向
            if (sj.type == Physics::JointType::Hinge || sj.type == Physics::JointType::Slider)
            {
                const glm::vec3 axisEnd = anchor + glm::normalize(sj.axis) * 1.5f;
                const glm::vec2 spAxis = Editor::ProjectWorldToScreen(axisEnd, gvp, gvpSize);
                if (spAxis.x > -1e8f)
                    dl->AddLine(ImVec2(spAnchor.x, spAnchor.y), ImVec2(spAxis.x, spAxis.y), IM_COL32(255, 255, 0, 200),
                                1.5f);
            }
        }
    }

    // ---- 导航网格调试线（A* 网格 + 障碍 + 路径；阶段 3d 数据在 NavHost 子系统） ----
    if (navHost_.enabled || navHost_.agentEnabled)
    {
        const glm::mat4 gvp = camera_.Proj() * camera_.View();
        const glm::vec2 gvpSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        if (navHost_.enabled)
        {
            const auto navLines = navHost_.grid.GetDebugLines(navHost_.cellSize, navHost_.origin, &navHost_.path);
            for (const auto& line : navLines)
            {
                const glm::vec2 p1 = Editor::ProjectWorldToScreen(line.a, gvp, gvpSize);
                const glm::vec2 p2 = Editor::ProjectWorldToScreen(line.b, gvp, gvpSize);
                if (p1.x < -1e8f || p2.x < -1e8f)
                    continue;
                const ImU32 col = IM_COL32(static_cast<int>(line.color.r * 255), static_cast<int>(line.color.g * 255),
                                           static_cast<int>(line.color.b * 255), 180);
                dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col, 1.0f);
            }
            // 起点（绿）/终点（红）标记
            const auto cellToScreen = [&](int cx, int cy) -> glm::vec2
            {
                const glm::vec3 w(navHost_.origin.x + (static_cast<float>(cx) + 0.5f) * navHost_.cellSize, 0.06f,
                                  navHost_.origin.y + (static_cast<float>(cy) + 0.5f) * navHost_.cellSize);
                return Editor::ProjectWorldToScreen(w, gvp, gvpSize);
            };
            glm::vec2 sp = cellToScreen(navHost_.startX, navHost_.startY);
            dl->AddCircleFilled(ImVec2(sp.x, sp.y), 5.0f, IM_COL32(0, 230, 90, 230));
            sp = cellToScreen(navHost_.goalX, navHost_.goalY);
            dl->AddCircleFilled(ImVec2(sp.x, sp.y), 5.0f, IM_COL32(230, 60, 60, 230));
        }

        // ---- 升级 18：AI 导航代理（NavAgent）可视化 ----
        if (navHost_.agentEnabled)
        {
            const auto agentLines = navHost_.agent.GetDebugLines();
            for (const auto& line : agentLines)
            {
                const glm::vec2 p1 = Editor::ProjectWorldToScreen(line.a, gvp, gvpSize);
                const glm::vec2 p2 = Editor::ProjectWorldToScreen(line.b, gvp, gvpSize);
                if (p1.x < -1e8f || p2.x < -1e8f)
                    continue;
                const ImU32 col = IM_COL32(static_cast<int>(line.color.r * 255), static_cast<int>(line.color.g * 255),
                                           static_cast<int>(line.color.b * 255), 220);
                dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col, 2.0f);
            }
            // 代理当前位置标记（金黄实心圆点）
            const glm::vec2 sp = Editor::ProjectWorldToScreen(navHost_.agent.Position(), gvp, gvpSize);
            dl->AddCircleFilled(ImVec2(sp.x, sp.y), 5.0f, IM_COL32(255, 230, 50, 240));
        }
    }

    // 升级20：提交本帧的属性编辑手势为可撤销命令（在显式命令处理之后，确保 suppress 标志已置位）
    HandlePropertyEditUndo(frameStart);

    editorOverlay_.Render(cmd, imageIndex);
}

void Application::RecordPrePass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D)
{
    (void)frameIndex; // 方向光阴影不依赖帧槽（独立深度图/描述符）

    // 方向光 CSM：单渲染通道内逐级联绘制到 2x2 图集子块，留在主命令缓冲内录制
    // （cascadeMatrices_ 已由本帧 UpdateUniforms 刷新）
    shadowMap_.RecordPass(cmd, [this](VkCommandBuffer c, uint32_t cascade)
                          { DrawShadowCasters(c, *shadowPipeline_, cascadeMatrices_[cascade]); });

    // 点光源立方体阴影已移至 RecordParallelCubeShadow（多线程并行录制），此处不再录制
}

void Application::RecordParallelCubeShadow(Render::ParallelCommandRecorder& recorder, uint32_t frameIndex)
{
    const bool anyPointShadow =
        !pointLights_.empty() && std::any_of(pointLights_.begin(), pointLights_.end(),
                                             [](const PointLightParams& pl) { return pl.castsShadow; });
    if (!anyPointShadow)
        return; // 无点光源阴影：不提交并行任务

    // 6 面相互独立：并行录制到 6 个独立 command buffer（DrawCubeShadowCasters 只读共享场景状态，线程安全）
    std::vector<std::function<void(VkCommandBuffer)>> tasks;
    tasks.reserve(CubeShadowMap::kFaceCount);
    for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
    {
        tasks.emplace_back(
            [this, frameIndex, face](VkCommandBuffer c)
            {
                // 命令缓冲生命周期自含：Reset 后处于 INITIAL 态，必须先 Begin 才能录制
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                VK_CHECK(vkBeginCommandBuffer(c, &beginInfo), "开始立方体阴影命令缓冲");

                cubeShadowMap_.RecordFace(c, face, [this, frameIndex](VkCommandBuffer cc, int f)
                                          { DrawCubeShadowCasters(cc, *cubeShadowPipeline_, f, frameIndex); });

                VK_CHECK(vkEndCommandBuffer(c), "结束立方体阴影命令缓冲");
            });
    }
    recorder.RecordParallel(tasks, frameIndex);
}

void Application::RecordLighting(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D)
{
    using RDS = Render::FrameDescriptorSet;
    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();

    // 更新 AO 描述符集：SSAO 启用时绑定 AO 输出，否则绑定 1x1 白纹理（AO=1）
    if (renderer_.IsSSAO() && renderer_.GetSSAO()->GetAOView() != VK_NULL_HANDLE)
        descManager_.UpdateAOSet(renderer_.GetSSAO()->GetAOView());
    else
        descManager_.UpdateAOSet(renderer_.GetDummyWhiteView());

    const VkDescriptorSet lightSets[] = {sets[Render::FrameSetIndex(frameIndex, RDS::Camera)],
                                         sets[Render::FrameSetIndex(frameIndex, RDS::Light)],
                                         descManager_.GetGBufferSets()[imageIndex], descManager_.aoSet};
    lightingPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline_->GetLayout(), 0, 4, lightSets, 0,
                            nullptr);
    const glm::mat4 invVP = glm::inverse(camera_.Proj() * camera_.View());
    vkCmdPushConstants(cmd, lightingPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &invVP);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// 延迟透明叠加通道：光照 Pass 之后，把 BLEND / 自发光批次混合到离屏 HDR 颜色上。
// 深度只读测试（沿用 GBuffer 深度），透明体被不透明几何正确遮挡；管线负责混合因子。
void Application::RecordTransparent(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent)
{
    if (!hasGltf_)
        return;

    using RDS = Render::FrameDescriptorSet;
    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();
    const VkDescriptorSet sceneSets[] = {sets[Render::FrameSetIndex(frameIndex, RDS::Camera)],
                                         sets[Render::FrameSetIndex(frameIndex, RDS::Light)]};

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // BLEND 批次：标准 Alpha 混合，远到近排序
    if (transBlendPipeline_)
    {
        transBlendPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transBlendPipeline_->GetLayout(), 0, 2, sceneSets,
                                0, nullptr);
        DrawGltfPrims(cmd, *transBlendPipeline_, 2);
    }

    // 自发光批次：加性叠加（延迟光照 Pass 不感知自发光，在此补写；加性混合与次序无关）
    if (transEmissivePipeline_)
    {
        transEmissivePipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transEmissivePipeline_->GetLayout(), 0, 2,
                                sceneSets, 0, nullptr);
        DrawGltfPrims(cmd, *transEmissivePipeline_, 3);
    }
}

// ========================================================================
// 管线重建
// ========================================================================

void Application::RebuildMainPipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass mainPass = renderer_.GetRenderPass();

    Render::ShaderModuleHandle v(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle f(dev, Render::ReadShaderFile(kFragSpvPath));
    pipelineConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    pipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(v), std::move(f), pipelineConfig_);

    Render::ShaderModuleHandle sv(dev, Render::ReadShaderFile("shaders/skybox.vert.spv"));
    Render::ShaderModuleHandle sf(dev, Render::ReadShaderFile("shaders/skybox.frag.spv"));
    skyboxConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    skyboxPipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(sv), std::move(sf), skyboxConfig_);

    // 粒子管线（交换链重建时一并重建；阶段 3e 移入 ParticleHost）
    particleHost_.CreatePipeline(dev, mainPass, renderer_.SampleCount());

    // glTF 透明（BLEND）前向管线（与主管线同 pass，随交换链重建）
    Render::ShaderModuleHandle bv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle bf(dev, Render::ReadShaderFile(kFragSpvPath));
    gltfBlendConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    gltfBlendPipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(bv), std::move(bf), gltfBlendConfig_);
}

void Application::RebuildDeferredPipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass geometryPass = renderer_.GetDeferredRenderPass();
    const VkRenderPass lightingPass = renderer_.GetLightingRenderPass();

    Render::ShaderModuleHandle gv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle gf(dev, Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
    gbufferConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    gbufferPipeline_ = Render::GraphicsPipeline(dev, geometryPass, std::move(gv), std::move(gf), gbufferConfig_);

    Render::ShaderModuleHandle lv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
    Render::ShaderModuleHandle lf(dev, Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
    defLightConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight, descManager_.layoutGBufferInput,
                                  descManager_.layoutAO};
    lightingPipeline_ = Render::GraphicsPipeline(dev, lightingPass, std::move(lv), std::move(lf), defLightConfig_);

    // 延迟透明叠加管线（随 transparentRenderPass_ 重建）
    const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
    Render::ShaderModuleHandle tv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle tf(dev, Render::ReadShaderFile(kFragSpvPath));
    transBlendPipeline_ = Render::GraphicsPipeline(dev, transparentPass, std::move(tv), std::move(tf),
                                                   transBlendConfig_);

    Render::ShaderModuleHandle ev(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle ef(dev, Render::ReadShaderFile(kFragSpvPath));
    transEmissivePipeline_ = Render::GraphicsPipeline(dev, transparentPass, std::move(ev), std::move(ef),
                                                      transEmissiveConfig_);
}

void Application::UpdateGBufferSets()
{
    const uint32_t n = renderer_.GetSwapchain().ImageCount();
    for (uint32_t i = 0; i < n; ++i)
        descManager_.UpdateGBufferSet(i, renderer_.GBufferAlbedoView(i), renderer_.GBufferNormalView(i),
                                      renderer_.GBufferPositionView(i));
    // 分配 AO 描述符集（首次调用时）
    if (descManager_.aoSet == VK_NULL_HANDLE)
        descManager_.AllocateAOSet();
}

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
        matrices[c] = glm::ortho(center.x - half, center.x + half, center.y - half, center.y + half, -maxP.z,
                                 -minP.z) *
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

void Application::DrawShadowCasters(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline,
                                    const glm::mat4& lightSpace)
{
    pipeline.Bind(cmd);

    const auto drawOne = [&](const glm::mat4& model, Render::Mesh& mesh, uint32_t count, uint32_t first)
    {
        const PushShadow push{lightSpace, model};
        vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushShadow), &push);
        mesh.Bind(cmd);
        mesh.DrawIndexed(cmd, count, first);
    };

    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (scene_[i].meshId != 0)
            continue;
        drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), sceneMesh_, Scene::kCubeIndexCount, 0);
    }

    drawOne(glm::mat4(1.0f), sceneMesh_, Scene::kGroundIndexCount, Scene::kGroundIndexOffset);

    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (scene_[i].meshId != 1)
            continue;
        drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), torusMesh_, torusMesh_.IndexCount(), 0);
    }

    // glTF 模型（索引区间连续，整模一次绘制）
    if (hasGltf_)
    {
        for (size_t i = 0; i < scene_.size(); ++i)
        {
            if (scene_[i].meshId != 2)
                continue;
            drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), gltfMesh_, gltfMesh_.IndexCount(), 0);
        }
    }
}

void Application::DrawCubeShadowCasters(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int face,
                                         uint32_t frameIndex)
{
    using RDS = Render::FrameDescriptorSet;
    pipeline.Bind(cmd);

    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();
    // PointShadowUBO 在布局的 set=2（与 shadow_cube.vert 的 set=2 声明一致）
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetLayout(), 2, 1,
                            &sets[Render::FrameSetIndex(frameIndex, RDS::PointShadow)], 0, nullptr);

    const auto drawOne = [&](const glm::mat4& model, Render::Mesh& mesh, uint32_t count, uint32_t first)
    {
        const PushCubeShadow push{model, glm::vec4(static_cast<float>(face), 0.0f, 0.0f, 0.0f)};
        vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow), &push);
        mesh.Bind(cmd);
        mesh.DrawIndexed(cmd, count, first);
    };

    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (scene_[i].meshId != 0)
            continue;
        drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), sceneMesh_, Scene::kCubeIndexCount, 0);
    }

    drawOne(glm::mat4(1.0f), sceneMesh_, Scene::kGroundIndexCount, Scene::kGroundIndexOffset);

    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (scene_[i].meshId != 1)
            continue;
        drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), torusMesh_, torusMesh_.IndexCount(), 0);
    }

    // glTF 模型（索引区间连续，整模一次绘制）
    if (hasGltf_)
    {
        for (size_t i = 0; i < scene_.size(); ++i)
        {
            if (scene_[i].meshId != 2)
                continue;
            drawOne(Scene::ComputeObjectModelMatrix(scene_[i], spinAngles_[i]), gltfMesh_, gltfMesh_.IndexCount(), 0);
        }
    }
}

glm::vec3 Application::GetActiveShadowLight(const std::vector<PointLightParams>& lights)
{
    for (const PointLightParams& pl : lights)
        if (pl.castsShadow)
            return pl.position;
    return glm::vec3(0.0f);
}
} // namespace BigHero
