// Application 渲染录制翻译单元：RenderGraph 录制回调（RecordScene / RecordUi / RecordPrePass /
// RecordParallelCubeShadow / RecordLighting / RecordTransparent）与阴影批次绘制辅助
// （DrawShadowCasters / DrawCubeShadowCasters）。
// 从 Application.cpp 拆出（2026-09 复评收尾：Application 主文件 ≤1200 行）。
#include "app/Application.h"

#include "core/VkCheck.h"

#include <algorithm>
#include <cmath>
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

        // 人物部件：球 + 胶囊（meshId=3/4），高模 + LOD 低模分组绘制
        sphereMesh_.Bind(cmd);
        sphereInstances_.Bind(cmd);
        sphereMesh_.DrawIndexedInstanced(cmd, sphereMesh_.IndexCount(), 0, sphereInstanceCount_);

        sphereLodMesh_.Bind(cmd);
        sphereLodInstances_.Bind(cmd);
        sphereLodMesh_.DrawIndexedInstanced(cmd, sphereLodMesh_.IndexCount(), 0, sphereLodInstanceCount_);

        capsuleMesh_.Bind(cmd);
        capsuleInstances_.Bind(cmd);
        capsuleMesh_.DrawIndexedInstanced(cmd, capsuleMesh_.IndexCount(), 0, capsuleInstanceCount_);

        capsuleLodMesh_.Bind(cmd);
        capsuleLodInstances_.Bind(cmd);
        capsuleLodMesh_.DrawIndexedInstanced(cmd, capsuleLodMesh_.IndexCount(), 0, capsuleLodInstanceCount_);

        // glTF 模型：不透明 + MASK 批次（BLEND 批次不进 GBuffer，由透明叠加通道处理）
        // 方块世界：逐区块合并网格（视锥剔除 + identity 实例绘制）
        if (voxelMode_ && voxelInstances_.IsValid())
        {
            const Render::Frustum frustum = Render::Frustum::FromViewProj(ActiveViewProj());
            for (VoxelChunkGpu& chunk : voxelChunks_)
            {
                if (!chunk.uploaded || !chunk.mesh.IsValid())
                    continue;
                if (!frustum.IntersectsSphere(chunk.center, chunk.radius))
                    continue;
                chunk.mesh.Bind(cmd);
                voxelInstances_.Bind(cmd);
                chunk.mesh.DrawIndexedInstanced(cmd, chunk.mesh.IndexCount(), 0, 1);
            }
        }
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

    // 人物部件：球 + 胶囊（meshId=3/4），高模 + LOD 低模分组绘制
    sphereMesh_.Bind(cmd);
    sphereInstances_.Bind(cmd);
    sphereMesh_.DrawIndexedInstanced(cmd, sphereMesh_.IndexCount(), 0, sphereInstanceCount_);

    sphereLodMesh_.Bind(cmd);
    sphereLodInstances_.Bind(cmd);
    sphereLodMesh_.DrawIndexedInstanced(cmd, sphereLodMesh_.IndexCount(), 0, sphereLodInstanceCount_);

    capsuleMesh_.Bind(cmd);
    capsuleInstances_.Bind(cmd);
    capsuleMesh_.DrawIndexedInstanced(cmd, capsuleMesh_.IndexCount(), 0, capsuleInstanceCount_);

    capsuleLodMesh_.Bind(cmd);
    capsuleLodInstances_.Bind(cmd);
    capsuleLodMesh_.DrawIndexedInstanced(cmd, capsuleLodMesh_.IndexCount(), 0, capsuleLodInstanceCount_);

    // 方块世界：逐区块合并网格（视锥剔除 + identity 实例绘制）
    if (voxelMode_ && voxelInstances_.IsValid())
    {
        const Render::Frustum frustum = Render::Frustum::FromViewProj(ActiveViewProj());
        for (VoxelChunkGpu& chunk : voxelChunks_)
        {
            if (!chunk.uploaded || !chunk.mesh.IsValid())
                continue;
            if (!frustum.IntersectsSphere(chunk.center, chunk.radius))
                continue;
            chunk.mesh.Bind(cmd);
            voxelInstances_.Bind(cmd);
            chunk.mesh.DrawIndexedInstanced(cmd, chunk.mesh.IndexCount(), 0, 1);
        }
    }

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

    // 方块世界「纯游戏模式」：编辑器面板整体收起，只留准星 + 方块世界 HUD（F1 切换 / --editor-ui 展开）。
    // 面板未绘制 ⇒ 不产生属性编辑手势、不发起烘焙请求；Undo 仍以 frameStart 为准，行为不变。
    const bool showEditorPanels = !(voxelMode_ && voxelPlayMode_);
    if (showEditorPanels)
    {
        editorPanel_.Draw(
            stats, scene_, lightParams_, camera_.fovDegrees_, pointLights_, selectedObject_, &postProcessSync_.deferred,
            &gizmoMode_, glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)), &masterVolume_,
            &postProcessSync_.postProcess, &postProcessSync_.ssao, &postProcessSync_.ssr, &physicsHost_.enabled,
            &physicsHost_.debugDraw, &physicsHost_.gravity, &physicsHost_.characterEnabled,
            &physicsHost_.characterSpeed, &physicsHost_.characterJumpForce, &physicsHost_.joints,
            &animationHost_.StateMachine(), &navHost_.enabled, &particleHost_.enabled, &navHost_.agentEnabled,
            &particleHost_.emitterConfig, &particleHost_.gravity, &particleHost_.damping,
            &particleHost_.emitterPresetIndex, &postProcessSync_.gradeSaturation, &postProcessSync_.gradeContrast,
            &postProcessSync_.gradeLift, &postProcessSync_.gradeGain, &postProcessSync_.gradeGamma,
            &postProcessSync_.dofEnabled, &postProcessSync_.dofFocusDistance, &postProcessSync_.dofAperture,
            &postProcessSync_.dofMaxBlur, &postProcessSync_.mbEnabled, &postProcessSync_.mbStrength,
            &postProcessSync_.mbMaxBlur, &postProcessSync_.mbMaxSamples, &postProcessSync_.fogEnabled,
            &postProcessSync_.fogDensity, &postProcessSync_.fogHeightFalloff, &postProcessSync_.fogBaseHeight,
            &postProcessSync_.fogScatter, &postProcessSync_.fogTint, &postProcessSync_.fogShadowEnabled,
            &postProcessSync_.fogSteps, &postProcessSync_.autoExposure, &postProcessSync_.exposureKeyValue,
            &postProcessSync_.adaptationSpeed, &postProcessSync_.vignetteIntensity, &postProcessSync_.vignetteRadius,
            &postProcessSync_.filmGrain, &postProcessSync_.taaEnabled, &postProcessSync_.taaFeedback, &assetRegistry_,
            &meshResources_);
        // U2 工程面板：资产数据库（引用图/断链）+ LOD/光照探针/遮挡剔除
        projectPanel_.Draw();
        // 烘焙请求在这里兑现：只有 Application 持有场景数据，面板不直接读场景。
        // （与命令行 --bake-probes/--bake-occlusion 共用 RunPendingBakes；Bake* 内部
        //  清除请求标志，命令行路径在主循环前已同步执行，此处再调用零成本）
        RunPendingBakes();
    }

    // 场景切换：编辑器"场景"下拉框请求（运行期原地切换，主循环消费后重建场景）
    if (!editorPanel_.requestedSceneKind_.empty())
    {
        pendingSceneKind_ = editorPanel_.requestedSceneKind_;
        editorPanel_.requestedSceneKind_.clear();
    }
    // 展示厅 HUD（--scene cybercity）：准星 / 交互提示 / 特性清单 / 按键帮助
    DrawShowcaseHud();
    DrawVoxelHud();

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

