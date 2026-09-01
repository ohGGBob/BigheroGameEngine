#include "app/Application.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace BigHero
{
Application::Application()
    : window_(kWindowWidth, kWindowHeight, "BigHero Engine - Vulkan"), ctx_(window_, kEnableValidation),
      renderer_(ctx_, window_)
{
}

Application::~Application() = default;

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

        lastTime_ = glfwGetTime();
        LOG_INFO("进入主循环（左键拖拽旋转 / 滚轮缩放 / WASD+QE平移）");

        while (!window_.ShouldClose())
        {
            frameProfiler_.BeginFrame();

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "PollEvents");
                window_.PollEvents();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Update");
                UpdateTime();
                UpdateCamera();
                UpdateGizmo();
                UpdatePhysics();
                UpdateVisibility();
                FillInstanceBuffers();
                UpdateUniforms();
                UpdateFpsTitle();
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Picking");
                HandlePicking();
                UpdateDeferredState();
            }

            // 场景序列化快捷键：F5 保存，F9 加载（边沿检测，避免按住重复触发）
            const bool f5Down = window_.IsKeyDown(GLFW_KEY_F5);
            const bool f9Down = window_.IsKeyDown(GLFW_KEY_F9);
            if ((f5Down && !saveKeyHeld_) || editorPanel_.saveRequested)
                SaveScene();
            if ((f9Down && !loadKeyHeld_) || editorPanel_.loadRequested)
                LoadScene();
            saveKeyHeld_ = f5Down;
            loadKeyHeld_ = f9Down;
            editorPanel_.saveRequested = false;
            editorPanel_.loadRequested = false;

            // 编辑器物体增删请求
            if (editorPanel_.addObjectRequested)
            {
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
                scene_.push_back(obj);
                spinAngles_.push_back(0.0f);
                visible_.push_back(1);
                RecalculateTriangleCount();
                RebuildPhysicsBodies();
                editorPanel_.addObjectRequested = false;
                LOG_INFO("添加物体: 总计 " << scene_.size() << " 个");
            }
            if (editorPanel_.deleteObjectRequested && selectedObject_ >= 0 &&
                selectedObject_ < static_cast<int>(scene_.size()))
            {
                scene_.erase(scene_.begin() + selectedObject_);
                spinAngles_.erase(spinAngles_.begin() + selectedObject_);
                visible_.erase(visible_.begin() + selectedObject_);
                selectedObject_ = -1;
                RecalculateTriangleCount();
                RebuildPhysicsBodies();
                editorPanel_.deleteObjectRequested = false;
                LOG_INFO("删除物体: 剩余 " << scene_.size() << " 个");
            }

            {
                Core::FrameProfiler::Scope s(frameProfiler_, "Render");
                renderer_.SetSSAOCamera(camera_.Proj() * camera_.View(), camera_.Position());
                renderer_.DrawFrame(
                    [this](VkCommandBuffer cmd, uint32_t fi, VkExtent2D ext) { RecordScene(cmd, fi, ext); },
                    [this](VkCommandBuffer cmd, uint32_t ii, VkExtent2D ext) { RecordUi(cmd, ii, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, VkExtent2D ext) { RecordPrePass(cmd, fi, ext); },
                    [this](VkCommandBuffer cmd, uint32_t fi, uint32_t ii, VkExtent2D ext)
                    { RecordLighting(cmd, fi, ii, ext); });
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

void Application::InitResources()
{
    // ---- 阴影与环境光 ----
    shadowMap_.Create(ctx_);
    cubeShadowMap_.Create(ctx_, 1024);
    envLighting_.Create(ctx_);

    // ---- 描述符与每帧 UBO（双帧并行，各自独立缓冲与描述符集） ----
    descManager_.Init(ctx_.Device());
    descManager_.AllocateSets(Renderer::MaxFramesInFlight());

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    cameraUbos_.reserve(kFrameCount);
    lightUbos_.reserve(kFrameCount);
    pointShadowUbos_.reserve(kFrameCount);
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        cameraUbos_.emplace_back(ctx_.Device(), ctx_.PhysicalDevice(), ctx_.GraphicsFamily());
        lightUbos_.emplace_back(ctx_.Device(), ctx_.PhysicalDevice(), ctx_.GraphicsFamily());
        pointShadowUbos_.emplace_back(ctx_.Device(), ctx_.PhysicalDevice(), ctx_.GraphicsFamily());
    }

    // ---- 纹理：通过 AssetManager 统一缓存（LRU + 引用计数），缺失时程序化回退 ----
    assetManager_.Cache<Texture>(16,
                                 [this](const std::string& key) -> std::shared_ptr<Texture>
                                 {
                                     auto tex = std::make_shared<Texture>();
                                     if (key == "checkerboard")
                                         tex->CreateCheckerboard(ctx_);
                                     else if (key == "flat_normal")
                                         tex->CreateFlatNormal(ctx_);
                                     else if (std::filesystem::exists(key))
                                         tex->CreateFromFile(ctx_, key.c_str(),
                                                             key.find("normal") == std::string::npos);
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

    // ---- 延迟渲染：GBuffer 输入附件描述符集（每交换链图像一组） ----
    descManager_.AllocateGBufferSets(renderer_.GetSwapchain().ImageCount());

    // ---- 场景几何：立方体+地面组合网格 ----
    const std::vector<Scene::Vertex> vertices = Scene::BuildSceneVertices();
    const std::vector<uint32_t> indices = Scene::BuildSceneIndices();
    sceneMesh_.Create(ctx_, vertices, indices);

    // ---- 外部模型：圆环体（OBJ），文件缺失时从场景中剔除 ----
    if (std::filesystem::exists(kTorusModelPath))
    {
        const Scene::MeshData torusData = Scene::LoadObjModel(kTorusModelPath);
        torusMesh_.Create(ctx_, torusData.vertices, torusData.indices);
        hasTorus_ = true;
        LOG_INFO("圆环体模型加载成功: " << torusData.vertices.size() << "顶点 / " << torusData.indices.size() / 3
                                        << "三角形");
    }
    else
    {
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

    // ---- 物理引擎 ----
    physicsEngine_.Init();
    physicsEngine_.SetGravity(glm::vec3(0.0f, gravity_, 0.0f));
}

void Application::CreatePipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass mainPass = renderer_.GetRenderPass();
    const VkRenderPass deferredPass = renderer_.GetDeferredRenderPass();

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
        cubeShadowConfig_.setLayouts = {descManager_.layoutCubeShadow};
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
                                      descManager_.layoutGBufferInput};
        defLightConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
        defLightConfig_.vertexBindings = {};
        defLightConfig_.vertexAttributes = {};
        defLightConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        defLightConfig_.colorAttachmentCount = 1;
        defLightConfig_.subpass = 1;
        defLightConfig_.depthTest = false;
        defLightConfig_.depthWrite = false;
        lightingPipeline_.emplace(dev, deferredPass, std::move(lv), std::move(lf), defLightConfig_);
    }
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

    editorOverlay_.Init(ctx_, window_, renderer_.GetSwapchain());
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
    scene_ = Scene::BuildDefaultScene();
    if (!hasTorus_)
    {
        scene_.erase(
            std::remove_if(scene_.begin(), scene_.end(), [](const Scene::SceneObject& obj) { return obj.meshId != 0; }),
            scene_.end());
    }

    spinAngles_.resize(scene_.size());
    for (size_t i = 0; i < scene_.size(); ++i)
        spinAngles_[i] = scene_[i].phase;

    pointLights_ = BuildDefaultPointLights();
    if (!pointLights_.empty())
        pointLights_[0].castsShadow = true; // 演示：默认启用 1 号灯投影阴影

    // 三角形总数
    triangleCount_ = Scene::kCubeIndexCount / 3 * static_cast<uint32_t>(scene_.size()) + Scene::kGroundIndexCount / 3;
    if (hasTorus_)
        triangleCount_ += static_cast<uint32_t>(torusMesh_.IndexCount() / 3);

    // 实例缓冲：立方体/圆环/地面三份
    const uint32_t kMaxInstances = static_cast<uint32_t>(scene_.size()) + 2;
    cubeInstances_.Create(ctx_, kMaxInstances);
    torusInstances_.Create(ctx_, kMaxInstances);
    groundInstances_.Create(ctx_, kMaxInstances);

    visible_.resize(scene_.size(), 1);

    RebuildPhysicsBodies();
}

// ========================================================================
// 每帧更新
// ========================================================================

void Application::UpdateTime()
{
    const double now = glfwGetTime();
    deltaTime_ = static_cast<float>(now - lastTime_);
    lastTime_ = now;

    for (size_t i = 0; i < scene_.size(); ++i)
    {
        spinAngles_[i] += scene_[i].spinSpeed * deltaTime_;
        if (spinAngles_[i] >= 360.0f)
            spinAngles_[i] -= 360.0f;
    }
}

void Application::UpdateCamera()
{
    const auto [dx, dy] = window_.GetCursorDelta();
    if (window_.IsMouseButtonDown(Window::kMouseButtonLeft) && !gizmoDragging_)
        camera_.Orbit(static_cast<float>(dx), static_cast<float>(dy));
    camera_.Zoom(window_.ConsumeScrollDelta());

    const float panStep = kPanSpeed * deltaTime_;
    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (window_.IsKeyDown(Window::kKeyW))
        forward += panStep;
    if (window_.IsKeyDown(Window::kKeyS))
        forward -= panStep;
    if (window_.IsKeyDown(Window::kKeyD))
        right += panStep;
    if (window_.IsKeyDown(Window::kKeyA))
        right -= panStep;
    if (window_.IsKeyDown(Window::kKeyE))
        up += panStep;
    if (window_.IsKeyDown(Window::kKeyQ))
        up -= panStep;
    if (forward != 0.0f || right != 0.0f || up != 0.0f)
        camera_.Pan(forward, right, up);

    const VkExtent2D frameExtent = renderer_.Extent();
    const float aspect =
        frameExtent.height > 0 ? static_cast<float>(frameExtent.width) / static_cast<float>(frameExtent.height) : 1.0f;
    camera_.Update(aspect);
}

void Application::UpdateGizmo()
{
    const auto [fbw, fbh] = window_.GetFramebufferSize();
    const glm::vec2 gizmoVp(static_cast<float>(fbw), static_cast<float>(fbh));
    const auto [mxp, myp] = window_.GetCursorPos();
    const glm::vec2 mousePx(static_cast<float>(mxp), static_cast<float>(myp));
    const bool leftDown = window_.IsMouseButtonDown(Window::kMouseButtonLeft);

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

    visibleCount_ = 0;
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        const Scene::SceneObject& obj = scene_[i];
        const bool isTorus = (obj.meshId == 1) && hasTorus_;
        const glm::vec3 center = obj.position + obj.scale * (isTorus ? torusCenterOffset : glm::vec3(0.0f));
        const float radius = obj.scale * (isTorus ? torusRadius : cubeRadius);
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
    const glm::mat4 lightSpace = ComputeLightSpaceMatrix();
    Render::PointShadowUBO pointShadowData{};
    FillPointShadowMatrices(pointShadowData);

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
        lightData.lightSpaceMatrix = lightSpace;
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
        window_.SetTitle(baseTitle_ + "  |  FPS: " + std::to_string(lastFps_) + "  |  MSAA " +
                         std::to_string(static_cast<uint32_t>(renderer_.SampleCount())) + "x");
        fpsTimer_ = 0.0;
        fpsFrames_ = 0;
    }
}

