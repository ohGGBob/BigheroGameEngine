#pragma once
#include "core/AssetMetadata.h"
#include "core/AssetRegistry.h"
#include "core/MeshResource.h"
#include "editor/BuildSettingsPanel.h"
#include "editor/Gizmo.h"
#include "editor/HierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "game/EmitterPresets.h"
#include "imgui.h"
#include "scene/AnimationStateMachine.h"
#include "scene/PersonParams.h"
#include "scene/Scene.h"
#include "script/ScriptFields.h"
#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
// 轻量停靠布局：内置 ImGui 为 master 分支（无 DockSpace API），
// 故以"边缘吸附 + 响应式重排"模拟停靠观感。预设决定排布：
//   Classic —— 面板分散停靠于四角/边缘，宽屏多面板并行；
//   Compact —— 面板在左侧单列堆叠，窄屏/笔记本友好。
enum class DockPreset : int
{
    Classic = 0,
    Compact = 1
};

struct DockLayout
{
    // 给定预设、面板槽位名与视口尺寸，返回停靠后的位置/尺寸（像素，左上原点）。
    static inline void Place(DockPreset preset, const char* slot, const glm::vec2& vp, ImVec2& pos, ImVec2& size)
    {
        const float m = 12.0f;
        const bool narrow = vp.x < 1180.0f;
        if (preset == DockPreset::Compact || narrow)
        {
            // 左侧单列堆叠：统计 / 场景 / 光照 / 相机 / 点光源 / 资源
            const float w = std::min(360.0f, std::max(260.0f, vp.x - 24.0f));
            static const struct
            {
                const char* name;
                float y;
            } kStack[] = {{"stats", m},
                          {"scene", m + 300.0f},
                          {"light", m + 648.0f},
                          {"camera", m + 648.0f + 300.0f},
                          {"pointLights", m + 648.0f + 300.0f + 250.0f},
                          {"person", m + 648.0f + 300.0f + 250.0f + 250.0f},
                          {"assets", m + 648.0f + 300.0f + 250.0f + 250.0f + 250.0f},
                          {"hierarchy", m + 648.0f + 300.0f + 250.0f + 250.0f + 250.0f + 250.0f},
                          {"build", m + 648.0f + 300.0f + 250.0f + 250.0f + 250.0f + 250.0f + 250.0f}};
            pos = ImVec2(m, m);
            size = ImVec2(w, 0.0f);
            for (const auto& l : kStack)
                if (std::strcmp(slot, l.name) == 0)
                {
                    pos.y = l.y;
                    break;
                }
            return;
        }
        // Classic：四角/边缘分散
        if (std::strcmp(slot, "stats") == 0)
        {
            pos = ImVec2(m, m);
            size = ImVec2(320.0f, 0.0f);
        }
        else if (std::strcmp(slot, "light") == 0)
        {
            pos = ImVec2(vp.x * 0.5f - 150.0f, m);
            size = ImVec2(300.0f, 0.0f);
        }
        else if (std::strcmp(slot, "camera") == 0)
        {
            pos = ImVec2(vp.x - 300.0f - m, m);
            size = ImVec2(280.0f, 0.0f);
        }
        else if (std::strcmp(slot, "pointLights") == 0)
        {
            pos = ImVec2(vp.x - 300.0f - m, 300.0f);
            size = ImVec2(300.0f, 300.0f);
        }
        else if (std::strcmp(slot, "assets") == 0)
        {
            pos = ImVec2(vp.x - 300.0f - m, 620.0f);
            size = ImVec2(300.0f, 320.0f);
        }
        else if (std::strcmp(slot, "scene") == 0)
        {
            // 场景面板：左下角、底部锚定。U1-S1d 后内容含"脚本 (TypeName)"分组
            //（原生 10 行可见 + 分隔线/标题 + 脚本字段行），高度 340 → 620 使脚本分组
            // 默认落入可视区（仅向上生长，不越出屏幕底部；小屏回退旧尺寸）。
            if (vp.y >= 700.0f)
            {
                pos = ImVec2(m, vp.y - 632.0f);
                size = ImVec2(380.0f, 620.0f);
            }
            else
            {
                pos = ImVec2(m, vp.y - 352.0f);
                size = ImVec2(380.0f, 340.0f);
            }
        }
        else if (std::strcmp(slot, "hierarchy") == 0)
        {
            // 层级树：资源面板下方（右侧列末端），首帧后可自由拖动
            pos = ImVec2(vp.x - 320.0f - m, 960.0f);
            size = ImVec2(320.0f, 420.0f);
        }
        else if (std::strcmp(slot, "build") == 0)
        {
            // 构建设置（U1-B1）：层级树面板下方（右侧列末端），首帧后可自由拖动
            pos = ImVec2(vp.x - 320.0f - m, 1400.0f);
            size = ImVec2(320.0f, 420.0f);
        }
        else
        {
            pos = ImVec2(m, m);
            size = ImVec2(300.0f, 0.0f);
        }
    }
};

// 光照参数（编辑器可调，每帧写入LightUBO）
struct LightParams
{
    glm::vec3 direction{0.5f, -1.0f, -0.35f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity = 3.0f;
    float ambient = 0.15f;
    float shadowStrength = 1.0f; // 阴影浓度（0关闭）
    float shadowBias = 0.0022f;  // 深度比较偏移
    float iblStrength = 1.0f;    // IBL环境光强度（0=常数环境光）
    float exposure = 1.0f;       // 色调映射曝光（HDR->LDR 整体缩放）
};

// 点光源参数
struct PointLightParams
{
    glm::vec3 position{0.0f, 2.5f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 30.0f;
    float radius = 9.0f;
    bool castsShadow = false; // 是否投射立方体阴影
};

// 默认点光源：四色彩灯环绕场景，填充方向光照不到的暗面
inline std::vector<PointLightParams> BuildDefaultPointLights()
{
    return {{{4.0f, 2.8f, 4.0f}, {1.0f, 0.60f, 0.35f}, 40.0f, 14.0f, false},
            {{-4.0f, 2.8f, 4.0f}, {0.35f, 0.60f, 1.0f}, 40.0f, 14.0f, false},
            {{4.0f, 2.8f, -4.0f}, {0.45f, 1.0f, 0.55f}, 35.0f, 14.0f, false},
            {{-4.0f, 2.8f, -4.0f}, {0.85f, 0.40f, 1.0f}, 35.0f, 14.0f, false}};
}

// 渲染统计信息（由主循环每帧填充）
struct EditorStats
{
    uint32_t fps = 0;
    float frameMs = 0.0f;
    const char* gpuName = "";
    VkExtent2D extent{0, 0};
    uint32_t msaaSamples = 1;
    uint32_t triangleCount = 0;
    uint32_t culledCount = 0; // 本帧被视锥剔除、未绘制的物体数
    uint32_t batchCount = 0;  // 本帧主场景的实例化绘制批次（立方体/圆环/地面）
    // GPU 各阶段耗时（毫秒），设备不支持时间戳查询时为 0
    float gpuFrameMs = 0.0f;
    float gpuShadowMs = 0.0f;
    float gpuSceneMs = 0.0f;
    float gpuUiMs = 0.0f;
    // CPU 帧剖析数据（由 FrameProfiler 填充）
    struct CpuScope
    {
        const char* name;
        float ms;
    };
    const CpuScope* cpuScopes = nullptr;
    uint32_t cpuScopeCount = 0;
    float cpuTotalMs = 0.0f;
    const float* fpsHistory = nullptr;
    uint32_t fpsHistoryCount = 0;
};

// 编辑器面板：渲染统计 / 光照 / 相机 / 场景物体属性，直接编辑运行时数据
class EditorPanel
{
  public:
    static constexpr uint32_t kMaxPointLights = 8;

