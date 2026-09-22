#pragma once
// 工程面板（ProjectPanel）：把 U2 阶段新增的两组能力接到编辑器上——
//   ① 资产数据库（引用图 / 断链检测 / 索引持久化）
//   ② 性能三件套（LOD Group / 光照探针 / 烘焙遮挡剔除）
// 只画 ImGui，不直接改动场景；需要场景数据的操作（遮挡烘焙）通过请求标志交给 Application。
//
// 设计取舍：
//   EditorPanel 的 Draw 已经带了 40 余个可选指针参数，再加只会让签名更不可读。
//   因此这两组能力独立成面板，自带状态、自行定位窗口，Application 只需一句 Draw()。
//
// 契约：
//   - Draw() 可在任意帧调用；烘焙按钮只置标志，真正的烘焙由 Application 在帧末执行
//     （Application 才持有场景数据），避免在 UI 回调里触碰场景。
//   - 资产数据库的 Root 默认指向源码根下的 assets/（BIGHERO_SOURCE_ROOT 宏）。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/AssetDatabase.h"
#include "imgui.h"
#include "render/LightProbe.h"
#include "render/LodGroup.h"
#include "render/OcclusionCulling.h"
#include <glm/glm.hpp>

namespace BigHero::Editor
{
class ProjectPanel
{
  public:
    bool assetDbOpen = false; // 「资产数据库」窗口
    bool cullingOpen = false; // 「LOD / 探针 / 遮挡」窗口

    // 烘焙请求标志：由本面板置位，Application 消费后复位（面板不直接读场景）。
    bool occlusionBakeRequested = false;
    bool probeBakeRequested = false;

    void ToggleAssetDb() { assetDbOpen = !assetDbOpen; }
    void ToggleCulling() { cullingOpen = !cullingOpen; }

    // 每帧绘制两个窗口（各自可关闭）。
    void Draw()
    {
        if (assetDbOpen)
            DrawAssetDatabase();
        if (cullingOpen)
            DrawCulling();
    }

    // ---- 由 Application 帧末消费 ----
    // 遮挡烘焙：occluders 为墙体/大型静态体，cullables 为可被剔除的对象。
    void BakeOcclusion(const std::vector<Render::Bounds3>& occluders, const std::vector<Render::Bounds3>& cullables)
    {
        occlusionBakeRequested = false;
        occlusion_.Resize(glm::ivec3(occDims_[0], occDims_[1], occDims_[2]),
                          glm::vec3(occOrigin_[0], occOrigin_[1], occOrigin_[2]),
                          glm::vec3(occSpacing_, occSpacing_, occSpacing_));
        Render::OcclusionBakeParams p;
        p.originSamples = occOriginSamples_;
        p.targetSamples = occTargetSamples_;
        p.maxRayDistance = occMaxDistance_;
        occlusion_.Bake(occluders, cullables, p);
        occStatus_ = "烘焙完成：" + std::to_string(occlusion_.CellCount()) + " 格 × " +
                     std::to_string(occlusion_.ObjectCount()) + " 对象";
    }

    // 渲染管线每帧按相机位置查 PVS（Application::UpdateRenderables）：只读视图。
    [[nodiscard]] const Render::OcclusionVolume& Occlusion() const { return occlusion_; }

    // 光照探针烘焙：内置简化烘焙器（天光 / 地面反弹解析模型，非路径追踪 GI）。
    // 真实项目应把 radianceFn 换成自己的 GI 后端（贴图烘焙、光追、辐照度体等）。
    void BakeProbes()
    {
        probeBakeRequested = false;
        probes_.Resize(glm::ivec3(probeDims_[0], probeDims_[1], probeDims_[2]),
                       glm::vec3(probeOrigin_[0], probeOrigin_[1], probeOrigin_[2]),
                       glm::vec3(probeSpacing_, probeSpacing_, probeSpacing_));
        const glm::vec3 sky = glm::vec3(skyColor_[0], skyColor_[1], skyColor_[2]) * skyIntensity_;
        const glm::vec3 ground = glm::vec3(groundColor_[0], groundColor_[1], groundColor_[2]) * groundIntensity_;
        const float groundY = probeGroundY_;
        probes_.Bake(
            [sky, ground](const glm::vec3&, const glm::vec3& dir)
            {
                const float up = std::max(0.0f, dir.y);
                const float down = std::max(0.0f, -dir.y);
                return sky * up + ground * down;
            },
            [groundY](const glm::vec3& pos) { return pos.y > groundY; }, probeSamples_);

        size_t valid = 0;
        for (int z = 0; z < probeDims_[2]; ++z)
            for (int y = 0; y < probeDims_[1]; ++y)
                for (int x = 0; x < probeDims_[0]; ++x)
                    if (probes_.IsProbeValid(glm::ivec3(x, y, z)))
                        ++valid;
        probeStatus_ =
            "烘焙完成：" + std::to_string(probes_.ProbeCount()) + " 探针（有效 " + std::to_string(valid) + "）";
    }

