#include "app/Application.h"

#include "core/Time.h"
#include "core/VkCheck.h"
#include "scene/CubeMesh.h"
#include "scene/GltfLoader.h"
#include "vertical_slice/SliceScene.h"
#include "open_world/OpenWorldScene.h"

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
            LOG_INFO("命令行启动第一人称漫游相机（--camera fp）");
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
                UpdateCamera();
                UpdateGizmo();
                // U1-E3 Play Mode：播放/暂停/停止请求（面板按钮 + Ctrl+P）先于仿真门消费，
                // 保证 Stop 还原/进入 Play 在本帧仿真前生效
                UpdatePlayModeRequests();
                SyncSceneEdits(); // ECS：包 -> ECS 写回（编辑器/Gizmo 修改持久化）
                // ---- U1-E3 仿真门：仅运行态推进（编辑态/暂停场景静态） ----
                // 冻结清单：C# 脚本 Update / 物理步进与角色控制器 / 动画状态机（含 glTF 根节点）/
                // 人物姿态动画 / 粒子模拟 / AI 导航代理；自转 Spin 在 UpdateTime 内同门冻结。
                // 渲染/相机/编辑器/序列化/撤销路径照常（编辑操作在编辑态即时生效不变）。
                const bool simulating = playMode_.ShouldSimulate();
                if (scripts_.Enabled() && simulating)
                    scripts_.Update(deltaTime_); // C# 脚本：热重载轮询 + OnStart/OnUpdate 批量派发（仅运行态）
                if (simulating)
                    physicsHost_.Update(deltaTime_);
                RepackScene(); // ECS：ECS -> 包投影（自转角/物理位置输出到渲染数据）
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
                    animationHost_.Update(deltaTime_, animInput, gltfModel_, hasGltf_);
                    personHost_.Update(deltaTime_); // 人物姿态动画（写 ECS Transform）
                }
                UpdateRenderables(); // ECS 渲染收敛：单趟直读 ECS（剔除 + 批次化 + 上传登记）
                if (simulating)
                {
                    particleHost_.Update(deltaTime_);
                    // 粒子：登记到帧瞬态上传（scratch 成员在录制前稳定）
                    if (particleHost_.enabled && !particleHost_.scratch.empty())
                        AppendUpload(particleHost_.buffer.Get(), particleHost_.scratch.data(),
                                     static_cast<VkDeviceSize>(particleHost_.scratch.size()) *
                                         sizeof(Render::ParticleInstance));
                    // 冻结时不重传：GPU 缓冲保留最后一次模拟的实例（暂停/编辑态粒子静止呈现）
                }
                if (simulating)
                    navHost_.UpdateAgent(deltaTime_);
                UpdateUniforms();
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
                //（P0-3 验收：--screenshot out/pp_on.png --post-process 与
                //  --screenshot out/pp_off.png 两次运行各截一张做暗部/曝光对比）
                if (!config_.screenshotPath.empty() && !screenshotIssued_ && frameCounter_ >= 30)
                {
                    renderer_.RequestScreenshot(config_.screenshotPath);
                    screenshotIssued_ = true;
                    LOG_INFO("已请求截图: " << config_.screenshotPath << "（第 " << frameCounter_ << " 帧）");
                }
                // 第二张截图（--screenshot2）：主循环时长达到 --screenshot2-delay 后再截一张
                //（时序/脚本对比：如 C# Spinner 两时刻的姿态差异）
                if (!config_.screenshot2Path.empty() && !screenshot2Issued_
                    && runTimeSeconds_ >= config_.screenshot2DelaySeconds)
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
            const bool allShotsRequested = (config_.screenshotPath.empty() || screenshotIssued_)
                                           && (config_.screenshot2Path.empty() || screenshot2Issued_);
            if (allShotsRequested && renderer_.ScreenshotDone())
            {
                LOG_INFO("截图完成，退出渲染循环");
                break;
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

    // ---- 人物部件几何：球体 + 胶囊（meshId=3/4） ----
    {
        std::vector<Scene::Vertex> sphereVerts;
        std::vector<uint32_t> sphereIdxs;
        Scene::BuildSphereVertices(sphereVerts, sphereIdxs, 20, 10, 0.5f);
        sphereMesh_.Create(ctx_, sphereVerts, sphereIdxs);
        RegisterMeshAsset("sphere", "<procedural>", sphereVerts, sphereIdxs,
                          bighero::AssetMetadata::LoadState::Loaded, 0);

        std::vector<Scene::Vertex> capVerts;
        std::vector<uint32_t> capIdxs;
        Scene::BuildCapsuleVertices(capVerts, capIdxs, 16, 5, 0.5f, 0.7f);
        capsuleMesh_.Create(ctx_, capVerts, capIdxs);
        RegisterMeshAsset("capsule", "<procedural>", capVerts, capIdxs,
                          bighero::AssetMetadata::LoadState::Loaded, 0);
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
    // ECS 场景实体化：先组装物体列表，再一次性灌入 ECS 权威存储
    // 场景分支：--scene slice 走垂直切片场景（1200 实体 + 95% 静止 + 50 条父子链），
    // 其余情况保持默认演示场景行为不变。
    const bool sliceScene = (config_.sceneKind == "slice");
    const bool openWorldScene = (config_.sceneKind == "openworld");
    std::vector<Scene::SceneObject> objs;
    if (sliceScene)
    {
        objs = Sample::VerticalSlice::BuildSliceScene();
    }
    else if (openWorldScene)
    {
        objs = Sample::OpenWorld::BuildOpenWorldScene();
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

    // ---- glTF 模型 + PBR 贴图映射（meshId=2）：网格/材质/纹理池，决定 hasGltf_ ----
    // 实例缓冲容量：场景实体数 + glTF 演示物体 + 余量
    const uint32_t kMaxInstances = static_cast<uint32_t>(objs.size()) + 3;
    LoadGltfAsset(kMaxInstances);

    // ---- glTF 演示物体：模型加载成功后自动入场景，展示材质贴图映射效果 ----
    // 切片场景的实体数/静止占比是规格断言（单测锁定），不掺入演示物体
    if (hasGltf_ && !sliceScene && !openWorldScene)
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

    ecsScene_.LoadPacket(objs); // 自转角初始化为 phase（父子经 parentIndex 走 SetParent 生产路径）
    RepackScene();

    if (sliceScene)
    {
        // 切片场景规格日志 + 相机取景：场景铺满 ±60m，默认 7m 轨道相机只能看到中央塔群，
        // 拉到上限 40m 对准场景中心（仅影响初始取景，不改变场景数据）。
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
                                  << stats.maxChainDepth << "），区块 " << stats.chunkCount
                                  << "（Near " << stats.nearChunks << " / Mid " << stats.midChunks << " / Far "
                                  << stats.farChunks << " / Outer " << stats.outerChunks << ")");
        camera_.SetTarget(glm::vec3(0.0f, 2.0f, 0.0f));
        camera_.SetDistance(60.0f);
    }

    pointLights_ = BuildDefaultPointLights();
    if (!pointLights_.empty())
        pointLights_[0].castsShadow = true; // 演示：默认启用 1 号灯投影阴影

    // 三角形总数（含圆环体/glTF 模型实际入场景的物体）
    RecalculateTriangleCount();

    // 实例缓冲：立方体/圆环/地面/球/胶囊 五份（glTF 逐 primitive 份在 LoadGltfAsset 内创建）
    cubeInstances_.Create(ctx_, kMaxInstances);
    torusInstances_.Create(ctx_, kMaxInstances);
    groundInstances_.Create(ctx_, kMaxInstances);
    sphereInstances_.Create(ctx_, kMaxInstances);
    capsuleInstances_.Create(ctx_, kMaxInstances);

    // 地面实例数据恒定（恒等模型 + 固定材质），初始化上传一次，不再逐帧中转
    Render::InstanceData ground{};
    ground.tint = glm::vec4(1.0f);
    ground.metallic = 0.0f;
    ground.roughness = 0.9f;
    groundInstances_.Upload(ctx_, &ground, 1);

physicsHost_.RebuildBodies();
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
        LOG_INFO("demo-person: SpawnPerson 返回 " << idx << "，人物总数 " << personHost_.Count() << "，实体数 " << ecsScene_.ObjectCount());
        (void)idx;
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
        //（路径 A 的 frameStart 同语义——两者都在本帧 UI 绘制前定格）
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
                    << (scriptEditBefore_.empty() ? 0 : scriptEditBefore_[0].size()) << " 个字段）");
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
// （UpdateRenderables），不再依赖本包。每帧仍重建：物理/自转每帧改写 ECS，
// Gizmo 与拾取需要当前世界坐标。
void Application::RepackScene()
{
    scene_ = ecsScene_.BuildPacket(&spinAngles_);
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
            LOG_INFO("相机模式: 轨道相机（Orbit）");
        }
        else
        {
            // Orbit -> FP：从轨道相机接管位置与朝向
            fpCamera_.SyncFromOrbit(camera_);
            cameraMode_ = CameraMode::FirstPerson;
            LOG_INFO("相机模式: 第一人称漫游（FP）—— WASD 移动 / 空格·Shift 升降 / 鼠标拖拽转视角");
        }
    }
    prevCameraMode_ = camKey;

    if (cameraMode_ == CameraMode::FirstPerson)
    {
        // ---- 第一人称：WASD 水平移动 + 空格/Shift 升降 + 左键拖拽视角 ----
        const auto [dx, dy] = window_->GetCursorDelta();
        // 仅在自己拖拽时旋转视角（与 Orbit 左键拖拽一致，不干扰 ImGui/运行时 UI）
        if (window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !ImGui::GetIO().WantCaptureMouse
            && !uiRuntime_.Blocked())
            fpCamera_.Rotate(static_cast<float>(dx), static_cast<float>(dy));

        // 滚轮调节行走速度（FP 模式下滚轮语义=速度而非缩放；正值加速）
        fpWalkSpeed_ = std::clamp(fpWalkSpeed_ + static_cast<float>(window_->ConsumeScrollDelta()) * 0.8f, 0.5f, 24.0f);

        float forward = 0.0f, right = 0.0f, up = 0.0f;
        if (window_->IsKeyDown(Window::kKeyW)) forward += 1.0f;
        if (window_->IsKeyDown(Window::kKeyS)) forward -= 1.0f;
        if (window_->IsKeyDown(Window::kKeyD)) right += 1.0f;
        if (window_->IsKeyDown(Window::kKeyA)) right -= 1.0f;
        if (window_->IsKeyDown(Window::kKeySpace)) up += 1.0f;
        if (window_->IsKeyDown(Window::kKeyLeftShift)) up -= 1.0f;
        // 按住左 Shift 为蹲行（慢速下沉视点）；E/Q 快速升降（与 Orbit 的 E/Q 上升语义区分）
        const float moveSpeed = fpWalkSpeed_;
        fpCamera_.Move(forward, right, up, deltaTime_, moveSpeed);

        // 蹲坐：P 键切换眼高（Walk 模式辅助）——交给人物系统，此处仅 FPS 视角
        const VkExtent2D frameExtent = renderer_.Extent();
        const float aspect =
            frameExtent.height > 0 ? static_cast<float>(frameExtent.width) / static_cast<float>(frameExtent.height)
                                   : 1.0f;
        const Render::PostProcessor* pp = renderer_.GetPostProcessor();
        const glm::vec2 jitter =
            postProcessSync_.AdvanceJitter(postProcessSync_.taaEnabled && pp && pp->UseMsaa(), frameExtent);
        fpCamera_.SetJitter(jitter.x, jitter.y);
        fpCamera_.Update(aspect);
        return;
    }

    const auto [dx, dy] = window_->GetCursorDelta();
    if (window_->IsMouseButtonDown(Window::kMouseButtonLeft) && !gizmoDragging_ && !ImGui::GetIO().WantCaptureMouse
        && !uiRuntime_.Blocked())
        camera_.Orbit(static_cast<float>(dx), static_cast<float>(dy));
    camera_.Zoom(window_->ConsumeScrollDelta());

    // 相机碰撞防护：持续放大（distance↓）时若相机穿入物体包围球内部，
    // 所有可见面均成背面→背面剔除全黑。沿当前视线方向求解退出距离，
    // 强制 distance 不小于该值（额外 5% margin 防数值抖动）。
    // 包围球用立方体外接球（kCubeBoundingRadius×scale），覆盖默认场景中全部物体。
    float minSafe = camera_.GetMinDistance();
    const glm::vec3 target = camera_.Target();
    const glm::vec3 camDir = glm::normalize(camera_.ComputePosition() - target);
    ecsScene_.ForEachRenderable([&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable&, const Scene::ecs::Spin&)
    {
        const float radius = t.scale * Scene::kCubeBoundingRadius * 1.05f;
        const glm::vec3 u = t.position - target;          // target → center
        const float udotd = glm::dot(u, camDir);           // u · d
        const float u2 = glm::dot(u, u);                   // |u|²
        const float disc = udotd * udotd - (u2 - radius * radius);
        if (disc > 0.0f)
        {
            const float need = -udotd + std::sqrt(disc);   // 较大正根 = 退出距离
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
    firstGltfModel_ = glm::mat4(1.0f);

    // 预计算常量包围球参数（同旧 UpdateVisibility 口径：hasTorus_/hasGltf_ 关闭时回退立方体球）
    const glm::mat4 camViewProj = ActiveViewProj();
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
            const bool isSphere = (r.meshId == 3);
            const bool isCapsule = (r.meshId == 4);
            const glm::vec3 centerOffset = isTorus ? torusCenterOffset : (isGltf ? gltfCenterOffset : glm::vec3(0.0f));
            const float boundsRadius = isTorus ? torusRadius : (isGltf ? gltfRadius : (isSphere ? Scene::kSphereBoundingRadius : (isCapsule ? Scene::kCapsuleBoundingRadius : cubeRadius)));
            // 剔除球心取世界矩阵平移列：t.position 对挂父实体是父空间局部坐标（切片塔群子节点
            // 会被按原点附近错误剔除/漏剔）。复用 ForEachRenderableWorld 已算好的层级世界矩阵，
            // 零额外开销；无父实体时平移列与 t.position 逐位一致（行为零变化）。
            const glm::vec3 center = glm::vec3(world[3]) + t.scale * centerOffset;
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
            else if (r.meshId == 3)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                sphereScratch_.push_back(d);
            }
            else if (r.meshId == 4)
            {
                d.model = model;
                d.tint = glm::vec4(r.tint, 1.0f);
                d.metallic = r.metallic;
                d.roughness = r.roughness;
                capsuleScratch_.push_back(d);
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
    sphereInstanceCount_ = static_cast<uint32_t>(sphereScratch_.size());
    AppendInstanceUpload(sphereInstances_, sphereScratch_);
    capsuleInstanceCount_ = static_cast<uint32_t>(capsuleScratch_.size());
    AppendInstanceUpload(capsuleInstances_, capsuleScratch_);
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
            LOG_INFO("实例缓冲扩容: " << ib.Capacity() << " → " << needed << " (对象 " << ecsScene_.ObjectCount() << ")");
        }
    };
    grow(cubeInstances_);
    grow(torusInstances_);
    grow(sphereInstances_);
    grow(capsuleInstances_);
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
    // 人物部件：球 (meshId=3) + 胶囊 (meshId=4)
    uint32_t sphereCount = 0, capsuleCount = 0;
    for (const auto& obj : scene_)
    {
        if (obj.meshId == 3) ++sphereCount;
        else if (obj.meshId == 4) ++capsuleCount;
    }
    if (sphereCount)
        triangleCount_ += sphereMesh_.IndexCount() / 3 * sphereCount;
    if (capsuleCount)
        triangleCount_ += capsuleMesh_.IndexCount() / 3 * capsuleCount;
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
        uiRuntime_.SetDemoStatsText("实体数: " + std::to_string(scene_.size()));
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
        if (scene_.empty())
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
