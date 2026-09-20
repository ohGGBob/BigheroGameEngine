// Application 渲染录制翻译单元：RenderGraph 录制回调（RecordScene / RecordUi / RecordPrePass /
// RecordParallelCubeShadow / RecordLighting / RecordTransparent）与阴影批次绘制辅助
// （DrawShadowCasters / DrawCubeShadowCasters）。
// 从 Application.cpp 拆出（2026-09 复评收尾：Application 主文件 ≤1200 行）。
#include "app/Application.h"

#include "core/VkCheck.h"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace BigHero
{
// 升级 20：场景快照命令短名（RecordUi 抓取帧起始快照用）
using Game::SceneSnapshot;

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

        // 人物部件：球 + 胶囊（meshId=3/4）
        sphereMesh_.Bind(cmd);
        sphereInstances_.Bind(cmd);
        sphereMesh_.DrawIndexedInstanced(cmd, sphereMesh_.IndexCount(), 0, sphereInstanceCount_);

        capsuleMesh_.Bind(cmd);
        capsuleInstances_.Bind(cmd);
        capsuleMesh_.DrawIndexedInstanced(cmd, capsuleMesh_.IndexCount(), 0, capsuleInstanceCount_);

        // glTF 模型：不透明 + MASK 批次（BLEND 批次不进 GBuffer，由透明叠加通道处理）
        DrawGltfPrims(cmd, *gbufferPipeline_, 0);
        return;
    }

    // 前向：天空盒最先绘制（不写深度，场景覆盖其上）。
    // 规范要求：每条管线绘制前用其自身管线布局绑定描述符集
    // （天空盒与主管线布局对象不同，复用对方布局绑定是 VUID-08600 违规）

    // 天空盒是前向分支首个 draw：所有管线均声明 dynamic viewport/scissor，
    // 绘制前必须设置状态（VUID-vkCmdDraw-None-07831/07832）。此前依赖阴影预通道
    // 遗留的 tile 视口——PRE 跳过时状态未定义（AMD 驱动挂死），完整配置下天空盒
    // 也被裁到级联 tile 区域外（渲染错误）
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

    skyboxPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_->GetLayout(), 0, 2, sceneSets, 0,
                            nullptr);
    // 后处理关=片元内直通 ACES（tonemapDirect=1）；后处理开=输出线性 HDR 交给合成端
    const PushSky skyPush{glm::inverse(ActiveViewProj()), renderer_.IsPostProcessing() ? 0.0f : 1.0f};
    vkCmdPushConstants(cmd, skyboxPipeline_->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushSky), &skyPush);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    pipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->GetLayout(), 0, 2, sceneSets, 0, nullptr);

    // 默认纹理池槽位（立方体/地面/圆环共用：0=全局反照率 1=全局法线 2=纯白mr透传）
    PushObject defaultPush{};
    // 后处理关=片元内直通 ACES；后处理开=输出线性 HDR 交给合成端统一色调映射
    defaultPush.outputTarget = renderer_.IsPostProcessing() ? 1 : 0;
    vkCmdPushConstants(cmd, pipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject), &defaultPush);

    // viewport/scissor 已在天空盒绘制前统一设置（本分支首个 draw 之前）

    sceneMesh_.Bind(cmd);
    cubeInstances_.Bind(cmd);
    sceneMesh_.DrawIndexedInstanced(cmd, Scene::kCubeIndexCount, 0, cubeInstanceCount_);

    sceneMesh_.Bind(cmd);
    groundInstances_.Bind(cmd);
    sceneMesh_.DrawIndexedInstanced(cmd, Scene::kGroundIndexCount, Scene::kGroundIndexOffset, 1);

    torusMesh_.Bind(cmd);
    torusInstances_.Bind(cmd);
    torusMesh_.DrawIndexedInstanced(cmd, torusMesh_.IndexCount(), 0, torusInstanceCount_);

    // 人物部件：球 + 胶囊（meshId=3/4）
    sphereMesh_.Bind(cmd);
    sphereInstances_.Bind(cmd);
    sphereMesh_.DrawIndexedInstanced(cmd, sphereMesh_.IndexCount(), 0, sphereInstanceCount_);

    capsuleMesh_.Bind(cmd);
    capsuleInstances_.Bind(cmd);
    capsuleMesh_.DrawIndexedInstanced(cmd, capsuleMesh_.IndexCount(), 0, capsuleInstanceCount_);

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
        const glm::mat4 viewProj = ActiveViewProj();
        // 由视图矩阵的行向量提取相机世界右/上轴（billboard 展开用）
        const glm::mat4& view = ActiveView();
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

