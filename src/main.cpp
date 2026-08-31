#include "core/Log.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/Renderer.h"
#include "render/EnvironmentLighting.h"
#include "render/Mesh.h"
#include "render/ShadowMap.h"
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

    // 推送常量：逐物体的模型矩阵 + 材质参数（PBR）
    struct PushObject
    {
        glm::mat4 model;
        glm::vec4 tint;
        float metallic;
        float roughness;
    };
    static_assert(sizeof(PushObject) <= 128, "推送常量超出保证的最小上限");

    // 阴影预通道推送常量：光照视投影矩阵 + 物体模型矩阵
    struct PushShadow
    {
        glm::mat4 lightSpace;
        glm::mat4 model;
    };
    static_assert(sizeof(PushShadow) == 128, "PushShadow必须恰为推送常量上限");

    // 天空盒推送常量：view*proj逆矩阵
    struct PushSky
    {
        glm::mat4 invViewProj;
    };
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
        cameraUbos.reserve(kFrameCount);
        lightUbos.reserve(kFrameCount);
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            cameraUbos.emplace_back(ctx.Device(), ctx.PhysicalDevice(), ctx.GraphicsFamily());
            lightUbos.emplace_back(ctx.Device(), ctx.PhysicalDevice(), ctx.GraphicsFamily());
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

        // ---- 绑定描述符：set0相机UBO，set1光照UBO+反照率纹理+法线贴图 ----
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            descManager.UpdateSet(i * 2 + 0, 0, cameraUbos[i]);
            descManager.UpdateSet(i * 2 + 1, 0, lightUbos[i]);
            descManager.UpdateSetImage(i * 2 + 1, 1, texture.View(), texture.Sampler());
            descManager.UpdateSetImage(i * 2 + 1, 2, normalTexture.View(), normalTexture.Sampler());
            descManager.UpdateSetImage(i * 2 + 1, 3, shadowMap.View(), shadowMap.Sampler(),
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
            descManager.UpdateSetImage(i * 2 + 1, 4, envLighting.EnvView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 2 + 1, 5, envLighting.IrradianceView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 2 + 1, 6, envLighting.PrefilteredView(), envLighting.Sampler());
            descManager.UpdateSetImage(i * 2 + 1, 7, envLighting.BrdfLutView(), envLighting.Sampler());
        }

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

        BigHero::Render::GraphicsPipelineConfig pipelineConfig;
        pipelineConfig.setLayouts = { descManager.layoutCamera, descManager.layoutLight };
        pipelineConfig.pushConstants = {
            VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushObject) } // 片段阶段读取材质参数
        };
        const VkVertexInputBindingDescription vertexBinding = BigHero::Scene::Vertex::getBindingDesc();
        const std::vector<VkVertexInputAttributeDescription> vertexAttributes = BigHero::Scene::Vertex::getAttrDesc();
        pipelineConfig.vertexBinding = &vertexBinding;
        pipelineConfig.vertexAttributes = &vertexAttributes;
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
        shadowConfig.vertexBinding = &vertexBinding;
        shadowConfig.vertexAttributes = &vertexAttributes;
        shadowConfig.cullMode = VK_CULL_MODE_FRONT_BIT;
        shadowConfig.depthOnly = true;

        BigHero::Render::GraphicsPipeline shadowPipeline(ctx.Device(), shadowMap.GetRenderPass(),
            std::move(shadowVert), std::move(shadowFrag), shadowConfig);

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
        renderer.SetRenderPassRecreateCallback(rebuildMainPipelines);

        // ---- 编辑器：UI覆盖层 + 可调光照参数 ----
        BigHero::EditorOverlay editorOverlay;
        editorOverlay.Init(ctx, window, renderer.GetSwapchain());
        renderer.SetResizeCallback([&editorOverlay, &renderer]()
        {
            editorOverlay.RecreateFramebuffers(renderer.GetSwapchain());
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

        BigHero::OrbitCamera camera;
        int selectedObject = -1; // 编辑器拾取选中（-1为空）
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
            if (window.IsMouseButtonDown(BigHero::Window::kMouseButtonLeft))
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

            // ---- 方向光阴影矩阵：以场景中心为靶点的正交光照视空间 ----
            const glm::vec3 lightEye = glm::normalize(-lightParams.direction) * 20.0f;
            const glm::vec3 lightUp = (std::fabs(lightParams.direction.y) > 0.99f)
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::mat4 lightSpace = glm::ortho(-14.0f, 14.0f, -14.0f, 14.0f, 0.5f, 45.0f)
                * glm::lookAt(lightEye, glm::vec3(0.0f), lightUp);

            // ---- 更新各帧并行槽位的UBO（光照参数由编辑器面板驱动） ----
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
                    if (li < pointLights.size())
                    {
                        lightData.lights[li].position = pointLights[li].position;
                        lightData.lights[li].intensity = pointLights[li].intensity;
                        lightData.lights[li].color = pointLights[li].color;
                        lightData.lights[li].radius = pointLights[li].radius;
                    }
                    else
                    {
                        lightData.lights[li] = {};
                    }
                }
                lightUbos[i].Update(lightData);
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
                    * glm::scale(glm::mat4(1.0f), glm::vec3(obj.scale));
            };

            const glm::mat4 invViewProj = glm::inverse(camera.Proj() * camera.View());

            // ---- 点击拾取：左键单击选择物体，右键取消（ImGui占用鼠标时跳过） ----
            const bool leftClicked = window.ConsumeClick();
            if (window.ConsumeRightClick())
                selectedObject = -1;
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

            renderer.DrawFrame(
                // ---- 场景通道 ----
                [&](VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent)
            {
                // 描述符先行绑定：天空盒与场景共用同一套set
                const VkDescriptorSet sets[] = {
                    descSets[frameIndex * 2 + 0],
                    descSets[frameIndex * 2 + 1]
                };
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

                // 立方体实例：共用立方体网格，模型矩阵与着色走推送常量
                sceneMesh.Bind(cmd);
                for (size_t i = 0; i < scene.size(); ++i)
                {
                    const BigHero::Scene::SceneObject& obj = scene[i];
                    if (obj.meshId != 0)
                        continue;

                    const PushObject push{ objectModel(obj, i), glm::vec4(obj.tint, 1.0f),
                        obj.metallic, obj.roughness };
                    vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(PushObject), &push);
                    sceneMesh.DrawIndexed(cmd, BigHero::Scene::kCubeIndexCount, 0);
                }

                // 地面（模型为恒等矩阵，哑光电介质材质）
                const PushObject groundPush{ glm::mat4(1.0f), glm::vec4(1.0f), 0.0f, 0.9f };
                vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                    0, sizeof(PushObject), &groundPush);
                sceneMesh.DrawIndexed(cmd, BigHero::Scene::kGroundIndexCount,
                    BigHero::Scene::kGroundIndexOffset);

                // 外部加载模型（圆环体）
                for (size_t i = 0; i < scene.size(); ++i)
                {
                    const BigHero::Scene::SceneObject& obj = scene[i];
                    if (obj.meshId != 1)
                        continue;

                    torusMesh.Bind(cmd);
                    const PushObject push{ objectModel(obj, i), glm::vec4(obj.tint, 1.0f),
                        obj.metallic, obj.roughness };
                    vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(PushObject), &push);
                    torusMesh.DrawIndexed(cmd, torusMesh.IndexCount(), 0);
                }
            },
            // UI覆盖层：构建面板并在UI渲染通道中录制ImGui绘制数据
            [&](VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D)
            {
                editorOverlay.NewFrame();

                BigHero::EditorStats stats;
                stats.fps = lastFps;
                stats.frameMs = lastFrameMs;
                stats.gpuName = ctx.PhysicalDeviceName();
                stats.extent = renderer.Extent();
                stats.msaaSamples = static_cast<uint32_t>(renderer.SampleCount());
                stats.triangleCount = triangleCount;
                if (const BigHero::Render::GpuProfiler* profiler = renderer.GetProfiler())
                {
                    stats.gpuFrameMs = profiler->FrameMs();
                    stats.gpuShadowMs = profiler->ShadowMs();
                    stats.gpuSceneMs = profiler->SceneMs();
                    stats.gpuUiMs = profiler->UiMs();
                }
                editorPanel.Draw(stats, scene, lightParams, camera.fovDegrees_, pointLights,
                    selectedObject);

                editorOverlay.Render(cmd, imageIndex);
            },
            // ---- 阴影深度预通道：从光源视空间把全部几何写入阴影贴图 ----
            [&](VkCommandBuffer cmd, uint32_t, VkExtent2D)
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
