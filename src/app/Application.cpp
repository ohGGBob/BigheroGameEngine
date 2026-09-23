#include "app/Application.h"

#include "core/Time.h"
#include "core/VkCheck.h"
#include "open_world/OpenWorldScene.h"
#include "scene/CubeMesh.h"
#include "scene/GltfLoader.h"
#include "vertical_slice/SliceScene.h"

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
        InitUiRuntime(); // U1-UI：运行时 UI 系统（headless/--no-ui 停用；--ui-demo 构建演示画布）

        // ---- C# 脚本系统（--scripts 启用；默认关闭，失败优雅降级，引擎正常跑） ----
        if (!config_.scriptsDir.empty())
        {
            if (scripts_.Init(&ecsScene_, config_.scriptsDir))
            {
                // 最简演示接线：给默认场景 0 号立方体挂 Spinner（方案文档示例脚本）
                if (scripts_.AttachToOrderIndex(0, "MyGame.Spinner") >= 0 && ecsScene_.ObjectCount() > 0)
                {
                    // 演示契约：0 号立方体的原生自转关闭，自转完全由 C# Spinner 接管，
                    // 保证截图对比的姿态差异信号只来自脚本
                    ecsScene_.Registry().Get<Scene::ecs::Spin>(ecsScene_.At(0)).speed = 0.0f;
                }
            }
            else
            {
                LOG_WARN("C# 脚本系统未启用（优雅降级，--scripts 已忽略，引擎以无脚本模式继续）");
            }
        }

        lastTime_ = Time::NowSeconds();
        LOG_INFO("进入主循环（左键拖拽旋转 / 滚轮缩放 / WASD+QE平移）");

        // 命令行强制开后处理（等价编辑器勾选；P0-3 验收无需手动操作）
        if (config_.postProcess)
        {
            postProcessSync_.postProcess = true;
            LOG_INFO("命令行启用后处理（--post-process）");
        }

        // 命令行设置初始曝光（等价编辑器"光照"面板曝光滑条；开始渲染前生效，
        // 每帧经 renderer_.SetExposure / LightUBO / PP 合成端同源读取）
        if (config_.exposure)
        {
            lightParams_.exposure = *config_.exposure;
            LOG_INFO("命令行设置曝光: " << *config_.exposure << "（--exposure）");
        }

        // 命令行指定启动相机模式（--camera fp：直接以第一人称漫游进入场景）
        if (config_.cameraMode == "fp")
        {
            fpCamera_.SyncFromOrbit(camera_);
            cameraMode_ = CameraMode::FirstPerson;
            window_->SetCursorLocked(true); // 鼠标位移直接转视角（Esc 释放）
            LOG_INFO("命令行启动第一人称漫游相机（--camera fp）");
        }

        // 命令行烘焙（--bake-probes / --bake-occlusion）：主循环前同步兑现，
        // 不依赖 RecordUi（--no-ui 下早退跳过面板路径）。Bake* 内部清除请求标志，
        // RecordUi 首帧再调 RunPendingBakes 时零成本，不会重复烘焙。
        // 等价编辑器面板按钮，供"烘焙前后"性能对比自动化。
        if (config_.bakeProbes || config_.bakeOcclusion)
        {
            if (config_.bakeProbes)
            {
                projectPanel_.probeBakeRequested = true;
                LOG_INFO("命令行请求光照探针烘焙（--bake-probes）");
            }
            if (config_.bakeOcclusion)
            {
                projectPanel_.occlusionBakeRequested = true;
                // PVS 烘焙 = 格×对象×起点×终点 全量射线测试：9×9 采样在 9k 实体场景
                // 可达分钟级。CLI 对比用 3×3 低采样（方向性结论不变；编辑器面板仍全精度）。
                projectPanel_.SetCliOcclusionSampling(3, 3);
                LOG_INFO("命令行请求遮挡剔除烘焙（--bake-occlusion，CLI 采样 3×3）");
            }
            RunPendingBakes();
            LOG_INFO("命令行烘焙请求已兑现");
        }

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
                runTimeSeconds_ += deltaTime_;
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "UpdateCamera");
                    UpdateCamera();
                }

                // 场景切换（编辑器"场景"下拉框）：原地重建场景，无需重启
                if (!pendingSceneKind_.empty())
                {
                    const std::string next = std::move(pendingSceneKind_);
                    if (next != currentSceneKind_)
                        BuildAndLoadScene(next);
                }

                UpdateShowcase();
                UpdateGizmo();
                // U1-E3 Play Mode：播放/暂停/停止请求（面板按钮 + Ctrl+P）先于仿真门消费，
                // 保证 Stop 还原/进入 Play 在本帧仿真前生效
                UpdatePlayModeRequests();
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "SyncSceneEdits");
                    SyncSceneEdits();
                } // ECS：包 -> ECS 写回（编辑器/Gizmo 修改持久化）
                // ---- U1-E3 仿真门：仅运行态推进（编辑态/暂停场景静态） ----
                // 冻结清单：C# 脚本 Update / 物理步进与角色控制器 / 动画状态机（含 glTF 根节点）/
                // 人物姿态动画 / 粒子模拟 / AI 导航代理；自转 Spin 在 UpdateTime 内同门冻结。
                // 渲染/相机/编辑器/序列化/撤销路径照常（编辑操作在编辑态即时生效不变）。
                const bool simulating = playMode_.ShouldSimulate();
                if (scripts_.Enabled() && simulating)
                    scripts_.Update(deltaTime_); // C# 脚本：热重载轮询 + OnStart/OnUpdate 批量派发（仅运行态）
                if (simulating)
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "Physics");
                    physicsHost_.Update(deltaTime_);
                }
                // 包投影按需重建：仅运行态每帧投影——物理步进与自转系统每帧改写 ECS
                // Transform/Spin，Gizmo 屏幕手柄/关节调试线/拾取回退仍读 scene_，须拿到新值。
                // 编辑态 scene_ 即编辑器数据模型（Gizmo/属性面板直接改写它），增删/改父/撤销/
                // 读档均已显式 Repack 或直接赋值，ECS 不再独立漂移，故跳过每帧 BuildPacket。
                if (simulating)
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "RepackScene");
                    RepackScene();
                }
                if (simulating)
                {
                    // 动画状态机（编辑器面板可暂停/拖动时间轴）：解析角色输入参数后交子系统推进
                    AnimationHost::FrameInput animInput;
                    animInput.characterActive =
                        physicsHost_.characterEnabled && physicsHost_.characterBodyId != UINT32_MAX;
                    if (animInput.characterActive)
                    {
                        const glm::vec3 vel = physicsHost_.engine.GetBodyLinearVelocity(physicsHost_.characterBodyId);
                        animInput.speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                        animInput.grounded = physicsHost_.characterGrounded;
                    }
                    {
                        Core::FrameProfiler::Scope sc(frameProfiler_, "Animation");
                        animationHost_.Update(deltaTime_, animInput, gltfModel_, hasGltf_);
                        personHost_.Update(deltaTime_);
                        // A1 动画事件派发（生产接线）：消费事件播放器产出的事件流。
                        // 内置轨在主 clip 25%/75% 发 "tick" 事件；此处记日志，约定名映射到音效。
                        for (const Scene::AnimationEvent& ev : animationHost_.DrainFiredEvents())
                        {
                            LOG_INFO("动画事件: [" << ev.name << "] clipTime=" << ev.time
                                                   << " param=" << ev.param);
                            if (ev.name == "click")
                            {
                                const bool played = audioEngine_.PlaySfx(Audio::SfxId::Click);
                                LOG_INFO("动画事件 'click' -> PlaySfx(Click): "
                                         << (played ? "已播放音效" : "无音频设备，优雅降级跳过"));
                            }
                        }
                    } // 人物姿态动画（写 ECS Transform）
                }
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "UpdateRenderables");
                    UpdateRenderables();
                } // ECS 渲染收敛：单趟直读 ECS（剔除 + 批次化 + 上传登记）
                if (simulating)
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "Particles");
                    particleHost_.Update(deltaTime_);
                    // 粒子：登记到帧瞬态上传（scratch 成员在录制前稳定）
                    if (particleHost_.enabled && !particleHost_.scratch.empty())
                        AppendUpload(particleHost_.buffer.Get(), particleHost_.scratch.data(),
                                     static_cast<VkDeviceSize>(particleHost_.scratch.size()) *
                                         sizeof(Render::ParticleInstance));
                    // 冻结时不重传：GPU 缓冲保留最后一次模拟的实例（暂停/编辑态粒子静止呈现）
                }
                if (simulating)
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "NavAgent");
                    navHost_.UpdateAgent(deltaTime_);
                }
                {
                    Core::FrameProfiler::Scope sc(frameProfiler_, "UpdateUniforms");
                    UpdateUniforms();
                }
                UpdateFpsTitle();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Picking");
                UpdateUi(); // U1-UI：UI 命中/按钮状态机（先于场景拾取：命中 UI 时点击不穿透）
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

            // U2 工程面板开关：F7 资产数据库 / F8 LOD·探针·遮挡（边沿触发）
            const bool f7Down = window_->IsKeyDown(Window::kKeyF7);
            const bool f8Down = window_->IsKeyDown(Window::kKeyF8);
            if (f7Down && !f7KeyHeld_)
                projectPanel_.ToggleAssetDb();
            if (f8Down && !f8KeyHeld_)
                projectPanel_.ToggleCulling();
            f7KeyHeld_ = f7Down;
            f8KeyHeld_ = f8Down;

            // 导航网格：启用状态切换时重算 A* 路径（阶段 3d：状态在 NavHost 子系统）
            if (navHost_.enabled != navHost_.prevEnabled)
            {
                navHost_.prevEnabled = navHost_.enabled;
                if (navHost_.enabled)
                    navHost_.UpdatePath();
            }

            // 撤销/重做：Ctrl+Z / Ctrl+Y（边沿触发，避免按住每帧重复）。
            // U1-E3 撤销栈边界：撤销/重做属于编辑操作，运行/暂停期间不可用
            // （运行时的场景变化不属于编辑历史，Stop 还原也不入栈）。
            const bool ctrlDown =
                window_->IsKeyDown(Window::kKeyLeftControl) || window_->IsKeyDown(Window::kKeyRightControl);
            const bool zDown = window_->IsKeyDown(Window::kKeyZ);
            const bool yDown = window_->IsKeyDown(Window::kKeyY);
            if (ctrlDown && zDown && !undoKeyHeld_)
            {
                if (playMode_.AllowsSceneEditCommands())
                {
                    commandStack_.Undo();
                    suppressEditGesture_ = true; // 显式命令，抑制本帧手势记录防重复
                    LOG_INFO("撤销: 重做栈顶 = " << commandStack_.TopRedoName());
                }
                else
                {
                    LOG_INFO("Play 模式：撤销不可用（运行中的场景变化不属于编辑历史）");
                }
            }
            undoKeyHeld_ = ctrlDown && zDown;
            if (ctrlDown && yDown && !redoKeyHeld_)
            {
                if (playMode_.AllowsSceneEditCommands())
                {
                    commandStack_.Redo();
                    suppressEditGesture_ = true;
                    LOG_INFO("重做: 撤销栈顶 = " << commandStack_.TopUndoName());
                }
                else
                {
                    LOG_INFO("Play 模式：重做不可用（运行中的场景变化不属于编辑历史）");
                }
            }
            redoKeyHeld_ = ctrlDown && yDown;
            // U1-E3：面板撤销/重做按钮与 Ctrl+Z/Y 同一边界（录制阶段消费前在此拦下）
            if (!playMode_.AllowsSceneEditCommands())
            {
                editorPanel_.undoRequested = false;
                editorPanel_.redoRequested = false;
            }

            // 粒子爆发：P 键（边沿触发，阶段 3e：状态在 ParticleHost 子系统）。
            // Ctrl+P 为 Play/Stop 切换快捷键，按住 Ctrl 时不触发爆发。
            const bool pDown = window_->IsKeyDown(Window::kKeyP);
            if (pDown && !ctrlDown && !particleHost_.keyHeld)
                particleHost_.EmitBurst(ActiveTarget());
            particleHost_.keyHeld = pDown;

            // U1-E3：Ctrl+P 切换 Play/Stop（边沿触发；Ctrl+Z/Y 已占用、Ctrl+S/F5 保存不冲突，
            // 故按规格选 Ctrl+P，与裸 P 的粒子爆发以上述 Ctrl 抑制区分）
            if (ctrlDown && pDown && !playKeyHeld_)
            {
                if (playMode_.IsEditor())
                    EnterPlayMode();
                else
                    StopPlayMode();
            }
            playKeyHeld_ = ctrlDown && pDown;

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
                ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "添加物体"));
                editorPanel_.addObjectRequested = false;
                LOG_INFO("添加物体: 总计 " << scene_.size() << " 个（可 Ctrl+Z 撤销）");
                EnsureInstanceCapacities();
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
                ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "删除物体"));
                editorPanel_.deleteObjectRequested = false;
                LOG_INFO("删除物体: 剩余 " << scene_.size() << " 个（可 Ctrl+Z 撤销）");
                EnsureInstanceCapacities();
            }

            // ---- 层级树（Hierarchy）面板请求（U1-E1：点击选中 / 拖拽改父，入撤销栈） ----
            if (editorPanel_.hierarchy.selectRequested)
            {
                editorPanel_.hierarchy.selectRequested = false;
                if (editorPanel_.hierarchy.selectedIndex >= 0 &&
                    editorPanel_.hierarchy.selectedIndex < static_cast<int>(scene_.size()))
                    selectedObject_ = editorPanel_.hierarchy.selectedIndex;
            }
            if (editorPanel_.hierarchy.reparentRequested)
            {
                editorPanel_.hierarchy.reparentRequested = false;
                const int child = editorPanel_.hierarchy.reparentChild;
                const int parent = editorPanel_.hierarchy.reparentParent; // -1 = 挂到根
                // 防御性复检（面板已校验）：范围合法 + 不成环（拖入自己的子树）
                if (child >= 0 && child < static_cast<int>(scene_.size()) && parent < static_cast<int>(scene_.size()) &&
                    parent != child && !Editor::Hierarchy::WouldCreateCycle(scene_, child, parent))
                {
                    const SceneSnapshot before = Snapshot();
                    ecsScene_.SetParent(static_cast<size_t>(child), parent); // 实体句柄权威存储，父下标次序不限
                    RepackScene();
                    const SceneSnapshot after = Snapshot();
                    suppressEditGesture_ = true; // 显式命令帧，抑制属性手势重复记录
                    if (Game::SceneSnapshotsDiffer(before, after))
                        ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "改变父子关系"));
                    LOG_INFO("改变父子关系: #" << child << " -> " << (parent < 0 ? "根" : "#" + std::to_string(parent))
                                               << "（可 Ctrl+Z 撤销）");
                }
            }

            // ---- 人物生成面板请求（Todo 3：Q版人物 = 球/胶囊 ECS 骨骼树） ----
            if (editorPanel_.addPersonRequested)
            {
                const SceneSnapshot before = Snapshot();
                Scene::PersonParams params = editorPanel_.personParams;
                if (editorPanel_.personSpawnAtCursor)
                {
                    // 射线投射地面 y=0 求落点
                    const auto [cx, cy] = window_->GetCursorPos();
                    const auto [fw, fh] = window_->GetFramebufferSize();
                    if (fh > 0)
                    {
                        const glm::mat4 invVP = glm::inverse(ActiveViewProj());
                        const float ndcX = 2.0f * static_cast<float>(cx) / static_cast<float>(fw) - 1.0f;
                        const float ndcY = 1.0f - 2.0f * static_cast<float>(cy) / static_cast<float>(fh);
                        const glm::vec4 farP = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                        const glm::vec3 rayO = ActivePosition();
                        const glm::vec3 rayD = glm::normalize(glm::vec3(farP) / farP.w - rayO);
                        // 与 y=0 平面相交
                        if (std::abs(rayD.y) > 1e-5f)
                        {
                            const float t = -rayO.y / rayD.y;
                            if (t > 0.0f)
                                params.position = rayO + rayD * t;
                        }
                    }
                }
                personHost_.SpawnPerson(params);
                personHost_.Update(0.0f); // U1-E3：编辑态冻结后一次性写入静态姿态（运行态仍每帧推进）
                // S1 演示钩子：生成人物 → 落点处 3D 空间化"爆发生成"音效
                audioEngine_.Play3D(Audio::SfxId::Spawn, Audio::SoundSource{.position = params.position});
                editorPanel_.personParams.position = params.position; // 回写实际落点
                RepackScene();
                RecalculateTriangleCount();
                const SceneSnapshot after = Snapshot();
                suppressEditGesture_ = true;
                ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "生成人物"));
                editorPanel_.addPersonRequested = false;
                LOG_INFO("生成人物: 总计 " << personHost_.Count() << " 个");
                EnsureInstanceCapacities();
            }
            if (editorPanel_.removePersonRequested)
            {
                const SceneSnapshot before = Snapshot();
                personHost_.RemovePerson(editorPanel_.selectedPerson);
                editorPanel_.selectedPerson = -1;
                RepackScene();
                RecalculateTriangleCount();
                const SceneSnapshot after = Snapshot();
                suppressEditGesture_ = true;
                ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "删除人物"));
                editorPanel_.removePersonRequested = false;
                LOG_INFO("删除人物: 剩余 " << personHost_.Count() << " 个");
                EnsureInstanceCapacities();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Render");
                renderer_.SetSSAOCamera(ActiveViewProj(), ActivePosition());
                renderer_.SetSSRCamera(ActiveViewProj(), ActivePosition());
                // 截图模式：渲染若干帧（TAA/自适应曝光收敛、动画/相机稳定）后请求截图
                // （P0-3 验收：--screenshot out/pp_on.png --post-process 与
                //  --screenshot out/pp_off.png 两次运行各截一张做暗部/曝光对比）
                if (!config_.screenshotPath.empty() && !screenshotIssued_ && frameCounter_ >= 30)
                {
                    renderer_.RequestScreenshot(config_.screenshotPath);
                    screenshotIssued_ = true;
                    LOG_INFO("已请求截图: " << config_.screenshotPath << "（第 " << frameCounter_ << " 帧）");
                }
                // 第二张截图（--screenshot2）：主循环时长达到 --screenshot2-delay 后再截一张
                // （时序/脚本对比：如 C# Spinner 两时刻的姿态差异）
                if (!config_.screenshot2Path.empty() && !screenshot2Issued_ &&
                    runTimeSeconds_ >= config_.screenshot2DelaySeconds)
                {
                    renderer_.RequestScreenshot(config_.screenshot2Path);
                    screenshot2Issued_ = true;
                    LOG_INFO("已请求第二张截图: " << config_.screenshot2Path << "（主循环 " << runTimeSeconds_
                                                  << "s，第 " << frameCounter_ << " 帧）");
                }
                // 曝光：deferred 链末端统一乘（P0-3 Commit3；与 lightUbo.exposure 同源）
                renderer_.SetExposure(lightParams_.exposure);
                // 升级 22：每帧把相机近/远平面交给后处理，供景深还原线性深度
                renderer_.SetPostProcessingCamera(ActiveNear(), ActiveFar());
                // 升级 23：计算当前帧视图投影，并把"上一帧→当前帧"重投影交给运动模糊
                postProcessSync_.currViewProj = ActiveViewProj();
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
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordUi(cmd, fi, ii, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, VkExtent2D ext) { RecordPrePass(cmd, fi, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordLighting(cmd, fi, ii, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordTransparent(cmd, fi, ii, ext); },
                    [this](Render::ParallelCommandRecorder& rec, uint32_t fi) { RecordParallelCubeShadow(rec, fi); });
            }

            frameProfiler_.EndFrame();

            // 截图模式：全部截图完成后退出（避免无头持续渲染；复用正常 present 流程保证画面完整）
            ++frameCounter_;
            const bool allShotsRequested = (config_.screenshotPath.empty() || screenshotIssued_) &&
                                           (config_.screenshot2Path.empty() || screenshot2Issued_);
            if (allShotsRequested && renderer_.ScreenshotDone())
            {
                LOG_INFO("截图完成，退出渲染循环");
                break;
            }

            // 基准模式（--bench-frames N）：跳过 warmup 帧后累计帧耗时与各阶段 CPU 平均耗时；
            // 统计满 N 帧后打印并退出（脚本化性能对比用，默认关闭零行为变化）
            if (config_.benchFrames > 0)
            {
                if (frameCounter_ > kBenchWarmupFrames)
                {
                    const float ms = deltaTime_ * 1000.0f;
                    benchMinMs_ = (benchCount_ == 0) ? ms : (ms < benchMinMs_ ? ms : benchMinMs_);
                    benchMaxMs_ = (ms > benchMaxMs_) ? ms : benchMaxMs_;
                    benchSumMs_ += ms;
                    ++benchCount_;
                    for (const auto& r : frameProfiler_.Records())
                    {
                        auto& acc = benchScopeAccum_[r.name];
                        acc.first += r.ms;
                        acc.second += 1;
                    }
                }
                if (frameCounter_ >= kBenchWarmupFrames + config_.benchFrames)
                {
                    PrintBenchSummary();
                    LOG_INFO("基准模式完成，退出渲染循环");
                    break;
                }
            }
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

