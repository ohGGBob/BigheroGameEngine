#include "core/Log.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/Renderer.h"
#include "render/EnvironmentLighting.h"
#include "render/Mesh.h"
#include "render/Frustum.h"
#include "render/InstanceBuffer.h"
#include "render/ShadowMap.h"
#include "render/CubeShadowMap.h"
#include "render/Texture.h"
#include "render/descriptor_set.h"
#include "render/ubo_buffer.h"
#include "render/ubo_structs.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include "scene/Camera.h"
#include "scene/CubeMesh.h"
#include "scene/ObjModel.h"
#include "scene/Picking.h"
#include "scene/Scene.h"
#include "editor/EditorOverlay.h"
#include "editor/EditorPanel.h"
#include "editor/Gizmo.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
#include <cstdlib>

namespace
{
    constexpr uint32_t kWindowWidth = 1600;
    constexpr uint32_t kWindowHeight = 900;
    constexpr const char* kVertSpvPath = "shaders/vert.spv";
    constexpr const char* kFragSpvPath = "shaders/frag.spv";
    constexpr const char* kDefaultTexturePath = "assets/tiles.png";
    constexpr const char* kNormalMapPath = "assets/tiles_normal.png";
    constexpr const char* kTorusModelPath = "assets/models/torus.obj";
    constexpr float kPanSpeed = 4.0f; // 键盘平移速度（单位/秒）

#ifdef NDEBUG
    constexpr bool kEnableValidation = false;
#else
    constexpr bool kEnableValidation = true;
#endif

    // 阴影预通道推送常量：光照视投影矩阵 + 物体模型矩阵
    struct PushShadow
    {
        glm::mat4 lightSpace;
        glm::mat4 model;
    };
    static_assert(sizeof(PushShadow) == 128, "PushShadow必须恰为推送常量上限");

    // 点光源立方体阴影推送常量：模型矩阵 + 面索引。
    // 6 个面的视投影矩阵通过 set 2 binding 0 的 PointShadowUBO 传入（std140 数组下标须用常量索引，
    // 由顶点着色器以 if-else 按 faceIndex 选择）。
    struct PushCubeShadow
    {
        glm::mat4 model;
        glm::vec4 faceIndex;   // x 分量 = 当前面索引（0..5）
    };
    static_assert(sizeof(PushCubeShadow) <= 128, "PushCubeShadow超出推送常量上限");

    // 天空盒推送常量：view*proj逆矩阵
    struct PushSky
    {
        glm::mat4 invViewProj;
    };

    // 返回第一个启用了阴影的点光源位置；无则返回原点（立方体贴图不会被采样到）
    inline glm::vec3 GetActiveShadowLight(const std::vector<BigHero::PointLightParams>& lights)
    {
        for (const BigHero::PointLightParams& pl : lights)
            if (pl.castsShadow)
                return pl.position;
        return glm::vec3(0.0f);
    }
}