void Application::RecordUi(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent)
{
    // headless（无窗口/交换链）下编辑器覆盖层未初始化：跳过整帧 UI 录制，
    // 避免对未创建 ImGui 上下文的 NewFrame/Render 调用
    if (!editorOverlay_.IsInitialized())
        return;

    // --no-ui（成像回归基线）：跳过编辑器覆盖层与运行时 UI 录制（ImGui NewFrame/面板绘制/
    // 运行时 UI 画布/Gizmo 与物理调试线/最终 Render），仅保留后处理参数同步副作用，保证
    // --post-process 下场景渲染与带 UI 路径一致；截图为纯场景，基线从此
    // 不受编辑器 UI 迭代影响。headless 已在上方早退，行为不变。
    if (config_.noUi)
    {
        SyncPostProcessFrameState(imageIndex, extent);
        return;
    }

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

    editorPanel_.Draw(
        stats, scene_, lightParams_, camera_.fovDegrees_, pointLights_, selectedObject_, &postProcessSync_.deferred,
        &gizmoMode_, glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)), &masterVolume_,
        &postProcessSync_.postProcess, &postProcessSync_.ssao, &postProcessSync_.ssr, &physicsHost_.enabled,
        &physicsHost_.debugDraw, &physicsHost_.gravity, &physicsHost_.characterEnabled, &physicsHost_.characterSpeed,
        &physicsHost_.characterJumpForce, &physicsHost_.joints, &animationHost_.StateMachine(), &navHost_.enabled,
        &particleHost_.enabled, &navHost_.agentEnabled, &particleHost_.emitterConfig, &particleHost_.gravity,
        &particleHost_.damping, &particleHost_.emitterPresetIndex, &postProcessSync_.gradeSaturation,
        &postProcessSync_.gradeContrast, &postProcessSync_.gradeLift, &postProcessSync_.gradeGain,
        &postProcessSync_.gradeGamma, &postProcessSync_.dofEnabled, &postProcessSync_.dofFocusDistance,
        &postProcessSync_.dofAperture, &postProcessSync_.dofMaxBlur, &postProcessSync_.mbEnabled,
        &postProcessSync_.mbStrength, &postProcessSync_.mbMaxBlur, &postProcessSync_.mbMaxSamples,
        &postProcessSync_.fogEnabled, &postProcessSync_.fogDensity, &postProcessSync_.fogHeightFalloff,
        &postProcessSync_.fogBaseHeight, &postProcessSync_.fogScatter, &postProcessSync_.fogTint,
        &postProcessSync_.fogShadowEnabled, &postProcessSync_.fogSteps, &postProcessSync_.autoExposure,
        &postProcessSync_.exposureKeyValue, &postProcessSync_.adaptationSpeed, &postProcessSync_.vignetteIntensity,
        &postProcessSync_.vignetteRadius, &postProcessSync_.filmGrain, &postProcessSync_.taaEnabled,
        &postProcessSync_.taaFeedback, &assetRegistry_, &meshResources_);
    audioEngine_.SetMasterVolume(masterVolume_);

    // S1 3D 音频：监听器每帧从活跃相机同步（Pod 参数，AudioEngine 不反向依赖相机类型；
    // up 沿用两套相机 lookAt 共用的世界竖直轴）
    audioEngine_.UpdateListener(
        Audio::AudioListenerState{ActivePosition(), ActiveForward(), glm::vec3(0.0f, 1.0f, 0.0f)});

    // 阶段 3a：后处理参数/相机环境/雾阴影资源每帧同步进 PostProcessor（子系统封装，升级 21-28）
    SyncPostProcessFrameState(imageIndex, extent);

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
        const glm::mat4 gvp = ActiveViewProj();
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
        const glm::mat4 gvp = ActiveViewProj();
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

    // ---- U1-UI 运行时 UI 画布：场景之上、编辑器 ImGui 覆盖层之前 ----
    // 单独渲染通道（loadOp=LOAD）：绘制后保持 COLOR_ATTACHMENT_OPTIMAL，由下方
    // editorOverlay_.Render 的覆盖层通道（finalLayout=PRESENT）完成呈现布局转换
    uiRuntime_.Record(cmd, frameIndex, imageIndex, extent);

    editorOverlay_.Render(cmd, imageIndex);
}

// 后处理参数/相机环境/雾阴影资源每帧同步进 PostProcessor：
// RecordUi 全路径与 --no-ui（成像回归基线）共用，保证两条路径的 PP 渲染一致。
void Application::SyncPostProcessFrameState(uint32_t imageIndex, VkExtent2D extent)
{
    // 防御：lightUbos_ 在资源初始化时按帧数填充；为空（异常初始化时序）时直接跳过本帧同步，
    // 避免 imageIndex % 0 整数除零与无意义的 UBO 引用。
    if (lightUbos_.empty())
    {
        static bool sWarned = false;
        if (!sWarned)
        {
            sWarned = true;
            LOG_WARN("光照 UBO 未初始化，跳过本帧后处理参数同步（后续同类告警已抑制）");
        }
        return;
    }
    postProcessSync_.SyncToPostProcessor(
        renderer_.GetPostProcessor(), extent, lightUbos_[imageIndex % lightUbos_.size()].buffer, shadowMap_.View(),
        shadowMap_.Sampler(), lightParams_, ActivePosition(), ActiveForward(),
        (cameraMode_ == CameraMode::FirstPerson ? fpCamera_.fovDegrees_ : camera_.fovDegrees_), deltaTime_);
}