void Application::HandlePicking()
{
    bool leftClicked = window_.ConsumeClick();
    if (window_.ConsumeRightClick())
        selectedObject_ = -1;
    if (gizmoSuppressClick_)
    {
        gizmoSuppressClick_ = false;
        leftClicked = false;
    }
    if (leftClicked && !ImGui::GetIO().WantCaptureMouse)
    {
        const auto [cx, cy] = window_.GetCursorPos();
        const auto [fw, fh] = window_.GetFramebufferSize();
        if (fh > 0)
        {
            const glm::mat4 invViewProj = glm::inverse(camera_.Proj() * camera_.View());
            const float ndcX = 2.0f * static_cast<float>(cx) / static_cast<float>(fw) - 1.0f;
            const float ndcY = 1.0f - 2.0f * static_cast<float>(cy) / static_cast<float>(fh);
            const glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) / farPoint.w - camera_.Position());
            selectedObject_ = Scene::PickObject(camera_.Position(), rayDir, scene_);
        }
    }
}

void Application::UpdateDeferredState()
{
    if (deferred_ != prevDeferred_)
    {
        renderer_.SetDeferred(deferred_);
        if (deferred_)
            UpdateGBufferSets();
        prevDeferred_ = deferred_;
    }
    if (postProcess_ != prevPostProcess_)
    {
        renderer_.SetPostProcessing(postProcess_);
        prevPostProcess_ = postProcess_;
    }
    if (ssao_ != prevSsao_)
    {
        renderer_.SetSSAO(ssao_);
        prevSsao_ = ssao_;
    }
}