    DockPreset dockPreset_ = DockPreset::Classic; // 停靠布局预设（经典/紧凑）
    glm::vec2 viewport_{0.0f};                    // 当前视口尺寸（像素），供 DockLayout 使用
    bool hierarchyOpen_ = true;                                // 层级树（Hierarchy）窗口开关（渲染统计面板可切换）
    bool buildSettingsOpen_ = false;                           // 构建设置（Build Settings）窗口开关（U1-B1）
    BigHero::Editor::HierarchyPanel hierarchy;                 // 层级树面板：树形浏览/点击选中/拖拽改父（U1-E1）
    BigHero::Editor::InspectorPanel inspector;                 // Inspector 属性面板：元数据驱动物体属性编辑（U1-E2）
    BigHero::Editor::BuildSettingsPanel buildSettings;         // 构建设置面板：配置编辑 + 一键构建（U1-B1）
    bool saveRequested = false;                   // 保存场景按钮被点击（Application 消费后重置）
    bool loadRequested = false;                   // 加载场景按钮被点击（Application 消费后重置）

    // ---- U1-E3 Play Mode（编辑态/运行态分离） ----
    // 请求标志由面板按钮置位、Application::UpdatePlayModeRequests 消费后重置；
    // playModeState 由 Application 每帧回写（0=编辑态 1=运行中 2=已暂停，PlayModeState 枚举值）。
    bool playRequested = false;  // 播放/停止按钮被点击（面板按当前状态发 play 或 stop）
    bool pauseRequested = false; // 暂停/继续按钮被点击
    bool stopRequested = false;  // 停止按钮被点击（停止并还原编辑态底稿）
    int playModeState = 0;       // 当前 Play Mode 状态（Application 回写，仅供显示）

    bool addObjectRequested = false;              // 添加物体按钮被点击（Application 消费后重置）
    bool deleteObjectRequested = false;
    bool undoRequested = false;
    bool redoRequested = false;           // 删除选中物体按钮被点击（Application 消费后重置）
    bool physicsRebuildRequested = false; // 物理属性变更，需重建刚体（Application 消费后重置）
    bool jointCreateRequested = false;    // 创建关节请求（Application 消费后重置）
    bool jointDeleteRequested = false;    // 删除关节请求（Application 消费后重置）
    int jointTargetObject = -1;           // 关节的第二个物体索引
    int jointType = 0;                    // 关节类型（JointType 枚举值）
    int jointDeleteIndex = -1;            // 要删除的关节索引

    // ---- 人物生成面板（Todo 3：Q版人物 = 球/胶囊 ECS 骨骼树） ----
    bool addPersonRequested = false;      // 生成人物请求（Application 消费后重置）
    bool removePersonRequested = false;   // 删除选中人物请求（Application 消费后重置）
    int selectedPerson = -1;              // 面板中选中的 PersonHost 人物下标
    Scene::PersonParams personParams;   // 生成/编辑人物的参数（面板同步）
    bool personSpawnAtCursor = true;      // true=鼠标点击位置生成；false=用 personSpawnPos
    glm::vec3 personSpawnPos{0.0f, 0.0f, 0.0f}; // 指定坐标生成（仅 personSpawnAtCursor=false）

    void Draw(const EditorStats& stats, std::vector<Scene::SceneObject>& scene, LightParams& light, float& cameraFov,
              std::vector<PointLightParams>& pointLights, int selectedObject = -1, bool* deferredMode = nullptr,
              BigHero::Editor::GizmoMode* gizmoMode = nullptr, glm::vec2 viewport = glm::vec2(0.0f),
              float* masterVolume = nullptr, bool* postProcessMode = nullptr, bool* ssaoMode = nullptr,
              bool* ssrMode = nullptr, bool* physicsEnabled = nullptr, bool* physicsDebug = nullptr,
              float* gravity = nullptr, bool* characterEnabled = nullptr, float* characterSpeed = nullptr,
              float* characterJump = nullptr, std::vector<Physics::SceneJoint>* joints = nullptr,
              Scene::AnimationStateMachine* animSM = nullptr, bool* navEnabledMode = nullptr,
              bool* particleEnabledMode = nullptr, bool* navAgentEnabledMode = nullptr,
              Game::Emitter* liveEmitter = nullptr, float* particleGravity = nullptr, float* particleDamping = nullptr,
              int* emitterPresetIndex = nullptr, float* gradeSaturation = nullptr, float* gradeContrast = nullptr,
              float* gradeLift = nullptr, float* gradeGain = nullptr, float* gradeGamma = nullptr,
              bool* dofEnabled = nullptr, float* dofFocusDistance = nullptr, float* dofAperture = nullptr,
              float* dofMaxBlur = nullptr, bool* mbEnabled = nullptr, float* mbStrength = nullptr,
              float* mbMaxBlur = nullptr, float* mbMaxSamples = nullptr, bool* fogEnabledMode = nullptr,
              float* fogDensity = nullptr, float* fogHeightFalloff = nullptr, float* fogBaseHeight = nullptr,
              float* fogScatter = nullptr, glm::vec3* fogTint = nullptr, bool* fogShadowMode = nullptr,
              int* fogSteps = nullptr, bool* autoExposureMode = nullptr,
              float* exposureKeyValue = nullptr, float* adaptationSpeed = nullptr, float* vignetteIntensity = nullptr,
              float* vignetteRadius = nullptr, float* filmGrain = nullptr,
              bool* taaEnabledMode = nullptr, float* taaFeedback = nullptr,
              const bighero::AssetRegistry* assets = nullptr,
              const std::unordered_map<std::string, bighero::MeshResource>* meshResources = nullptr)
    {
        viewport_ = viewport;
        DrawStatsWindow(stats, scene, deferredMode, masterVolume, postProcessMode, ssaoMode, ssrMode, physicsEnabled,
                        physicsDebug, gravity, characterEnabled, characterSpeed, characterJump, animSM, navEnabledMode,
                        particleEnabledMode, navAgentEnabledMode, liveEmitter, particleGravity, particleDamping,
                        emitterPresetIndex, gradeSaturation, gradeContrast, gradeLift, gradeGain, gradeGamma,
                        dofEnabled, dofFocusDistance, dofAperture, dofMaxBlur, mbEnabled, mbStrength, mbMaxBlur,
                        mbMaxSamples, fogEnabledMode, fogDensity, fogHeightFalloff, fogBaseHeight, fogScatter,
                        fogTint, fogShadowMode, fogSteps, autoExposureMode, exposureKeyValue, adaptationSpeed,
                        vignetteIntensity, vignetteRadius, filmGrain, taaEnabledMode, taaFeedback);
        DrawLightWindow(light);
        DrawPointLightsWindow(pointLights);
        DrawCameraWindow(cameraFov);
        DrawSceneWindow(scene, selectedObject, gizmoMode, joints);
        DrawPersonWindow();
        DrawAssetsWindow(assets, meshResources);
        // 层级树（Hierarchy）面板（U1-E1）：树形浏览/点击选中/拖拽改父/搜索过滤
        if (hierarchyOpen_)
        {
            ImVec2 hPos, hSize;
            DockLayout::Place(dockPreset_, "hierarchy", viewport_, hPos, hSize);
            hierarchy.Draw(scene, selectedObject, hPos, hSize);
        }
        // 构建设置面板（U1-B1）：配置编辑 + 一键构建（自包含可分发目录）
        if (buildSettingsOpen_)
        {
            ImVec2 bPos, bSize;
            DockLayout::Place(dockPreset_, "build", viewport_, bPos, bSize);
            buildSettings.Draw(bPos, bSize);
        }
        // 层级树"删除选中"复用现有删除路径（含撤销与"子节点提升为根"策略）
        if (hierarchy.deleteRequested)
        {
            deleteObjectRequested = true;
            hierarchy.deleteRequested = false;
        }
    }