  private:
    // ---------------- 资产数据库 ----------------
    void DrawAssetDatabase()
    {
        ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("资产数据库 (Asset Database)", &assetDbOpen))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("资产根目录（相对路径均基于此）");
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##AssetRoot", assetRoot_, sizeof(assetRoot_));
        ImGui::PopItemWidth();
        if (ImGui::Button("扫描导入"))
        {
            assetDb_.SetRoot(assetRoot_);
            const size_t n = assetDb_.ImportAll();
            assetStatus_ = "导入 " + std::to_string(n) + " 个资产，共 " + std::to_string(assetDb_.Count()) + " 条记录";
            selectedAsset_ = -1;
            assetPaths_ = assetDb_.AllPaths();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存索引"))
        {
            assetStatus_ = assetDb_.SaveIndex(indexPath_) ? std::string("索引已写入: ") + indexPath_
                                                          : std::string("索引写入失败: ") + indexPath_;
        }
        ImGui::SameLine();
        if (ImGui::Button("载入索引"))
        {
            assetStatus_ = assetDb_.LoadIndex(indexPath_) ? std::string("索引已载入: ") + indexPath_
                                                          : std::string("索引载入失败: ") + indexPath_;
        }
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##IndexPath", indexPath_, sizeof(indexPath_));
        ImGui::PopItemWidth();
        if (!assetStatus_.empty())
            ImGui::TextWrapped("%s", assetStatus_.c_str());

        ImGui::Separator();
        const auto broken = assetDb_.BrokenReferences();
        ImGui::Text("资产 %zu 个  断链 %zu 条", assetDb_.Count(), broken.size());

        // 资产列表（左）+ 详情（右）
        if (ImGui::BeginChild("##AssetList", ImVec2(0.0f, 200.0f), true))
        {
            for (size_t i = 0; i < assetPaths_.size(); ++i)
            {
                const std::string& p = assetPaths_[i];
                char label[320];
                std::snprintf(label, sizeof(label), "%s [%s]", p.c_str(), Core::AssetKindName(assetDb_.KindOf(p)));
                if (ImGui::Selectable(label, selectedAsset_ == static_cast<int>(i)))
                    selectedAsset_ = static_cast<int>(i);
            }
        }
        ImGui::EndChild();

        if (selectedAsset_ >= 0 && selectedAsset_ < static_cast<int>(assetPaths_.size()))
        {
            const std::string& sel = assetPaths_[static_cast<size_t>(selectedAsset_)];
            ImGui::TextWrapped("选中: %s", sel.c_str());
            if (const Core::Guid* g = assetDb_.GuidFor(sel); g != nullptr)
            {
                const std::string guidText = g->ToString();
                ImGui::Text("GUID: %s", guidText.c_str());
            }
            ImGui::Text("体积: %llu 字节", static_cast<unsigned long long>(assetDb_.SizeOf(sel)));

            const auto deps = assetDb_.DependenciesOf(sel);
            const auto dependents = assetDb_.DependentsOf(sel);
            ImGui::Text("依赖 %zu 项 / 被依赖 %zu 项", deps.size(), dependents.size());
            for (const std::string& d : deps)
                ImGui::BulletText("→ %s", d.c_str());
            for (const std::string& d : dependents)
                ImGui::BulletText("← %s", d.c_str());
        }

        if (!broken.empty())
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "断链引用（%zu）", broken.size());
            if (ImGui::BeginChild("##BrokenList", ImVec2(0.0f, 120.0f), true))
            {
                for (const Core::BrokenReference& b : broken)
                    ImGui::BulletText("%s → 缺失: %s", b.ownerPath.c_str(), b.referencePath.c_str());
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // ---------------- LOD / 探针 / 遮挡 ----------------
    void DrawCulling()
    {
        ImGui::SetNextWindowSize(ImVec2(460.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("LOD / 光照探针 / 遮挡剔除", &cullingOpen))
        {
            ImGui::End();
            return;
        }

        // ---- LOD ----
        if (ImGui::CollapsingHeader("LOD Group（多细节层次）", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("LOD Bias", &lodBias_, 0.01f, 10.0f, "%.2f");
            ImGui::SliderInt("最高档位上限", &lodMaxLevel_, -1, 7);
            ImGui::Checkbox("低于末档阈值即剔除", &lodCullBeyondLast_);
            ImGui::SliderFloat("档 0 阈值", &lodH0_, 0.02f, 1.0f, "%.2f");
            ImGui::SliderFloat("档 1 阈值", &lodH1_, 0.01f, 1.0f, "%.2f");
            ImGui::SliderFloat("档 2 阈值", &lodH2_, 0.005f, 1.0f, "%.2f");
            ImGui::SliderFloat("渐变宽度", &lodFade_, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("测试物体半径 (m)", &lodRadius_, 0.1f, 20.0f, "%.1f");

            // 用当前参数现搭一个 LOD 组，展示「距离 → 档位」的切换表
            Render::LodGroup g;
            g.SetLevels({{lodH0_, lodFade_}, {lodH1_, lodFade_}, {lodH2_, lodFade_}});
            g.SetBias(lodBias_);
            g.SetMaxLevel(lodMaxLevel_);
            g.SetCullBeyondLast(lodCullBeyondLast_);

            const float tanHalfFov = std::tan(glm::radians(lodFovDeg_ * 0.5f));
            const auto dists = g.DistanceThresholds(lodRadius_, tanHalfFov);
            ImGui::Text("档位切换距离（FOV %.0f°）:", lodFovDeg_);
            for (size_t i = 0; i < dists.size(); ++i)
                ImGui::BulletText("档 %zu：%.1f m 以外启用", i, dists[i]);
            if (!dists.empty())
            {
                const float near = dists[0] * 0.5f;
                const auto sel = g.Evaluate(Render::LodGroup::ScreenRelativeHeight(lodRadius_, near, tanHalfFov));
                ImGui::Text("示例：%.1f m 处 → 档 %d%s", near, sel.level,
                            sel.IsBlending() ? "（交叉渐变中）" : (sel.IsCulled() ? "（剔除）" : ""));
            }
        }

        // ---- 光照探针 ----
        if (ImGui::CollapsingHeader("光照探针（Light Probes）"))
        {
            ImGui::InputInt3("网格维度", probeDims_);
            ImGui::InputFloat3("原点", probeOrigin_);
            ImGui::SliderFloat("间距 (m)", &probeSpacing_, 0.5f, 16.0f, "%.1f");
            ImGui::SliderFloat("地面高度 Y", &probeGroundY_, -20.0f, 20.0f, "%.1f");
            ImGui::SliderInt("球面采样数", &probeSamples_, 32, 1024);
            ImGui::ColorEdit3("天光颜色×强度", skyColor_);
            ImGui::SliderFloat("天光强度", &skyIntensity_, 0.0f, 4.0f, "%.2f");
            ImGui::ColorEdit3("地面反弹色", groundColor_);
            ImGui::SliderFloat("地面强度", &groundIntensity_, 0.0f, 2.0f, "%.2f");
            if (ImGui::Button("烘焙探针"))
                probeBakeRequested = true;
            if (!probeStatus_.empty())
                ImGui::TextWrapped("%s", probeStatus_.c_str());
            if (probes_.ProbeCount() > 0)
            {
                const glm::vec3 up(0.0f, 1.0f, 0.0f);
                const glm::vec3 center = glm::vec3(probeOrigin_[0], probeOrigin_[1], probeOrigin_[2]) +
                                         glm::vec3(probeDims_[0], probeDims_[1], probeDims_[2]) * probeSpacing_ * 0.5f;
                const glm::vec3 lit = probes_.SampleIrradiance(center, up);
                ImGui::Text("体心朝上采样: (%.3f, %.3f, %.3f)", lit.r, lit.g, lit.b);
            }
        }

        // ---- 遮挡剔除 ----
        if (ImGui::CollapsingHeader("遮挡剔除（烘焙 PVS）"))
        {
            ImGui::InputInt3("网格维度", occDims_);
            ImGui::InputFloat3("原点", occOrigin_);
            ImGui::SliderFloat("格子边长 (m)", &occSpacing_, 0.5f, 16.0f, "%.1f");
            ImGui::SliderFloat("射线最远距离", &occMaxDistance_, 10.0f, 2000.0f, "%.0f");
            ImGui::SliderInt("起点采样数", &occOriginSamples_, 1, 9);
            ImGui::SliderInt("终点采样数", &occTargetSamples_, 1, 27);
            if (ImGui::Button("请求遮挡烘焙"))
                occlusionBakeRequested = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("烘焙需要场景包围盒，由主循环在帧末提供；\n"
                                  "烘焙是保守的：宁可多画，绝不错剔。");
            if (!occStatus_.empty())
                ImGui::TextWrapped("%s", occStatus_.c_str());
            if (occlusion_.IsBaked())
            {
                ImGui::Text("平均可见比例: %.1f%%", occlusion_.AverageVisibilityRatio() * 100.0);
                ImGui::Text("PVS 体积: %.1f KB", static_cast<double>(occlusion_.PvsBytes()) / 1024.0);
            }
        }
        ImGui::End();
    }

    // ---- 资产数据库 ----
    Core::AssetDatabase assetDb_;
    std::vector<std::string> assetPaths_;
    int selectedAsset_ = -1;
    std::string assetStatus_;
    char assetRoot_[260] =
#ifdef BIGHERO_SOURCE_ROOT
        BIGHERO_SOURCE_ROOT "/assets";
#else
        "assets";
#endif
    char indexPath_[260] = "asset_index.txt";

    // ---- LOD ----
  public:
    // 生产管线（Application::UpdateRenderables）逐帧直读以下 LOD 选档参数
    // （片1 冻结接口要求直接成员访问）。其余面板状态仍保持 private。
    float lodBias_ = 1.0f;
    int lodMaxLevel_ = -1;
    bool lodCullBeyondLast_ = true;
    float lodH0_ = 0.6f;
    float lodH1_ = 0.3f;
    float lodH2_ = 0.1f;
    float lodFade_ = 0.0f;
    float lodRadius_ = 2.0f;
    float lodFovDeg_ = 60.0f;

    // LightProbe 只读句柄：渲染管线（Application::UpdateUniforms）每帧按相机位置采样辐照度注入环境光。
    // 烘焙仍只经 BakeProbes()（本类内部），此处仅暴露只读视图，不开放写回。
    [[nodiscard]] const Render::LightProbeVolume& Probes() const { return probes_; }

  private:
    // ---- 光照探针 ----
    Render::LightProbeVolume probes_;
    int probeDims_[3] = {8, 6, 8};
    float probeOrigin_[3] = {-16.0f, 0.0f, -16.0f};
    float probeSpacing_ = 4.0f;
    float probeGroundY_ = 0.5f;
    int probeSamples_ = 256;
    float skyColor_[3] = {0.55f, 0.72f, 1.0f};
    float skyIntensity_ = 1.0f;
    float groundColor_[3] = {0.28f, 0.24f, 0.20f};
    float groundIntensity_ = 0.35f;
    std::string probeStatus_;

    // ---- 遮挡剔除 ----
    Render::OcclusionVolume occlusion_;
    int occDims_[3] = {16, 4, 16};
    float occOrigin_[3] = {-32.0f, 0.0f, -32.0f};
    float occSpacing_ = 4.0f;
    float occMaxDistance_ = 400.0f;
    int occOriginSamples_ = 9;
    int occTargetSamples_ = 9;
    std::string occStatus_;
};
} // namespace BigHero::Editor