void Application::RecalculateTriangleCount()
{
    triangleCount_ = Scene::kCubeIndexCount / 3 * static_cast<uint32_t>(scene_.size()) + Scene::kGroundIndexCount / 3;
    if (hasTorus_)
    {
        uint32_t torusCount = 0;
        for (const auto& obj : scene_)
            if (obj.meshId != 0)
                ++torusCount;
        triangleCount_ += torusMesh_.IndexCount() / 3 * torusCount;
    }
}

// ========================================================================
// 物理系统
// ========================================================================

void Application::RebuildPhysicsBodies()
{
    physicsEngine_.RemoveAllBodies();
    physicsBodyIds_.assign(scene_.size(), UINT32_MAX);

    // 地面：静态大盒体（顶面 y=0，与渲染地面对齐）
    Physics::BodyConfig groundCfg;
    groundCfg.type = Physics::BodyType::Static;
    groundCfg.shape = Physics::ShapeType::Box;
    groundCfg.halfExtents = glm::vec3(50.0f, 0.5f, 50.0f);
    groundCfg.friction = 0.8f;
    groundCfg.restitution = 0.0f;
    physicsEngine_.CreateBody(groundCfg, glm::vec3(0.0f, -0.5f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // 场景物体
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        const Scene::SceneObject& obj = scene_[i];
        if (obj.physicsType == Physics::BodyType::None)
            continue;

        Physics::BodyConfig cfg;
        cfg.type = obj.physicsType;
        cfg.shape = obj.physicsShape;
        cfg.mass = obj.physicsMass;
        cfg.friction = obj.physicsFriction;
        cfg.restitution = obj.physicsRestitution;
        cfg.halfExtents = glm::vec3(obj.scale * 0.5f);
        cfg.radius = obj.scale * 0.5f;
        cfg.capsuleHeight = obj.scale * 0.5f;

        // 物体中心 position，旋转用欧拉角（自转 spinAngle 不参与物理，由渲染叠加）
        const glm::quat rot = glm::quat(glm::radians(obj.rotation));
        physicsBodyIds_[i] = physicsEngine_.CreateBody(cfg, obj.position, rot);
    }

    LOG_INFO("物理刚体重建: " << physicsEngine_.BodyCount() << " 个（含地面）");
}

void Application::SyncPhysicsBodies()
{
    // 运动学/静态体：场景变换同步到物理
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (physicsBodyIds_[i] == UINT32_MAX)
            continue;
        const Scene::SceneObject& obj = scene_[i];
        if (obj.physicsType == Physics::BodyType::Kinematic || obj.physicsType == Physics::BodyType::Static)
        {
            const glm::quat rot = glm::quat(glm::radians(obj.rotation));
            physicsEngine_.SetBodyTransform(physicsBodyIds_[i], obj.position, rot);
        }
    }
}