// 兑现待处理的烘焙请求（面板按钮与命令行 --bake-probes/--bake-occlusion 共用）：
// 只有 Application 持有场景数据，面板不直接读场景；Bake* 内部清除请求标志，
// 因此命令行路径（主循环前同步执行）与 RecordUi 首帧不会重复烘焙。
void Application::RunPendingBakes()
{
    if (projectPanel_.probeBakeRequested)
    {
        projectPanel_.BakeProbes();
        probeDirty_ = true; // 探针体已变化，标记脏以触发下次 UpdateUniforms 全量上传
    }
    if (projectPanel_.occlusionBakeRequested)
    {
        std::vector<BigHero::Render::Bounds3> occluders;
        std::vector<BigHero::Render::Bounds3> cullables;
        occluders.reserve(scene_.size());
        cullables.reserve(scene_.size());
        for (const Scene::SceneObject& o : scene_)
        {
            BigHero::Render::Bounds3 b;
            const float h = o.scale * 0.5f;
            b.min = o.position - glm::vec3(h);
            b.max = o.position + glm::vec3(h);
            cullables.push_back(b);
            // 大体积静态体才当遮挡体（小道具挡不住东西，只会拖慢烘焙）
            if (o.scale >= kOccluderMinScale)
                occluders.push_back(b);
        }
        projectPanel_.BakeOcclusion(occluders, cullables);
        // 烘焙证据：对象数 / 格数 / 平均可见比例（越低剔除越有效）/ PVS 位图体积
        LOG_INFO("遮挡 PVS 烘焙完成: " << projectPanel_.Occlusion().ObjectCount() << " 个 cullable × "
                                                  << projectPanel_.Occlusion().CellCount() << " 格，平均可见比例 "
                                                  << (projectPanel_.Occlusion().AverageVisibilityRatio() * 100.0) << "%，PVS "
                                                  << (static_cast<double>(projectPanel_.Occlusion().PvsBytes()) / 1024.0) << " KB");
    }
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

void Application::DrawVoxelHud()
{
    if (!voxelMode_ || !hudEnabled_ || config_.noUi || !editorOverlay_.IsInitialized())
        return;

    const VkExtent2D ext = renderer_.Extent();
    const float sw = static_cast<float>(ext.width);
    const float sh = static_cast<float>(ext.height);
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    // ---- 准星（前景层，与展示厅同一绘制层）----
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 c(sw * 0.5f, sh * 0.5f);
        const ImU32 col = IM_COL32(226, 238, 255, 205);
        fg->AddLine(ImVec2(c.x - 9.0f, c.y), ImVec2(c.x - 3.0f, c.y), col, 2.0f);
        fg->AddLine(ImVec2(c.x + 3.0f, c.y), ImVec2(c.x + 9.0f, c.y), col, 2.0f);
        fg->AddLine(ImVec2(c.x, c.y - 9.0f), ImVec2(c.x, c.y - 3.0f), col, 2.0f);
        fg->AddLine(ImVec2(c.x, c.y + 3.0f), ImVec2(c.x, c.y + 9.0f), col, 2.0f);
    }

    // ---- 瞄准方块高亮描边：世界立方体 12 条棱投影到屏幕（MC 式选中反馈）----
    if (voxelAimValid_)
    {
        const glm::mat4 aimVp = ActiveViewProj();
        const glm::vec2 aimVpSize(sw, sh);
        const glm::vec3 bLo(static_cast<float>(voxelAim_.x), static_cast<float>(voxelAim_.y),
                            static_cast<float>(voxelAim_.z));
        const float kInflate = 0.003f; // 轻微外扩，避免棱线被方块表面遮挡
        glm::vec2 sp[8];
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 corner((i & 1) ? bLo.x + 1.0f + kInflate : bLo.x - kInflate,
                                   (i & 2) ? bLo.y + 1.0f + kInflate : bLo.y - kInflate,
                                   (i & 4) ? bLo.z + 1.0f + kInflate : bLo.z - kInflate);
            sp[i] = Editor::ProjectWorldToScreen(corner, aimVp, aimVpSize);
        }
        static const int kEdges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                          {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImU32 outline = IM_COL32(16, 18, 22, 215);
        const ImU32 edge = IM_COL32(255, 255, 255, 200);
        for (const auto& e : kEdges)
        {
            // 端点在相机背后的棱直接跳过（不做裁剪，避免半可见时整框闪烁）
            if (sp[e[0]].x < -1e8f || sp[e[1]].x < -1e8f)
                continue;
            const ImVec2 p1(sp[e[0]].x, sp[e[0]].y);
            const ImVec2 p2(sp[e[1]].x, sp[e[1]].y);
            fg->AddLine(p1, p2, outline, 3.5f);
            fg->AddLine(p1, p2, edge, 1.4f);
        }
    }

    const ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.09f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.78f, 1.0f, 0.30f));

    // ---- 左上：操作说明 + 手持方块 + 世界状态 ----
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##voxel_help", nullptr, kFlags);
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 1.0f, 1.0f), "方块世界 · 体素地形");
        ImGui::Separator();
        ImGui::Text("[WASD] 移动   [空格] 跳跃");
        ImGui::Text("[Shift] 冲刺  [Ctrl] 蹲下");
        ImGui::Text("[左键] 挖掘   [右键] 放置");
        ImGui::Text("[1-6] 选方块  [F] 光标锁定");
        ImGui::Text("[F1] 编辑器面板（当前%s）", voxelPlayMode_ ? "已收起·纯游戏" : "已展开");
        ImGui::Text("视距 %d 区块（[ ] 键调整）", voxelWorld_.Config().viewRadius);
        ImGui::Text("时段 %s（[T] 切换）", VoxelDayName());
        ImGui::Separator();

        const char* blockName = "岩石";
        switch (voxelPlaceBlock_)
        {
        case Sample::Voxel::BlockType::Grass:
            blockName = "草地";
            break;
        case Sample::Voxel::BlockType::Dirt:
            blockName = "泥土";
            break;
        case Sample::Voxel::BlockType::Sand:
            blockName = "沙";
            break;
        case Sample::Voxel::BlockType::Wood:
            blockName = "木头";
            break;
        case Sample::Voxel::BlockType::Leaves:
            blockName = "树叶";
            break;
        default:
            blockName = "岩石";
            break;
        }
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "手持：%s", blockName);
        if (voxelPlaceBlocked_)
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.40f, 1.0f), "放不下：目标格与你重叠");
        if (voxelInWater_)
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "水下：[空格] 上浮");

        const glm::vec3 feet = fpController_.FeetPosition();
        ImGui::Text("坐标 %.0f / %.0f / %.0f", feet.x, feet.y, feet.z);
        ImGui::Text("区块 %zu   网格 %zu", voxelWorld_.ChunkCount(), voxelChunks_.size());
        ImGui::End();
    }

    ImGui::PopStyleColor(2);
}
void Application::DrawShowcaseHud()
{
    if (!showcase_.Active() || !hudEnabled_ || config_.noUi || !editorOverlay_.IsInitialized())
        return;

    const VkExtent2D ext = renderer_.Extent();
    const float sw = static_cast<float>(ext.width);
    const float sh = static_cast<float>(ext.height);
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    const Sample::Showcase::CyberExhibit* focus = showcase_.Focused();

    // ---- 准星（前景层，不进窗口栈） ----
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 c(sw * 0.5f, sh * 0.5f);
        const ImU32 col = focus ? IM_COL32(255, 214, 120, 235) : IM_COL32(210, 232, 255, 165);
        const float gap = focus ? 8.0f : 5.0f;
        const float len = focus ? 13.0f : 9.0f;
        if (focus)
            fg->AddCircle(c, 11.0f, col, 20, 2.0f);
        else
            fg->AddCircle(c, 3.0f, col, 12, 1.6f);
        fg->AddLine(ImVec2(c.x - gap - len, c.y), ImVec2(c.x - gap, c.y), col, 2.0f);
        fg->AddLine(ImVec2(c.x + gap, c.y), ImVec2(c.x + gap + len, c.y), col, 2.0f);
        fg->AddLine(ImVec2(c.x, c.y - gap - len), ImVec2(c.x, c.y - gap), col, 2.0f);
        fg->AddLine(ImVec2(c.x, c.y + gap), ImVec2(c.x, c.y + gap + len), col, 2.0f);
    }

    const ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.09f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.78f, 1.0f, 0.30f));

    // ---- 左上：引擎特性清单（热键 / 名称 / 状态） ----
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##showcase_features", nullptr, kFlags);
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 1.0f, 1.0f), "BigHero 引擎 · 特性展台");
        ImGui::Separator();

        auto row = [](const char* key, const char* name, bool on)
        {
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[%s]", key);
            ImGui::SameLine(38.0f);
            ImGui::Text("%s", name);
            ImGui::SameLine(232.0f);
            ImGui::TextColored(on ? ImVec4(0.40f, 1.0f, 0.62f, 1.0f) : ImVec4(0.72f, 0.76f, 0.84f, 0.85f), "%s",
                               on ? "开" : "关");
        };
        row("1", "泛光 Bloom / 后处理", postProcessSync_.postProcess);
        row("2", "体积雾 · 体积光", postProcessSync_.fogEnabled);
        row("3", "TAA 时间抗锯齿", postProcessSync_.taaEnabled);
        row("4", "景深 DoF", postProcessSync_.dofEnabled);
        row("5", "运动模糊", postProcessSync_.mbEnabled);
        row("6", "自动曝光 · 电影化", postProcessSync_.autoExposure);

        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[7]");
        ImGui::SameLine(38.0f);
        ImGui::Text("粒子喷泉");
        ImGui::SameLine(232.0f);
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.84f, 0.85f), "触发");

        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[8]");
        ImGui::SameLine(38.0f);
        ImGui::Text("昼夜氛围");
        ImGui::SameLine(232.0f);
        ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.62f, 1.0f), "%s", showcase_.Preset().name);

        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[9]");
        ImGui::SameLine(38.0f);
        ImGui::Text("物理方块");
        ImGui::SameLine(232.0f);
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.84f, 0.85f), "投放");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[G]");
        ImGui::SameLine(38.0f);
        ImGui::Text("画面风格");
        ImGui::SameLine(232.0f);
        ImGui::TextColored(ImVec4(0.62f, 0.92f, 1.0f, 1.0f), "%s", showcase_.Look().name);

        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.35f, 1.0f), "[O]");
        ImGui::SameLine(38.0f);
        ImGui::Text("自动昼夜");
        ImGui::SameLine(232.0f);
        ImGui::TextColored(showcase_.AutoCycle() ? ImVec4(0.40f, 1.0f, 0.62f, 1.0f)
                                                 : ImVec4(0.72f, 0.76f, 0.84f, 0.85f),
                           "%s", showcase_.AutoCycle() ? "开" : "关");

        ImGui::Separator();
        const int visited = showcase_.VisitedCount();
        ImGui::TextColored(ImVec4(0.80f, 0.86f, 0.95f, 0.95f), "已体验 %d / 9     按 H %s帮助", visited,
                           helpVisible_ ? "隐藏" : "显示");
        // 已体验进度条（点亮全部九座展台即满）
        {
            const float ratio = std::clamp(static_cast<float>(visited) / 9.0f, 0.0f, 1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, 6.0f);
            dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(70, 78, 92, 190));
            dl->AddRectFilled(p0, ImVec2(p0.x + size.x * ratio, p0.y + size.y), IM_COL32(96, 214, 255, 235));
            ImGui::Dummy(size);
        }
        ImGui::End();
    }

    // ---- 右上：运行状态 ----
    {
        const glm::vec3 pos = ActivePosition();
        ImGui::SetNextWindowPos(ImVec2(sw - 250.0f, 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(230.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##showcase_stats", nullptr, kFlags);
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 1.0f, 1.0f), "%u FPS   %u x MSAA", lastFps_,
                           static_cast<uint32_t>(renderer_.SampleCount()));
        ImGui::Text("时段  %s%s", showcase_.Preset().name, showcase_.AutoCycle() ? "  (自动)" : "");
        ImGui::Text("风格  %s", showcase_.Look().name);
        ImGui::Text("位置  %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
        ImGui::Text("模式  %s", fpFlyMode_ ? "飞行俯瞰 (V)" : "第一人称陆行 (V)");
        ImGui::Text("实体  %u", static_cast<uint32_t>(ecsScene_.ObjectCount()));
        ImGui::End();
    }

    // ---- 底部居中：注视展台提示 ----
    if (focus != nullptr)
    {
        std::string title = "【" + std::string(focus->name) + "】";
        std::string line = std::string(focus->hint) + "   ——  按 E 或 " + focus->hotkey + " 切换";
        ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh - 150.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("##showcase_focus", nullptr, kFlags | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(focus->accent.r, focus->accent.g, focus->accent.b, 1.0f), "%s", title.c_str());
        ImGui::Text("%s", line.c_str());
        ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.90f, 0.85f), "距离 %.1f m", showcase_.FocusDistance());
        ImGui::End();
    }

    // ---- 左下：按键帮助 ----
    if (helpVisible_)
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, sh - 190.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##showcase_help", nullptr, kFlags);
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 1.0f, 1.0f), "操作");
        ImGui::Separator();
        ImGui::Text("WASD 移动    Shift 冲刺    Ctrl 蹲下    空格 跳跃");
        ImGui::Text("鼠标 转视角    V 飞行俯瞰    R 回出生点");
        ImGui::Text("E 与展台交互    1-9 直达特性    T 切换昼夜");
        ImGui::Text("G 画面风格    O 自动昼夜循环    P 粒子爆发");
        ImGui::Text("Tab 轨道/第一人称    Esc 释放光标");
        ImGui::End();
    }

    // ---- 欢迎卡（进入后若干秒淡出） ----
    if (welcomeTimer_ > 0.0f)
    {
        const float alpha = std::clamp(welcomeTimer_ / 2.0f, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.28f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("##showcase_welcome", nullptr, kFlags | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 1.0f, 1.0f), "BigHero 引擎 · 赛博城市展示厅");
        ImGui::Text("第一人称自由漫游：走进八座展台，逐个点亮引擎特性。");
        ImGui::Text("看向展台按 E，或直接按 1-9 切换对应特性。");
        ImGui::Text("按 T 切换昼夜氛围，按 G 切换画面风格，按 O 开启自动昼夜。");
        ImGui::Text("按 V 切换飞行俯瞰，俯瞰全城别有一番景致。");
        ImGui::End();
        ImGui::PopStyleVar();
    }

    ImGui::PopStyleColor(2);
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

    // 人物部件：球 + 胶囊（meshId=3/4），按 LOD 选档绘制阴影
    const glm::vec3 shadowCamPos = ActivePosition();
    const float shadowFovDeg = (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.fovDegrees_ : camera_.fovDegrees_;
    const float shadowTanHalfFov = std::tan(glm::radians(shadowFovDeg * 0.5f));
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            const bool isSphere = (r.meshId == 3);
            const bool isCapsule = (r.meshId == 4);
            if (!isSphere && !isCapsule)
                return;
            const float boundsRadius = isSphere ? Scene::kSphereBoundingRadius : Scene::kCapsuleBoundingRadius;
            const glm::vec3 center = glm::vec3(world[3]);
            const float radius = t.scale * boundsRadius;
            const float dist = glm::distance(shadowCamPos, center);
            const float screenH = Render::LodGroup::ScreenRelativeHeight(radius, dist, shadowTanHalfFov);
            const int lodLevel = lodGroup_.SelectLevel(screenH);
            if (lodLevel == Render::LodGroup::kCulled)
                return;
            if (lodLevel == 0)
            {
                if (isSphere)
                    drawOne(world, sphereMesh_, sphereMesh_.IndexCount(), 0);
                else
                    drawOne(world, capsuleMesh_, capsuleMesh_.IndexCount(), 0);
            }
            else
            {
                if (isSphere)
                    drawOne(world, sphereLodMesh_, sphereLodMesh_.IndexCount(), 0);
                else
                    drawOne(world, capsuleLodMesh_, capsuleLodMesh_.IndexCount(), 0);
            }
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
    // 方块世界：逐区块合并网格投影（顶点已是世界坐标，模型矩阵取单位阵）。
    // 只投影玩家附近 kShadowChunkRadius 个区块 —— 远处区块的阴影在屏幕上贡献极小，
    // 却要被 4 个 CSM 级联各画一遍（省下的顶点量随视距平方增长）。
    if (voxelMode_)
    {
        constexpr float kShadowChunkRadius = 2.5f;
        const glm::vec3 shadowCenter = fpController_.FeetPosition();
        const float limitBlocks = kShadowChunkRadius * static_cast<float>(voxelWorld_.Config().chunkX);
        for (VoxelChunkGpu& chunk : voxelChunks_)
        {
            if (!chunk.uploaded || !chunk.mesh.IsValid())
                continue;
            const float dx = chunk.center.x - shadowCenter.x;
            const float dz = chunk.center.z - shadowCenter.z;
            if (dx * dx + dz * dz > limitBlocks * limitBlocks)
                continue;
            drawOne(glm::mat4(1.0f), chunk.mesh, chunk.mesh.IndexCount(), 0);
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

    // 人物部件：球 + 胶囊（meshId=3/4），按 LOD 选档绘制阴影
    const glm::vec3 shadowCamPos = ActivePosition();
    const float shadowFovDeg = (cameraMode_ == CameraMode::FirstPerson) ? fpCamera_.fovDegrees_ : camera_.fovDegrees_;
    const float shadowTanHalfFov = std::tan(glm::radians(shadowFovDeg * 0.5f));
    ecsScene_.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& world)
        {
            const bool isSphere = (r.meshId == 3);
            const bool isCapsule = (r.meshId == 4);
            if (!isSphere && !isCapsule)
                return;
            const float boundsRadius = isSphere ? Scene::kSphereBoundingRadius : Scene::kCapsuleBoundingRadius;
            const glm::vec3 center = glm::vec3(world[3]);
            const float radius = t.scale * boundsRadius;
            const float dist = glm::distance(shadowCamPos, center);
            const float screenH = Render::LodGroup::ScreenRelativeHeight(radius, dist, shadowTanHalfFov);
            const int lodLevel = lodGroup_.SelectLevel(screenH);
            if (lodLevel == Render::LodGroup::kCulled)
                return;
            if (lodLevel == 0)
            {
                if (isSphere)
                    drawOne(world, sphereMesh_, sphereMesh_.IndexCount(), 0);
                else
                    drawOne(world, capsuleMesh_, capsuleMesh_.IndexCount(), 0);
            }
            else
            {
                if (isSphere)
                    drawOne(world, sphereLodMesh_, sphereLodMesh_.IndexCount(), 0);
                else
                    drawOne(world, capsuleLodMesh_, capsuleLodMesh_.IndexCount(), 0);
            }
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
