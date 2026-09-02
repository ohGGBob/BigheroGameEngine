#pragma once
#include "editor/Gizmo.h"
#include "imgui.h"
#include "scene/AnimationStateMachine.h"
#include "scene/Scene.h"
#include <cstring>
#include <glm/glm.hpp>
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
            // 左侧单列堆叠：统计 / 场景 / 光照 / 相机 / 点光源
            const float w = std::min(360.0f, std::max(260.0f, vp.x - 24.0f));
            static const struct
            {
                const char* name;
                float y;
            } kStack[] = {{"stats", m},
                          {"scene", m + 300.0f},
                          {"light", m + 648.0f},
                          {"camera", m + 648.0f + 300.0f},
                          {"pointLights", m + 648.0f + 300.0f + 250.0f}};
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
        else if (std::strcmp(slot, "scene") == 0)
        {
            pos = ImVec2(m, vp.y - 352.0f);
            size = ImVec2(380.0f, 340.0f);
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
    bool saveRequested = false;                   // 保存场景按钮被点击（Application 消费后重置）
    bool loadRequested = false;                   // 加载场景按钮被点击（Application 消费后重置）
    bool addObjectRequested = false;              // 添加物体按钮被点击（Application 消费后重置）
    bool deleteObjectRequested = false;
    bool undoRequested = false;
    bool redoRequested = false;           // 删除选中物体按钮被点击（Application 消费后重置）
    bool physicsRebuildRequested = false;         // 物理属性变更，需重建刚体（Application 消费后重置）
    bool jointCreateRequested = false;            // 创建关节请求（Application 消费后重置）
    bool jointDeleteRequested = false;            // 删除关节请求（Application 消费后重置）
    int jointTargetObject = -1;                   // 关节的第二个物体索引
    int jointType = 0;                            // 关节类型（JointType 枚举值）
    int jointDeleteIndex = -1;                    // 要删除的关节索引

    void Draw(const EditorStats& stats, std::vector<Scene::SceneObject>& scene, LightParams& light, float& cameraFov,
              std::vector<PointLightParams>& pointLights, int selectedObject = -1, bool* deferredMode = nullptr,
              BigHero::Editor::GizmoMode* gizmoMode = nullptr, glm::vec2 viewport = glm::vec2(0.0f),
              float* masterVolume = nullptr, bool* postProcessMode = nullptr, bool* ssaoMode = nullptr,
              bool* ssrMode = nullptr, bool* physicsEnabled = nullptr, bool* physicsDebug = nullptr,
              float* gravity = nullptr, bool* characterEnabled = nullptr, float* characterSpeed = nullptr,
              float* characterJump = nullptr, std::vector<Physics::SceneJoint>* joints = nullptr,
              const Scene::AnimationStateMachine* animSM = nullptr, bool* navEnabledMode = nullptr,
              bool* particleEnabledMode = nullptr)
    {
        viewport_ = viewport;
        DrawStatsWindow(stats, scene, deferredMode, masterVolume, postProcessMode, ssaoMode, ssrMode, physicsEnabled,
                        physicsDebug, gravity, characterEnabled, characterSpeed, characterJump, animSM, navEnabledMode,
                        particleEnabledMode);
        DrawLightWindow(light);
        DrawPointLightsWindow(pointLights);
        DrawCameraWindow(cameraFov);
        DrawSceneWindow(scene, selectedObject, gizmoMode, joints);
    }

  private:
    void DrawStatsWindow(const EditorStats& stats, const std::vector<Scene::SceneObject>& scene,
                         bool* deferredMode = nullptr, float* masterVolume = nullptr, bool* postProcessMode = nullptr,
                         bool* ssaoMode = nullptr, bool* ssrMode = nullptr, bool* physicsEnabled = nullptr,
                         bool* physicsDebug = nullptr, float* gravity = nullptr, bool* characterEnabled = nullptr,
                         float* characterSpeed = nullptr, float* characterJump = nullptr,
                         const Scene::AnimationStateMachine* animSM = nullptr, bool* navEnabledMode = nullptr,
                         bool* particleEnabledMode = nullptr)
    {
        ImVec2 winPos, winSize;
        DockLayout::Place(dockPreset_, "stats", viewport_, winPos, winSize);
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("渲染统计");

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

            // 动画状态机监控
            if (animSM && animSM->StateCount() > 0)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("动画状态机");
                ImGui::Text("当前状态: %s", animSM->CurrentStateName());
                ImGui::Text("状态时间: %.2f s", animSM->CurrentTime());
                if (animSM->IsTransitioning())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "过渡中: %.0f%%",
                                       animSM->TransitionProgress() * 100.0f);
                }
                else
                {
                    ImGui::TextDisabled("稳定");
                }

                // 参数列表
                if (!animSM->FloatParams().empty() || !animSM->BoolParams().empty())
                {
                    ImGui::TextUnformatted("参数:");
                    for (const auto& [name, val] : animSM->FloatParams())
                        ImGui::Text("  %s = %.2f", name.c_str(), val);
                    for (const auto& [name, val] : animSM->BoolParams())
                        ImGui::Text("  %s = %s", name.c_str(), val ? "true" : "false");
                }

                // 状态列表
                ImGui::Text("状态数: %zu  过渡数: %zu", animSM->StateCount(), animSM->TransitionCount());
            }
        }
        ImGui::Separator();
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

        ImGui::End();
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
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "已选中: %s #%d（右键取消）",
                               (obj.meshId == 0) ? "立方体" : "圆环体", selectedObject);
        }
        else
        {
            ImGui::TextDisabled("左键点击场景物体以选中，右键取消");
        }
        ImGui::Separator();

        for (size_t i = 0; i < scene.size(); ++i)
        {
            Scene::SceneObject& obj = scene[i];
            const char* kind = (obj.meshId == 0) ? "立方体" : "圆环体";
            char label[32];
            snprintf(label, sizeof(label), "%s #%u", kind, static_cast<uint32_t>(i));

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_DefaultOpen;
            if (static_cast<int>(i) == selectedObject)
                nodeFlags |= ImGuiTreeNodeFlags_Selected;

            if (ImGui::TreeNodeEx(label, nodeFlags))
            {
                float pos[3] = {obj.position.x, obj.position.y, obj.position.z};
                if (ImGui::DragFloat3("位置", pos, 0.05f, -10.0f, 10.0f))
                    obj.position = glm::vec3(pos[0], pos[1], pos[2]);

                ImGui::DragFloat("缩放", &obj.scale, 0.02f, 0.1f, 5.0f);

                float tint[3] = {obj.tint.r, obj.tint.g, obj.tint.b};
                if (ImGui::ColorEdit3("色调", tint))
                    obj.tint = glm::vec3(tint[0], tint[1], tint[2]);

                ImGui::SliderFloat("金属度", &obj.metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("粗糙度", &obj.roughness, 0.045f, 1.0f);
                ImGui::DragFloat("自转速度", &obj.spinSpeed, 0.5f, -180.0f, 180.0f, "%.1f deg/s");
                ImGui::SliderFloat("旋转X", &obj.rotation.x, -180.0f, 180.0f, "%.0f deg");
                ImGui::SliderFloat("旋转Y", &obj.rotation.y, -180.0f, 180.0f, "%.0f deg");
                ImGui::SliderFloat("旋转Z", &obj.rotation.z, -180.0f, 180.0f, "%.0f deg");

                // ---- 物理属性 ----
                ImGui::Separator();
                ImGui::TextUnformatted("物理");
                const char* bodyTypes[] = {"无", "静态", "动态", "运动学"};
                int curBody = static_cast<int>(obj.physicsType);
                if (ImGui::Combo("刚体类型", &curBody, bodyTypes, 4))
                {
                    obj.physicsType = static_cast<Physics::BodyType>(curBody);
                    physicsRebuildRequested = true;
                }
                if (obj.physicsType != Physics::BodyType::None)
                {
                    const char* shapes[] = {"盒", "球", "胶囊"};
                    int curShape = static_cast<int>(obj.physicsShape);
                    if (ImGui::Combo("碰撞形状", &curShape, shapes, 3))
                    {
                        obj.physicsShape = static_cast<Physics::ShapeType>(curShape);
                        physicsRebuildRequested = true;
                    }
                    if (obj.physicsType == Physics::BodyType::Dynamic)
                    {
                        if (ImGui::DragFloat("质量(kg)", &obj.physicsMass, 0.1f, 0.01f, 1000.0f))
                            physicsRebuildRequested = true;
                    }
                    if (ImGui::SliderFloat("摩擦", &obj.physicsFriction, 0.0f, 1.0f))
                        physicsRebuildRequested = true;
                    if (ImGui::SliderFloat("弹性", &obj.physicsRestitution, 0.0f, 1.0f))
                        physicsRebuildRequested = true;
                }

                ImGui::TreePop();
            }
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
                        ? std::string((scene[jointTargetObject].meshId == 0) ? "立方体" : "圆环体") + " #" +
                              std::to_string(jointTargetObject)
                        : "无";
                if (ImGui::BeginCombo("连接到", targetLabel.c_str()))
                {
                    for (int i = 0; i < static_cast<int>(scene.size()); ++i)
                    {
                        if (i == selectedObject)
                            continue;
                        const bool isSel = (i == jointTargetObject);
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%s #%d", (scene[i].meshId == 0) ? "立方体" : "圆环体", i);
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
};
} // namespace BigHero