void Application::RecordPrePass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D)
{
    // 帧瞬态上传：把本帧登记的实例/粒子数据从 arena 拷入设备本地缓冲（frameIndex = 当前帧槽位；
    // 该槽位栅栏已等待并 Reset，覆写安全）。在首个 pass 内录制，本帧后续所有 pass（场景/透明/
    // 粒子）经 TRANSFER→VERTEX_INPUT 屏障可见。替代旧路径逐帧 staging 分配+一次性提交。
    renderer_.Staging().RecordUploads(cmd, frameIndex, pendingUploads_.data(), pendingUploads_.size());
    pendingUploads_.clear();

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

void Application::RecordLighting(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent)
{
    using RDS = Render::FrameDescriptorSet;
    const std::vector<VkDescriptorSet>& sets = descManager_.GetSets();

    // 更新 AO 描述符集：SSAO 启用时绑定 AO 输出，否则绑定 1x1 白纹理（AO=1）
    if (renderer_.IsSSAO() && renderer_.GetSSAO()->GetAOView() != VK_NULL_HANDLE)
        descManager_.UpdateAOSet(renderer_.GetSSAO()->GetAOView());
    else
        descManager_.UpdateAOSet(renderer_.GetDummyWhiteView());

    // GBuffer 描述符集数量应等于交换链图像数（由 UpdateGBufferSets 维护）。若因交换链
    // 重建时序不一致导致 imageIndex 越界，直接跳过本帧光照（画面一帧缺失）远优于裸下标
    // 触发断言崩溃——后者会中断整个渲染循环。
    const std::vector<VkDescriptorSet>& gbufferSets = descManager_.GetGBufferSets();
    if (gbufferSets.empty() || imageIndex >= gbufferSets.size())
    {
        static bool sWarned = false;
        if (!sWarned)
        {
            sWarned = true;
            LOG_WARN("光照 Pass 跳过：GBuffer 描述符集数量 " << gbufferSets.size() << " 与 imageIndex " << imageIndex
                                                             << " 不匹配（后续同类警告已抑制）");
        }
        return;
    }
    const VkDescriptorSet lightSets[] = {sets[Render::FrameSetIndex(frameIndex, RDS::Camera)],
                                         sets[Render::FrameSetIndex(frameIndex, RDS::Light)], gbufferSets[imageIndex],
                                         descManager_.aoSet};
    // 管线为 dynamic viewport/scissor（VUID-07831/07832）：光照通道绘制前显式设置
    // 全屏视口，不依赖 gBuffer 通道遗留的动态状态
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

    lightingPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline_->GetLayout(), 0, 4, lightSets, 0,
                            nullptr);
    const glm::mat4 invVP = glm::inverse(ActiveViewProj());
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

    // ECS 渲染收敛：直读 ECS（稳定序与包一致；阴影不剔射、glTF 不乘 GltfOffset——现状保持）
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId != 0)
                return;
            drawOne(world, sceneMesh_, Scene::kCubeIndexCount, 0);
        });

    drawOne(glm::mat4(1.0f), sceneMesh_, Scene::kGroundIndexCount, Scene::kGroundIndexOffset);

    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId != 1)
                return;
            drawOne(world, torusMesh_, torusMesh_.IndexCount(), 0);
        });

    // 人物部件：球 + 胶囊（meshId=3/4）
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId == 3)
                drawOne(world, sphereMesh_, sphereMesh_.IndexCount(), 0);
            else if (r.meshId == 4)
                drawOne(world, capsuleMesh_, capsuleMesh_.IndexCount(), 0);
        });

    // glTF 模型（索引区间连续，整模一次绘制）
    if (hasGltf_)
    {
        ecsScene_.ForEachRenderableWorld(
            [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
                const glm::mat4& world)
            {
                if (r.meshId != 2)
                    return;
                drawOne(world, gltfMesh_, gltfMesh_.IndexCount(), 0);
            });
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

    // ECS 渲染收敛：直读 ECS（同 DrawShadowCasters 口径）
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId != 0)
                return;
            drawOne(world, sceneMesh_, Scene::kCubeIndexCount, 0);
        });

    drawOne(glm::mat4(1.0f), sceneMesh_, Scene::kGroundIndexCount, Scene::kGroundIndexOffset);

    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId != 1)
                return;
            drawOne(world, torusMesh_, torusMesh_.IndexCount(), 0);
        });

    // 人物部件：球 + 胶囊（meshId=3/4）
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            if (r.meshId == 3)
                drawOne(world, sphereMesh_, sphereMesh_.IndexCount(), 0);
            else if (r.meshId == 4)
                drawOne(world, capsuleMesh_, capsuleMesh_.IndexCount(), 0);
        });

    // glTF 模型（索引区间连续，整模一次绘制）
    if (hasGltf_)
    {
        ecsScene_.ForEachRenderableWorld(
            [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
                const glm::mat4& world)
            {
                if (r.meshId != 2)
                    return;
                drawOne(world, gltfMesh_, gltfMesh_.IndexCount(), 0);
            });
    }
}

} // namespace BigHero
