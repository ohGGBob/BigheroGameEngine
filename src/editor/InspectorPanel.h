#pragma once
// Inspector 属性面板（绘制器）—— Unity 对标清单 U1-E2。
// 遍历 InspectorModel 的 ComponentMeta 元数据绘制控件
// （DragFloat/DragFloat3/SliderFloat/SliderInt/ColorEdit3/Combo/Checkbox/InputText），
// 不再逐字段手写：新增组件/脚本字段（U1-S1d）只需注册元数据即可上屏。
//
// 撤销语义不变：本面板只改数据不记快照——Application 的 HandlePropertyEditUndo 以
// ImGui::IsAnyItemActive 边沿抓"手势起始快照 vs 松手快照"，经 SceneSnapshotCommand
// 一次完整拖拽/输入 = 一个撤销步（与旧手写控件同一条路径）。
// 物理属性（rebuildPhysics 标记）变更经 physicsRebuildRequested 上抛，由 EditorPanel 转发。

#include "editor/InspectorModel.h"
#include "imgui.h"

#include <cstddef>

namespace BigHero::Editor
{
class InspectorPanel
{
  public:
    // ---- 请求标志（EditorPanel 转发给 Application，消费后重置；与 panel 物理重建同模式） ----
    bool physicsRebuildRequested = false; // 元数据标记 rebuildPhysics 的属性被修改

    // 绘制一个物体的属性行（元数据驱动；字段顺序/控件/范围与旧手写面板逐字等价）
    void DrawObject(Scene::SceneObject& obj)
    {
        Inspector::EnsureBuiltins();
        const Inspector::ComponentMeta* meta =
            Inspector::MetaRegistry::Global().Find(Inspector::kSceneObjectComponent);
        if (meta == nullptr)
            return;

        const char* lastGroup = nullptr;
        for (const Inspector::PropertyDesc& d : meta->properties)
        {
            // 条件可见（物理子面板依赖刚体类型）；可见性判定先于分组标题，避免"空组"孤立标题
            if (d.visibleIf != nullptr && !d.visibleIf(&obj))
                continue;
            // 分组标题：首个该组成员前画分隔线 + 标题（组名为静态字面量，指针比较安全）
            if (d.group != nullptr && d.group != lastGroup)
            {
                ImGui::Separator();
                ImGui::TextUnformatted(d.group);
            }
            if (d.group != nullptr)
                lastGroup = d.group;
            DrawProperty(d, &obj);
        }
    }

  private:
    // 属性修改上抛：物理属性变更需重建刚体
    void NotifyChanged(const Inspector::PropertyDesc& d)
    {
        if (d.rebuildPhysics)
            physicsRebuildRequested = true;
    }

    // 按类型/存储/提示分派控件（控件调用形态与旧手写面板逐字一致）
    void DrawProperty(const Inspector::PropertyDesc& d, void* base)
    {
        using Inspector::PropType;
        switch (d.type)
        {
        case PropType::Float:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            float v = cur.f[0];
            const float speed = d.step > 0.0f ? d.step : 0.02f;
            // min==max 时 ImGui 不启用 clamp，恰好覆盖"无范围"情形
            const float lo = d.hasRange ? d.minV : 0.0f;
            const float hi = d.hasRange ? d.maxV : 0.0f;
            const bool changed =
                (d.uiHint == Inspector::UiHint::Slider && d.hasRange)
                    ? ImGui::SliderFloat(d.label, &v, lo, hi, d.format)
                    : ImGui::DragFloat(d.label, &v, speed, lo, hi, d.format);
            if (changed)
            {
                Inspector::WriteValue(d, base, Inspector::FloatValue(v));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::Int:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            int v = static_cast<int>(cur.i);
            const int lo = d.hasRange ? static_cast<int>(d.minV) : 0;
            const int hi = d.hasRange ? static_cast<int>(d.maxV) : 0;
            const bool changed =
                (d.uiHint == Inspector::UiHint::Slider && d.hasRange)
                    ? ImGui::SliderInt(d.label, &v, lo, hi, d.format)
                    : ImGui::DragInt(d.label, &v, d.step > 0.0f ? d.step : 1.0f, lo, hi, d.format);
            if (changed)
            {
                Inspector::WriteValue(d, base, Inspector::IntValue(v));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::Bool:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            bool v = cur.b;
            if (ImGui::Checkbox(d.label, &v))
            {
                Inspector::WriteValue(d, base, Inspector::BoolValue(v));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::Vec3:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            float v[3] = {cur.f[0], cur.f[1], cur.f[2]};
            const float speed = d.step > 0.0f ? d.step : 0.02f;
            const float lo = d.hasRange ? d.minV : 0.0f;
            const float hi = d.hasRange ? d.maxV : 0.0f;
            if (ImGui::DragFloat3(d.label, v, speed, lo, hi, d.format))
            {
                Inspector::WriteValue(d, base, Inspector::Vec3Value(glm::vec3(v[0], v[1], v[2])));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::Color:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            float v[3] = {cur.f[0], cur.f[1], cur.f[2]};
            if (ImGui::ColorEdit3(d.label, v))
            {
                Inspector::WriteValue(d, base, Inspector::Vec3Value(glm::vec3(v[0], v[1], v[2])));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::Enum:
        {
            const Inspector::PropValue cur = Inspector::ReadValue(d, base);
            int idx = static_cast<int>(cur.i);
            if (d.enumItems != nullptr && d.enumCount > 0 && ImGui::Combo(d.label, &idx, d.enumItems, d.enumCount))
            {
                Inspector::WriteValue(d, base, Inspector::IntValue(idx));
                NotifyChanged(d);
            }
            break;
        }
        case PropType::String:
        {
            // 字符串直改组件内的 char 缓冲（InputText 原地写回）
            char* buf = static_cast<char*>(Inspector::ResolvePtr(d, base));
            const int cap = d.strCapacity > 0 ? d.strCapacity : 1;
            if (ImGui::InputText(d.label, buf, static_cast<size_t>(cap)))
                NotifyChanged(d);
            break;
        }
        }
    }
};
} // namespace BigHero::Editor
