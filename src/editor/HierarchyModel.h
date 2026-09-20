#pragma once
// 层级树（Hierarchy）纯逻辑模型 —— Unity 对标清单 U1-E1。
// 与 Gizmo.h 同一约定：纯函数、不依赖 ImGui/Vulkan，可离线单元测试。
//
// 数据源是 SceneObject 包（编辑器兼容层）：parentIndex 为父物体在稳定序中的下标
// （-1=根；越界/自环等非法值一律视为根，与 EcsScene 语义一致）。
// 编辑器 HierarchyPanel 借此绘制树、做防环校验与搜索过滤；改父本身经
// EcsScene::SetParent（实体句柄权威存储）生效，包投影随之刷新。

#include "scene/Scene.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace BigHero::Editor::Hierarchy
{
// ---- 树结构：由包一次构建，children 按稳定序排列 ----
struct Tree
{
    std::vector<std::vector<int>> children; // children[i] = 第 i 个物体的子下标列表
    std::vector<int> roots;                 // parentIndex 非法（<0/越界/自环）的根入口
};

// 逐稳定序构建父子邻接表。非法 parentIndex（负值/越界/自环）视为根，
// 与 EcsScene::EnsureWorld / SliceScene 的容错语义一致。
[[nodiscard]] inline Tree BuildTree(const std::vector<Scene::SceneObject>& objs)
{
    Tree t;
    const int n = static_cast<int>(objs.size());
    t.children.assign(objs.size(), {});
    t.roots.reserve(objs.size());
    for (int i = 0; i < n; ++i)
    {
        const int32_t p = objs[static_cast<size_t>(i)].parentIndex;
        if (p >= 0 && p < n && p != i)
            t.children[static_cast<size_t>(p)].push_back(i);
        else
            t.roots.push_back(i);
    }
    return t;
}

// ---- 行显示名：网格类型中文名 + 稳定序下标（与"场景"面板命名一致） ----
// 人物组根（胶囊且子部件 >=3）追加"人物"标记，与"人物"面板呼应。
[[nodiscard]] inline std::string DisplayName(const std::vector<Scene::SceneObject>& objs, const Tree& tree, int i)
{
    if (i < 0 || static_cast<size_t>(i) >= objs.size())
        return "?";
    const Scene::SceneObject& obj = objs[static_cast<size_t>(i)];
    const char* kind = (obj.meshId == 0)   ? "立方体"
                       : (obj.meshId == 1) ? "圆环体"
                       : (obj.meshId == 2) ? "glTF 模型"
                       : (obj.meshId == 3) ? "球"
                                           : "胶囊";
    std::string name = std::string(kind) + " #" + std::to_string(i);
    // 人物组根启发式：胶囊根且挂了 >=3 个部件（人物 = body + head/双臂/双腿 5 子部件）
    if (obj.meshId == 4 && i < static_cast<int>(tree.children.size()) &&
        tree.children[static_cast<size_t>(i)].size() >= 3)
        name = "[人物] " + name;
    return name;
}

// 防环校验：把 dragged 拖到 target 行上（SetParent(dragged, target)）是否会成环。
// 规则：target 位于 dragged 的子树中（沿 parent 链上溯会到达 dragged）或就是 dragged 自身
// 时成环，返回 true。target 为 -1（挂到根）永不成环。
[[nodiscard]] inline bool WouldCreateCycle(const std::vector<Scene::SceneObject>& objs, int dragged, int target)
{
    const int n = static_cast<int>(objs.size());
    if (dragged < 0 || dragged >= n)
        return false; // 非法拖拽源交给上层忽略
    if (target < 0 || target >= n)
        return false; // 挂到根：无环
    if (target == dragged)
        return true; // 自己不能做自己的父
    // 从 target 沿父链上溯，遇到 dragged 说明 target 在 dragged 子树内
    int cur = target;
    int guard = 0; // 数据异常（成环包）时的防死循环兜底
    while (cur >= 0 && cur < n && guard <= n)
    {
        const int32_t p = objs[static_cast<size_t>(cur)].parentIndex;
        if (p == dragged)
            return true;
        if (p < 0 || p >= n || p == cur)
            break;
        cur = p;
        ++guard;
    }
    return false;
}

// ---- 搜索过滤：匹配名字的节点与其祖先链、子树均保持可见 ----
// 返回与 objs 等长的可见标志数组。filter 为空/全空白时全部可见。
// 匹配规则：大小写不敏感的子串匹配（仅 ASCII 折叠，中文逐字节比较）。
[[nodiscard]] inline std::vector<uint8_t> FilterVisible(const std::vector<Scene::SceneObject>& objs, const Tree& tree,
                                                        const char* filter)
{
    const size_t n = objs.size();
    if (filter == nullptr)
        return std::vector<uint8_t>(n, 1);

    // 折叠为小写后做子串匹配（避免依赖 locale 的大小写转换）
    std::string needle;
    for (const char* p = filter; *p != '\0'; ++p)
        needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    if (needle.find_first_not_of(" \t") == std::string::npos)
        return std::vector<uint8_t>(n, 1);

    std::vector<uint8_t> visible(n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const std::string name = DisplayName(objs, tree, static_cast<int>(i));
        std::string hay;
        hay.reserve(name.size());
        for (const char c : name)
            hay.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (hay.find(needle) == std::string::npos)
            continue;
        // 命中：本节点 + 沿父链的全部祖先 + 整棵子树保持可见（保留上下文）
        int cur = static_cast<int>(i);
        int guard = 0;
        while (cur >= 0 && static_cast<size_t>(cur) < n && guard <= static_cast<int>(n))
        {
            visible[static_cast<size_t>(cur)] = 1;
            const int32_t p = objs[static_cast<size_t>(cur)].parentIndex;
            cur = (p >= 0 && p < static_cast<int>(n) && p != cur) ? static_cast<int>(p) : -1;
            ++guard;
        }
        // 子树可见（脏包可能含环，visible 标志兼作已访问防重入）
        std::vector<int> pending(tree.children[i].begin(), tree.children[i].end());
        while (!pending.empty())
        {
            const int c = pending.back();
            pending.pop_back();
            if (c < 0 || static_cast<size_t>(c) >= n || visible[static_cast<size_t>(c)] != 0)
                continue;
            visible[static_cast<size_t>(c)] = 1;
            for (const int gc : tree.children[static_cast<size_t>(c)])
                pending.push_back(gc);
        }
    }
    return visible;
}
} // namespace BigHero::Editor::Hierarchy