// 基准模式（--bench-frames N）退出时的统计输出：平均/最差帧耗时 + 各阶段 CPU 平均耗时。
// 数据由主循环内跨帧累计（benchScopeAccum_），与 FrameProfiler::BuildSummary 口径一致。
void Application::PrintBenchSummary()
{
    if (benchCount_ == 0)
    {
        LOG_INFO("[BENCH] 无有效帧计入统计（warmup 帧未满 " << kBenchWarmupFrames << "），跳过");
        return;
    }
    const double avgMs = benchSumMs_ / static_cast<double>(benchCount_);
    LOG_INFO("[BENCH] frames=" << benchCount_ << " avg=" << avgMs << "ms min=" << benchMinMs_
                               << "ms max=" << benchMaxMs_ << "ms avg_fps=" << (avgMs > 0.0 ? 1000.0 / avgMs : 0.0));
    if (!benchScopeAccum_.empty())
    {
        LOG_INFO("[BENCH] 各阶段 CPU 平均耗时（按帧累计口径）：");
        for (const auto& [name, acc] : benchScopeAccum_)
        {
            if (acc.second > 0)
                LOG_INFO("[BENCH]   " << name << " avg=" << (acc.first / static_cast<double>(acc.second))
                                      << "ms (" << acc.second << " frames)");
        }
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
        probeUbos_.emplace_back(ctx_, ctx_.GraphicsFamily());
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
        descManager_.UpdateSet(Render::FrameSetIndex(i, RDS::Light), 10, probeUbos_[i]); // set1 binding10: 逐片元探针辐照度体
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

    // ---- 人物部件几何：球体 + 胶囊（meshId=3/4） ----
    {
        std::vector<Scene::Vertex> sphereVerts;
        std::vector<uint32_t> sphereIdxs;
        Scene::BuildSphereVertices(sphereVerts, sphereIdxs, 20, 10, 0.5f);
        sphereMesh_.Create(ctx_, sphereVerts, sphereIdxs);
        RegisterMeshAsset("sphere", "<procedural>", sphereVerts, sphereIdxs, bighero::AssetMetadata::LoadState::Loaded,
                          0);

        std::vector<Scene::Vertex> capVerts;
        std::vector<uint32_t> capIdxs;
        Scene::BuildCapsuleVertices(capVerts, capIdxs, 16, 5, 0.5f, 0.7f);
        capsuleMesh_.Create(ctx_, capVerts, capIdxs);
        RegisterMeshAsset("capsule", "<procedural>", capVerts, capIdxs, bighero::AssetMetadata::LoadState::Loaded, 0);

        // LOD 低模：球 8×4（高模 20×10），胶囊 8×3（高模 16×5）
        std::vector<Scene::Vertex> sphereLodVerts;
        std::vector<uint32_t> sphereLodIdxs;
        Scene::BuildSphereVertices(sphereLodVerts, sphereLodIdxs, 8, 4, 0.5f);
        sphereLodMesh_.Create(ctx_, sphereLodVerts, sphereLodIdxs);

        std::vector<Scene::Vertex> capLodVerts;
        std::vector<uint32_t> capLodIdxs;
        Scene::BuildCapsuleVertices(capLodVerts, capLodIdxs, 8, 3, 0.5f, 0.7f);
        capsuleLodMesh_.Create(ctx_, capLodVerts, capLodIdxs);
    }

    // ---- 音频系统：初始化设备 + 尝试加载背景音乐（S1 3D 空间化 / S2 总线混音） ----
    if (audioEngine_.IsValid())
    {
        audioEngine_.SetMasterVolume(0.5f);
        LOG_INFO("音频总线初始化完成: Master → {Music, SFX}（miniaudio 节点图，独立音量/静音）");
        LOG_INFO("3D 空间化已启用: miniaudio spatializer（监听器随活跃相机逐帧同步）");
        if (audioEngine_.HasProceduralSfx())
            LOG_INFO("内置程序化音效就绪: Click / Spawn / Impact（Play3D 空间化播放）");
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
            uiRuntime_.OnSwapchainRecreated(renderer_.GetSwapchain()); // U1-UI：帧缓冲随交换链重建
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
    // 一次性资源初始化：实例缓冲统一分配极大值容量（覆盖最大场景 openworld ~8K + glTF 演示物体），
    // 运行时切换场景不再重建 GPU 缓冲——UpdateRenderables 每帧按 ECS 重新填充实例数据。
    // 场景构建 / 灯光 / 取景 / 物理 / 实例容量统一由 BuildAndLoadScene 处理（初始启动与
    // 编辑器"场景"下拉框切换共用同一实现，保证行为一致）。
    const uint32_t kMaxInstances = 16384u;
    LoadGltfAsset(kMaxInstances);

    cubeInstances_.Create(ctx_, kMaxInstances);
    torusInstances_.Create(ctx_, kMaxInstances);
    groundInstances_.Create(ctx_, kMaxInstances);
    sphereInstances_.Create(ctx_, kMaxInstances);
    capsuleInstances_.Create(ctx_, kMaxInstances);

    // 初始场景（由 --scene 指定，默认 default）
    BuildAndLoadScene(config_.sceneKind);

    LOG_INFO("InitScene 完成: " << ecsScene_.ObjectCount() << " 个实体");

    // 冒烟验收钩子：--demo-person 时场景中央生成一名默认人物（球/胶囊渲染接入的端到端验证）
    if (config_.demoPerson)
    {
        Scene::PersonParams demo = Scene::PersonParams::Default();
        demo.position = glm::vec3(0.0f, 0.0f, 0.0f);
        demo.height = 1.5f;
        demo.pose = Scene::PersonPose::Walking;
        const int idx = personHost_.SpawnPerson(demo);
        personHost_.Update(0.0f); // U1-E3：编辑态冻结后一次性写入静态姿态（运行态仍每帧推进）
        RepackScene();
        RecalculateTriangleCount();
        EnsureInstanceCapacities();
        LOG_INFO("demo-person: SpawnPerson 返回 " << idx << "，人物总数 " << personHost_.Count() << "，实体数 "
                                                  << ecsScene_.ObjectCount());
        (void)idx;
    }

    // A1 演示钩子（--demo-events）：内置轨事件名设为 "click"，并自动进入 Play 态，
    // 使 AnimationEventPlayer 在运行期推进时间轴、经仿真段 DrainFiredEvents() 消费事件
    // （日志 + "click"->SfxId::Click 路径可见）。须在主循环首帧前完成（事件播放器首帧构建）。
    if (config_.demoEvents)
    {
        animationHost_.SetDemoEventName("click");
        EnterPlayMode();
    }
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

// ========================================================================
// U1-E3 Play Mode（编辑态/运行态分离）
// ========================================================================

// 进入运行态：拍全量场景快照存为"编辑态底稿"（含实体/属性/父子层级/自转角）。
// 此后仿真系统按运行态推进；运行期间的场景变化属于运行时状态（不入撤销栈），
// Stop 时用底稿经 RestoreScene（LoadPacket 全量重建）还原。
void Application::EnterPlayMode()
{
    if (!playMode_.EnterPlay(Snapshot()))
        return;
    LOG_INFO("进入 Play 模式（Ctrl+P / 停止按钮退出；运行期变化不入撤销栈，停止时还原场景）");
}

// 停止运行：取回编辑态底稿并 RestoreScene 全量还原（实体增删/属性/父子层级/自转角
// 全部回到进入 Play 前），回到编辑态。还原是"恢复"而非"编辑"，不入撤销栈。
void Application::StopPlayMode()
{
    SceneSnapshot baseline;
    if (!playMode_.Stop(baseline))
        return;
    RestoreScene(baseline);
    // 运行期实体可能经编辑路径增删：实例缓冲按需扩容防御（LoadPacket 全量重建后实体数可能超初始容量）
    EnsureInstanceCapacities();
    // 运行期粒子是运行时特效（不在场景快照内）：清空并生成空实例表，
    // 让本帧粒子路径把 GPU 缓冲呈现为空，避免编辑态残留冻结粒子
    particleHost_.system.Clear();
    particleHost_.Update(0.0f);
    LOG_INFO("退出 Play 模式：场景已还原到进入 Play 前（撤销栈不受影响）");
}

// 面板播放/暂停/停止按钮 + Ctrl+P 的统一消费点（每帧一次，先于仿真门）。
// 按钮请求由 EditorPanel 置位、此处消费后重置；状态指示回写 playModeState 供面板显示。
void Application::UpdatePlayModeRequests()
{
    if (editorPanel_.playRequested)
    {
        editorPanel_.playRequested = false;
        if (playMode_.IsEditor())
            EnterPlayMode();
    }
    if (editorPanel_.pauseRequested)
    {
        editorPanel_.pauseRequested = false;
        if (playMode_.TogglePause())
        {
            const char* pauseMsg =
                playMode_.IsPaused() ? "Play 模式: 已暂停（推进冻结，状态保持）" : "Play 模式: 继续运行";
            LOG_INFO(pauseMsg);
        }
    }
    if (editorPanel_.stopRequested)
    {
        editorPanel_.stopRequested = false;
        if (playMode_.IsActive())
            StopPlayMode();
    }
    editorPanel_.playModeState = static_cast<int>(playMode_.State());
}

// 场景编辑命令统一收口（U1-E3 撤销栈边界）：仅编辑态入撤销栈。
// 运行/暂停期间的增删/改父/生成人物等路径照常修改场景（运行时状态），只是不留编辑历史；
// Stop 的全量还原直接调 RestoreScene，不经本函数（它是恢复不是编辑）。
void Application::ExecuteEditCommand(std::unique_ptr<Game::Command> cmd)
{
    if (!playMode_.AllowsSceneEditCommands())
        return;
    commandStack_.Execute(std::move(cmd));
}

void Application::HandlePropertyEditUndo(const SceneSnapshot& frameStart)
{
    // U1-E3 撤销栈边界：运行/暂停期间的属性/Gizmo/脚本字段编辑属于运行时状态
    // （变化照常生效但不入撤销栈）。手势跟踪全部丢弃，但仍消费 suppressEditGesture_
    // （与编辑态同语义），避免 Stop 回编辑态后残留单帧抑制。
    if (!playMode_.AllowsSceneEditCommands())
    {
        editGestureActive_ = false;
        propertyEditBefore_.reset();
        scriptEditBefore_.clear();
        gizmoEditActive_ = false;
        gizmoEditBefore_.reset();
        suppressEditGesture_ = false; // 每帧消费一次
        return;
    }

    // 本帧已执行显式命令（增删/撤销/重做/右键生成）：放弃手势记录，避免与显式命令重复
    if (suppressEditGesture_)
    {
        editGestureActive_ = false;
        propertyEditBefore_.reset();
        scriptEditBefore_.clear();
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
        // U1-S1d：脚本字段手势与路径 A 同边沿；基线取 Update 阶段拉取的绘制前值表
        // （路径 A 的 frameStart 同语义——两者都在本帧 UI 绘制前定格）
        scriptEditBefore_ = scripts_.PolledFieldValues();
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

    // 路径 C（U1-S1d）：脚本字段手势提交。ScriptFieldValuesCommand Do/Undo 经
    // CSharpHost::ApplyFieldValues 把 before/after 值表写回托管实例（Ctrl+Z 还原字段值）。
    if (!active && !scriptEditBefore_.empty())
    {
        std::vector<Script::ScriptFieldTable> scriptAfter;
        scripts_.CaptureFieldValues(scriptAfter);
        if (!Script::ScriptFieldTablesEqual(scriptEditBefore_, scriptAfter))
        {
            commandStack_.Execute(std::make_unique<Game::ScriptFieldValuesCommand>(
                &scripts_, scripts_.BindingIdentities(), scriptEditBefore_, scriptAfter, "编辑脚本字段"));
            LOG_INFO("撤销栈: 编辑脚本字段（" << scriptEditBefore_.size() << " 个脚本 / "
                                              << (scriptEditBefore_.empty() ? 0 : scriptEditBefore_[0].size())
                                              << " 个字段）");
        }
        scriptEditBefore_.clear();
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

    // ECS 组件化自转系统：Spin.angle += speed*dt（包投影由 RepackScene 统一输出）。
    // U1-E3：仅运行态推进；编辑态/暂停自转冻结（编辑态场景静态——与 Unity 行为对齐）。
    if (playMode_.ShouldSimulate())
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
// （UpdateRenderables），不再依赖本包。仅运行态每帧重建（物理/自转改写 ECS）；
// 编辑态由各编辑点显式调用本函数或直接赋值 scene_，无需每帧投影。
void Application::RepackScene()
{
    scene_ = ecsScene_.BuildPacket(&spinAngles_);
}

void Application::BuildAndLoadScene(const std::string& kind)
{
    const bool sliceScene = (kind == "slice");
    const bool openWorldScene = (kind == "openworld");
    const bool cyberCityScene = (kind == "cybercity");
    const bool voxelScene = (kind == "voxel");
    voxelMode_ = voxelScene;
    // 方块世界默认进「纯游戏模式」：收起编辑器面板，只留准星 + 方块世界 HUD。
    // --editor-ui 可让启动即展开；运行期按 F1 来回切（voxelPlayMode_ 为其取反）。
    voxelPlayMode_ = !config_.editorUiInVoxel;

    // ---- 1. 构建物体列表（纯函数，确定性输出）----
    std::vector<Scene::SceneObject> objs;
    if (sliceScene)
    {
        objs = Sample::VerticalSlice::BuildSliceScene();
    }
    else if (openWorldScene)
    {
        objs = Sample::OpenWorld::BuildOpenWorldScene();
    }
    else if (cyberCityScene)
    {
        city_ = Sample::Showcase::BuildCyberCity();
        objs = city_.objects;
    }
    else if (voxelScene)
    {
        // 方块世界：场景物体列表留空，地形完全由体素区块网格单独绘制
    }
    else
    {
        objs = Scene::BuildDefaultScene();
        if (!hasTorus_)
        {
            objs.erase(
                std::remove_if(objs.begin(), objs.end(), [](const Scene::SceneObject& obj) { return obj.meshId != 0; }),
                objs.end());
        }
    }

    // ---- 2. glTF 演示物体（材质贴图映射活样本；切片 / openworld 规格锁定不掺入）----
    if (hasGltf_ && !sliceScene && !openWorldScene && !voxelScene)
    {
        if (cyberCityScene)
        {
            const glm::vec3 spots[] = {glm::vec3(-7.5f, 0.0f, -7.5f), glm::vec3(7.5f, 0.0f, -7.5f)};
            for (int i = 0; i < 2; ++i)
            {
                Scene::SceneObject demo;
                demo.position = spots[i];
                demo.scale = 1.6f;
                demo.tint = glm::vec3(1.0f);
                demo.meshId = 2;
                demo.metallic = 1.0f;
                demo.roughness = 1.0f;
                demo.spinSpeed = 22.0f + static_cast<float>(i) * 8.0f;
                demo.phase = static_cast<float>(i) * 90.0f;
                objs.push_back(demo);
            }
            LOG_INFO("glTF 演示物体已加入展示厅（广场两侧旋转样本）");
        }
        else
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
    }

    // ---- 3. 灌入 ECS 并投影 ----
    ecsScene_.LoadPacket(objs);
    RepackScene();
    selectedObject_ = -1;

    // NavMesh 后端接线：当启用 NavMesh 后端时，从真实 ECS 场景几何（而非程序化演示）烘焙可行走区域。
    // ECS 此时已是权威存储；BuildFromEcsScene 内部按 meshId 提取世界空间立方体三角形。
    if (navHost_.useNavMesh)
        navHost_.BuildFromEcsScene(ecsScene_);

    // ---- 4. 取景（赛博城市由 InitCyberCity 单独处理）----
    if (sliceScene)
    {
        const Sample::VerticalSlice::SliceSceneStats stats = Sample::VerticalSlice::ComputeSliceStats(objs);
        LOG_INFO("垂直切片场景: " << stats.totalEntities << " 实体（静止 " << stats.staticCount << " / 自转 "
                                  << stats.spinnerCount << "，静止占比 " << stats.staticRatio << "），父子链 "
                                  << stats.chainCount << " 条（层数 " << stats.minChainDepth << "~"
                                  << stats.maxChainDepth << "）");
        camera_.SetTarget(glm::vec3(0.0f, 2.0f, 0.0f));
        camera_.SetDistance(40.0f);
    }
    else if (openWorldScene)
    {
        const Sample::OpenWorld::OpenWorldStats stats = Sample::OpenWorld::ComputeOpenWorldStats(objs);
        LOG_INFO("开放世界场景: " << stats.totalEntities << " 实体（静止 " << stats.staticCount << " / 动态 "
                                  << stats.dynamicCount << "，静止占比 " << stats.staticRatio << "），父子链 "
                                  << stats.chainCount << " 条（层数 " << stats.minChainDepth << "~"
                                  << stats.maxChainDepth << "），区块 " << stats.chunkCount << "（Near "
                                  << stats.nearChunks << " / Mid " << stats.midChunks << " / Far " << stats.farChunks
                                  << " / Outer " << stats.outerChunks << "）");
        camera_.SetTarget(glm::vec3(0.0f, 5.0f, 0.0f));
        camera_.SetDistance(20.0f);
    }

    // ---- 5. 灯光 / 展台 / 碰撞 ----
    if (cyberCityScene)
    {
        InitCyberCity();
    }
    else
    {
        pointLights_ = BuildDefaultPointLights();
        if (!pointLights_.empty())
            pointLights_[0].castsShadow = true; // 演示：默认启用 1 号灯投影阴影

        // 复位为默认光照（避免从 openworld 等已调亮场景继承偏亮/偏暗参数）
        lightParams_.ambient = 0.15f;
        lightParams_.intensity = 3.0f;
        if (!config_.exposure)
            lightParams_.exposure = 1.0f;

        showcase_.Reset();
        showcase_.SetActive(false);
        fpColliders_.clear();
        navHost_.enabled = false;
        navHost_.agentEnabled = false;
        cameraMode_ = CameraMode::Orbit;
        if (window_)
            window_->SetCursorLocked(false);
        fpController_.Teleport(glm::vec3(0.0f, 0.0f, 0.0f));

        // 开放世界：户外大场景默认偏暗，整体调亮（曝光 / 环境光 / 太阳光 + 填充点光）
        if (openWorldScene)
        {
            lightParams_.ambient = 0.35f;
            lightParams_.intensity = 4.5f;
            if (!config_.exposure)
                lightParams_.exposure = 1.7f;
            for (auto& pl : pointLights_)
            {
                pl.intensity *= 1.5f;
                pl.radius *= 1.3f;
            }
        }
    }

    // ---- 6. 地面材质（赛博城市湿滑沥青 vs 其余）----
    UploadGround(cyberCityScene);

    // ---- 7. 物理 / 统计 / 扩容 ----
    physicsHost_.RebuildBodies();
    RecalculateTriangleCount();
    EnsureInstanceCapacities();

    currentSceneKind_ = kind;
    LOG_INFO("场景已加载: " << kind << "（" << ecsScene_.ObjectCount() << " 个实体）");
}

void Application::UploadGround(bool cyberCity)
{
    // 地面实例数据恒定（恒等模型 + 固定材质），仅在场景切换时按需重传
    Render::InstanceData ground{};
    ground.tint = glm::vec4(1.0f);
    ground.metallic = 0.0f;
    ground.roughness = 0.9f;
    if (cyberCity)
    {
        // 赛博城市：湿滑沥青——低粗糙度 + 中等金属度，把霓虹与天空映进地面
        ground.tint = glm::vec4(0.15f, 0.16f, 0.20f, 1.0f);
        ground.metallic = 0.55f;
        ground.roughness = 0.18f;
    }
    groundInstances_.Upload(ctx_, &ground, 1);
}

void Application::UpdateCamera()
{
    // ---- 双模式相机切换：Tab 边沿触发（Orbit <-> FirstPerson） ----
    const bool camKey = window_->IsKeyDown(Window::kKeyTab);
    const bool fpNow = (cameraMode_ == CameraMode::FirstPerson);
    if (camKey && !prevCameraMode_)
    {
        if (fpNow)
        {
            // FP -> Orbit：把位置/朝向同步回轨道相机（保留视觉连续性）
            fpCamera_.SyncToOrbit(camera_);
            cameraMode_ = CameraMode::Orbit;
            window_->SetCursorLocked(false); // 释放光标，交还给编辑器面板
            LOG_INFO("相机模式: 轨道相机（Orbit）");
        }
        else
        {
            // Orbit -> FP：从轨道相机接管位置与朝向
            fpCamera_.SyncFromOrbit(camera_);
            cameraMode_ = CameraMode::FirstPerson;
            // 从眼位反推脚底，避免切换后人物「陷进地面」或悬空
            fpController_.Teleport(
                glm::vec3(fpCamera_.Position().x, fpCamera_.Position().y - 1.70f, fpCamera_.Position().z));
            window_->SetCursorLocked(true);
            LOG_INFO("相机模式: 第一人称漫游（FP）—— WASD 移动 / 空格跳跃 / Ctrl 蹲下 / Shift 冲刺");
        }
    }
    // Esc：释放光标（不切模式，方便临时点编辑器面板）；再按 Tab 才回到轨道相机
    const bool escDown = window_->IsKeyDown(Window::kKeyEscape);
    if (escDown && !escHeld_ && window_->IsCursorLocked())
    {
        window_->SetCursorLocked(false);
        LOG_INFO("第一人称：光标已释放（Tab 回到轨道相机 / 点击画面重新锁定）");
    }
    escHeld_ = escDown;
    prevCameraMode_ = camKey;

    // 左键点击画面：光标释放状态下重新锁定（FP 模式沉浸回归）
    if (cameraMode_ == CameraMode::FirstPerson && !window_->IsCursorLocked() &&
        window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !ImGui::GetIO().WantCaptureMouse &&
        !uiRuntime_.Blocked())
    {
        window_->SetCursorLocked(true);
    }

    if (cameraMode_ == CameraMode::FirstPerson)
    {
        // 陆行 / 飞行移动（重力、跳跃、蹲伏、碰撞滑动；V 键切换飞行俯瞰）
        UpdateFirstPersonMovement();

        const VkExtent2D frameExtent = renderer_.Extent();
        const float aspect = frameExtent.height > 0
                                 ? static_cast<float>(frameExtent.width) / static_cast<float>(frameExtent.height)
                                 : 1.0f;
        const Render::PostProcessor* pp = renderer_.GetPostProcessor();
        const glm::vec2 jitter =
            postProcessSync_.AdvanceJitter(postProcessSync_.taaEnabled && pp && pp->UseMsaa(), frameExtent);
        fpCamera_.SetJitter(jitter.x, jitter.y);
        fpCamera_.Update(aspect);
        return;
    }

    const auto [dx, dy] = window_->GetCursorDelta();
    if (window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !gizmoDragging_ && !ImGui::GetIO().WantCaptureMouse &&
        !uiRuntime_.Blocked())
        camera_.Orbit(static_cast<float>(dx), static_cast<float>(dy));
    camera_.Zoom(window_->ConsumeScrollDelta());

    // 相机碰撞防护：持续放大（distance↓）时若相机穿入物体包围球内部，
    // 所有可见面均成背面→背面剔除全黑。沿当前视线方向求解退出距离，
    // 强制 distance 不小于该值（额外 5% margin 防数值抖动）。
    // 包围球用立方体外接球（kCubeBoundingRadius×scale），覆盖默认场景中全部物体。
    float minSafe = camera_.GetMinDistance();
    const glm::vec3 target = camera_.Target();
    const glm::vec3 camDir = glm::normalize(camera_.ComputePosition() - target);
    ecsScene_.ForEachRenderable(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable&, const Scene::ecs::Spin&)
        {
            const float radius = t.scale * Scene::kCubeBoundingRadius * 1.05f;
            const glm::vec3 u = t.position - target; // target → center
            const float udotd = glm::dot(u, camDir); // u · d
            const float u2 = glm::dot(u, u);         // |u|²
            const float disc = udotd * udotd - (u2 - radius * radius);
            if (disc > 0.0f)
            {
                const float need = -udotd + std::sqrt(disc); // 较大正根 = 退出距离
                if (need > minSafe)
                    minSafe = need;
            }
        });
    camera_.ClampDistance(minSafe);

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
    const glm::vec2 jitter =
        postProcessSync_.AdvanceJitter(postProcessSync_.taaEnabled && pp && pp->UseMsaa(), frameExtent);
    camera_.SetJitter(jitter.x, jitter.y);

    camera_.Update(aspect);
}

void Application::UpdateFirstPersonMovement()
{
    const bool uiBlocking = ImGui::GetIO().WantCaptureMouse || uiRuntime_.Blocked();
    const auto [dx, dy] = window_->GetCursorDelta();
    // 光标锁定（展示厅默认）：鼠标位移直接转视角；未锁定时沿用左键拖拽（兼容触摸/远程桌面）
    const bool rotate =
        window_->IsCursorLocked() ? !uiBlocking : (window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !uiBlocking);
    if (rotate)
        fpCamera_.Rotate(static_cast<float>(dx), static_cast<float>(dy));

    // 滚轮：调节移动速度（FP 模式下语义为速度而非缩放）
    const float scroll = static_cast<float>(window_->ConsumeScrollDelta());
    if (scroll != 0.0f)
        fpWalkSpeed_ = std::clamp(fpWalkSpeed_ + scroll * 0.8f, 0.5f, 24.0f);

    // V：飞行俯瞰 / 陆行 切换（边沿触发）
    const bool vDown = window_->IsKeyDown(Window::kKeyV);
    if (vDown && !fpKeyHeld_)
    {
        fpFlyMode_ = !fpFlyMode_;
        if (fpFlyMode_)
            LOG_INFO("第一人称：飞行俯瞰模式（空格上升 / Ctrl 下降）");
        else
            LOG_INFO("第一人称：陆行模式（空格跳跃 / Ctrl 蹲下 / Shift 冲刺）");
    }
    fpKeyHeld_ = vDown;

    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (window_->IsKeyDown(Window::kKeyW))
        forward += 1.0f;
    if (window_->IsKeyDown(Window::kKeyS))
        forward -= 1.0f;
    if (window_->IsKeyDown(Window::kKeyD))
        right += 1.0f;
    if (window_->IsKeyDown(Window::kKeyA))
        right -= 1.0f;

    if (fpFlyMode_)
    {
        // 飞行：旧语义（空格上升 / Ctrl 下降），无重力无碰撞，便于俯瞰整座城市
        if (window_->IsKeyDown(Window::kKeySpace))
            up += 1.0f;
        if (window_->IsKeyDown(Window::kKeyLeftControl))
            up -= 1.0f;
        fpCamera_.Move(forward, right, up, deltaTime_, fpWalkSpeed_);
        return;
    }

    // ---- 陆行：输入按 yaw 旋转到世界空间后交给 FpController ----
    // 与 FirstPersonCamera::Move 同构：前向 (sin yaw, 0, cos yaw)、右向 (-cos yaw, 0, sin yaw)
    const float yaw = fpCamera_.Yaw();
    const glm::vec3 fwdW(std::sin(yaw), 0.0f, std::cos(yaw));
    const glm::vec3 rightW(-std::cos(yaw), 0.0f, std::sin(yaw));
    const glm::vec3 wish = fwdW * forward + rightW * right;

    Game::FpInput in;
    in.forward = wish.z;
    in.right = wish.x;
    const bool jumpDown = window_->IsKeyDown(Window::kKeySpace);
    in.jump = jumpDown && !fpJumpHeld_;
    fpJumpHeld_ = jumpDown;
    in.crouch = window_->IsKeyDown(Window::kKeyLeftControl);
    in.sprint = window_->IsKeyDown(Window::kKeyLeftShift) || window_->IsKeyDown(Window::kKeyRightShift);

    // 方块世界：刷新流式区块 / 重建变更网格，并把周边实心方块投影为碰撞体
    if (voxelMode_)
    {
        UpdateVoxelWorld();
    }
    fpController_.SetWalkSpeed(fpWalkSpeed_);
    fpController_.Update(deltaTime_, in, fpColliders_);
    fpCamera_.SetPosition(fpController_.EyePosition());
}

namespace
{
// 每帧最多重建的区块数：地形网格生成（面剔除 + AO）是 CPU 密集操作，
// 摊到多帧可让跨区块移动不留卡顿尖峰（首屏初始化另用大预算一次性建完）。
constexpr int kVoxelRebuildBudget = 2;
// 水下雾色（冷蓝）；陆地雾色改由 voxelFogTintLand_ 成员承载，随时段平滑插值
const glm::vec3 kVoxelFogTintWater{0.07f, 0.28f, 0.42f};
} // namespace

const char* Application::VoxelDayName() const noexcept
{
    static const char* kNames[] = {"正午", "黄昏", "夜晚", "清晨"};
    return kNames[(voxelDayIndex_ >= 0 && voxelDayIndex_ < 4) ? voxelDayIndex_ : 0];
}

void Application::ApplyVoxelDayTime(bool immediate)
{
    // 四档时段：太阳方向 / 光色 / 强度 / 环境光 / 天空染色 / 雾色
    struct DayPreset
    {
        glm::vec3 dir;
        glm::vec3 color;
        float intensity;
        float ambient;
        float exposure;
        glm::vec4 sky;
        glm::vec3 fog;
    };
    static const DayPreset kPresets[] = {
        {glm::vec3(0.35f, -1.0f, -0.25f), glm::vec3(1.0f, 0.97f, 0.92f), 3.2f, 0.16f, 1.0f,
         glm::vec4(0.55f, 0.72f, 1.0f, 1.0f), glm::vec3(0.72f, 0.82f, 1.0f)}, // 正午
        {glm::vec3(0.90f, -0.32f, -0.18f), glm::vec3(1.0f, 0.55f, 0.28f), 2.4f, 0.13f, 1.05f,
         glm::vec4(1.0f, 0.52f, 0.32f, 0.85f), glm::vec3(0.85f, 0.58f, 0.48f)}, // 黄昏
        {glm::vec3(-0.40f, -0.80f, 0.50f), glm::vec3(0.45f, 0.56f, 0.88f), 0.85f, 0.09f, 1.25f,
         glm::vec4(0.06f, 0.09f, 0.20f, 0.55f), glm::vec3(0.10f, 0.13f, 0.24f)}, // 夜晚
        {glm::vec3(-0.70f, -0.48f, 0.35f), glm::vec3(1.0f, 0.82f, 0.62f), 2.0f, 0.14f, 1.0f,
         glm::vec4(0.72f, 0.84f, 1.0f, 0.9f), glm::vec3(0.80f, 0.86f, 0.96f)}, // 清晨
    };
    const DayPreset& p = kPresets[(voxelDayIndex_ >= 0 && voxelDayIndex_ < 4) ? voxelDayIndex_ : 0];

    voxelTargetLight_.direction = glm::normalize(p.dir);
    voxelTargetLight_.color = p.color;
    voxelTargetLight_.intensity = p.intensity;
    voxelTargetLight_.ambient = p.ambient;
    voxelTargetLight_.exposure = p.exposure;
    voxelTargetSky_ = p.sky;
    voxelTargetFog_ = p.fog;

    if (immediate)
    {
        lightParams_.direction = voxelTargetLight_.direction;
        lightParams_.color = voxelTargetLight_.color;
        lightParams_.intensity = voxelTargetLight_.intensity;
        lightParams_.ambient = voxelTargetLight_.ambient;
        lightParams_.exposure = voxelTargetLight_.exposure;
        skyTint_ = voxelTargetSky_;
        voxelFogTintLand_ = voxelTargetFog_;
    }
}

void Application::InitVoxelWorld()
{
    // 出生：先流式加载首屏区块，再落到地表之上（避免卡在方块里）
    voxelWorld_.UpdateStreaming(glm::vec3(0.0f, 30.0f, 0.0f));
    const glm::vec3 spawn = voxelWorld_.FindSpawn(0.0f, 0.0f);
    fpController_.Teleport(spawn);
    fpCamera_.SetPosition(fpController_.EyePosition());

    // 体素网格顶点已是世界坐标，故只需一个单位矩阵实例（tint 透传顶点色）
    voxelInstances_.Create(ctx_, 1);
    Render::InstanceData identity{};
    identity.model = glm::mat4(1.0f);
    identity.tint = glm::vec4(1.0f);
    identity.metallic = 0.0f;
    identity.roughness = 0.85f;
    voxelInstances_.Upload(ctx_, &identity, 1);

    voxelLastChunkX_ = voxelWorld_.ChunkCoordOf(spawn.x);
    voxelLastChunkZ_ = voxelWorld_.ChunkCoordOf(spawn.z);
    SyncVoxelChunks();
    RebuildPendingVoxelChunks(64); // 首屏一次性建完，之后才走每帧预算
    voxelReady_ = true;
    LOG_INFO("方块世界就绪：出生点 (" << spawn.x << ", " << spawn.y << ", " << spawn.z << ")");
    // 体素世界靠视距加载，远处必须有雾把区块边界淡出，否则会看到世界被"切开"的硬边
    postProcessSync_.fogEnabled = true;
    postProcessSync_.fogDensity = 0.028f;
    postProcessSync_.fogTint = voxelFogTintLand_;
    ApplyVoxelDayTime(true); // 时段光照 / 天空 / 雾色（首帧直接落位）
}

void Application::RebuildVoxelMeshes()
{
    SyncVoxelChunks();
    // 每帧只重建少量区块：跨区块移动时网格生成被摊平到多帧，避免掉帧尖峰
    RebuildPendingVoxelChunks(kVoxelRebuildBudget);
}

void Application::SyncVoxelChunks()
{
    const auto loaded = voxelWorld_.LoadedChunks();
    const auto& cfg = voxelWorld_.Config();

    // 移除已卸载区块
    for (auto it = voxelChunks_.begin(); it != voxelChunks_.end();)
    {
        const bool still = std::any_of(loaded.begin(), loaded.end(), [&](const Sample::Voxel::VoxelWorld::ChunkCoord& c)
                                       { return c.x == it->cx && c.z == it->cz; });
        if (!still)
        {
            it->mesh.Destroy();
            it = voxelChunks_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 新增区块：仅登记槽位，网格留待分帧重建
    for (const auto& coord : loaded)
    {
        const bool exists = std::any_of(voxelChunks_.begin(), voxelChunks_.end(),
                                        [&](const VoxelChunkGpu& g) { return g.cx == coord.x && g.cz == coord.z; });
        if (exists)
            continue;
        VoxelChunkGpu gpu;
        gpu.cx = coord.x;
        gpu.cz = coord.z;
        gpu.uploaded = false;
        const glm::vec3 bmin(static_cast<float>(coord.x * cfg.chunkX), 0.0f, static_cast<float>(coord.z * cfg.chunkZ));
        const glm::vec3 bmax(bmin.x + static_cast<float>(cfg.chunkX), static_cast<float>(cfg.height),
                             bmin.z + static_cast<float>(cfg.chunkZ));
        gpu.center = (bmin + bmax) * 0.5f;
        gpu.radius = glm::length(bmax - bmin) * 0.5f;
        voxelChunks_.push_back(std::move(gpu));
    }
}

void Application::RebuildPendingVoxelChunks(int budget)
{
    // 近处优先：玩家跑动 / 大视距时，先建脚下的区块，远处排队靠后 ——
    // 否则会出现「眼前是空的，远处倒先冒出来」的观感问题。
    const glm::vec3 feet = fpController_.FeetPosition();
    std::vector<VoxelChunkGpu*> pending;
    pending.reserve(voxelChunks_.size());
    for (VoxelChunkGpu& gpu : voxelChunks_)
    {
        if (!gpu.uploaded)
            pending.push_back(&gpu);
    }
    if (pending.empty())
        return;
    if (pending.size() > 1u)
    {
        std::sort(pending.begin(), pending.end(),
                  [&feet](const VoxelChunkGpu* a, const VoxelChunkGpu* b)
                  {
                      const float dax = a->center.x - feet.x;
                      const float daz = a->center.z - feet.z;
                      const float dbx = b->center.x - feet.x;
                      const float dbz = b->center.z - feet.z;
                      return (dax * dax + daz * daz) < (dbx * dbx + dbz * dbz);
                  });
    }
    const size_t count = std::min<size_t>(static_cast<size_t>(budget), pending.size());
    for (size_t i = 0; i < count; ++i)
    {
        VoxelChunkGpu& gpu = *pending[i];
        const Sample::Voxel::VoxelMesh data = voxelWorld_.BuildChunkMesh(gpu.cx, gpu.cz);
        gpu.mesh.Destroy();
        if (!data.vertices.empty() && !data.indices.empty())
            gpu.mesh.Create(ctx_, data.vertices, data.indices);
        gpu.uploaded = true;
    }
}

void Application::MarkVoxelChunkDirty(int cx, int cz)
{
    for (VoxelChunkGpu& gpu : voxelChunks_)
    {
        if (gpu.cx == cx && gpu.cz == cz)
            gpu.uploaded = false;
    }
}

void Application::UpdateVoxelWorld()
{
    if (!voxelReady_)
    {
        InitVoxelWorld();
        return;
    }

    const glm::vec3 feet = fpController_.FeetPosition();
    const int cx = voxelWorld_.ChunkCoordOf(feet.x);
    const int cz = voxelWorld_.ChunkCoordOf(feet.z);

    // 跨区块移动或发生过编辑时才重建网格（避免每帧无谓重建）
    if (cx != voxelLastChunkX_ || cz != voxelLastChunkZ_)
    {
        voxelWorld_.UpdateStreaming(feet);
        RebuildVoxelMeshes(); // 同步区块集合（新区块只是登记，网格留待分帧）
        voxelLastChunkX_ = cx;
        voxelLastChunkZ_ = cz;
    }

    // 每帧推进重建队列：新加载区块与编辑过的区块都在这里消化
    RebuildVoxelMeshes();

    // 玩家身体中心（脚底 + 半身高）周边的实心方块 -> FP 碰撞体
    const glm::vec3 bodyCenter = feet + glm::vec3(0.0f, 0.9f, 0.0f);
    voxelWorld_.CollectColliders(bodyCenter, glm::vec3(0.35f, 0.9f, 0.35f), fpColliders_);

    // 时段过渡：向目标光照 / 天空 / 雾色平滑逼近（约 0.4s 收敛，避免硬跳变）
    {
        const float k = std::min(1.0f, deltaTime_ * 2.5f);
        auto mixf = [k](float a, float b) { return a + (b - a) * k; };
        lightParams_.direction = glm::normalize(glm::mix(lightParams_.direction, voxelTargetLight_.direction, k));
        lightParams_.color = glm::mix(lightParams_.color, voxelTargetLight_.color, k);
        lightParams_.intensity = mixf(lightParams_.intensity, voxelTargetLight_.intensity);
        lightParams_.ambient = mixf(lightParams_.ambient, voxelTargetLight_.ambient);
        lightParams_.exposure = mixf(lightParams_.exposure, voxelTargetLight_.exposure);
        skyTint_ = glm::mix(skyTint_, voxelTargetSky_, k);
        voxelFogTintLand_ = glm::mix(voxelFogTintLand_, voxelTargetFog_, k);
    }

    // 头部所在格是否为水体：游泳手感（浮力 / 减速 / 空格上浮）+ 水下浓雾
    const glm::vec3 head = fpCamera_.Position();
    voxelInWater_ = voxelWorld_.Get(static_cast<int>(std::floor(head.x)), static_cast<int>(std::floor(head.y)),
                                    static_cast<int>(std::floor(head.z))) == Sample::Voxel::BlockType::Water;
    fpController_.SetInWater(voxelInWater_);
    // 水下雾：能见度骤降 + 冷色压迫感；出水后恢复常规地形雾
    postProcessSync_.fogEnabled = true;
    postProcessSync_.fogDensity = voxelInWater_ ? 0.34f : 0.028f;
    postProcessSync_.fogTint = voxelInWater_ ? kVoxelFogTintWater : voxelFogTintLand_;

    // 准星瞄准：每帧一次射线，高亮描边与挖掘/放置共用，避免重复求交
    voxelAim_ = voxelWorld_.Raycast(fpCamera_.Position(), fpCamera_.Forward(), 6.0f);
    voxelAimValid_ = voxelAim_.hit;

    HandleVoxelInteraction();
}

void Application::HandleVoxelInteraction()
{
    voxelPlaceBlocked_ = false;

    // 数字键 1~6 切换待放置方块（不受光标锁定限制，随时可切）
    static const Sample::Voxel::BlockType kPalette[] = {
        Sample::Voxel::BlockType::Stone, Sample::Voxel::BlockType::Grass, Sample::Voxel::BlockType::Dirt,
        Sample::Voxel::BlockType::Sand,  Sample::Voxel::BlockType::Wood,  Sample::Voxel::BlockType::Leaves};
    for (int i = 0; i < 6; ++i)
    {
        if (window_->IsKeyDown(Window::kKey1 + i))
            voxelPlaceBlock_ = kPalette[i];
    }
    // [F1] 纯游戏模式 / 编辑器面板 显隐切换（边沿触发；不受光标锁定限制）
    const bool panelKeyDown = window_->IsKeyDown(Window::kKeyF1);
    if (panelKeyDown && !voxelPanelKeyHeld_)
    {
        voxelPlayMode_ = !voxelPlayMode_;
        LOG_INFO("方块世界：编辑器面板 " << (voxelPlayMode_ ? "已收起（纯游戏模式）" : "已展开（可调参）"));
    }
    voxelPanelKeyHeld_ = panelKeyDown;

    // [F] 光标锁定开关：HUD 长期写着 [F] 光标锁定，但该键从未接线（本次补上）
    const bool lockKeyDown = window_->IsKeyDown(Window::kKeyF);
    if (lockKeyDown && !voxelLockKeyHeld_)
    {
        const bool lock = !window_->IsCursorLocked();
        window_->SetCursorLocked(lock);
        LOG_INFO("方块世界：光标" << (lock ? "已锁定" : "已释放"));
    }
    voxelLockKeyHeld_ = lockKeyDown;

    // [T] 循环时段（正午 / 黄昏 / 夜晚 / 清晨）：改变太阳方向与光色、天空与雾色
    const bool dayDown = window_->IsKeyDown(Window::kKeyT);
    if (dayDown && !voxelDayHeld_)
    {
        voxelDayIndex_ = (voxelDayIndex_ + 1) % 4;
        ApplyVoxelDayTime(false);
        LOG_INFO("方块世界：时段切换为 " << VoxelDayName());
    }
    voxelDayHeld_ = dayDown;

    // '[' / ']' 调整流式视距（边沿触发；不受光标锁定限制，随时可切）
    const bool viewUp = window_->IsKeyDown(Window::kKeyRightBracket);
    const bool viewDown = window_->IsKeyDown(Window::kKeyLeftBracket);
    const bool viewUpEdge = viewUp && !voxelViewUpHeld_;
    const bool viewDownEdge = viewDown && !voxelViewDownHeld_;
    voxelViewUpHeld_ = viewUp;
    voxelViewDownHeld_ = viewDown;
    if (viewUpEdge || viewDownEdge)
    {
        const int cur = voxelWorld_.Config().viewRadius;
        if (voxelWorld_.SetViewRadius(viewUpEdge ? cur + 1 : cur - 1))
        {
            // 立即按新半径重新流式加载并同步槽位（网格仍走每帧预算，不会卡帧）
            voxelWorld_.UpdateStreaming(fpController_.FeetPosition());
            RebuildVoxelMeshes();
            LOG_INFO("方块世界：视距 " << voxelWorld_.Config().viewRadius << " 区块");
        }
    }

    if (!window_->IsCursorLocked())
        return; // 仅在第一人称光标锁定（游玩）状态下响应挖掘 / 放置

    const bool leftDown = window_->IsMouseButtonDown(Window::kMouseButtonLeft);
    const bool rightDown = window_->IsMouseButtonDown(Window::kMouseButtonRight);

    // 放置保持边沿（避免拖出一串方块）；挖掘支持按住连挖（固定间隔，手感接近 MC）
    constexpr float kMineRepeat = 0.22f;
    bool acted = false;
    if (leftDown && !voxelLeftHeld_)
    {
        acted = true;
        voxelMineTimer_ = kMineRepeat;
    }
    else if (rightDown && !voxelRightHeld_)
    {
        acted = true;
    }
    else if (leftDown)
    {
        voxelMineTimer_ -= deltaTime_;
        if (voxelMineTimer_ <= 0.0f)
        {
            acted = true;
            voxelMineTimer_ = kMineRepeat;
        }
    }
    voxelLeftHeld_ = leftDown;
    voxelRightHeld_ = rightDown;
    if (!acted)
        return;

    if (!voxelAimValid_)
        return;
    const Sample::Voxel::VoxelHit hit = voxelAim_;

    if (leftDown)
    {
        if (hit.block == Sample::Voxel::BlockType::Bedrock)
            return; // 基岩不可破坏，防止挖穿世界底部
        voxelWorld_.Set(hit.x, hit.y, hit.z, Sample::Voxel::BlockType::Air);
        // 立刻重算准星：否则按住连挖会一直指向已经消失的那一格
        voxelAim_ = voxelWorld_.Raycast(fpCamera_.Position(), fpCamera_.Forward(), 6.0f);
        voxelAimValid_ = voxelAim_.hit;
    }
    else
    {
        // 放置：命中方块沿入射面外法线偏移一格
        const int px = hit.x + static_cast<int>(hit.normal.x);
        const int py = hit.y + static_cast<int>(hit.normal.y);
        const int pz = hit.z + static_cast<int>(hit.normal.z);
        if (voxelWorld_.Get(px, py, pz) != Sample::Voxel::BlockType::Air)
        {
            voxelPlaceBlocked_ = true;
            return;
        }
        // 不得把方块放进玩家自身体积：否则会把角色卡死在实心里
        const glm::vec3 feet = fpController_.FeetPosition();
        const glm::vec3 blockMin(static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz));
        const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);
        const glm::vec3 bodyMin(feet.x - 0.35f, feet.y, feet.z - 0.35f);
        const glm::vec3 bodyMax(feet.x + 0.35f, feet.y + 1.8f, feet.z + 0.35f);
        constexpr float kSkin = 0.02f; // 贴边不算重叠，避免贴墙站立时无法放置
        const bool overlapsBody = (blockMin.x < bodyMax.x - kSkin) && (blockMax.x > bodyMin.x + kSkin) &&
                                  (blockMin.y < bodyMax.y - kSkin) && (blockMax.y > bodyMin.y + kSkin) &&
                                  (blockMin.z < bodyMax.z - kSkin) && (blockMax.z > bodyMin.z + kSkin);
        if (overlapsBody)
        {
            voxelPlaceBlocked_ = true;
            return;
        }
        voxelWorld_.Set(px, py, pz, voxelPlaceBlock_);
    }

    // 精确标记受影响区块：目标格所在区块 + 落在边界时的相邻区块（邻接面需重算）
    const auto& cfg = voxelWorld_.Config();
    auto markDirtyAt = [&](int bx, int bz)
    {
        const int ccx = voxelWorld_.ChunkCoordOf(static_cast<float>(bx));
        const int ccz = voxelWorld_.ChunkCoordOf(static_cast<float>(bz));
        const int lx = bx - ccx * cfg.chunkX;
        const int lz = bz - ccz * cfg.chunkZ;
        MarkVoxelChunkDirty(ccx, ccz);
        if (lx == 0)
            MarkVoxelChunkDirty(ccx - 1, ccz);
        if (lx == cfg.chunkX - 1)
            MarkVoxelChunkDirty(ccx + 1, ccz);
        if (lz == 0)
            MarkVoxelChunkDirty(ccx, ccz - 1);
        if (lz == cfg.chunkZ - 1)
            MarkVoxelChunkDirty(ccx, ccz + 1);
    };
    markDirtyAt(hit.x, hit.z);
}
void Application::UpdateShowcase()
{
    if (!showcase_.Active())
        return;

    // 展台注视解算（准星交互）
    showcase_.Update(ActivePosition(), ActiveForward());
    if (welcomeTimer_ > 0.0f)
        welcomeTimer_ -= deltaTime_;

    // 连续昼夜推进（自动循环 / 向目标锚点过渡）+ 霓虹呼吸，然后整体落地到光照与后处理
    showcaseClock_ += deltaTime_;
    showcase_.AdvanceTime(deltaTime_);
    UpdateNeonPulse();
    ApplyAtmosphere();

    const bool eDown = window_->IsKeyDown(Window::kKeyE);
    if (eDown && !eKeyHeld_)
    {
        if (const Sample::Showcase::CyberExhibit* ex = showcase_.Focused())
            ToggleFeature(ex->featureId);
    }
    eKeyHeld_ = eDown;

    // 数字键 1..9 直达特性（绕过注视）
    for (int d = 1; d <= 9; ++d)
    {
        const bool down = window_->IsKeyDown(Window::kKey1 + (d - 1));
        if (down && !digitHeld_[static_cast<std::size_t>(d)])
            ToggleFeature(d);
        digitHeld_[static_cast<std::size_t>(d)] = down;
    }

    const bool tDown = window_->IsKeyDown(Window::kKeyT);
    if (tDown && !tKeyHeld_)
    {
        showcase_.CycleTimeOfDay();
        showcase_.MarkFeature(static_cast<int>(Sample::Showcase::FeatureId::TimeOfDay));
        LOG_INFO("展示厅：切换时段 -> " << showcase_.Preset().name);
    }
    tKeyHeld_ = tDown;

    const bool gDown = window_->IsKeyDown(Window::kKeyG);
    if (gDown && !gKeyHeld_)
    {
        showcase_.CycleLook();
        LOG_INFO("展示厅：画面风格 -> " << showcase_.Look().name);
    }
    gKeyHeld_ = gDown;

    const bool oDown = window_->IsKeyDown(Window::kKeyO);
    if (oDown && !oKeyHeld_)
    {
        showcase_.ToggleAutoCycle();
        LOG_INFO("展示厅：自动昼夜循环 " << (showcase_.AutoCycle() ? "开" : "关"));
    }
    oKeyHeld_ = oDown;

    const bool hDown = window_->IsKeyDown(Window::kKeyH);
    if (hDown && !hKeyHeld_)
        helpVisible_ = !helpVisible_;
    hKeyHeld_ = hDown;

    const bool rDown = window_->IsKeyDown(Window::kKeyR);
    if (rDown && !rKeyHeld_)
    {
        fpController_.Teleport(showcase_.Spawn());
        fpCamera_.SetYawPitch(showcase_.SpawnYaw(), -0.05f);
        LOG_INFO("展示厅：回到出生点");
    }
    rKeyHeld_ = rDown;
}

void Application::InitCyberCity()
{
    // 展台 / 出生点 / 碰撞体
    showcase_.Load(city_);
    fpColliders_ = city_.colliders;

    // 霓虹点光源（与灯柱几何一一对应，上限 8 盏）
    pointLights_.clear();
    for (const Sample::Showcase::NeonLight& n : city_.neons)
    {
        PointLightParams pl;
        pl.position = n.position;
        pl.color = n.color;
        pl.intensity = n.intensity;
        pl.radius = n.radius;
        pl.castsShadow = n.castsShadow;
        pointLights_.push_back(pl);
    }

    // 出生点：陆行控制器就位 + 相机取景
    fpController_.Teleport(city_.spawn);
    fpCamera_.SetPosition(fpController_.EyePosition());
    fpCamera_.SetYawPitch(city_.spawnYaw, -0.05f);
    camera_.SetTarget(glm::vec3(0.0f, 5.0f, 0.0f));
    camera_.SetDistance(28.0f);

    // 默认以第一人称进入（沉浸体验），并锁定光标
    cameraMode_ = CameraMode::FirstPerson;
    if (window_)
        window_->SetCursorLocked(true);

    // 玩法系统：A* 导航网格与 AI 巡逻代理在广场中央可见（金黄代理沿路径巡逻）
    navHost_.enabled = true;
    navHost_.agentEnabled = true;
    navHost_.UpdatePath();

    // 默认夜晚（赛博朋克主视觉）+ 雾 + 后处理全开
    postProcessSync_.postProcess = true;
    postProcessSync_.prevPostProcess = true;
    postProcessSync_.fogEnabled = true;
    postProcessSync_.fogShadowEnabled = true;
    postProcessSync_.taaEnabled = true;
    postProcessSync_.autoExposure = true;
    postProcessSync_.dofEnabled = false; // 默认关：景深会虚化远景建筑，留给玩家按需开
    ApplyAtmosphere();
    welcomeTimer_ = 8.0f;

    LOG_INFO("赛博城市展示厅: " << city_.objects.size() << " 实体（楼 " << city_.buildingCount << " / 天际线 "
                                << city_.skylineCount << "），展台 " << city_.exhibits.size() << " 座，霓虹 "
                                << city_.neons.size() << " 盏，碰撞体 " << fpColliders_.size() << " 个");
}

void Application::ApplyAtmosphere()
{
    // 时段：连续插值后的实际值（相邻预设按 dayTime_ 小数位混合，切换不再是硬跳变）
    const Sample::Showcase::CyberTimePreset p = showcase_.BlendedPreset();
    // 画面风格：色调分级 / 泛光 / 暗角 / 颗粒 / 雾密度倍率
    const Sample::Showcase::CyberLookPreset& lk = showcase_.Look();

    lightParams_.direction = p.sunDir;
    lightParams_.color = p.sunColor;
    lightParams_.intensity = p.sunIntensity;
    lightParams_.ambient = p.ambient;
    lightParams_.exposure = p.exposure;
    skyTint_ = glm::vec4(p.skyTint, p.skyIntensity);
    postProcessSync_.fogTint = p.fogTint;
    postProcessSync_.fogDensity = p.fogDensity * lk.fogDensityScale;

    postProcessSync_.bloomStrength = lk.bloomStrength;
    postProcessSync_.bloomThreshold = lk.bloomThreshold;
    postProcessSync_.gradeSaturation = lk.gradeSaturation;
    postProcessSync_.gradeContrast = lk.gradeContrast;
    postProcessSync_.gradeLift = lk.gradeLift;
    postProcessSync_.gradeGain = lk.gradeGain;
    postProcessSync_.gradeGamma = lk.gradeGamma;
    postProcessSync_.vignetteIntensity = lk.vignetteIntensity;
    postProcessSync_.filmGrain = lk.filmGrain;

    // 霓虹基准强度随昼夜变化（夜里最亮）；逐灯呼吸系数在 UpdateNeonPulse 里叠加
    for (std::size_t i = 0; i < pointLights_.size() && i < city_.neons.size(); ++i)
        pointLights_[i].intensity = city_.neons[i].intensity * p.neonBoost;
}

void Application::UpdateNeonPulse()
{
    const Sample::Showcase::CyberTimePreset p = showcase_.BlendedPreset();
    for (std::size_t i = 0; i < pointLights_.size() && i < city_.neons.size(); ++i)
    {
        const float pulse = Sample::Showcase::NeonPulse(static_cast<uint32_t>(i), showcaseClock_);
        pointLights_[i].intensity = city_.neons[i].intensity * p.neonBoost * pulse;
    }
}

void Application::ToggleFeature(int featureId)
{
    using F = Sample::Showcase::FeatureId;
    // 依赖后处理的特性：自动打开总开关（否则切换无视觉反馈）
    auto ensurePost = [this]()
    {
        if (!postProcessSync_.postProcess)
        {
            postProcessSync_.postProcess = true;
            LOG_INFO("展示厅：自动开启后处理（该特性位于后处理链）");
        }
    };

    showcase_.MarkFeature(featureId);
    switch (static_cast<F>(featureId))
    {
    case F::Bloom:
        postProcessSync_.postProcess = !postProcessSync_.postProcess;
        LOG_INFO("展示厅：泛光/后处理 " << (postProcessSync_.postProcess ? "开" : "关"));
        break;
    case F::VolumetricFog:
        ensurePost();
        postProcessSync_.fogEnabled = !postProcessSync_.fogEnabled;
        LOG_INFO("展示厅：体积雾·体积光 " << (postProcessSync_.fogEnabled ? "开" : "关"));
        break;
    case F::Taa:
        ensurePost();
        postProcessSync_.taaEnabled = !postProcessSync_.taaEnabled;
        LOG_INFO("展示厅：TAA " << (postProcessSync_.taaEnabled ? "开" : "关"));
        break;
    case F::DepthOfField:
        ensurePost();
        postProcessSync_.dofEnabled = !postProcessSync_.dofEnabled;
        LOG_INFO("展示厅：景深 " << (postProcessSync_.dofEnabled ? "开" : "关"));
        break;
    case F::MotionBlur:
        ensurePost();
        postProcessSync_.mbEnabled = !postProcessSync_.mbEnabled;
        LOG_INFO("展示厅：运动模糊 " << (postProcessSync_.mbEnabled ? "开" : "关"));
        break;
    case F::AutoExposure:
        ensurePost();
        postProcessSync_.autoExposure = !postProcessSync_.autoExposure;
        LOG_INFO("展示厅：自动曝光·电影化 " << (postProcessSync_.autoExposure ? "开" : "关"));
        break;
    case F::Particles:
        TriggerShowcaseParticles();
        break;
    case F::TimeOfDay:
        showcase_.CycleTimeOfDay();
        break;
    case F::Physics:
        // 物理展台：在视线前方 6 m 处生成一个动态发光立方体（可撤销）
        if (!physicsHost_.enabled)
            physicsHost_.enabled = true;
        SpawnShowcaseCube(ActivePosition() + ActiveForward() * 6.0f + glm::vec3(0.0f, 2.0f, 0.0f));
        break;
    default:
        break;
    }
}

void Application::TriggerShowcaseParticles()
{
    if (!particleHost_.enabled)
        particleHost_.enabled = true;
    const glm::vec3 at = ActivePosition() + ActiveForward() * 5.0f;
    particleHost_.EmitBurst(at);
    LOG_INFO("展示厅：粒子爆发 @ (" << at.x << ", " << at.y << ", " << at.z << ")");
}

void Application::SpawnShowcaseCube(const glm::vec3& at)
{
    const SceneSnapshot before = Snapshot();
    Scene::SceneObject cube;
    cube.position = at;
    cube.scale = 0.45f;
    cube.tint = glm::vec3(1.0f, 0.85f, 0.45f);
    cube.meshId = 0;
    cube.metallic = 0.35f;
    cube.roughness = 0.35f;
    cube.emissive = glm::vec3(0.60f, 0.45f, 0.16f); // 自发光：夜里也看得见落点
    cube.spinSpeed = 20.0f;
    cube.physicsType = Physics::BodyType::Dynamic;
    cube.physicsShape = Physics::ShapeType::Box;
    cube.physicsMass = 1.2f;
    cube.physicsFriction = 0.5f;
    cube.physicsRestitution = 0.35f;
    ecsScene_.CreateObject(cube);
    audioEngine_.Play3D(Audio::SfxId::Impact, Audio::SoundSource{.position = cube.position});
    RepackScene();
    RecalculateTriangleCount();
    physicsHost_.RebuildBodies();
    const SceneSnapshot after = Snapshot();
    suppressEditGesture_ = true;
    ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "展示厅：生成物理立方体"));
}

void Application::UpdateGizmo()
{
    // 第一人称漫游模式：左键用于转视角，禁用 Gizmo 拖拽（避免输入冲突）
    if (cameraMode_ == CameraMode::FirstPerson)
        return;

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

    const glm::mat4 camViewProj = ActiveViewProj();

    // 左键按下且未命中 UI：尝试拾取最近手柄轴
    if (selectedObject_ >= 0 && gizmoMode_ != Editor::GizmoMode::None && !ImGui::GetIO().WantCaptureMouse && leftDown &&
        !gizmoDragging_ && !uiRuntime_.Blocked())
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
    sphereScratch_.clear();
    capsuleScratch_.clear();
    sphereLodScratch_.clear();
    capsuleLodScratch_.clear();
    firstGltfModel_ = glm::mat4(1.0f);

    // 预计算常量包围球参数（同旧 UpdateVisibility 口径：hasTorus_/hasGltf_ 关闭时回退立方体球）
    const glm::mat4 camViewProj = ActiveViewProj();
    const Render::Frustum frustum = Render::Frustum::FromViewProj(camViewProj);
    const float cubeRadius = Scene::kCubeBoundingRadius * kCullMargin;
    const glm::vec3 torusCenterOffset = hasTorus_ ? torusMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float torusRadius = hasTorus_ ? torusMesh_.BoundingRadius() * kCullMargin : 0.0f;
    const glm::vec3 gltfCenterOffset = hasGltf_ ? gltfMesh_.BoundingCenter() : glm::vec3(0.0f);
    const float gltfRadius = hasGltf_ ? gltfMesh_.BoundingRadius() * kCullMargin : 0.0f;

    // LOD：每帧从 ProjectPanel 同步参数（面板成员均为 public）
    lodGroup_.SetLevels({{projectPanel_.lodH0_, projectPanel_.lodFade_},
                         {projectPanel_.lodH1_, projectPanel_.lodFade_},
                         {projectPanel_.lodH2_, projectPanel_.lodFade_}});
    lodGroup_.SetBias(projectPanel_.lodBias_);
    lodGroup_.SetMaxLevel(projectPanel_.lodMaxLevel_);
    lodGroup_.SetCullBeyondLast(projectPanel_.lodCullBeyondLast_);

    // LOD 选档用：相机位置 + 半 FOV 正切
    const glm::vec3 camPos = ActivePosition();
    const float activeFovDeg = (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.fovDegrees_ : camera_.fovDegrees_;
    const float tanHalfFov = std::tan(glm::radians(activeFovDeg * 0.5f));

    uint32_t visibleCount = 0;
    bool gltfModelSet = false;
    pvsCulledCount_ = 0;
    // LightProbe per-object: sample probe volume at each entity world translation column.
    // Unbaked (ProbeCount()==0): skip sampling; InstanceData.probeIrradiance stays default 0 (zero visual change).
    const auto& probes = projectPanel_.Probes();
    const bool probeBaked = probes.ProbeCount() > 0;
    // PVS cullable 下标：ForEachRenderableWorld 与 BuildPacket 同序遍历 order_，
    // 回调第 k 次调用即烘焙 cullables[k]（= scene_[k]）。回调内自增即一一对应。
    size_t renderIdx = 0;
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            const size_t cullableIndex = renderIdx++; // 与烘焙 cullables 同序（见上）
            // 视锥剔除（每实体一次；球心/半径与旧实现逐项一致）
            const bool isTorus = (r.meshId == 1) && hasTorus_;
            const bool isGltf = (r.meshId == 2) && hasGltf_;
            const bool isSphere = (r.meshId == 3);
            const bool isCapsule = (r.meshId == 4);
            const glm::vec3 centerOffset = isTorus ? torusCenterOffset : (isGltf ? gltfCenterOffset : glm::vec3(0.0f));
            const float boundsRadius =
                isTorus ? torusRadius
                        : (isGltf ? gltfRadius
                                  : (isSphere ? Scene::kSphereBoundingRadius
                                              : (isCapsule ? Scene::kCapsuleBoundingRadius : cubeRadius)));
            // 剔除球心取世界矩阵平移列：t.position 对挂父实体是父空间局部坐标（切片塔群子节点
            // 会被按原点附近错误剔除/漏剔）。复用 ForEachRenderableWorld 已算好的层级世界矩阵，
            // 零额外开销；无父实体时平移列与 t.position 逐位一致（行为零变化）。
            const glm::vec3 center = glm::vec3(world[3]) + t.scale * centerOffset;
            const float radius = t.scale * boundsRadius;
            if (!frustum.IntersectsSphere(center, radius))
                return;
            // PVS 遮挡剔除（烘焙式）：未烘焙时 IsVisibleAt 恒 true，行为零变化；
            // 越界对象也由 IsVisibleAt 保守放行。仅当烘焙完成且相机所在格判该对象不可见时跳过。
            if (projectPanel_.Occlusion().IsBaked() &&
                !projectPanel_.Occlusion().IsVisibleAt(camPos, cullableIndex))
            {
                ++pvsCulledCount_;
                return;
            }
            ++visibleCount;

            const glm::mat4 model = world; // 层级世界矩阵（无 Parent 时等价 ComputeEntityModelMatrix）
            Render::InstanceData d{};
            // Per-object probe irradiance: sample at world translation column (world up normal).
            if (probeBaked)
            {
                d.probeIrradiance =
                    glm::vec4(probes.SampleIrradiance(glm::vec3(world[3]), glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f);
            }
            if (r.meshId == 0)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                d.emissive = glm::vec4(r.emissive, 0.0f);
                cubeScratch_.push_back(d);
            }
            else if (r.meshId == 1)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                d.emissive = glm::vec4(r.emissive, 0.0f);
                torusScratch_.push_back(d);
            }
            else if (r.meshId == 3)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                d.emissive = glm::vec4(r.emissive, 0.0f);
                if (config_.lodOff)
                {
                    sphereScratch_.push_back(d);
                    return;
                }
                // LOD 选档：屏幕相对高度 -> 档位
                const float dist = glm::distance(camPos, center);
                const float screenH = Render::LodGroup::ScreenRelativeHeight(radius, dist, tanHalfFov);
                const int lodLevel = lodGroup_.SelectLevel(screenH);
                if (lodLevel == Render::LodGroup::kCulled)
                    return; // LOD 剔除：不绘制（visibleCount 已在剔除前递增，此处不回退）
                if (lodLevel == 0)
                    sphereScratch_.push_back(d);
                else
                    sphereLodScratch_.push_back(d);
            }
            else if (r.meshId == 4)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                d.emissive = glm::vec4(r.emissive, 0.0f);
                if (config_.lodOff)
                {
                    capsuleScratch_.push_back(d);
                    return;
                }
                const float dist = glm::distance(camPos, center);
                const float screenH = Render::LodGroup::ScreenRelativeHeight(radius, dist, tanHalfFov);
                const int lodLevel = lodGroup_.SelectLevel(screenH);
                if (lodLevel == Render::LodGroup::kCulled)
                    return;
                if (lodLevel == 0)
                    capsuleScratch_.push_back(d);
                else
                    capsuleLodScratch_.push_back(d);
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
    // PVS 活证据：烘焙后每 120 帧打印一次本帧被 PVS 剔除的实体数（未烘焙时为 0，不打印）
    if (projectPanel_.Occlusion().IsBaked() && (frameCounter_ % 120 == 0))
    {
        LOG_INFO("PVS 遮挡剔除: 本帧剔除 " << pvsCulledCount_ << " / 总对象 "
                                          << ecsScene_.ObjectCount()
                                          << "（视锥剔除 " << culledCount_ - pvsCulledCount_ << "）");
    }

    // 逐桶登记上传（计数从桶大小取，空桶登记为 0 与旧 Fill 语义一致）
    cubeInstanceCount_ = static_cast<uint32_t>(cubeScratch_.size());
    AppendInstanceUpload(cubeInstances_, cubeScratch_);
    torusInstanceCount_ = static_cast<uint32_t>(torusScratch_.size());
    AppendInstanceUpload(torusInstances_, torusScratch_);
    sphereInstanceCount_ = static_cast<uint32_t>(sphereScratch_.size());
    AppendInstanceUpload(sphereInstances_, sphereScratch_);
    capsuleInstanceCount_ = static_cast<uint32_t>(capsuleScratch_.size());
    AppendInstanceUpload(capsuleInstances_, capsuleScratch_);
    sphereLodInstanceCount_ = static_cast<uint32_t>(sphereLodScratch_.size());
    AppendInstanceUpload(sphereLodInstances_, sphereLodScratch_);
    capsuleLodInstanceCount_ = static_cast<uint32_t>(capsuleLodScratch_.size());
    AppendInstanceUpload(capsuleLodInstances_, capsuleLodScratch_);
    for (size_t p = 0; p < gltfPrimScratch_.size(); ++p)
    {
        gltfPrimCounts_[p] = static_cast<uint32_t>(gltfPrimScratch_[p].size());
        AppendInstanceUpload(gltfPrimInstances_[p], gltfPrimScratch_[p]);
    }
    // 地面：实例数据恒定，已在初始化时一次性上传
}

void Application::EnsureInstanceCapacities()
{
    // 场景实体增删（含生成/删除人物）后，实例缓冲可能超过初始化容量 kMaxInstances。
    // InstanceBuffer::Upload 对越界实例数静默裁剪 → 物体凭空消失；这里按当前实体数 + 余量
    // 统一扩容（球/胶囊各 9 部件、glTF 每 prim 一实例，均以实体数为上界）。
    const uint32_t needed = static_cast<uint32_t>(ecsScene_.ObjectCount()) + 8u;
    const auto grow = [&](Render::InstanceBuffer& ib)
    {
        if (ib.Capacity() < needed)
        {
            ctx_.WaitIdle(); // 重建设备本地缓冲前等待在飞命令结束（旧缓冲可能被引用）
            ib.Create(ctx_, needed);
            LOG_INFO("实例缓冲扩容: " << ib.Capacity() << " → " << needed << " (对象 " << ecsScene_.ObjectCount()
                                      << ")");
        }
    };
    grow(cubeInstances_);
    grow(torusInstances_);
    grow(sphereInstances_);
    grow(capsuleInstances_);
    grow(sphereLodInstances_);
    grow(capsuleLodInstances_);
    for (Render::InstanceBuffer& ib : gltfPrimInstances_)
        grow(ib);
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

    const glm::vec3 cameraForward = ActiveForward();

    // ---- LightProbe（SH9）相机位置单探针辐照度注入 ----
    // 烘焙后每帧按相机世界位置采样探针体（世界 up 做法线），把结果（可直接乘 albedo 的辐照度颜色）
    // 写入 LightUBO.probeAmbient。未烘焙（ProbeCount()==0）时恒为 0，渲染行为零变化。
    // 限制：当前为相机位置单探针采样，非逐对象/逐片元法线插值。
    glm::vec3 probeAmbient(0.0f);
    const auto& probes = projectPanel_.Probes();
    if (probes.ProbeCount() > 0)
        probeAmbient = probes.SampleIrradiance(ActivePosition(), glm::vec3(0.0f, 1.0f, 0.0f));
    // 节流日志（约每 2 秒一次）：作为「探针被渲染管线真实消费」的证据
    static float sProbeLogTimer = 0.0f;
    sProbeLogTimer += deltaTime_;
    if (sProbeLogTimer >= 2.0f)
    {
        sProbeLogTimer = 0.0f;
        LOG_INFO("LightProbe ambient(camPos,up): (" << probeAmbient.x << ", " << probeAmbient.y << ", "
                 << probeAmbient.z << ") probes=" << probes.ProbeCount());
    }

    // 逐片元探针辐照度体：脏标记延迟上传——探针烘焙后静态不变，仅在脏时重新打包上传全部 UBO 槽。
    // 未烘焙/未重新烘焙时恒跳过，消除每帧 PackProbeIrradianceUp（384 探针 SH 求值）+ UBO 写入。
    Render::ProbeUBO probeData{};
    if (probeDirty_)
    {
        const int packedProbes =
            Render::PackProbeIrradianceUp(probes, probeData.probes, Render::ShaderBindings::kMaterialProbeMax);
        probeData.dimsCount = glm::ivec4(probes.Dims(), packedProbes);
        probeData.originPad = glm::vec4(probes.Origin(), 0.0f);
        probeData.spacingPad = glm::vec4(probes.Spacing(), 0.0f);
    }

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        Render::CameraUBO camData{};
        camData.view = ActiveView();
        camData.proj = ActiveProj();
        cameraUbos_[i].Update(camData);

        Render::LightUBO lightData{};
        lightData.lightDir = lightParams_.direction;
        lightData.dirIntensity = lightParams_.intensity;
        lightData.lightColor = lightParams_.color;
        lightData.ambientFactor = lightParams_.ambient;
        lightData.cameraPos = ActivePosition();
        lightData.pointLightCount = static_cast<float>(pointLights_.size());
        lightData.shadowStrength = lightParams_.shadowStrength;
        lightData.shadowBias = lightParams_.shadowBias;
        lightData.iblStrength = lightParams_.iblStrength;
        lightData.exposure = lightParams_.exposure;
        lightData.probeAmbient = probeAmbient;
        for (uint32_t c = 0; c < Render::kMaxCascades; ++c)
            lightData.lightSpaceMatrices[c] = cascadeMatrices_[c];
        lightData.cascadeSplits = cascadeSplits_;
        lightData.cameraForward = glm::vec4(cameraForward, kShadowDrawDistance);
        lightData.skyTint = skyTint_;
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
        // 脏时上传探针数据到全部 UBO 槽；未脏时 GPU 侧数据已在描述符中，跳过
        if (probeDirty_)
            probeUbos_[i].Update(probeData);
    }
    probeDirty_ = false;
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
    // 第一人称漫游模式：左键专用于视角旋转，不参与物体拾取（保持观察沉浸感）
    if (cameraMode_ == CameraMode::FirstPerson)
        return;

    // U1-UI：UI 启用时单击统一由 UpdateUi 消费（命中 UI 不穿透）；uiClickForward_ 透传给拾取
    bool leftClicked = uiRuntime_.Enabled() ? uiClickForward_ : window_->ConsumeClick();
    uiClickForward_ = false;
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

        const glm::mat4 invViewProj = glm::inverse(ActiveViewProj());
        const float ndcX = 2.0f * static_cast<float>(cx) / static_cast<float>(fw) - 1.0f;
        const float ndcY = 1.0f - 2.0f * static_cast<float>(cy) / static_cast<float>(fh);
        const glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) / farPoint.w - ActivePosition());
        const glm::vec3 rayOrigin = ActivePosition();

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
            // S1 演示钩子：右键生成物理立方体 → 生成点 3D 空间化冲击音效
            audioEngine_.Play3D(Audio::SfxId::Impact, Audio::SoundSource{.position = ball.position});
            RepackScene();
            RecalculateTriangleCount();
            physicsHost_.RebuildBodies();
            const SceneSnapshot after = Snapshot();
            suppressEditGesture_ = true;
            ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "生成物理立方体"));
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
    // 直读 ECS：物体总数与 meshId 分布来自 Renderable 组件，不再依赖包投影。
    triangleCount_ =
        Scene::kCubeIndexCount / 3 * static_cast<uint32_t>(ecsScene_.ObjectCount()) + Scene::kGroundIndexCount / 3;
    uint32_t torusCount = 0;
    uint32_t gltfCount = 0;
    ecsScene_.ForEachRenderable(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&)
        {
            if (hasTorus_ && r.meshId == 1)
                ++torusCount;
            else if (hasGltf_ && r.meshId == 2)
                ++gltfCount;
        });
    if (hasTorus_)
        triangleCount_ += torusMesh_.IndexCount() / 3 * torusCount;
    if (hasGltf_)
        triangleCount_ += gltfMesh_.IndexCount() / 3 * gltfCount;
    // 人物部件：球 (meshId=3) + 胶囊 (meshId=4)，按 LOD 档位分别统计
    triangleCount_ += sphereMesh_.IndexCount() / 3 * sphereInstanceCount_;
    triangleCount_ += sphereLodMesh_.IndexCount() / 3 * sphereLodInstanceCount_;
    triangleCount_ += capsuleMesh_.IndexCount() / 3 * capsuleInstanceCount_;
    triangleCount_ += capsuleLodMesh_.IndexCount() / 3 * capsuleLodInstanceCount_;
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
    const float nearZ = ActiveNear();
    const float farZ = std::min(ActiveFar(), kShadowDrawDistance);
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

    const glm::mat4 invViewProj = glm::inverse(ActiveViewProj());
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

