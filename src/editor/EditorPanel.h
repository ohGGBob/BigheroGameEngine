#pragma once
#include "imgui.h"
#include "scene/Scene.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace BigHero
{
    // 光照参数（编辑器可调，每帧写入LightUBO）
    struct LightParams
    {
        glm::vec3 direction{ 0.5f, -1.0f, -0.35f };
        glm::vec3 color{ 1.0f, 0.95f, 0.85f };
        float ambient = 0.18f;
        float specPower = 32.0f;
        float specStrength = 0.9f;
    };

    // 渲染统计信息（由主循环每帧填充）
    struct EditorStats
    {
        uint32_t fps = 0;
        float frameMs = 0.0f;
        const char* gpuName = "";
        VkExtent2D extent{ 0, 0 };
        uint32_t msaaSamples = 1;
        uint32_t triangleCount = 0;
    };

    // 编辑器面板：渲染统计 / 光照 / 相机 / 场景物体属性，直接编辑运行时数据
    class EditorPanel
    {
    public:
        void Draw(const EditorStats& stats, std::vector<Scene::SceneObject>& scene,
            LightParams& light, float& cameraFov)
        {
            DrawStatsWindow(stats, scene);
            DrawLightWindow(light);
            DrawCameraWindow(cameraFov);
            DrawSceneWindow(scene);
        }

    private:
        static void DrawStatsWindow(const EditorStats& stats, const std::vector<Scene::SceneObject>& scene)
        {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("渲染统计");

            ImGui::Text("FPS: %u", stats.fps);
            ImGui::Text("帧耗时: %.2f ms", stats.frameMs);
            ImGui::Separator();
            ImGui::Text("GPU: %s", stats.gpuName);
            ImGui::Text("分辨率: %u x %u", stats.extent.width, stats.extent.height);
            ImGui::Text("MSAA: %ux", stats.msaaSamples);
            ImGui::Text("三角形: %u", stats.triangleCount);
            ImGui::Text("物体数: %u", static_cast<uint32_t>(scene.size()));
            ImGui::Separator();
            ImGui::TextUnformatted("操作: 左键拖拽旋转 | 滚轮缩放");
            ImGui::TextUnformatted("WASD+QE 平移相机 | 面板可直接拖动");

            ImGui::End();
        }

        static void DrawLightWindow(LightParams& light)
        {
            ImGui::SetNextWindowPos(ImVec2(348.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("光照");

            float dir[3] = { light.direction.x, light.direction.y, light.direction.z };
            if (ImGui::DragFloat3("光源方向", dir, 0.02f, -2.0f, 2.0f))
                light.direction = glm::normalize(glm::vec3(dir[0], dir[1], dir[2]));

            float color[3] = { light.color.r, light.color.g, light.color.b };
            if (ImGui::ColorEdit3("光源颜色", color))
                light.color = glm::vec3(color[0], color[1], color[2]);

            ImGui::SliderFloat("环境光强度", &light.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("高光锐度", &light.specPower, 2.0f, 128.0f);
            ImGui::SliderFloat("高光强度", &light.specStrength, 0.0f, 2.0f);
            if (ImGui::Button("重置光照"))
                light = LightParams{};

            ImGui::End();
        }

        static void DrawCameraWindow(float& cameraFov)
        {
            ImGui::SetNextWindowPos(ImVec2(664.0f, 12.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("相机");
            ImGui::SliderFloat("视场角 (FOV)", &cameraFov, 20.0f, 120.0f, "%.0f deg");
            ImGui::End();
        }

        static void DrawSceneWindow(std::vector<Scene::SceneObject>& scene)
        {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 260.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380.0f, 340.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("场景");

            for (size_t i = 0; i < scene.size(); ++i)
            {
                Scene::SceneObject& obj = scene[i];
                const char* kind = (obj.meshId == 0) ? "立方体" : "圆环体";
                char label[32];
                snprintf(label, sizeof(label), "%s #%u", kind, static_cast<uint32_t>(i));

                if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
                {
                    float pos[3] = { obj.position.x, obj.position.y, obj.position.z };
                    if (ImGui::DragFloat3("位置", pos, 0.05f, -10.0f, 10.0f))
                        obj.position = glm::vec3(pos[0], pos[1], pos[2]);

                    ImGui::DragFloat("缩放", &obj.scale, 0.02f, 0.1f, 5.0f);

                    float tint[3] = { obj.tint.r, obj.tint.g, obj.tint.b };
                    if (ImGui::ColorEdit3("色调", tint))
                        obj.tint = glm::vec3(tint[0], tint[1], tint[2]);

                    ImGui::DragFloat("自转速度", &obj.spinSpeed, 0.5f, -180.0f, 180.0f, "%.1f deg/s");

                    ImGui::TreePop();
                }
            }

            ImGui::End();
        }
    };
}
