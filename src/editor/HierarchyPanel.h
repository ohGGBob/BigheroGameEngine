#pragma once
// 层级树（Hierarchy）窗口 —— Unity 对标清单 U1-E1。
// 以根实体为入口按 parentIndex 递归绘制 ImGui TreeNode 树：
//   - 点击行 = 选中该实体（请求经 EditorPanel.hierarchy 交回 Application，接 Gizmo 高亮）；
//   - 拖拽行到另一行 = 改父（EcsScene::SetParent，经命令栈入撤销，防环校验拒绝成环拖拽）；
//   - 拖拽到底部"挂到根"区 = 清除父级；删除复用现有"删除选中"路径（子节点提升为根）；
//   - 顶部搜索框按名字过滤，命中节点与其祖先链保持可见。
// 纯 ImGui 覆盖层面板：只产请求标志，不直接写场景数据（与增删物体请求同模式）。

#include "editor/HierarchyModel.h"
#include "imgui.h"
#include "scene/Scene.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace BigHero::Editor
{
class HierarchyPanel
{
  public:
    // ---- 请求标志（Application 消费后重置；与 addObjectRequested 同模式） ----
    bool selectRequested = false;   // 点击行请求选中
    int selectedIndex = -1;         // selectRequested 携带的目标下标
    bool reparentRequested = false; // 拖拽放下请求改父
    int reparentChild = -1;         // 被拖拽实体（稳定序下标）
    int reparentParent = -1;        // 新父（-1 = 挂到根）

    bool deleteRequested = false; // 面板"删除选中"按钮（复用现有删除路径）

    // pos/size 由调用方（EditorPanel）经 DockLayout::Place 解析，本面板不依赖布局类型
    void Draw(std::vector<Scene::SceneObject>& scene, int selectedObject, ImVec2 winPos, ImVec2 winSize)
    {
        ImGui::SetNextWindowPos(winPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(winSize, ImGuiCond_FirstUseEver);
        ImGui::Begin("层级树");

        // ---- 搜索过滤 ----
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##hierFilter", "按名称过滤...", filter_, sizeof(filter_));
        const bool filtering = filter_[0] != '\0';

        const Hierarchy::Tree tree = Hierarchy::BuildTree(scene);
        const std::vector<uint8_t> visible = Hierarchy::FilterVisible(scene, tree, filter_);

        // ---- 树形递归绘制 ----
        ImGui::BeginChild("##hierTree", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f));
        for (const int root : tree.roots)
            DrawNode(scene, tree, visible, root, selectedObject, filtering, 0);
        if (!AnyVisible(visible))
            ImGui::TextDisabled("（无匹配项）");

        // ---- 拖到空白处 = 清除父级（挂到根） ----
        ImGui::Spacing();
        ImGui::TextDisabled("拖到此处 = 挂到根");
        const float dropH = ImGui::GetContentRegionAvail().y;
        if (dropH > 12.0f)
        {
            ImGui::InvisibleButton("##hierRootDrop", ImVec2(-1.0f, dropH));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(kDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    int child = -1;
                    if (payload->DataSize == sizeof(int))
                        child = *static_cast<const int*>(payload->Data);
                    RequestReparent(scene, child, -1);
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::EndChild();

        // ---- 底部操作与提示 ----
        if (ImGui::Button("删除选中") && selectedObject >= 0 && selectedObject < static_cast<int>(scene.size()))
            deleteRequested = true;
        ImGui::SameLine();
        ImGui::TextDisabled("删除父物体时子节点提升为根");

        // 防环/无效拖拽的瞬时提示（数帧后淡出）
        if (statusFrames_ > 0)
        {
            --statusFrames_;
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", status_);
        }

        ImGui::End();
    }

  private:
    static constexpr const char* kDragPayload = "BH_HIER_NODE"; // 拖拽载荷类型

    char filter_[128] = "";
    char status_[256] = "";
    int statusFrames_ = 0;

    [[nodiscard]] bool AnyVisible(const std::vector<uint8_t>& visible) const
    {
        for (const uint8_t v : visible)
            if (v)
                return true;
        return false;
    }

    void SetStatus(const char* msg)
    {
        snprintf(status_, sizeof(status_), "%s", msg);
        statusFrames_ = 180; // 约 3 秒
    }

    // 请求改父：防环校验 + 与当前状态去重后才发请求（成环/无效在 UI 层直接拒绝并提示）
    void RequestReparent(const std::vector<Scene::SceneObject>& scene, int child, int parent)
    {
        if (child < 0 || child >= static_cast<int>(scene.size()))
            return;
        if (parent >= static_cast<int>(scene.size()))
            return;
        if (parent == child)
        {
            SetStatus("不能把实体拖到自己身上");
            return;
        }
        if (Hierarchy::WouldCreateCycle(scene, child, parent))
        {
            SetStatus("已忽略：不能把实体拖入自己的子树（会成环）");
            return;
        }
        if (scene[static_cast<size_t>(child)].parentIndex == parent)
            return; // 父级未变化，不产生空命令
        reparentChild = child;
        reparentParent = parent;
        reparentRequested = true;
    }

    // 递归绘制单个节点及其子树（缩进层级 = 树深度；过滤时仅绘制可见节点并自动展开）
    void DrawNode(const std::vector<Scene::SceneObject>& scene, const Hierarchy::Tree& tree,
                  const std::vector<uint8_t>& visible, int idx, int selectedObject, bool filtering, int depth)
    {
        if (idx < 0 || idx >= static_cast<int>(scene.size()) || !visible[static_cast<size_t>(idx)])
            return;

        const std::string name = Hierarchy::DisplayName(scene, tree, idx);
        char id[48];
        snprintf(id, sizeof(id), "##hier%d", idx);

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (tree.children[static_cast<size_t>(idx)].empty())
            flags |= ImGuiTreeNodeFlags_Leaf; // 无子节点：不显示展开箭头
        if (idx == selectedObject)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (depth == 0 || filtering)
            flags |= ImGuiTreeNodeFlags_DefaultOpen; // 根层默认展开；过滤时保留祖先链展开

        const bool opened = ImGui::TreeNodeEx(id, flags, "%s", name.c_str());

        // 点击行 = 选中（点箭头折叠不算选中）
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        {
            selectedIndex = idx;
            selectRequested = true;
        }

        // 拖拽源：把本行实体拖出
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kDragPayload, &idx, sizeof(int));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        // 放置目标：把别的实体拖到本行 = 改父
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload))
            {
                int child = -1;
                if (payload->DataSize == sizeof(int))
                    child = *static_cast<const int*>(payload->Data);
                RequestReparent(scene, child, idx);
            }
            ImGui::EndDragDropTarget();
        }

        if (opened)
        {
            for (const int child : tree.children[static_cast<size_t>(idx)])
                DrawNode(scene, tree, visible, child, selectedObject, filtering, depth + 1);
            ImGui::TreePop();
        }
    }
};
} // namespace BigHero::Editor