  private:
    void DrawStatsWindow(const EditorStats& stats, const std::vector<Scene::SceneObject>& scene,
                         bool* deferredMode = nullptr, float* masterVolume = nullptr, bool* postProcessMode = nullptr,
                         bool* ssaoMode = nullptr, bool* ssrMode = nullptr, bool* physicsEnabled = nullptr,
                         bool* physicsDebug = nullptr, float* gravity = nullptr, bool* characterEnabled = nullptr,
                         float* characterSpeed = nullptr, float* characterJump = nullptr,
                         Scene::AnimationStateMachine* animSM = nullptr, bool* navEnabledMode = nullptr,
                         bool* particleEnabledMode = nullptr, bool* navAgentEnabledMode = nullptr,
                         Game::Emitter* liveEmitter = nullptr, float* particleGravity = nullptr,
                         float* particleDamping = nullptr, int* emitterPresetIndex = nullptr,
                         float* gradeSaturation = nullptr, float* gradeContrast = nullptr, float* gradeLift = nullptr,
                         float* gradeGain = nullptr, float* gradeGamma = nullptr, bool* dofEnabled = nullptr,
                         float* dofFocusDistance = nullptr, float* dofAperture = nullptr, float* dofMaxBlur = nullptr,
                         bool* mbEnabled = nullptr, float* mbStrength = nullptr, float* mbMaxBlur = nullptr,
                         float* mbMaxSamples = nullptr, bool* fogEnabledMode = nullptr, float* fogDensity = nullptr,
                         float* fogHeightFalloff = nullptr, float* fogBaseHeight = nullptr,
                         float* fogScatter = nullptr, glm::vec3* fogTint = nullptr, bool* fogShadowMode = nullptr,
                         int* fogSteps = nullptr, bool* autoExposureMode = nullptr, float* exposureKeyValue = nullptr,
                         float* adaptationSpeed = nullptr, float* vignetteIntensity = nullptr,
                         float* vignetteRadius = nullptr, float* filmGrain = nullptr,
                         bool* taaEnabledMode = nullptr, float* taaFeedback = nullptr)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "stats", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("渲染统计");

