#pragma once
// 构建设置面板（绘制器）—— Unity 对标清单 U1-B1：Build Settings 最小可用版 + 一键构建。
// 配置编辑（输出目录 / 版本串 / 打包 assets / 拷贝 shaders / 拷贝 exe）+ 场景清单展示
// + "构建"按钮：BuildSettingsModel 生成清单 → BuildExecutor 逐文件拷贝 →
// 逐文件成功/失败结果日志与总数。产物 = builds/<时间戳>/ 下的独立可分发目录
// （当前 exe + shaders/ + assets/ + 场景文件，理论上拷走即能跑）。
// 与 HierarchyPanel 同一约定：纯 ImGui 覆盖层，不写场景数据；构建在点击帧内同步完成
// （资产量小可接受；异步执行/进度条留待 U1-Bx 扩展）。

#include "editor/BuildExecutor.h"
#include "editor/BuildSettingsModel.h"
#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace BigHero::Editor
{
class BuildSettingsPanel
{
  public:
    BuildSettings::BuildConfig config; // 构建配置（本面板持有与编辑）

    BuildSettingsPanel() { SyncBuffersFromConfig(); }

    // pos/size 由调用方（EditorPanel）经 DockLayout::Place 解析，本面板不依赖布局类型
    void Draw(ImVec2 winPos, ImVec2 winSize)
    {
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("构建设置");

        // ---- 配置编辑 ----
        if (ImGui::InputText("输出目录", outputRootBuf_, sizeof(outputRootBuf_)))
            config.outputRoot = outputRootBuf_;
        ImGui::TextDisabled("输出: %s/%s/<时间戳>/", CurrentWorkDirString().c_str(), config.outputRoot.c_str());
        if (ImGui::InputText("版本字符串", versionBuf_, sizeof(versionBuf_)))
            config.versionString = versionBuf_;
        ImGui::Text("目标平台: %s", config.platformTarget.c_str());
        ImGui::Checkbox("拷贝可执行文件", &config.copyExecutable);
        ImGui::Checkbox("拷贝着色器 shaders/", &config.copyShaders);
        ImGui::Checkbox("打包资产 assets/", &config.packageAssets);

        ImGui::Separator();

        // ---- 场景清单展示（内置场景随 exe 编译，无需拷贝文件） ----
        ImGui::TextUnformatted("场景清单:");
        for (const std::string& name : config.builtinScenes)
            ImGui::BulletText("内置: %s", name.c_str());
        for (const std::string& file : config.sceneFiles)
        {
            ImGui::BulletText("%s", file.c_str());
            if (!std::filesystem::exists(std::filesystem::path(file)))
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "（缺失）");
            }
        }
        ImGui::TextDisabled("内置场景随 exe 编译，仅分发外部场景文件");

        ImGui::Separator();

        // ---- 一键构建 + 清单预览 ----
        if (ImGui::Button("构建 (一键)", ImVec2(120.0f, 0.0f)))
            RunBuild();
        ImGui::SameLine();
        if (ImGui::Button("刷新清单预览"))
            PreviewManifest();

        if (hasManifest_)
        {
            ImGui::Text("待拷贝文件: %u", static_cast<unsigned>(lastManifest_.entries.size()));
            for (const std::string& w : lastManifest_.warnings)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "! %s", w.c_str());
        }

        // ---- 构建结果日志（逐文件成功/失败 + 总数） ----
        if (hasReport_)
        {
            ImGui::Separator();
            if (!lastReport_.fatalError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "构建失败: %s", lastReport_.fatalError.c_str());
            }
            else
            {
                const bool ok = lastReport_.AllSucceeded();
                const ImVec4 col = ok ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                ImGui::TextColored(col, "构建%s: %u 成功 / %u 失败 → %s", ok ? "完成" : "完成（有失败项）",
                                   static_cast<unsigned>(lastReport_.SuccessCount()),
                                   static_cast<unsigned>(lastReport_.FailureCount()),
                                   lastReport_.outputDirectory.c_str());
            }
            if (!lastReport_.results.empty())
            {
                if (ImGui::BeginChild("##buildLog", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders))
                {
                    for (const BuildSettings::CopyResult& r : lastReport_.results)
                    {
                        if (r.success)
                            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "[成功] %s", r.destination.c_str());
                        else
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[失败] %s", r.destination.c_str());
                            ImGui::TextDisabled("    %s", r.error.c_str());
                        }
                    }
                }
                ImGui::EndChild();
            }
        }

        ImGui::End();
    }

  private:
    char outputRootBuf_[260] = {}; // 输出目录编辑缓冲（InputText 用 char 缓冲，变更后写回 config）
    char versionBuf_[64] = {};     // 版本字符串编辑缓冲
    BuildSettings::BuildManifest lastManifest_;
    BuildSettings::ExecuteReport lastReport_;
    bool hasManifest_ = false;
    bool hasReport_ = false;

    void SyncBuffersFromConfig()
    {
        std::snprintf(outputRootBuf_, sizeof(outputRootBuf_), "%s", config.outputRoot.c_str());
        std::snprintf(versionBuf_, sizeof(versionBuf_), "%s", config.versionString.c_str());
    }

    static std::string CurrentWorkDirString()
    {
        std::error_code ec;
        return std::filesystem::current_path(ec).generic_string();
    }

    // 生成清单（不执行拷贝）：预览待拷贝文件数与缺失告警
    void PreviewManifest()
    {
        std::error_code ec;
        const std::filesystem::path workDir = std::filesystem::current_path(ec);
        lastManifest_ = BuildSettings::GenerateManifest(config, workDir, BuildSettings::ResolveExePath(),
                                                        BuildSettings::CurrentTimestampDirName());
        hasManifest_ = true;
    }

    // 一键构建：生成清单 → 执行拷贝 → 缓存逐文件报告（点击帧内同步完成）
    void RunBuild()
    {
        std::error_code ec;
        const std::filesystem::path workDir = std::filesystem::current_path(ec);
        lastManifest_ = BuildSettings::GenerateManifest(config, workDir, BuildSettings::ResolveExePath(),
                                                        BuildSettings::CurrentTimestampDirName());
        hasManifest_ = true;
        lastReport_ = BuildSettings::ExecuteManifest(lastManifest_, workDir);
        hasReport_ = true;
    }
};
} // namespace BigHero::Editor