void Application::UpdatePhysics()
{
    if (!physicsEnabled_)
        return;

    SyncPhysicsBodies();
    physicsEngine_.Step(deltaTime_);

    // 动态体：物理变换同步回场景
    for (size_t i = 0; i < scene_.size(); ++i)
    {
        if (physicsBodyIds_[i] == UINT32_MAX)
            continue;
        Scene::SceneObject& obj = scene_[i];
        if (obj.physicsType != Physics::BodyType::Dynamic)
            continue;

        glm::vec3 pos;
        glm::quat rot;
        physicsEngine_.GetBodyTransform(physicsBodyIds_[i], pos, rot);
        obj.position = pos;
        obj.rotation = glm::degrees(glm::eulerAngles(rot));
    }
}

// ========================================================================
// 场景序列化
// ========================================================================

void Application::SaveScene()
{
    Scene::SceneData data;
    data.version = 1;
    data.cameraFov = camera_.fovDegrees_;

    // 方向光
    data.light.direction = lightParams_.direction;
    data.light.color = lightParams_.color;
    data.light.intensity = lightParams_.intensity;
    data.light.ambient = lightParams_.ambient;
    data.light.shadowStrength = lightParams_.shadowStrength;
    data.light.shadowBias = lightParams_.shadowBias;
    data.light.iblStrength = lightParams_.iblStrength;
    data.light.exposure = lightParams_.exposure;

    // 点光源
    data.pointLights.reserve(pointLights_.size());
    for (const auto& pl : pointLights_)
    {
        Scene::SerializablePointLight spl;
        spl.position = pl.position;
        spl.color = pl.color;
        spl.intensity = pl.intensity;
        spl.radius = pl.radius;
        spl.castsShadow = pl.castsShadow;
        data.pointLights.push_back(spl);
    }

    // 场景物体
    data.objects = scene_;

    if (Scene::SaveSceneToFile(data, kScenePath))
        LOG_INFO("场景已保存: " << kScenePath << "（" << data.objects.size() << "物体 / " << data.pointLights.size()
                                << "灯）");
    else
        LOG_ERROR("场景保存失败: " << kScenePath);
}