        // ---- U1-E3 Play Mode：播放/暂停/停止（对标 Unity 工具条，停靠统计面板顶部） ----
        // 注：编辑器字体经 GetGlyphRangesChineseSimplifiedCommon 加载（不含 ▶/⏸/⏹ 符号区段，
        // 直接用图标会缺字渲染为"?"），故以文字按钮保持风格一致。
        {
            if (ImGui::Button(playModeState == 0 ? "播放 (Ctrl+P)" : "停止 (Ctrl+P)", ImVec2(120, 0)))
            {
                if (playModeState == 0)
                    playRequested = true; // 编辑态 -> 进入 Play
                else
                    stopRequested = true; // 运行/暂停 -> 停止并还原编辑态底稿
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(playModeState == 0); // 编辑态无运行可暂停
            if (ImGui::Button(playModeState == 2 ? "继续" : "暂停", ImVec2(72, 0)))
                pauseRequested = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            const char* stateText = (playModeState == 1) ? "运行中" : (playModeState == 2) ? "已暂停" : "编辑态";
            const ImVec4 stateCol = (playModeState == 1)   ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                    : (playModeState == 2) ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                                                           : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::TextColored(stateCol, "%s", stateText);
        }
        ImGui::Separator();

        ImGui::Text("FPS: %u", stats.fps);
        ImGui::Text("帧耗时: %.2f ms", stats.frameMs);
        ImGui::Separator();
        ImGui::Text("GPU 整帧: %.2f ms", stats.gpuFrameMs);
        ImGui::Text("  阴影预通道: %.3f ms", stats.gpuShadowMs);
        ImGui::Text("  场景通道:   %.3f ms", stats.gpuSceneMs);
        ImGui::Text("  UI 通道:    %.3f ms", stats.gpuUiMs);
        ImGui::Separator();
        ImGui::Text("CPU 整帧: %.2f ms", stats.cpuTotalMs);
        if (stats.cpuScopes && stats.cpuScopeCount > 0)
        {
            for (uint32_t i = 0; i < stats.cpuScopeCount; ++i)
            {
                const auto& s = stats.cpuScopes[i];
                const float pct = stats.cpuTotalMs > 0.0f ? s.ms / stats.cpuTotalMs * 100.0f : 0.0f;
                ImGui::Text("  %-12s %6.3f ms  (%5.1f%%)", s.name, s.ms, pct);
            }
        }
        if (stats.fpsHistory && stats.fpsHistoryCount > 1)
        {
            ImGui::Separator();
            ImGui::Text("帧率历史（最近 %u 帧）:", stats.fpsHistoryCount);
            ImGui::PlotLines("##fpsHist", stats.fpsHistory, static_cast<int>(stats.fpsHistoryCount), 0, nullptr, 0.0f,
                             100.0f, ImVec2(0, 50));
        }
        ImGui::Separator();
        ImGui::Text("GPU: %s", stats.gpuName);
        ImGui::Text("分辨率: %u x %u", stats.extent.width, stats.extent.height);
        ImGui::Text("MSAA: %ux", stats.msaaSamples);
        ImGui::Text("三角形: %u", stats.triangleCount);
        ImGui::Text("物体数: %u（剔除 %u）", static_cast<uint32_t>(scene.size()), stats.culledCount);
        ImGui::Text("绘制批次: %u（实例化）", stats.batchCount);
        ImGui::Separator();
        if (deferredMode)
        {
            ImGui::Checkbox("延迟渲染 (Deferred)", deferredMode);
            ImGui::Text("当前: %s", *deferredMode ? "延迟 (GBuffer+MRT)" : "前向 (Forward)");
        }
        if (postProcessMode)
        {
            ImGui::Checkbox("后处理 Bloom", postProcessMode);
            if (*postProcessMode)
                ImGui::TextDisabled("Bloom + ACES 色调映射（仅前向模式）");
            // 升级 21：色调分级（Color Grading）滑杆——作用于合成阶段，需开启后处理才可见效果
            if (ImGui::TreeNode("色调分级 (Color Grading)"))
            {
                if (gradeSaturation)
                    ImGui::SliderFloat("饱和度 Saturation", gradeSaturation, 0.0f, 2.0f, "%.2f");
                if (gradeContrast)
                    ImGui::SliderFloat("对比度 Contrast", gradeContrast, 0.5f, 2.0f, "%.2f");
                if (gradeLift)
                    ImGui::SliderFloat("暗部提升 Lift", gradeLift, -0.5f, 0.5f, "%.2f");
                if (gradeGain)
                    ImGui::SliderFloat("增益 Gain", gradeGain, 0.0f, 3.0f, "%.2f");
                if (gradeGamma)
                    ImGui::SliderFloat("伽马 Gamma", gradeGamma, 0.3f, 2.5f, "%.2f");
                ImGui::TreePop();
            }
            // 升级 22：景深（DoF）滑杆——作用于独立景深 Pass，需开启后处理才可见效果
            if (ImGui::TreeNode("景深 (Depth of Field)"))
            {
                if (dofEnabled)
                    ImGui::Checkbox("启用景深", dofEnabled);
                if (dofFocusDistance)
                    ImGui::SliderFloat("对焦距离", dofFocusDistance, 0.5f, 50.0f, "%.1f m");
                if (dofAperture)
                    ImGui::SliderFloat("光圈强度", dofAperture, 0.0f, 0.2f, "%.3f");
                if (dofMaxBlur)
                    ImGui::SliderFloat("最大模糊", dofMaxBlur, 0.001f, 0.06f, "%.3f");
                ImGui::TreePop();
            }
            // 升级 23：相机运动模糊（Motion Blur）滑杆——作用于独立运动模糊 Pass，需开启后处理才可见效果
            if (ImGui::TreeNode("运动模糊 (Motion Blur)"))
            {
                if (mbEnabled)
                    ImGui::Checkbox("启用运动模糊", mbEnabled);
                if (mbStrength)
                    ImGui::SliderFloat("拖尾强度", mbStrength, 0.0f, 1.0f, "%.2f");
                if (mbMaxBlur)
                    ImGui::SliderFloat("最大模糊", mbMaxBlur, 0.001f, 0.1f, "%.3f");
                if (mbMaxSamples)
                    ImGui::SliderFloat("采样数", mbMaxSamples, 4.0f, 32.0f, "%.0f");
                ImGui::TreePop();
            }
            // 升级 25：体积雾（Volumetric Fog）——合成 Pass 光线步进高度雾，需开启后处理才可见效果
            if (ImGui::TreeNode("体积雾 (Volumetric Fog)"))
            {
                if (fogEnabledMode)
                    ImGui::Checkbox("启用体积雾", fogEnabledMode);
                if (fogDensity)
                    ImGui::SliderFloat("雾密度", fogDensity, 0.0f, 0.30f, "%.3f");
                if (fogHeightFalloff)
                    ImGui::SliderFloat("高度衰减", fogHeightFalloff, 0.01f, 1.00f, "%.2f");
                if (fogBaseHeight)
                    ImGui::SliderFloat("基准高度", fogBaseHeight, -10.0f, 20.0f, "%.1f m");
                if (fogScatter)
                    ImGui::SliderFloat("阳光散射", fogScatter, 0.0f, 1.0f, "%.2f");
                if (fogTint)
                    ImGui::ColorEdit3("雾染色", &fogTint->x);
                // 升级 27：雾中投影（God Rays）——步进点采样 CSM，遮挡体在雾中投出光柱
                if (fogShadowMode)
                    ImGui::Checkbox("雾中投影 (God Rays)", fogShadowMode);
                if (fogSteps)
                {
                    static const char* kStepItems[] = {"低 (16 步)", "中 (32 步)", "高 (64 步)"};
                    int idx = (*fogSteps <= 16) ? 0 : ((*fogSteps <= 32) ? 1 : 2);
                    if (ImGui::Combo("步进质量", &idx, kStepItems, IM_ARRAYSIZE(kStepItems)))
                        *fogSteps = (idx == 0) ? 16 : ((idx == 1) ? 32 : 64);
                }
                ImGui::TreePop();
            }
            // 升级 26：自动曝光 + 电影化（暗角/胶片颗粒）——亮度适应作用于合成 Pass
            if (ImGui::TreeNode("自动曝光 / 电影化"))
            {
                if (autoExposureMode)
                    ImGui::Checkbox("启用自动曝光", autoExposureMode);
                if (exposureKeyValue)
                    ImGui::SliderFloat("中灰键值", exposureKeyValue, 0.05f, 0.50f, "%.2f");
                if (adaptationSpeed)
                    ImGui::SliderFloat("适应速度", adaptationSpeed, 0.1f, 5.0f, "%.1f");
                if (vignetteIntensity)
                    ImGui::SliderFloat("暗角强度", vignetteIntensity, 0.0f, 1.0f, "%.2f");
                if (vignetteRadius)
                    ImGui::SliderFloat("暗角半径", vignetteRadius, 0.2f, 0.9f, "%.2f");
                if (filmGrain)
                    ImGui::SliderFloat("胶片颗粒", filmGrain, 0.0f, 0.20f, "%.3f");
                ImGui::TreePop();
            }
            // 升级 28：TAA 时间抗锯齿——历史帧重投影混合，平滑几何锯齿/SSAO 噪点/雾抖动颗粒
            // （仅前向 MSAA 路径；需相机抖动投影配合，关闭时画面不变）
            if (ImGui::TreeNode("TAA 时间抗锯齿"))
            {
                if (taaEnabledMode)
                    ImGui::Checkbox("启用 TAA", taaEnabledMode);
                if (taaFeedback)
                    ImGui::SliderFloat("历史权重", taaFeedback, 0.0f, 0.95f, "%.2f");
                if (taaEnabledMode && *taaEnabledMode)
                    ImGui::TextDisabled("拖影时降低历史权重");
                ImGui::TreePop();
            }
        }
        if (ssaoMode && deferredMode && *deferredMode)
        {
            ImGui::Checkbox("环境光遮蔽 SSAO", ssaoMode);
            if (*ssaoMode)
                ImGui::TextDisabled("半分辨率 + 高斯模糊（仅延迟模式）");
        }
        if (ssrMode && deferredMode && *deferredMode)
        {
            ImGui::Checkbox("屏幕空间反射 SSR", ssrMode);
            if (*ssrMode)
                ImGui::TextDisabled("半分辨率 ray march + 高斯模糊（仅延迟模式）");
        }
        ImGui::Separator();
        if (physicsEnabled)
        {
            ImGui::Checkbox("物理模拟", physicsEnabled);
            if (physicsDebug)
                ImGui::Checkbox("物理调试线框", physicsDebug);
            if (gravity)
                ImGui::SliderFloat("重力 Y", gravity, -30.0f, 0.0f, "%.1f");
            if (characterEnabled)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("角色控制器");
                ImGui::Checkbox("启用角色", characterEnabled);
                if (*characterEnabled)
                {
                    if (characterSpeed)
                        ImGui::SliderFloat("移动速度", characterSpeed, 1.0f, 20.0f, "%.1f m/s");
                    if (characterJump)
                        ImGui::SliderFloat("跳跃力度", characterJump, 2.0f, 15.0f, "%.1f m/s");
                    ImGui::TextDisabled("WASD 移动 / 空格跳跃");
                }
            }
        }
        ImGui::Separator();