int main()
{
    try
    {
        // ---- 窗口与Vulkan上下文 ----
        BigHero::Window window(kWindowWidth, kWindowHeight, "BigHero Engine - Vulkan");
        BigHero::Context ctx(window, kEnableValidation);

        // ---- 渲染器：交换链/渲染通道/MSAA/深度附件/命令缓冲/同步 ----
        BigHero::Renderer renderer(ctx, window);

        // ---- 阴影贴图：固定2048深度预通道 ----
        BigHero::ShadowMap shadowMap;
        shadowMap.Create(ctx);

        // ---- 点光源立方体阴影贴图：1024 深度立方图 + 6 面预通道 ----
        BigHero::CubeShadowMap cubeShadowMap;
        cubeShadowMap.Create(ctx, 1024);

        // ---- IBL环境光照：程序化天空 + 辐照度/预滤波/BRDF LUT预计算 ----
        BigHero::EnvironmentLighting envLighting;
        envLighting.Create(ctx);

        // ---- 描述符与每帧UBO（双帧并行，各自独立缓冲与描述符集） ----
        BigHero::Render::DescriptorManager descManager;
        descManager.Init(ctx.Device());
        descManager.AllocateSets(BigHero::Renderer::MaxFramesInFlight());

        constexpr uint32_t kFrameCount = BigHero::Renderer::MaxFramesInFlight();
        std::vector<BigHero::Render::UboBuffer<BigHero::Render::CameraUBO>> cameraUbos;
        std::vector<BigHero::Render::UboBuffer<BigHero::Render::LightUBO>> lightUbos;
        std::vector<BigHero::Render::UboBuffer<BigHero::Render::PointShadowUBO>> pointShadowUbos;
        cameraUbos.reserve(kFrameCount);
        lightUbos.reserve(kFrameCount);
        pointShadowUbos.reserve(kFrameCount);
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            cameraUbos.emplace_back(ctx.Device(), ctx.PhysicalDevice(), ctx.GraphicsFamily());
            lightUbos.emplace_back(ctx.Device(), ctx.PhysicalDevice(), ctx.GraphicsFamily());
            pointShadowUbos.emplace_back(ctx.Device(), ctx.PhysicalDevice(), ctx.GraphicsFamily());
        }

        // ---- 纹理：反照率（SRGB）+ 法线贴图（UNORM），缺失时程序化回退 ----
        BigHero::Texture texture;
        if (std::filesystem::exists(kDefaultTexturePath))
        {
            texture.CreateFromFile(ctx, kDefaultTexturePath);
        }
        else
        {
            LOG_WARN("未找到 " << kDefaultTexturePath << "，使用程序化棋盘格纹理");
            texture.CreateCheckerboard(ctx);
        }

        BigHero::Texture normalTexture;
        if (std::filesystem::exists(kNormalMapPath))
        {
            normalTexture.CreateFromFile(ctx, kNormalMapPath, /*sRGB=*/false);
        }
        else
        {
            LOG_WARN("未找到 " << kNormalMapPath << "，使用平坦法线");
            normalTexture.CreateFlatNormal(ctx);
        }

        // ---- 绑定描述符：set0相机UBO，set1光照UBO+反照率纹理+法线贴图，set2点光源立方体阴影矩阵 ----
        // 每组 3 个集合（相机, 光照, 立方体阴影），帧 i 的集合下标 = i*3 + {0,1,2}
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            descManager.UpdateSet(i * 3 + 0, 0, cameraUbos[i]);
            descManager.UpdateSet(i * 3 + 1, 0, lightUbos[i]);
            descManager.UpdateSetImage(i * 3 + 1, 1, texture.View(), texture.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 2, normalTexture.View(), normalTexture.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 3, shadowMap.View(), shadowMap.Sampler(),
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            descManager.UpdateSetImage(i * 3 + 1, 4, envLighting.EnvView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 5, envLighting.IrradianceView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 6, envLighting.PrefilteredView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 7, envLighting.BrdfLutView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 3 + 1, 8, cubeShadowMap.View(), cubeShadowMap.Sampler(),
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            descManager.UpdateSet(i * 3 + 2, 0, pointShadowUbos[i]);
        }

        // ---- 延迟渲染：GBuffer 输入附件描述符集（每交换链图像一组） ----
        // 仅分配集合；仅当延迟模式启用（GBuffer 图像已创建）后才写入图像视图
        descManager.AllocateGBufferSets(renderer.GetSwapchain().ImageCount());
        auto updateGBufferSets = [&]()
        {
            const uint32_t n = renderer.GetSwapchain().ImageCount();
            for (uint32_t i = 0; i < n; ++i)
                descManager.UpdateGBufferSet(i, renderer.GBufferAlbedoView(i),
                    renderer.GBufferNormalView(i), renderer.GBufferPositionView(i));
        };

        // ---- 场景几何：立方体+地面组合网格（一份立方体供所有实例复用） ----
        const std::vector<BigHero::Scene::Vertex> vertices = BigHero::Scene::BuildSceneVertices();
        const std::vector<uint32_t> indices = BigHero::Scene::BuildSceneIndices();

        BigHero::Render::Mesh sceneMesh;
        sceneMesh.Create(ctx, vertices, indices);

        // ---- 外部模型：圆环体（OBJ），文件缺失时从场景中剔除 ----
        BigHero::Render::Mesh torusMesh;
        bool hasTorus = false;
        if (std::filesystem::exists(kTorusModelPath))
        {
            const BigHero::Scene::MeshData torusData = BigHero::Scene::LoadObjModel(kTorusModelPath);
            torusMesh.Create(ctx, torusData.vertices, torusData.indices);
            hasTorus = true;
            LOG_INFO("圆环体模型加载成功: " << torusData.vertices.size() << "顶点 / "
                << torusData.indices.size() / 3 << "三角形");
        }
        else
        {
            LOG_WARN("未找到 " << kTorusModelPath << "，场景不含外部模型");
        }

        // ---- 图形管线（MSAA采样数与渲染通道保持一致） ----
        BigHero::Render::ShaderModuleHandle vertModule(ctx.Device(), BigHero::Render::ReadShaderFile(kVertSpvPath));
        BigHero::Render::ShaderModuleHandle fragModule(ctx.Device(), BigHero::Render::ReadShaderFile(kFragSpvPath));

        // 顶点输入：逐顶点绑定0 + 逐实例绑定1（实例化渲染）
        const VkVertexInputBindingDescription vertexBinding = BigHero::Scene::Vertex::getBindingDesc();
        const std::vector<VkVertexInputAttributeDescription> vertexAttributes = BigHero::Scene::Vertex::getAttrDesc();
        const VkVertexInputBindingDescription instanceBinding = BigHero::Render::InstanceBuffer::GetBindingDesc();
        const std::vector<VkVertexInputAttributeDescription> instanceAttributes = BigHero::Render::InstanceBuffer::GetAttrDesc();

        BigHero::Render::GraphicsPipelineConfig pipelineConfig;
        pipelineConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
        pipelineConfig.vertexBindings = { vertexBinding, instanceBinding };
        {
            std::vector<VkVertexInputAttributeDescription> attrs = vertexAttributes;
            attrs.insert(attrs.end(), instanceAttributes.begin(), instanceAttributes.end());
            pipelineConfig.vertexAttributes = std::move(attrs);
        }
        pipelineConfig.rasterSamples = renderer.SampleCount();

        BigHero::Render::GraphicsPipeline pipeline(ctx.Device(), renderer.GetRenderPass(),
            std::move(vertModule), std::move(fragModule), pipelineConfig);

        // ---- 阴影深度管线：仅深度、前向剔除减轻阴影痤疮 ----
        BigHero::Render::ShaderModuleHandle shadowVert(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/shadow.vert.spv"));
        BigHero::Render::ShaderModuleHandle shadowFrag(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/shadow.frag.spv"));

        BigHero::Render::GraphicsPipelineConfig shadowConfig;
        shadowConfig.pushConstants = {
            VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushShadow) }
        };
        shadowConfig.vertexBindings = { vertexBinding };
        shadowConfig.vertexAttributes = vertexAttributes;
        shadowConfig.cullMode = VK_CULL_MODE_FRONT_BIT;
        shadowConfig.depthOnly = true;

        BigHero::Render::GraphicsPipeline shadowPipeline(ctx.Device(), shadowMap.GetRenderPass(),
            std::move(shadowVert), std::move(shadowFrag), shadowConfig);

        // ---- 点光源立方体阴影深度管线：仅深度、前向剔除 + 面选择 ----
        BigHero::Render::ShaderModuleHandle cubeShadowVert(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/shadow_cube.vert.spv"));
        BigHero::Render::ShaderModuleHandle cubeShadowFrag(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/shadow_cube.frag.spv"));

        BigHero::Render::GraphicsPipelineConfig cubeShadowConfig;
        cubeShadowConfig.setLayouts = { descManager.layoutCubeShadow };
        cubeShadowConfig.pushConstants = {
            VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow) }
        };
        cubeShadowConfig.vertexBindings = { vertexBinding };
        cubeShadowConfig.vertexAttributes = vertexAttributes;
        cubeShadowConfig.cullMode = VK_CULL_MODE_FRONT_BIT;
        cubeShadowConfig.depthOnly = true;

        BigHero::Render::GraphicsPipeline cubeShadowPipeline(ctx.Device(), cubeShadowMap.GetRenderPass(),
            std::move(cubeShadowVert), std::move(cubeShadowFrag), cubeShadowConfig);

        // ---- 天空盒管线：无顶点输入全屏三角形，深度比较恒通过、不写深度 ----
        BigHero::Render::ShaderModuleHandle skyboxVert(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/skybox.vert.spv"));
        BigHero::Render::ShaderModuleHandle skyboxFrag(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/skybox.frag.spv"));

        BigHero::Render::GraphicsPipelineConfig skyboxConfig;
        skyboxConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
        skyboxConfig.pushConstants = {
            VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushSky) }
        };
        skyboxConfig.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        skyboxConfig.depthWrite = false;
        skyboxConfig.cullMode = VK_CULL_MODE_NONE;
        skyboxConfig.rasterSamples = renderer.SampleCount();

        BigHero::Render::GraphicsPipeline skyboxPipeline(ctx.Device(), renderer.GetRenderPass(),
            std::move(skyboxVert), std::move(skyboxFrag), skyboxConfig);

        // 场景/天空盒管线依赖主渲染通道，交换链格式变化导致渲染通道重建时必须随之重建，
        // 否则后续绘制会使用已失效的管线而崩溃。该函数可在重建回调中被反复调用。
        auto rebuildMainPipelines = [&]()
        {
            BigHero::Render::ShaderModuleHandle v(ctx.Device(), BigHero::Render::ReadShaderFile(kVertSpvPath));
            BigHero::Render::ShaderModuleHandle f(ctx.Device(), BigHero::Render::ReadShaderFile(kFragSpvPath));
            pipelineConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
            pipeline = BigHero::Render::GraphicsPipeline(ctx.Device(), renderer.GetRenderPass(),
                std::move(v), std::move(f), pipelineConfig);

            BigHero::Render::ShaderModuleHandle sv(ctx.Device(), BigHero::Render::ReadShaderFile("shaders/skybox.vert.spv"));
            BigHero::Render::ShaderModuleHandle sf(ctx.Device(), BigHero::Render::ReadShaderFile("shaders/skybox.frag.spv"));
            skyboxConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
            skyboxPipeline = BigHero::Render::GraphicsPipeline(ctx.Device(), renderer.GetRenderPass(),
                std::move(sv), std::move(sf), skyboxConfig);
        };

        // ---- 延迟渲染管线：GBuffer 几何（MRT 写 3 张） + 全屏延迟光照（输入附件） ----
        // GBuffer 顶点复用 forward 的 vert.spv（输出 worldPos/normal/uv/color/tangent/material），
        // 片段改用 gbuffer.frag（写 MRT，不做光照）。延迟光照为全屏三角形，片段采样 GBuffer。
        BigHero::Render::ShaderModuleHandle gbufferVert(ctx.Device(),
            BigHero::Render::ReadShaderFile(kVertSpvPath));
        BigHero::Render::ShaderModuleHandle gbufferFrag(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/gbuffer.frag.spv"));

        BigHero::Render::GraphicsPipelineConfig gbufferConfig;
        gbufferConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
        gbufferConfig.vertexBindings = { vertexBinding, instanceBinding };
        {
            std::vector<VkVertexInputAttributeDescription> attrs = vertexAttributes;
            attrs.insert(attrs.end(), instanceAttributes.begin(), instanceAttributes.end());
            gbufferConfig.vertexAttributes = std::move(attrs);
        }
        gbufferConfig.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        gbufferConfig.colorAttachmentCount = 3; // 反照率/法线/世界坐标 三张 MRT
        gbufferConfig.subpass = 0;               // 几何子通道
        gbufferConfig.depthTest = true;
        gbufferConfig.depthWrite = true;

        BigHero::Render::GraphicsPipeline gbufferPipeline(ctx.Device(), renderer.GetDeferredRenderPass(),
            std::move(gbufferVert), std::move(gbufferFrag), gbufferConfig);

        BigHero::Render::ShaderModuleHandle defLightVert(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
        BigHero::Render::ShaderModuleHandle defLightFrag(ctx.Device(),
            BigHero::Render::ReadShaderFile("shaders/deferred_light.frag.spv"));

        BigHero::Render::GraphicsPipelineConfig defLightConfig;
        // set0 相机(本管线未用但保留)，set1 光照/阴影/IBL，set2 GBuffer 输入附件
        defLightConfig.setLayouts = {
            descManager.layoutCamera, descManager.layoutLight, descManager.layoutGBufferInput
        };
        defLightConfig.pushConstants = {
            VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4) } // invViewProj
        };
        defLightConfig.vertexBindings = {};   // 全屏三角形，无顶点缓冲
        defLightConfig.vertexAttributes = {};
        defLightConfig.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        defLightConfig.colorAttachmentCount = 1;
        defLightConfig.subpass = 1;           // 延迟光照子通道
        defLightConfig.depthTest = false;
        defLightConfig.depthWrite = false;

        BigHero::Render::GraphicsPipeline lightingPipeline(ctx.Device(), renderer.GetDeferredRenderPass(),
            std::move(defLightVert), std::move(defLightFrag), defLightConfig);

        // 延迟管线同样依赖延迟渲染通道，格式变化重建时一并重建
        auto rebuildDeferredPipelines = [&]()
        {
            BigHero::Render::ShaderModuleHandle gv(ctx.Device(), BigHero::Render::ReadShaderFile(kVertSpvPath));
            BigHero::Render::ShaderModuleHandle gf(ctx.Device(), BigHero::Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
            gbufferConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
            gbufferPipeline = BigHero::Render::GraphicsPipeline(ctx.Device(), renderer.GetDeferredRenderPass(),
                std::move(gv), std::move(gf), gbufferConfig);

            BigHero::Render::ShaderModuleHandle lv(ctx.Device(), BigHero::Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
            BigHero::Render::ShaderModuleHandle lf(ctx.Device(), BigHero::Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
            defLightConfig.setLayouts = {
                descManager.layoutCamera, descManager.layoutLight, descManager.layoutGBufferInput
            };
            lightingPipeline = BigHero::Render::GraphicsPipeline(ctx.Device(), renderer.GetDeferredRenderPass(),
                std::move(lv), std::move(lf), defLightConfig);
        };

        renderer.SetRenderPassRecreateCallback([&]()
        {
            rebuildMainPipelines();
            rebuildDeferredPipelines();
            // GBuffer 图像随交换链重建，延迟模式下需重写输入附件描述符集
            if (renderer.IsDeferred())
                updateGBufferSets();
        });

        // ---- 编辑器：UI覆盖层 + 可调光照参数 ----
        BigHero::EditorOverlay editorOverlay;
        editorOverlay.Init(ctx, window, renderer.GetSwapchain());
        renderer.SetResizeCallback([&editorOverlay, &renderer, &updateGBufferSets]()
        {
            editorOverlay.RecreateFramebuffers(renderer.GetSwapchain());
            // 交换链重建后 GBuffer 图像已重建，延迟模式需重写输入附件描述符集
            if (renderer.IsDeferred())
                updateGBufferSets();
        });

        BigHero::EditorPanel editorPanel;
        BigHero::LightParams lightParams;

        // ---- 场景与主循环 ----
        std::vector<BigHero::Scene::SceneObject> scene = BigHero::Scene::BuildDefaultScene();
        if (!hasTorus)
        {
            scene.erase(std::remove_if(scene.begin(), scene.end(),
                [](const BigHero::Scene::SceneObject& obj) { return obj.meshId != 0; }),
                scene.end());
        }
        std::vector<float> spinAngles(scene.size());
        for (size_t i = 0; i < scene.size(); ++i)
            spinAngles[i] = scene[i].phase;

        std::vector<BigHero::PointLightParams> pointLights = BigHero::BuildDefaultPointLights();
        // 演示点光源立方体阴影：默认启用 1 号灯（正前方橙色）的投影阴影
        if (!pointLights.empty())
            pointLights[0].castsShadow = true;

        BigHero::OrbitCamera camera;
        int selectedObject = -1; // 编辑器拾取选中（-1为空）
        bool deferred = false;   // 延迟渲染开关（编辑器面板可切换）
        bool prevDeferred = false;
        // ---- Gizmo 交互状态：选中物体后启用手柄，左键拖拽改位姿 ----
        BigHero::Editor::GizmoMode gizmoMode = BigHero::Editor::GizmoMode::None;
        BigHero::Editor::GizmoAxis gizmoDragAxis = BigHero::Editor::GizmoAxis::None;
        bool gizmoDragging = false;
        bool gizmoSuppressClick = false;  // 拖拽起始帧吞掉一次拾取点击
        glm::vec2 gizmoLastMouse(0.0f);
        double lastTime = glfwGetTime();
        double fpsTimer = 0.0;
        uint32_t fpsFrames = 0;
        uint32_t lastFps = 0;
        float lastFrameMs = 0.0f;
        const std::string baseTitle = "BigHero Engine - Vulkan";

        // 编辑器统计：三角形总数
        uint32_t triangleCount = BigHero::Scene::kCubeIndexCount / 3
            * static_cast<uint32_t>(scene.size())
            + BigHero::Scene::kGroundIndexCount / 3;
        if (hasTorus)
            triangleCount += static_cast<uint32_t>(torusMesh.IndexCount() / 3);

        // ---- 实例缓冲：立方体/圆环/地面三份，容量取场景物体数（含地面1个占位） ----
        const uint32_t kMaxInstances = static_cast<uint32_t>(scene.size()) + 2;
        BigHero::Render::InstanceBuffer cubeInstances;
        BigHero::Render::InstanceBuffer torusInstances;
        BigHero::Render::InstanceBuffer groundInstances;
        cubeInstances.Create(ctx, kMaxInstances);
        torusInstances.Create(ctx, kMaxInstances);
        groundInstances.Create(ctx, kMaxInstances);
        uint32_t cubeInstanceCount = 0;  // 每帧填充后的可见立方体实例数
        uint32_t torusInstanceCount = 0; // 每帧填充后的可见圆环实例数

        const std::vector<VkDescriptorSet>& descSets = descManager.GetSets();
        LOG_INFO("进入主循环（左键拖拽旋转 / 滚轮缩放 / WASD+QE平移）");

        while (!window.ShouldClose())
        {
            window.PollEvents();

            // ---- 时间与场景动画 ----
            const double now = glfwGetTime();
            const float deltaTime = static_cast<float>(now - lastTime);
            lastTime = now;
            for (size_t i = 0; i < scene.size(); ++i)
            {
                spinAngles[i] += scene[i].spinSpeed * deltaTime;
                if (spinAngles[i] >= 360.0f)
                    spinAngles[i] -= 360.0f;
            }

            // ---- 输入 -> 相机 ----
            const auto [dx, dy] = window.GetCursorDelta();
            if (window.IsMouseButtonDown(BigHero::Window::kMouseButtonLeft) && !gizmoDragging)
                camera.Orbit(static_cast<float>(dx), static_cast<float>(dy));
            camera.Zoom(window.ConsumeScrollDelta());

            const float panStep = kPanSpeed * deltaTime;
            float forward = 0.0f, right = 0.0f, up = 0.0f;
            if (window.IsKeyDown(BigHero::Window::kKeyW)) forward += panStep;
            if (window.IsKeyDown(BigHero::Window::kKeyS)) forward -= panStep;
            if (window.IsKeyDown(BigHero::Window::kKeyD)) right += panStep;
            if (window.IsKeyDown(BigHero::Window::kKeyA)) right -= panStep;
            if (window.IsKeyDown(BigHero::Window::kKeyE)) up += panStep;
            if (window.IsKeyDown(BigHero::Window::kKeyQ)) up -= panStep;
            if (forward != 0.0f || right != 0.0f || up != 0.0f)
                camera.Pan(forward, right, up);

            const VkExtent2D frameExtent = renderer.Extent();
            const float aspect = frameExtent.height > 0
                ? static_cast<float>(frameExtent.width) / static_cast<float>(frameExtent.height)
                : 1.0f;
            camera.Update(aspect);

            // ---- 相机视图投影（Gizmo / 视锥剔除 / 拾取 共用） ----
            const glm::mat4 camViewProj = camera.Proj() * camera.View();

            // ---- Gizmo 交互：选中物体且启用手柄时，左键拖拽改位姿 ----
            {
                const auto [fbw, fbh] = window.GetFramebufferSize();
                const glm::vec2 gizmoVp(static_cast<float>(fbw), static_cast<float>(fbh));
                const auto [mxp, myp] = window.GetCursorPos();
                const glm::vec2 mousePx(static_cast<float>(mxp), static_cast<float>(myp));
                const bool leftDown = window.IsMouseButtonDown(BigHero::Window::kMouseButtonLeft);

                // 左键松开 -> 结束拖拽
                if (gizmoDragging && !leftDown)
                {
                    gizmoDragging = false;
                    gizmoDragAxis = BigHero::Editor::GizmoAxis::None;
                }

                // 左键按下且未命中 UI：尝试拾取最近手柄轴
                if (selectedObject >= 0
                    && gizmoMode != BigHero::Editor::GizmoMode::None
                    && !ImGui::GetIO().WantCaptureMouse
                    && leftDown && !gizmoDragging)
                {
                    auto& obj = scene[static_cast<size_t>(selectedObject)];
                    const auto axis = BigHero::Editor::PickAxis(obj.position, camViewProj,
                        gizmoVp, mousePx, 12.0f, 80.0f);
                    if (axis != BigHero::Editor::GizmoAxis::None)
                    {
                        gizmoDragging = true;
                        gizmoDragAxis = axis;
                        gizmoLastMouse = mousePx;
                        gizmoSuppressClick = true; // 吞掉本次按下对应的拾取点击
                    }
                }

                // 拖拽中：把屏幕鼠标位移换算为世界平移/旋转增量，写入选中物体
                if (gizmoDragging && gizmoDragAxis != BigHero::Editor::GizmoAxis::None && leftDown)
                {
                    auto& obj = scene[static_cast<size_t>(selectedObject)];
                    const glm::vec2 delta = mousePx - gizmoLastMouse;
                    if (gizmoMode == BigHero::Editor::GizmoMode::Translate)
                    {
                        const float worldDelta = BigHero::Editor::TranslateDragDelta(
                            obj.position, gizmoDragAxis, camViewProj, gizmoVp, delta);
                        obj.position += BigHero::Editor::GizmoAxisVector(gizmoDragAxis) * worldDelta;
                    }
                    else // Rotate：手柄中心为物体屏幕投影，叉积定符号
                    {
                        const glm::vec2 center = BigHero::Editor::ProjectWorldToScreen(
                            obj.position, camViewProj, gizmoVp);
                        const float ang = BigHero::Editor::RotateDragAngle(center, gizmoLastMouse, mousePx);
                        if (gizmoDragAxis == BigHero::Editor::GizmoAxis::X) obj.rotation.x += glm::degrees(ang);
                        else if (gizmoDragAxis == BigHero::Editor::GizmoAxis::Y) obj.rotation.y += glm::degrees(ang);
                        else obj.rotation.z += glm::degrees(ang);
                    }
                    gizmoLastMouse = mousePx;
                }
            }

            // ---- 视锥剔除：从相机 VP 提取视锥，预算每个物体的可见性（地面/天空盒始终可见） ----
            const BigHero::Render::Frustum frustum = BigHero::Render::Frustum::FromViewProj(camViewProj);
            std::vector<uint8_t> visible(scene.size(), 1);
            uint32_t visibleCount = 0;
            {
                constexpr float kCullMargin = 1.05f; // 保守放大包围球，避免自转/边缘误剔
                for (size_t i = 0; i < scene.size(); ++i)
                {
                    const BigHero::Scene::SceneObject& obj = scene[i];
                    const bool useTorus = (obj.meshId == 1) && hasTorus;
                    const glm::vec3 center = obj.position + obj.scale *
                        (useTorus ? torusMesh.BoundingCenter() : glm::vec3(0.0f));
                    const float radius = obj.scale *
                        (useTorus ? torusMesh.BoundingRadius() : BigHero::Scene::kCubeBoundingRadius) * kCullMargin;
                    visible[i] = frustum.IntersectsSphere(center, radius) ? 1 : 0;
                    if (visible[i]) ++visibleCount;
                }
            }
            const uint32_t culledCount = static_cast<uint32_t>(scene.size()) - visibleCount;

            // ---- 填充实例缓冲：立方体/圆环/地面三类各一次 DrawIndexedInstanced ----
            // 立方体：可见的 meshId==0 实例（平移*绕Y自转*缩放）
            {
                std::vector<BigHero::Render::InstanceData> inst;
                inst.reserve(visibleCount + 1);
                for (size_t i = 0; i < scene.size(); ++i)
                {
                    const BigHero::Scene::SceneObject& obj = scene[i];
                    if (obj.meshId != 0 || !visible[i])
                        continue;
                    BigHero::Render::InstanceData d{};
                    d.model = glm::translate(glm::mat4(1.0f), obj.position)
                        * glm::rotate(glm::mat4(1.0f), glm::radians(spinAngles[i]), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
                        * glm::scale(glm::mat4(1.0f), glm::vec3(obj.scale));
                    d.tint = glm::vec4(obj.tint, 1.0f);
                    d.metallic = obj.metallic;
                    d.roughness = obj.roughness;
                    inst.push_back(d);
                }
                cubeInstances.Upload(ctx, inst.data(), static_cast<uint32_t>(inst.size()));
                cubeInstanceCount = static_cast<uint32_t>(inst.size());
            }
            // 圆环：可见的 meshId==1 实例
            {
                std::vector<BigHero::Render::InstanceData> inst;
                inst.reserve(visibleCount + 1);
                for (size_t i = 0; i < scene.size(); ++i)
                {
                    const BigHero::Scene::SceneObject& obj = scene[i];
                    if (obj.meshId != 1 || !visible[i])
                        continue;
                    BigHero::Render::InstanceData d{};
                    d.model = glm::translate(glm::mat4(1.0f), obj.position)
                        * glm::rotate(glm::mat4(1.0f), glm::radians(spinAngles[i]), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
                        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
                        * glm::scale(glm::mat4(1.0f), glm::vec3(obj.scale));
                    d.tint = glm::vec4(obj.tint, 1.0f);
                    d.metallic = obj.metallic;
                    d.roughness = obj.roughness;
                    inst.push_back(d);
                }
                torusInstances.Upload(ctx, inst.data(), static_cast<uint32_t>(inst.size()));
                torusInstanceCount = static_cast<uint32_t>(inst.size());
            }
            // 地面：恒等模型，单个实例（哑光电介质材质，始终绘制）
            {
                BigHero::Render::InstanceData ground{};
                ground.tint = glm::vec4(1.0f);
                ground.metallic = 0.0f;
                ground.roughness = 0.9f;
                groundInstances.Upload(ctx, &ground, 1);
            }

            // ---- 方向光阴影矩阵：以场景中心为靶点的正交光照视空间 ----
            const glm::vec3 lightEye = glm::normalize(-lightParams.direction) * 20.0f;
            const glm::vec3 lightUp = (std::fabs(lightParams.direction.y) > 0.99f)
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::mat4 lightSpace = glm::ortho(-14.0f, 14.0f, -14.0f, 14.0f, 0.5f, 45.0f)
                * glm::lookAt(lightEye, glm::vec3(0.0f), lightUp);

            // ---- 更新各帧并行槽位的UBO（光照参数由编辑器面板驱动） ----
            // 点光源立方体阴影：取第一个启用了阴影的灯，计算其 6 个面视投影矩阵（每帧一次）
            BigHero::Render::PointShadowUBO pointShadowData{};
            {
                const glm::vec3 shadowLightPos = GetActiveShadowLight(pointLights);
                constexpr float kPointShadowNear = 0.1f;
                constexpr float kPointShadowFar = 50.0f;
                const std::array<glm::vec3, 6> faceCenters = {
                    glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
                    glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
                    glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)
                };
                const std::array<glm::vec3, 6> faceUps = {
                    glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
                    glm::vec3(0, 0, 1),  glm::vec3(0, 0, -1),
                    glm::vec3(0, -1, 0), glm::vec3(0, -1, 0)
                };
                const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
                    kPointShadowNear, kPointShadowFar);
                for (int f = 0; f < 6; ++f)
                {
                    // Vulkan Y 翻转，保持与主相机一致的 NDC 约定
                    glm::mat4 viewProj = proj
                        * glm::lookAt(shadowLightPos, shadowLightPos + faceCenters[f], faceUps[f]);
                    viewProj[1][1] *= -1.0f;
                    pointShadowData.faceMatrices[f] = viewProj;
                }
            }

            for (uint32_t i = 0; i < kFrameCount; ++i)
            {
                BigHero::Render::CameraUBO camData{};
                camData.view = camera.View();
                camData.proj = camera.Proj();
                cameraUbos[i].Update(camData);

                BigHero::Render::LightUBO lightData{};
                lightData.lightDir = lightParams.direction;
                lightData.dirIntensity = lightParams.intensity;
                lightData.lightColor = lightParams.color;
                lightData.ambientFactor = lightParams.ambient;
                lightData.cameraPos = camera.Position();
                lightData.pointLightCount = static_cast<float>(pointLights.size());
                lightData.shadowStrength = lightParams.shadowStrength;
                lightData.shadowBias = lightParams.shadowBias;
                lightData.iblStrength = lightParams.iblStrength;
                lightData.exposure = lightParams.exposure;
                lightData.lightSpaceMatrix = lightSpace;
                for (uint32_t li = 0; li < BigHero::Render::kMaxPointLights; ++li)
                {
                    lightData.lights[li] = BigHero::Render::GpuPointLight{};
                    if (li < pointLights.size())
                    {
                        lightData.lights[li].position = pointLights[li].position;
                        lightData.lights[li].intensity = pointLights[li].intensity;
                        lightData.lights[li].color = pointLights[li].color;
                        lightData.lights[li].radius = pointLights[li].radius;
                        lightData.lights[li].castsShadow = pointLights[li].castsShadow ? 1.0f : 0.0f;
                    }
                }
                lightUbos[i].Update(lightData);
                pointShadowUbos[i].Update(pointShadowData);
            }

            // ---- FPS统计与标题 ----
            fpsTimer += deltaTime;
            ++fpsFrames;
            lastFrameMs = lastFrameMs * 0.9f + deltaTime * 1000.0f; // 平滑帧耗时（毫秒）
            if (fpsTimer >= 0.5)
            {
                lastFps = static_cast<uint32_t>(fpsFrames / fpsTimer + 0.5);
                window.SetTitle(baseTitle + "  |  FPS: " + std::to_string(lastFps)
                    + "  |  MSAA " + std::to_string(static_cast<uint32_t>(renderer.SampleCount())) + "x");
                fpsTimer = 0.0;
                fpsFrames = 0;
            }

            const auto objectModel = [&spinAngles](const BigHero::Scene::SceneObject& obj, size_t i)
            {
                return glm::translate(glm::mat4(1.0f), obj.position)
                    * glm::rotate(glm::mat4(1.0f), glm::radians(spinAngles[i]), glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f))
                    * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(obj.scale));
            };

            const glm::mat4 invViewProj = glm::inverse(camera.Proj() * camera.View());

            // ---- 点击拾取：左键单击选择物体，右键取消（ImGui占用鼠标时跳过） ----
            bool leftClicked = window.ConsumeClick();
            if (window.ConsumeRightClick())
                selectedObject = -1;
            if (gizmoSuppressClick)
            {
                gizmoSuppressClick = false;
                leftClicked = false; // 拖拽起始帧不触发物体拾取
            }
            if (leftClicked && !ImGui::GetIO().WantCaptureMouse)
            {
                const auto [cx, cy] = window.GetCursorPos();
                const auto [fw, fh] = window.GetFramebufferSize();
                if (fh > 0)
                {
                    const float ndcX = 2.0f * static_cast<float>(cx) / static_cast<float>(fw) - 1.0f;
                    const float ndcY = 1.0f - 2.0f * static_cast<float>(cy) / static_cast<float>(fh);
                    const glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                    const glm::vec3 rayDir = glm::normalize(
                        glm::vec3(farPoint) / farPoint.w - camera.Position());
                    selectedObject = BigHero::Scene::PickObject(camera.Position(), rayDir, scene);
                }
            }

            // ---- 延迟渲染开关：编辑器面板切换后即时重建 GBuffer 资源并生效 ----
            if (deferred != prevDeferred)
            {
                renderer.SetDeferred(deferred);
                if (deferred)
                    updateGBufferSets();
                prevDeferred = deferred;
            }

            renderer.DrawFrame(
                // ---- 几何/场景通道（延迟模式下写 GBuffer，前向模式下写交换链） ----
                [&](VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent)
            {
                const VkDescriptorSet sets[] = {
                    descSets[frameIndex * 3 + 0],
                    descSets[frameIndex * 3 + 1]
                };

                if (renderer.IsDeferred())
                {
                    // 几何子通道：写 GBuffer（反照率/法线/世界坐标），背景在延迟光照阶段处理
                    gbufferPipeline.Bind(cmd);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline.GetLayout(),
                        0, 2, sets, 0, nullptr);

                    VkViewport viewport{};
                    viewport.x = 0.0f; viewport.y = 0.0f;
                    viewport.width = static_cast<float>(extent.width);
                    viewport.height = static_cast<float>(extent.height);
                    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{ { 0, 0 }, extent };
                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                    sceneMesh.Bind(cmd);
                    cubeInstances.Bind(cmd);
                    sceneMesh.DrawIndexedInstanced(cmd, BigHero::Scene::kCubeIndexCount, 0, cubeInstanceCount);

                    sceneMesh.Bind(cmd);
                    groundInstances.Bind(cmd);
                    sceneMesh.DrawIndexedInstanced(cmd, BigHero::Scene::kGroundIndexCount,
                        BigHero::Scene::kGroundIndexOffset, 1);

                    torusMesh.Bind(cmd);
                    torusInstances.Bind(cmd);
                    torusMesh.DrawIndexedInstanced(cmd, torusMesh.IndexCount(), 0, torusInstanceCount);
                    return;
                }

                // 描述符先行绑定：天空盒与场景共用同一套set（帧i = i*3 + {0相机,1光照}）
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetLayout(),
                    0, 2, sets, 0, nullptr);

                // 天空盒：最先绘制（不写深度，场景覆盖其上）
                skyboxPipeline.Bind(cmd);
                const PushSky skyPush{ invViewProj };
                vkCmdPushConstants(cmd, skyboxPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                    0, sizeof(PushSky), &skyPush);
                vkCmdDraw(cmd, 3, 1, 0, 0);

                pipeline.Bind(cmd);

                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(extent.width);
                viewport.height = static_cast<float>(extent.height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{ { 0, 0 }, extent };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                // 立方体实例：一次实例化绘制全部可见立方体（视锥外已剔除）
                sceneMesh.Bind(cmd);
                cubeInstances.Bind(cmd);
                sceneMesh.DrawIndexedInstanced(cmd, BigHero::Scene::kCubeIndexCount, 0,
                    cubeInstanceCount);

                // 地面（恒等模型，哑光电介质材质；始终可见，不参与剔除）
                sceneMesh.Bind(cmd);
                groundInstances.Bind(cmd);
                sceneMesh.DrawIndexedInstanced(cmd, BigHero::Scene::kGroundIndexCount,
                    BigHero::Scene::kGroundIndexOffset, 1);

                // 外部加载模型（圆环体；一次实例化绘制全部可见实例）
                torusMesh.Bind(cmd);
                torusInstances.Bind(cmd);
                torusMesh.DrawIndexedInstanced(cmd, torusMesh.IndexCount(), 0, torusInstanceCount);
            },
            // UI覆盖层：构建面板并在UI渲染通道中录制ImGui绘制数据
            [&](VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent)
            {
                editorOverlay.NewFrame();

                BigHero::EditorStats stats;
                stats.fps = lastFps;
                stats.frameMs = lastFrameMs;
                stats.gpuName = ctx.PhysicalDeviceName();
                stats.extent = renderer.Extent();
                stats.msaaSamples = static_cast<uint32_t>(renderer.SampleCount());
                stats.triangleCount = triangleCount;
                stats.culledCount = culledCount;
                // 主场景实例化批次：立方体(若>0) + 地面(恒1) + 圆环(若>0)
                stats.batchCount = (cubeInstanceCount > 0 ? 1u : 0u) + 1u +
                    (torusInstanceCount > 0 ? 1u : 0u);
                if (const BigHero::Render::GpuProfiler* profiler = renderer.GetProfiler())
                {
                    stats.gpuFrameMs = profiler->FrameMs();
                    stats.gpuShadowMs = profiler->ShadowMs();
                    stats.gpuSceneMs = profiler->SceneMs();
                    stats.gpuUiMs = profiler->UiMs();
                }
                editorPanel.Draw(stats, scene, lightParams, camera.fovDegrees_, pointLights,
                    selectedObject, &deferred, &gizmoMode,
                    glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));

                // ---- Gizmo 屏幕手柄：选中物体时绘制三轴（X红/Y绿/Z蓝），拖拽轴加粗高亮 ----
                if (selectedObject >= 0 && selectedObject < static_cast<int>(scene.size()))
                {
                    const glm::mat4 gvp = camViewProj;
                    const glm::vec2 gvpSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
                    const glm::vec3 origin = scene[static_cast<size_t>(selectedObject)].position;
                    const glm::vec2 o = BigHero::Editor::ProjectWorldToScreen(origin, gvp, gvpSize);
                    if (o.x > -1e8f)
                    {
                        ImDrawList* dl = ImGui::GetForegroundDrawList();
                        const glm::vec3 axesW[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
                        const ImU32 cols[3] = {
                            IM_COL32(232, 72, 72, 255), IM_COL32(72, 210, 96, 255), IM_COL32(80, 130, 240, 255) };
                        const ImVec2 oPx(o.x, o.y);
                        for (int a = 0; a < 3; ++a)
                        {
                            const glm::vec2 tip = BigHero::Editor::ProjectWorldToScreen(
                                origin + axesW[a], gvp, gvpSize);
                            if (tip.x < -1e8f)
                                continue;
                            const bool active = gizmoDragging &&
                                (static_cast<BigHero::Editor::GizmoAxis>(a) == gizmoDragAxis);
                            dl->AddLine(oPx, ImVec2(tip.x, tip.y), cols[a], active ? 4.0f : 2.5f);
                            dl->AddCircleFilled(ImVec2(tip.x, tip.y), active ? 8.0f : 5.0f, cols[a]);
                        }
                        dl->AddCircleFilled(oPx, 4.0f, IM_COL32(235, 235, 235, 255));
                    }
                }

                editorOverlay.Render(cmd, imageIndex);
            },
            // ---- 阴影深度预通道：从光源视空间把全部几何写入阴影贴图 ----
            [&](VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D)
            {
                shadowMap.RecordPass(cmd, [&](VkCommandBuffer c)
                {
                    shadowPipeline.Bind(c);

                    const auto drawShadow = [&](const glm::mat4& model,
                        BigHero::Render::Mesh& mesh, uint32_t count, uint32_t first)
                    {
                        const PushShadow push{ lightSpace, model };
                        vkCmdPushConstants(c, shadowPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                            0, sizeof(PushShadow), &push);
                        mesh.Bind(c);
                        mesh.DrawIndexed(c, count, first);
                    };

                    for (size_t i = 0; i < scene.size(); ++i)
                    {
                        const BigHero::Scene::SceneObject& obj = scene[i];
                        if (obj.meshId != 0)
                            continue;
                        drawShadow(objectModel(obj, i), sceneMesh, BigHero::Scene::kCubeIndexCount, 0);
                    }

                    drawShadow(glm::mat4(1.0f), sceneMesh,
                        BigHero::Scene::kGroundIndexCount, BigHero::Scene::kGroundIndexOffset);

                    for (size_t i = 0; i < scene.size(); ++i)
                    {
                        const BigHero::Scene::SceneObject& obj = scene[i];
                        if (obj.meshId != 1)
                            continue;
                        drawShadow(objectModel(obj, i), torusMesh, torusMesh.IndexCount(), 0);
                    }
                });

                // 点光源立方体阴影：仅当存在启用阴影的灯时才录制 6 面深度预通道
                if (pointLights.size() > 0 &&
                    std::any_of(pointLights.begin(), pointLights.end(),
                        [](const BigHero::PointLightParams& pl) { return pl.castsShadow; }))
                {
                    cubeShadowMap.RecordPass(cmd, [&](VkCommandBuffer c, int face)
                    {
                        cubeShadowPipeline.Bind(c);

                        // 绑定帧并行槽位的第 3 个描述符集（set2：6 个面视投影矩阵 UBO）。
                        // face 矩阵在每帧 UBO 中为同一光源，任意面共用同一 set。
                        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            cubeShadowPipeline.GetLayout(), 0, 1,
                            &descSets[frameIndex * 3 + 2], 0, nullptr);

                        const auto drawCubeShadow = [&](const glm::mat4& model,
                            BigHero::Render::Mesh& mesh, uint32_t count, uint32_t first)
                        {
                            const PushCubeShadow push{
                                model, glm::vec4(static_cast<float>(face), 0.0f, 0.0f, 0.0f)
                            };
                            vkCmdPushConstants(c, cubeShadowPipeline.GetLayout(),
                                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow), &push);
                            mesh.Bind(c);
                            mesh.DrawIndexed(c, count, first);
                        };

                        for (size_t i = 0; i < scene.size(); ++i)
                        {
                            const BigHero::Scene::SceneObject& obj = scene[i];
                            if (obj.meshId != 0)
                                continue;
                            drawCubeShadow(objectModel(obj, i), sceneMesh,
                                BigHero::Scene::kCubeIndexCount, 0);
                        }

                        drawCubeShadow(glm::mat4(1.0f), sceneMesh,
                            BigHero::Scene::kGroundIndexCount, BigHero::Scene::kGroundIndexOffset);

                        for (size_t i = 0; i < scene.size(); ++i)
                        {
                            const BigHero::Scene::SceneObject& obj = scene[i];
                            if (obj.meshId != 1)
                                continue;
                            drawCubeShadow(objectModel(obj, i), torusMesh, torusMesh.IndexCount(), 0);
                        }
                    });
                }
            },
            // ---- 延迟光照子通道：全屏三角形采样 GBuffer 输入附件，输出到交换链 ----
            [&](VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D)
            {
                const VkDescriptorSet sets[] = {
                    descSets[frameIndex * 3 + 0],               // set0 相机
                    descSets[frameIndex * 3 + 1],               // set1 光照/阴影/IBL
                    descManager.GetGBufferSets()[imageIndex]    // set2 GBuffer 输入附件
                };
                lightingPipeline.Bind(cmd);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline.GetLayout(),
                    0, 3, sets, 0, nullptr);
                const glm::mat4 invVP = invViewProj; // 复用外层每帧计算的逆视投影
                vkCmdPushConstants(cmd, lightingPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(glm::mat4), &invVP);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });
        }

        ctx.WaitIdle();
        LOG_INFO("渲染循环结束，资源由RAII自动释放");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("引擎异常退出: " << e.what());
        return EXIT_FAILURE;
    }
}