void Application::LoadScene()
{
    Scene::SceneData data;
    if (!Scene::LoadSceneFromFile(kScenePath, data))
    {
        LOG_WARN("场景文件不存在或解析失败: " << kScenePath << "，保持当前场景");
        return;
    }

    // 方向光
    lightParams_.direction = data.light.direction;
    lightParams_.color = data.light.color;
    lightParams_.intensity = data.light.intensity;
    lightParams_.ambient = data.light.ambient;
    lightParams_.shadowStrength = data.light.shadowStrength;
    lightParams_.shadowBias = data.light.shadowBias;
    lightParams_.iblStrength = data.light.iblStrength;
    lightParams_.exposure = data.light.exposure;

    // 点光源（上限 kMaxPointLights）
    pointLights_.clear();
    for (size_t i = 0; i < data.pointLights.size() && i < EditorPanel::kMaxPointLights; ++i)
    {
        PointLightParams pl;
        pl.position = data.pointLights[i].position;
        pl.color = data.pointLights[i].color;
        pl.intensity = data.pointLights[i].intensity;
        pl.radius = data.pointLights[i].radius;
        pl.castsShadow = data.pointLights[i].castsShadow;
        pointLights_.push_back(pl);
    }

    // 场景物体（过滤掉 torus 物体如果 torus 模型未加载）
    scene_.clear();
    for (const auto& obj : data.objects)
    {
        if (obj.meshId != 0 && !hasTorus_)
            continue;
        scene_.push_back(obj);
    }

    // 重置自转角、可见性数组
    spinAngles_.resize(scene_.size());
    visible_.resize(scene_.size(), 1);
    for (size_t i = 0; i < scene_.size(); ++i)
        spinAngles_[i] = scene_[i].phase;

    // 相机 FOV
    camera_.fovDegrees_ = data.cameraFov;

    // 取消选中（索引可能失效）
    selectedObject_ = -1;

    // 重算三角形数
    RecalculateTriangleCount();

    // 重建物理刚体
    RebuildPhysicsBodies();

    LOG_INFO("场景已加载: " << kScenePath << "（" << scene_.size() << "物体 / " << pointLights_.size() << "灯）");
}

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
    stats.batchCount = (cubeInstanceCount_ > 0 ? 1u : 0u) + 1u + (torusInstanceCount_ > 0 ? 1u : 0u);
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

    editorPanel_.Draw(stats, scene_, lightParams_, camera_.fovDegrees_, pointLights_, selectedObject_, &deferred_,
                      &gizmoMode_, glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)),
                      &masterVolume_, &postProcess_, &ssao_, &physicsEnabled_, &physicsDebugDraw_, &gravity_);
    audioEngine_.SetMasterVolume(masterVolume_);

    // 物理属性变更 -> 重建刚体
    if (editorPanel_.physicsRebuildRequested)
    {
        RebuildPhysicsBodies();
        editorPanel_.physicsRebuildRequested = false;
    }
    physicsEngine_.SetGravity(glm::vec3(0.0f, gravity_, 0.0f));

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

    // ---- 物理调试线框 ----
    if (physicsDebugDraw_)
    {
        const glm::mat4 gvp = camera_.Proj() * camera_.View();
        const glm::vec2 gvpSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
        const auto debugLines = physicsEngine_.GetDebugLines();
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
    }

    editorOverlay_.Render(cmd, imageIndex);
}