// ========================================================================
// U1-UI：运行时 UI 系统（第一增量）
// ========================================================================

void Application::InitUiRuntime()
{
    // --no-ui / headless / validate-only：UI 系统整体停用（画布空、输入不消费、无录制）
    const bool enabled = !config_.headless && !config_.validateOnly && !config_.noUi;
    uiRuntime_.Init(ctx_, renderer_.GetSwapchain(), enabled);
    if (!enabled)
        return;
    uiRuntime_.CreatePipeline(ctx_.Device()); // 图集布局/集合 + 渲染通道就绪后创建 UI 管线

    if (config_.uiDemo)
    {
        // 中文字体链：与编辑器覆盖层同源（打包 wqy → 系统 msyh/simhei），失败优雅降级为无文本
        const char* kFontCandidates[] = {
            "assets/fonts/wqy-microhei.ttc", // 打包开源字体（仓库当前未含，保留与覆盖层一致的候选序）
            "C:/Windows/Fonts/msyh.ttc",     // Windows 微软雅黑
            "C:/Windows/Fonts/simhei.ttf",   // Windows 黑体
        };
        for (const char* path : kFontCandidates)
        {
            if (std::filesystem::exists(path))
            {
                uiRuntime_.LoadFont(path);
                if (uiRuntime_.FontLoaded())
                    break;
            }
        }
        if (!uiRuntime_.FontLoaded())
            LOG_WARN("UI 演示: 未找到可用中文字体，画布文本将缺失（按钮/面板仍可用）");
        uiRuntime_.BuildDemoCanvas();
        uiRuntime_.SetClickHandler([this](const Ui::UiEvent& ev) { HandleUiDemoClick(ev); });
        LOG_INFO("UI 演示模式: --ui-demo（运行时 UI 画布叠加场景之上，渲染于编辑器 ImGui 之前）");
    }
}