        // ---- 升级 24：动画控制面板（播放/暂停/时间轴/速度/循环，与 AnimationStateMachine 双向绑定） ----
        DrawAnimationWindow(animSM);

        if (masterVolume)
        {
            ImGui::SliderFloat("主音量", masterVolume, 0.0f, 1.0f, "%.2f");
            ImGui::Separator();
        }

        // ---- 玩法系统：导航 / 粒子 / 撤销重做 ----
        ImGui::Separator();
        if (navEnabledMode)
        {
            ImGui::Checkbox("导航网格 (A*)", navEnabledMode);
            if (*navEnabledMode)
                ImGui::TextDisabled("绿=起点 红=终点 蓝=网格 红叉=障碍");
        }
        if (particleEnabledMode)
        {
            ImGui::Checkbox("粒子系统", particleEnabledMode);
            ImGui::TextDisabled("按 P 触发粒子爆发");
        }
        if (navAgentEnabledMode)
        {
            ImGui::Checkbox("AI 导航代理 (NavAgent)", navAgentEnabledMode);
            if (*navAgentEnabledMode)
                ImGui::TextDisabled("金黄=代理位置 黄线=当前朝向/路径");
        }

        // ---- 升级 19：粒子编辑器（发射器实时调参 + 预设） ----
        if (liveEmitter)
        {
            ImGui::Separator();
            if (ImGui::TreeNode("粒子编辑器"))
            {
                if (emitterPresetIndex)
                {
                    int idx = *emitterPresetIndex;
                    if (ImGui::Combo("预设", &idx, Game::kEmitterPresetNames, Game::EmitterPresetCount()))
                        *emitterPresetIndex = idx;
                }
                ImGui::SliderFloat("发射速率", &liveEmitter->rate, 0.0f, 200.0f, "%.0f/s");
                ImGui::SliderFloat("初速度 Y", &liveEmitter->initialVelocity.y, 0.0f, 10.0f);
                ImGui::SliderFloat("附加速率", &liveEmitter->speed, 0.0f, 10.0f);
                ImGui::SliderFloat("出生半径", &liveEmitter->spawnRadius, 0.0f, 2.0f);
                ImGui::SliderFloat("寿命 min", &liveEmitter->lifetimeMin, 0.1f, 5.0f);
                ImGui::SliderFloat("寿命 max", &liveEmitter->lifetimeMax, 0.1f, 5.0f);
                ImGui::SliderFloat("尺寸 min", &liveEmitter->sizeMin, 0.01f, 1.0f);
                ImGui::SliderFloat("尺寸 max", &liveEmitter->sizeMax, 0.01f, 1.0f);
                ImGui::SliderFloat("抖动", &liveEmitter->jitter, 0.0f, 3.0f);
                float col[3] = {liveEmitter->color.r, liveEmitter->color.g, liveEmitter->color.b};
                if (ImGui::ColorEdit3("颜色", col))
                    liveEmitter->color = glm::vec3(col[0], col[1], col[2]);
                if (particleGravity)
                    ImGui::SliderFloat("重力 Y", particleGravity, -30.0f, 5.0f, "%.1f");
                if (particleDamping)
                    ImGui::SliderFloat("阻尼", particleDamping, 0.0f, 3.0f, "%.2f");
                ImGui::TextDisabled("按 P 触发粒子爆发");
                ImGui::TreePop();
            }
        }

        if (ImGui::Button("撤销 (Ctrl+Z)"))
            undoRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("重做 (Ctrl+Y)"))
            redoRequested = true;
        if (ImGui::Button("保存场景 (F5)"))
            saveRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("加载场景 (F9)"))
            loadRequested = true;
        ImGui::TextDisabled("场景文件: scene.json");
        ImGui::Separator();
        ImGui::TextUnformatted("操作: 左键拖拽旋转 | 滚轮缩放");
        ImGui::TextUnformatted("WASD+QE 平移相机 | 面板可直接拖动");
        ImGui::Separator();
        ImGui::Text("停靠布局:");
        ImGui::SameLine();
        const char* presets[] = {"经典", "紧凑"};
        const int curPreset = static_cast<int>(dockPreset_);
        for (int p = 0; p < 2; ++p)
        {
            if (ImGui::RadioButton(presets[p], curPreset == p))
                dockPreset_ = static_cast<DockPreset>(p);
            if (p < 1)
                ImGui::SameLine();
        }
        ImGui::Checkbox("层级树 (Hierarchy)", &hierarchyOpen_);
        ImGui::Checkbox("构建设置 (Build Settings)", &buildSettingsOpen_);