void Application::RecordPrePass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D)
{
    const glm::mat4 lightSpace = ComputeLightSpaceMatrix();

    shadowMap_.RecordPass(cmd, [this, &lightSpace](VkCommandBuffer c)
                          { DrawShadowCasters(c, *shadowPipeline_, lightSpace); });

    if (!pointLights_.empty() && std::any_of(pointLights_.begin(), pointLights_.end(),
                                             [](const PointLightParams& pl) { return pl.castsShadow; }))
    {
        cubeShadowMap_.RecordPass(cmd, [this, frameIndex](VkCommandBuffer c, int face)
                                  { DrawCubeShadowCasters(c, *cubeShadowPipeline_, face, frameIndex); });
    }
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

glm::mat4 Application::ComputeLightSpaceMatrix() const
{
    const glm::vec3 lightEye = glm::normalize(-lightParams_.direction) * kShadowEyeDistance;
    const glm::vec3 lightUp =
        (std::fabs(lightParams_.direction.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::ortho(-kShadowOrthoHalf, kShadowOrthoHalf, -kShadowOrthoHalf, kShadowOrthoHalf, kShadowNear,
                      kShadowFar) *
           glm::lookAt(lightEye, glm::vec3(0.0f), lightUp);
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
}

void Application::DrawCubeShadowCasters(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int face,
                                        uint32_t frameIndex)
{
    using RDS = Render::FrameDescriptorSet;
    pipeline.Bind(cmd);

    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetLayout(), 0, 1,
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
}

glm::vec3 Application::GetActiveShadowLight(const std::vector<PointLightParams>& lights)
{
    for (const PointLightParams& pl : lights)
        if (pl.castsShadow)
            return pl.position;
    return glm::vec3(0.0f);
}
} // namespace BigHero