void Application::UpdateUi()
{
    uiClickForward_ = false;
    if (!uiRuntime_.Enabled())
        return; // UI 关闭：ConsumeClick 留给 HandlePicking 原路径

    const auto [fbw, fbh] = window_->GetFramebufferSize();
    if (fbw <= 0 || fbh <= 0)
        return;
    const auto [cx, cy] = window_->GetCursorPos();
    const bool leftDown = window_->IsMouseButtonDown(Window::kMouseButtonLeft);
    // UI 启用时本帧单击统一在此消费：命中阻断性 UI（含按钮悬停）时不再转发引擎拾取
    const bool frameClick = window_->ConsumeClick();
    const auto events = uiRuntime_.Update(ctx_, glm::vec2(static_cast<float>(cx), static_cast<float>(cy)), leftDown,
                                          static_cast<float>(fbw), static_cast<float>(fbh));
    uiClickForward_ = frameClick && !events.blocked;

    if (config_.uiDemo)
        uiRuntime_.SetDemoStatsText("实体数: " + std::to_string(ecsScene_.ObjectCount()));
}

void Application::HandleUiDemoClick(const Ui::UiEvent& ev)
{
    if (ev.id == Ui::UiRuntime::kDemoBtnSpawn)
    {
        // 复用编辑器"添加物体"路径（含撤销栈、物理重建、实例缓冲扩容），同一迭代内生效
        editorPanel_.addObjectRequested = true;
        LOG_INFO("UI 演示: 点击[生成方块]（经编辑器添加物体路径）");
    }
    else if (ev.id == Ui::UiRuntime::kDemoBtnClear)
    {
        if (ecsScene_.ObjectCount() == 0)
        {
            LOG_INFO("UI 演示: 点击[清空]（场景已为空，忽略）");
            return;
        }
        const SceneSnapshot before = Snapshot();
        while (ecsScene_.ObjectCount() > 0)
            ecsScene_.DestroyAt(0);
        selectedObject_ = -1;
        RepackScene();
        RecalculateTriangleCount();
        physicsHost_.RebuildBodies();
        const SceneSnapshot after = Snapshot();
        suppressEditGesture_ = true;
        ExecuteEditCommand(std::make_unique<SceneSnapshotCommand>(this, before, after, "UI 清空场景"));
        EnsureInstanceCapacities();
        LOG_INFO("UI 演示: 点击[清空]（场景物体已清空，可 Ctrl+Z 撤销）");
    }
}
} // namespace BigHero