        ImGui::End();
    }

    // ---- 动画控制面板：与 AnimationStateMachine 双向绑定（编辑器写回 → 运行时同帧生效） ----
    void DrawAnimationWindow(Scene::AnimationStateMachine* animSM)
    {
        if (!animSM || animSM->StateCount() == 0)
            return;

        ImGui::TextUnformatted("动画状态机");

        // 播放/暂停按钮 + 过渡状态指示
        const bool paused = animSM->IsPaused();
        if (ImGui::Button(paused ? "播放" : "暂停", ImVec2(72, 0)))
            animSM->SetPaused(!paused);
        ImGui::SameLine();
        if (animSM->IsTransitioning())
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "过渡中 %.0f%%", animSM->TransitionProgress() * 100.0f);
        else
            ImGui::TextDisabled("稳定 | 状态: %s", animSM->CurrentStateName());

        // 时间轴：拖动直接写回当前状态时间（时长来自状态绑定的动画）
        const float dur = animSM->CurrentStateDuration();
        float t = animSM->CurrentTime();
        if (dur > 0.0f)
        {
            if (ImGui::SliderFloat("时间轴", &t, 0.0f, dur, "%.2f s"))
                animSM->SetCurrentTime(t);
            ImGui::Text("时长: %.2f s", dur);
        }
        else
        {
            ImGui::TextDisabled("当前状态未绑定动画，时间轴不可用");
        }

        // 当前状态属性：速度倍率 / 循环开关（写回状态机，下一帧 Update 生效）
        const int cs = animSM->CurrentState();
        if (cs >= 0)
        {
            float speed = animSM->GetState(cs).speed;
            if (ImGui::SliderFloat("播放速度", &speed, 0.0f, 3.0f, "%.2f x"))
                animSM->SetStateSpeed(cs, speed);
            bool loop = animSM->GetState(cs).loop;
            if (ImGui::Checkbox("循环播放", &loop))
                animSM->SetStateLoop(cs, loop);
        }

        // 状态跳转按钮：点击强制 crossfade 到目标状态（* 标记当前状态）
        ImGui::TextUnformatted("状态跳转:");
        for (size_t i = 0; i < animSM->StateCount(); ++i)
        {
            const Scene::AnimState& st = animSM->GetState(static_cast<int>(i));
            char label[96];
            snprintf(label, sizeof(label), "%s%s##st%zu", st.name.c_str(),
                     (static_cast<int>(i) == animSM->CurrentState()) ? " *" : "", i);
            if (ImGui::Button(label))
                animSM->ForceTransition(static_cast<int>(i));
            if (i + 1 < animSM->StateCount())
                ImGui::SameLine();
        }

        // 运行时参数：Float 可拖动、Bool 可勾选（直接写回参数表，过渡条件即时响应）
        if (!animSM->FloatParams().empty() || !animSM->BoolParams().empty())
        {
            ImGui::TextUnformatted("参数:");
            for (const auto& [name, val] : animSM->FloatParams())
            {
                float v = val;
                if (ImGui::SliderFloat(name.c_str(), &v, 0.0f, 20.0f, "%.2f"))
                    animSM->SetFloat(name, v);
            }
            for (const auto& [name, val] : animSM->BoolParams())
            {
                bool b = val;
                if (ImGui::Checkbox(name.c_str(), &b))
                    animSM->SetBool(name, b);
            }
        }
        ImGui::Text("状态数: %zu  过渡数: %zu", animSM->StateCount(), animSM->TransitionCount());
    }

    void DrawLightWindow(LightParams& light)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "light", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("光照");

        float dir[3] = {light.direction.x, light.direction.y, light.direction.z};
        if (ImGui::DragFloat3("光源方向", dir, 0.02f, -2.0f, 2.0f))
            light.direction = glm::normalize(glm::vec3(dir[0], dir[1], dir[2]));

        float color[3] = {light.color.r, light.color.g, light.color.b};
        if (ImGui::ColorEdit3("光源颜色", color))
            light.color = glm::vec3(color[0], color[1], color[2]);

        ImGui::SliderFloat("光照强度", &light.intensity, 0.0f, 10.0f);
        ImGui::SliderFloat("环境光强度", &light.ambient, 0.0f, 1.0f);
        ImGui::SliderFloat("IBL环境强度", &light.iblStrength, 0.0f, 2.0f);
        ImGui::SliderFloat("曝光 (Exposure)", &light.exposure, 0.1f, 4.0f, "%.2f");
        ImGui::Separator();
        ImGui::SliderFloat("阴影浓度", &light.shadowStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("阴影偏移", &light.shadowBias, 0.0002f, 0.01f, "%.4f");
        if (ImGui::Button("重置光照"))
            light = LightParams{};

        ImGui::End();
    }

    void DrawPointLightsWindow(std::vector<PointLightParams>& lights)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "pointLights", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("点光源");

        ImGui::Text("数量: %u / %u", static_cast<uint32_t>(lights.size()), kMaxPointLights);
        for (size_t i = 0; i < lights.size(); ++i)
        {
            PointLightParams& pl = lights[i];
            char label[32];
            snprintf(label, sizeof(label), "灯 #%u", static_cast<uint32_t>(i));

            if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
            {
                float pos[3] = {pl.position.x, pl.position.y, pl.position.z};
                if (ImGui::DragFloat3("位置", pos, 0.1f, -12.0f, 12.0f))
                    pl.position = glm::vec3(pos[0], pos[1], pos[2]);

                float color[3] = {pl.color.r, pl.color.g, pl.color.b};
                if (ImGui::ColorEdit3("颜色", color))
                    pl.color = glm::vec3(color[0], color[1], color[2]);

                ImGui::DragFloat("强度", &pl.intensity, 1.0f, 0.0f, 200.0f);
                ImGui::DragFloat("半径", &pl.radius, 0.25f, 1.0f, 30.0f);
                ImGui::Checkbox("投影阴影", &pl.castsShadow);
                ImGui::TreePop();
            }
        }

        if (ImGui::Button("+ 添加") && lights.size() < kMaxPointLights)
            lights.push_back(PointLightParams{});
        ImGui::SameLine();
        if (ImGui::Button("- 移除") && !lights.empty())
            lights.pop_back();

        ImGui::End();
    }

    void DrawCameraWindow(float& cameraFov)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "camera", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("相机");
        ImGui::SliderFloat("视场角 (FOV)", &cameraFov, 20.0f, 120.0f, "%.0f deg");
        ImGui::End();
    }

    void DrawSceneWindow(std::vector<Scene::SceneObject>& scene, int selectedObject,
                         BigHero::Editor::GizmoMode* gizmoMode = nullptr,
                         std::vector<Physics::SceneJoint>* joints = nullptr)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "scene", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("场景");

        if (gizmoMode)
        {
            const char* modes[] = {"无", "平移", "旋转"};
            const int cur = static_cast<int>(*gizmoMode);
            ImGui::Text("Gizmo 模式:");
            ImGui::SameLine();
            for (int m = 0; m < 3; ++m)
            {
                if (ImGui::RadioButton(modes[m], cur == m))
                    *gizmoMode = static_cast<BigHero::Editor::GizmoMode>(m);
                if (m < 2)
                    ImGui::SameLine();
            }
            ImGui::Separator();
        }

        if (selectedObject >= 0 && selectedObject < static_cast<int>(scene.size()))
        {
            const Scene::SceneObject& obj = scene[static_cast<size_t>(selectedObject)];
            const char* kind = (obj.meshId == 0) ? "立方体" : (obj.meshId == 1) ? "圆环体" : (obj.meshId == 2) ? "glTF 模型" : (obj.meshId == 3) ? "球" : "胶囊";
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "已选中: %s #%d（右键取消）", kind, selectedObject);
        }
        else
        {
            ImGui::TextDisabled("左键点击场景物体以选中，右键取消");
        }
        ImGui::Separator();

        for (size_t i = 0; i < scene.size(); ++i)
        {
            Scene::SceneObject& obj = scene[i];
            const char* kind = (obj.meshId == 0) ? "立方体" : (obj.meshId == 1) ? "圆环体" : (obj.meshId == 2) ? "glTF 模型" : (obj.meshId == 3) ? "球" : "胶囊";
            char label[32];
            snprintf(label, sizeof(label), "%s #%u", kind, static_cast<uint32_t>(i));

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_DefaultOpen;
            if (static_cast<int>(i) == selectedObject)
                nodeFlags |= ImGuiTreeNodeFlags_Selected;

            if (ImGui::TreeNodeEx(label, nodeFlags))
            {
                // 属性区由 Inspector 面板绘制（U1-E2 元数据驱动）：字段/控件/范围与旧手写版逐字等价，
                // 撤销手势仍走 Application 的 IsAnyItemActive 边沿 + SceneSnapshotCommand 机制
                inspector.DrawObject(obj);

                // U1-S1d：挂接脚本的实体在原生组件后追加"脚本 (TypeName)"分组。
                // 字段经 MetaRegistry 读写器通道（PropertyDesc.get/set）直达托管实例，
                // 每行 component 指针为逐字段 ScriptFieldContext；撤销与原生属性行同一条
                // IsAnyItemActive 手势路径（Application 折算为 ScriptFieldValuesCommand）。
                Script::ScriptFieldView* scriptView = Script::FindScriptFieldView(i);
                if (scriptView != nullptr && !scriptView->fields.empty())
                {
                    const Editor::Inspector::ComponentMeta* scriptMeta =
                        Editor::Inspector::MetaRegistry::Global().Find(Script::ScriptMetaKey(scriptView->typeName));
                    if (scriptMeta != nullptr)
                    {
                        const std::string groupTitle = Script::ScriptGroupTitle(scriptView->typeName);
                        ImGui::Separator();
                        ImGui::TextUnformatted(groupTitle.c_str());
                        const size_t fieldCount =
                            std::min(scriptMeta->properties.size(), scriptView->fields.size());
                        for (size_t k = 0; k < fieldCount; ++k)
                            inspector.DrawSingle(scriptMeta->properties[k], &scriptView->fields[k]);
                    }
                }
                ImGui::TreePop();
            }
        }

        // Inspector 物理属性变更上抛（Application 消费后重建刚体，与旧手写路径同一标志）
        if (inspector.physicsRebuildRequested)
        {
            physicsRebuildRequested = true;
            inspector.physicsRebuildRequested = false;
        }

        // ---- 关节管理 ----
        if (joints && selectedObject >= 0)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("关节（连接选中物体与另一物体）");

            // 选择第二个物体
            if (jointTargetObject < 0 || jointTargetObject >= static_cast<int>(scene.size()))
                jointTargetObject = (selectedObject > 0) ? 0 : (scene.size() > 1 ? 1 : -1);
            if (jointTargetObject == selectedObject && scene.size() > 1)
                jointTargetObject = (selectedObject > 0) ? 0 : 1;
            if (scene.size() > 1)
            {
                std::string targetLabel =
                    (jointTargetObject >= 0)
                        ? std::string((scene[jointTargetObject].meshId == 0)
                                          ? "立方体"
                                          : (scene[jointTargetObject].meshId == 1) ? "圆环体" : "glTF 模型") +
                              " #" + std::to_string(jointTargetObject)
                        : "无";
                if (ImGui::BeginCombo("连接到", targetLabel.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(scene.size()); ++i)
                    {
                        if (i == selectedObject)
                            continue;
                        const bool isSel = (i == jointTargetObject);
                        char buf[32];
                        const char* name = (scene[i].meshId == 0) ? "立方体" : (scene[i].meshId == 1) ? "圆环体" : (scene[i].meshId == 2) ? "glTF 模型" : (scene[i].meshId == 3) ? "球" : "胶囊";
                        snprintf(buf, sizeof(buf), "%s #%d", name, i);
                        if (ImGui::Selectable(buf, isSel))
                            jointTargetObject = i;
                        if (isSel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const char* jointTypes[] = {"固定", "铰链", "球窝", "滑块"};
                ImGui::Combo("关节类型", &jointType, jointTypes, 4);

                if (ImGui::Button("创建关节") && jointTargetObject >= 0 && jointTargetObject != selectedObject)
                    jointCreateRequested = true;
            }
            else
            {
                ImGui::TextDisabled("至少需要两个物体才能创建关节");
            }

            // 列出现有关节（涉及选中物体的）
            if (!joints->empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("已有关节:");
                for (size_t i = 0; i < joints->size(); ++i)
                {
                    const Physics::SceneJoint& j = (*joints)[i];
                    if (j.objectA != static_cast<uint32_t>(selectedObject) &&
                        j.objectB != static_cast<uint32_t>(selectedObject))
                        continue;
                    const char* jtNames[] = {"固定", "铰链", "球窝", "滑块"};
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[%s] #%u <-> #%u", jtNames[static_cast<int>(j.type)], j.objectA,
                             j.objectB);
                    ImGui::TextUnformatted(buf);
                    ImGui::SameLine();
                    char delBtn[16];
                    snprintf(delBtn, sizeof(delBtn), "删除##j%zu", i);
                    if (ImGui::SmallButton(delBtn))
                    {
                        jointDeleteIndex = static_cast<int>(i);
                        jointDeleteRequested = true;
                    }
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("添加立方体"))
            addObjectRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("删除选中") && selectedObject >= 0)
            deleteObjectRequested = true;

        ImGui::End();
    }

    // ---- 人物面板：生成/编辑/删除 Q 版人物（Todo 3） ----
    void DrawPersonWindow()
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "person", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("人物");

        // 生成位置：鼠标点击处 / 手动输入
        ImGui::Checkbox("鼠标点击生成", &personSpawnAtCursor);
        if (!personSpawnAtCursor)
        {
            float sp[3] = {personSpawnPos.x, personSpawnPos.y, personSpawnPos.z};
            if (ImGui::DragFloat3("生成位置", sp, 0.1f, -20.0f, 20.0f))
                personSpawnPos = glm::vec3(sp[0], sp[1], sp[2]);
        }
        else
        {
            ImGui::TextDisabled("左键点击场景地面生成人物");
        }

        if (ImGui::Button("生成人物"))
            addPersonRequested = true;

        ImGui::Separator();
        // 体型
        ImGui::SliderFloat("身高", &personParams.height, 0.5f, 2.5f, "%.2f m");
        ImGui::SliderFloat("头比例", &personParams.headScale, 0.3f, 2.0f, "%.2f");
        ImGui::SliderFloat("躯干宽", &personParams.bodyWidth, 0.3f, 2.0f, "%.2f");
        ImGui::SliderFloat("四肢粗", &personParams.limbWidth, 0.3f, 2.0f, "%.2f");

        // 外观
        float skin[3] = {personParams.skinTint.r, personParams.skinTint.g, personParams.skinTint.b};
        if (ImGui::ColorEdit3("肤色", skin))
            personParams.skinTint = glm::vec3(skin[0], skin[1], skin[2]);
        float cloth[3] = {personParams.clothTint.r, personParams.clothTint.g, personParams.clothTint.b};
        if (ImGui::ColorEdit3("衣色", cloth))
            personParams.clothTint = glm::vec3(cloth[0], cloth[1], cloth[2]);
        float hair[3] = {personParams.hairTint.r, personParams.hairTint.g, personParams.hairTint.b};
        if (ImGui::ColorEdit3("发色", hair))
            personParams.hairTint = glm::vec3(hair[0], hair[1], hair[2]);
        float eye[3] = {personParams.eyeTint.r, personParams.eyeTint.g, personParams.eyeTint.b};
        if (ImGui::ColorEdit3("眼色", eye))
            personParams.eyeTint = glm::vec3(eye[0], eye[1], eye[2]);

        // 姿态
        const char* poseNames[] = {"站立", "行走", "挥手", "转身", "蹲坐"};
        int poseIdx = static_cast<int>(personParams.pose);
        if (ImGui::Combo("姿态", &poseIdx, poseNames, IM_ARRAYSIZE(poseNames)))
            personParams.pose = static_cast<Scene::PersonPose>(poseIdx);
        ImGui::SliderFloat("动作速度", &personParams.poseSpeed, 0.0f, 3.0f, "%.1fx");

        ImGui::Separator();
        if (selectedPerson >= 0)
        {
            ImGui::Text("当前选中: 人物 #%d", selectedPerson);
            if (ImGui::Button("删除选中人物"))
                removePersonRequested = true;
        }
        else
        {
            ImGui::TextDisabled("选中人物后可删除（场景列表点击 body/head 部件）");
        }

        ImGui::End();
    }

    // ---- 资源面板：展示 AssetRegistry 登记条目 + MeshResource 几何详情 ----
    void DrawAssetsWindow(const bighero::AssetRegistry* assets,
                          const std::unordered_map<std::string, bighero::MeshResource>* meshResources)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "assets", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("资源");

        if (!assets)
        {
            ImGui::TextDisabled("资源注册表不可用");
            ImGui::End();
            return;
        }

        // 顶部统计：条目数 + 各状态计数（Loaded 绿 / Failed 红）
        const uint32_t failed = static_cast<uint32_t>(
            assets->ByState(static_cast<uint32_t>(bighero::AssetMetadata::LoadState::Failed)).size());
        const uint32_t loaded = static_cast<uint32_t>(
            assets->ByState(static_cast<uint32_t>(bighero::AssetMetadata::LoadState::Loaded)).size());
        ImGui::Text("条目: %u  已加载: %u  失败: %u", static_cast<uint32_t>(assets->Count()), loaded, failed);
        ImGui::Separator();

        for (const bighero::AssetRegistry::Entry& e : assets->Entries())
        {
            const bighero::AssetMetadata::LoadState st = static_cast<bighero::AssetMetadata::LoadState>(e.state);
            const char* stateStr = "未知";
            ImVec4 stateCol = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            switch (st)
            {
            case bighero::AssetMetadata::LoadState::Loaded:
                stateStr = "已加载";
                stateCol = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
                break;
            case bighero::AssetMetadata::LoadState::Failed:
                stateStr = "失败";
                stateCol = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                break;
            case bighero::AssetMetadata::LoadState::Loading:
                stateStr = "加载中";
                stateCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                break;
            case bighero::AssetMetadata::LoadState::Unloaded:
                stateStr = "未加载";
                break;
            }

            // 资源类型标记
            const auto ty = static_cast<bighero::AssetMetadata::AssetType>(e.type);
            const char* typeStr = "?";
            switch (ty)
            {
            case bighero::AssetMetadata::AssetType::Texture: typeStr = "纹理"; break;
            case bighero::AssetMetadata::AssetType::Mesh: typeStr = "网格"; break;
            case bighero::AssetMetadata::AssetType::Shader: typeStr = "着色器"; break;
            case bighero::AssetMetadata::AssetType::Material: typeStr = "材质"; break;
            case bighero::AssetMetadata::AssetType::AnimationClip: typeStr = "动画"; break;
            case bighero::AssetMetadata::AssetType::Audio: typeStr = "音频"; break;
            case bighero::AssetMetadata::AssetType::Font: typeStr = "字体"; break;
            case bighero::AssetMetadata::AssetType::Scene: typeStr = "场景"; break;
            default: break;
            }

            char header[160];
            snprintf(header, sizeof(header), "%s [%s]", e.name.c_str(), typeStr);
            if (ImGui::TreeNodeEx(header))
            {
                ImGui::TextColored(stateCol, "状态: %s", stateStr);
                if (!e.path.empty())
                    ImGui::Text("路径: %s", e.path.c_str());
                if (e.size > 0)
                {
                    char sizeBuf[32];
                    if (e.size >= 1024 * 1024)
                        snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", static_cast<double>(e.size) / (1024.0 * 1024.0));
                    else if (e.size >= 1024)
                        snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", static_cast<double>(e.size) / 1024.0);
                    else
                        snprintf(sizeBuf, sizeof(sizeBuf), "%llu B", static_cast<unsigned long long>(e.size));
                    ImGui::Text("大小: %s", sizeBuf);
                }

                // 几何详情（网格资源）
                if (meshResources)
                {
                    const auto it = meshResources->find(e.name);
                    if (it != meshResources->end())
                    {
                        const bighero::MeshResource& mr = it->second;
                        ImGui::Separator();
                        ImGui::Text("顶点: %u  索引: %u  三角形: %u", mr.VertexCount(), mr.IndexCount(),
                                    mr.TriangleCount());
                        if (mr.HasBounds())
                            ImGui::Text("包围盒: (%.1f, %.1f, %.1f) ~ (%.1f, %.1f, %.1f)", mr.MinX(), mr.MinY(),
                                        mr.MinZ(), mr.MaxX(), mr.MaxY(), mr.MaxZ());
                    }
                }
                ImGui::TreePop();
            }
        }

        ImGui::End();
    }
};
} // namespace BigHero
