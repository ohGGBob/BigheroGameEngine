#pragma once
// 运行时 UI 系统第一增量（U1-UI）：纯逻辑核心（对标 uGUI 的最小可用版，游戏内 UI 而非编辑器 ImGui）。
// 与 Editor 的 InspectorModel/HierarchyModel 同一范式：纯函数、不依赖 Vulkan/ImGui，可离线单元测试。
//
// 职责划分：
//   - UiRect：锚点矩形（简化版 uGUI RectTransform）——单点锚+尺寸模式，拉伸锚 v2 同步支持；
//     SolveRect(parent) 为纯函数，把锚/偏移/pivot 解算为屏幕像素矩形（左上原点、y 向下）。
//   - UiCanvas：节点树（Canvas 根 = 屏幕；z 顺序 = 插入序，后插入者在上），SolveCanvas 递归解算。
//   - HitTest：命中测试——自顶向下找第一个"可见且阻断输入"的节点；interactable 决定是否成为
//     交互命中（对标 uGUI raycastTarget + ICanvasRaycastFilter 的简化）。
//   - UpdateInteraction：按钮状态机（hover/pressed/clicked）——按下锁定节点、原节点上松开触发
//     clicked 事件（uGUI PointerClick 语义）；输出 blocked 供 Application 让引擎拾取不穿透。
//   - MeasureTextWidth：文本测量纯函数（宽度 = 逐字 advance 和），advance 回调由字体图集/测试桩提供。
//
// GPU 侧见 UiFontAtlas（stb_truetype 动态字集图集）与 UiRenderer（单 draw call 批渲染）；
// 每帧接线见 UiRuntime。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Ui
{
using UiNodeId = int32_t;
inline constexpr UiNodeId kInvalidNode = -1;

// ---- 解算后的屏幕矩形（像素，左上原点，y 向下） ----
struct UiSolvedRect
{
    glm::vec2 pos{0.0f};  // 左上角
    glm::vec2 size{0.0f};

    [[nodiscard]] float Left() const noexcept { return pos.x; }
    [[nodiscard]] float Top() const noexcept { return pos.y; }
    [[nodiscard]] float Right() const noexcept { return pos.x + size.x; }
    [[nodiscard]] float Bottom() const noexcept { return pos.y + size.y; }
    [[nodiscard]] glm::vec2 Center() const noexcept { return pos + size * 0.5f; }
    [[nodiscard]] bool Contains(glm::vec2 p) const noexcept
    {
        return p.x >= pos.x && p.x < pos.x + size.x && p.y >= pos.y && p.y < pos.y + size.y;
    }
};

// ---- 锚点矩形（简化版 uGUI RectTransform） ----
// 坐标语义（屏幕像素，y 向下）：
//   - 单点锚（anchorMin == anchorMax，两轴各自判断）：
//       锚点 = 父矩形左上角 + anchor * 父尺寸；
//       锚定点 = 锚点 + offsetMin（对标 uGUI anchoredPosition）；
//       左上角 = 锚定点 - pivot * size；尺寸 = size（offsetMax 忽略）。
//   - 拉伸锚（该轴 anchorMin != anchorMax）：
//       左边 = 父左 + anchorMin.x * 父宽 + offsetMin.x；右边 = 父左 + anchorMax.x * 父宽 - offsetMax.x；
//       宽 = 右 - 左（size/pivot 该轴忽略——对标 uGUI stretch 模式，pivot 不影响解算矩形）。
struct UiRect
{
    glm::vec2 anchorMin{0.5f, 0.5f}; // 0-1，相对父矩形
    glm::vec2 anchorMax{0.5f, 0.5f}; // != anchorMin 时该轴为拉伸锚
    glm::vec2 offsetMin{0.0f, 0.0f}; // 单点锚 = 锚点到 pivot 的像素偏移；拉伸锚 = 左/上外扩
    glm::vec2 offsetMax{0.0f, 0.0f}; // 拉伸锚 = 右/下内缩（正值内缩）
    glm::vec2 size{100.0f, 100.0f};  // 单点锚模式的尺寸
    glm::vec2 pivot{0.5f, 0.5f};     // 0-1，锚定点在矩形内的位置

    // 便捷构造：单点锚 + 尺寸 + pivot + 像素偏移
    [[nodiscard]] static UiRect Anchored(glm::vec2 anchor, glm::vec2 sz, glm::vec2 piv,
                                         glm::vec2 offset = glm::vec2(0.0f))
    {
        UiRect r;
        r.anchorMin = anchor;
        r.anchorMax = anchor;
        r.size = sz;
        r.pivot = piv;
        r.offsetMin = offset;
        return r;
    }

    [[nodiscard]] bool IsStretchedX() const noexcept { return anchorMin.x != anchorMax.x; }
    [[nodiscard]] bool IsStretchedY() const noexcept { return anchorMin.y != anchorMax.y; }

    // 纯函数解算：parent 为父节点已解算矩形（Canvas 根节点传屏幕矩形）
    [[nodiscard]] UiSolvedRect Solve(const UiSolvedRect& parent) const
    {
        const glm::vec2 anchorMinPt = parent.pos + anchorMin * parent.size;
        const glm::vec2 anchorMaxPt = parent.pos + anchorMax * parent.size;
        UiSolvedRect out;
        // X 轴
        if (IsStretchedX())
        {
            out.pos.x = anchorMinPt.x + offsetMin.x;
            out.size.x = (anchorMaxPt.x - offsetMax.x) - out.pos.x;
        }
        else
        {
            out.pos.x = anchorMinPt.x + offsetMin.x - pivot.x * size.x;
            out.size.x = size.x;
        }
        // Y 轴
        if (IsStretchedY())
        {
            out.pos.y = anchorMinPt.y + offsetMin.y;
            out.size.y = (anchorMaxPt.y - offsetMax.y) - out.pos.y;
        }
        else
        {
            out.pos.y = anchorMinPt.y + offsetMin.y - pivot.y * size.y;
            out.size.y = size.y;
        }
        return out;
    }
};

// ---- 控件种类 ----
enum class UiKind : uint8_t
{
    Panel,  // 填充矩形（可带圆角）
    Text,   // 文本（fontSize/textColor，图集渲染）
    Button  // 按钮（Panel 外观 + 中心标签 + 状态机）
};

// ---- 控件数据 ----
struct UiNode
{
    UiKind kind = UiKind::Panel;
    UiNodeId id = kInvalidNode;      // 交互回调用的稳定 id（应用层自定，如演示按钮 1/2）
    UiRect rect;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // Panel/Button 填充色（0-1 RGBA）
    float cornerRadius = 0.0f;       // 圆角半径（像素，0=直角）
    uint32_t textureId = 0;          // 纹理引用（0=无，第一增量预留字段，图集白像素直出）
    std::string text;                // Text 内容 / Button 标签（UTF-8）
    float fontSize = 16.0f;          // 字号（像素）
    glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool textCentered = false;       // 文本/标签在矩形内水平居中
    bool interactable = true;        // 可交互（参与按钮状态机 / 可成为命中节点）
    bool visible = true;             // 隐藏节点不参与解算绘制与命中
    bool raycastBlock = true;        // 命中时是否阻断引擎拾取（装饰元素可关，点击穿透）
};

// ---- 画布节点树：Canvas → 子节点；z 顺序 = 插入序（下标大者在上）----
// 约束：父节点下标必须小于子节点下标（Add 不重排，Solve 单趟递推即可）。
struct UiCanvas
{
    std::vector<UiNode> nodes;
    std::vector<UiNodeId> parent; // 与 nodes 平行；kInvalidNode = 根（相对屏幕）

    [[nodiscard]] size_t Count() const noexcept { return nodes.size(); }

    UiNodeId Add(UiNodeId parentIdx, const UiNode& node)
    {
        const auto idx = static_cast<UiNodeId>(nodes.size());
        nodes.push_back(node);
        parent.push_back(parentIdx);
        return idx;
    }
};

// 递归解算全部矩形：根节点相对屏幕（0,0,W,H），子节点相对父节点解算矩形。
// outRects 与 canvas.nodes 平行（重建容量复用）。不可见节点仍解算（命中/绘制各自跳过），
// 保证下标对齐恒成立。
inline void SolveCanvas(const UiCanvas& canvas, float screenW, float screenH, std::vector<UiSolvedRect>& outRects)
{
    outRects.resize(canvas.nodes.size());
    const UiSolvedRect screen{glm::vec2(0.0f), glm::vec2(screenW, screenH)};
    for (size_t i = 0; i < canvas.nodes.size(); ++i)
    {
        const UiNodeId p = canvas.parent[i];
        // 约束：父下标 < 子下标，故 parent 的解算结果已就绪；越界/自引用防御为屏幕解算
        const UiSolvedRect& parentRect =
            (p >= 0 && static_cast<size_t>(p) < i) ? outRects[static_cast<size_t>(p)] : screen;
        outRects[i] = canvas.nodes[i].rect.Solve(parentRect);
    }
}

// ---- 命中测试 ----
// 自顶向下（插入序末尾 → 开头）扫描：
//   - 不可见节点跳过；不含鼠标点的节点跳过；
//   - 第一个含点且 raycastBlock=true 的节点即命中表面：interactable 则成为交互命中，
//     否则点击被装饰层截留（blocked=true 但 node=无效）——此后不再向下层穿透；
//   - raycastBlock=false 的节点对输入透明（点击穿透到下层/场景）。
struct UiHitResult
{
    UiNodeId node = kInvalidNode; // 最顶可交互节点（kInvalidNode=无）
    bool blocked = false;         // 命中了阻断节点（点击不可穿透到引擎场景）
};
inline UiHitResult HitTest(const UiCanvas& canvas, const std::vector<UiSolvedRect>& rects, glm::vec2 screenPos)
{
    UiHitResult hit;
    if (rects.size() != canvas.nodes.size())
        return hit; // 解算结果与树不同步（未 SolveCanvas）防御
    for (size_t step = canvas.nodes.size(); step-- > 0;)
    {
        const UiNode& n = canvas.nodes[step];
        if (!n.visible || !rects[step].Contains(screenPos))
            continue;
        if (!n.raycastBlock)
            continue; // 透明装饰：继续向下找
        hit.blocked = true;
        hit.node = n.interactable ? static_cast<UiNodeId>(step) : kInvalidNode;
        break;
    }
    return hit;
}

// ---- 按钮状态机 ----
// uGUI 语义简化版：
//   - hover：鼠标位于按钮矩形内（可交互）；
//   - pressed：按下边沿（false→true）时鼠标在该按钮上则锁定该节点，按住期间保持 pressed
//     （鼠标移出矩形不取消锁定，移回原节点松开仍触发——与 uGUI 一致）；
//   - clicked：松开边沿（true→false）时鼠标仍在按下锁定的按钮上 → 触发一次 clicked 事件。
struct UiButtonRuntime
{
    bool hover = false;
    bool pressed = false;
};

struct UiEvent
{
    UiNodeId node = kInvalidNode; // 按钮节点下标
    UiNodeId id = kInvalidNode;   // 节点业务 id（UiNode::id）
};

// 跨帧交互状态（调用方持久持有；首帧 hasState=false 不产生边沿）
struct UiInteractState
{
    std::vector<UiButtonRuntime> buttons; // 与 canvas.nodes 平行（非按钮节点占位）
    UiNodeId pressedNode = kInvalidNode;  // 按下锁定（uGUI pointerPress）
    bool leftDown = false;
    bool hasState = false;                // 首帧校准：无上一帧状态时不算按下/松开边沿
};

struct UiInteractOutput
{
    UiInteractState state;        // 下一帧的 prev
    std::vector<UiEvent> clicked; // 本帧触发的点击事件
    UiNodeId hovered = kInvalidNode;
    bool blocked = false;         // UI 吞掉鼠标（Application 据此让引擎拾取/相机不响应）
};

inline UiInteractOutput UpdateInteraction(const UiCanvas& canvas, const std::vector<UiSolvedRect>& rects,
                                          glm::vec2 mouse, bool leftDown, const UiInteractState& prev)
{
    UiInteractOutput out;
    out.state = prev;
    if (out.state.buttons.size() != canvas.nodes.size())
    {
        out.state.buttons.clear();
        out.state.buttons.resize(canvas.nodes.size());
        out.state.pressedNode = kInvalidNode;
        out.state.hasState = false;
    }
    out.state.buttons.resize(canvas.nodes.size());
    for (UiButtonRuntime& b : out.state.buttons)
        b.hover = false;

    const UiHitResult hit = HitTest(canvas, rects, mouse);
    out.blocked = hit.blocked;
    out.hovered = hit.node;
    const bool nodeIsButton = hit.node != kInvalidNode && canvas.nodes[static_cast<size_t>(hit.node)].kind == UiKind::Button
                              && canvas.nodes[static_cast<size_t>(hit.node)].interactable;
    if (nodeIsButton)
        out.state.buttons[static_cast<size_t>(hit.node)].hover = true;

    // 按下边沿：首帧（prev.hasState=false）就带 leftDown 视为合法按下——首帧校准只用于
    // 抑制"松开"边沿误触发（进程启动前无按下历史），按下本身是当前帧事实。
    const bool pressedEdge = leftDown && !prev.leftDown;
    const bool releaseEdge = !leftDown && prev.leftDown && prev.hasState;

    if (pressedEdge && nodeIsButton)
        out.state.pressedNode = hit.node;
    if (releaseEdge)
    {
        // 松开：仍在按下锁定的按钮上 → 触发点击（uGUI PointerClick）
        if (prev.pressedNode != kInvalidNode && prev.pressedNode == hit.node)
        {
            const UiNode& n = canvas.nodes[static_cast<size_t>(prev.pressedNode)];
            out.clicked.push_back({prev.pressedNode, n.id});
        }
        out.state.pressedNode = kInvalidNode;
    }
    // pressed 视觉态：按住期间锁定节点保持（含移出矩形再移回）
    for (size_t i = 0; i < out.state.buttons.size(); ++i)
        out.state.buttons[i].pressed = (out.state.pressedNode == static_cast<UiNodeId>(i)) && leftDown;

    out.state.leftDown = leftDown;
    out.state.hasState = true;
    return out;
}

// ---- 文本测量（纯函数） ----
// UTF-8 解码为码点序列；非法序列以 U+FFFD 回退（保持推进，不抛异常）。
inline void Utf8Decode(std::string_view text, std::vector<uint32_t>& out)
{
    out.clear();
    constexpr uint32_t kReplacement = 0xFFFDu;
    size_t i = 0;
    while (i < text.size())
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (c < 0x80)
        {
            cp = c;
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1Fu;
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0Fu;
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07u;
            len = 4;
        }
        else
        {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        if (i + len > text.size())
        {
            out.push_back(kReplacement); // 截断的多字节序列
            break;
        }
        bool valid = true;
        for (size_t k = 1; k < len; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!valid)
        {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        // 过约束/代理区/超 Unicode 范围检查（UTF-8 严格性）：len=3 的合法下界是 U+0800
        // （三字节编码恰好覆盖 U+0800–U+FFFF，全部 < 0x10000——边界写 0x10000 会把
        // 所有合法三字节判成 overlong）
        const bool overlong = (len == 1 && cp >= 0x80) || (len == 2 && cp < 0x800) || (len == 3 && cp < 0x800);
        const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
        if (overlong || surrogate || cp > 0x10FFFF)
            out.push_back(kReplacement);
        else
            out.push_back(cp);
        i += len;
    }
}

// 宽度 = 逐字 advance 之和。advanceOf(codepoint, fontSize) 由字体图集提供（未加载字体时
// 测试可注入假度量表）；换行符宽度为 0（第一增量单行文本，\n 直接计 0）。
inline float MeasureTextWidth(std::string_view utf8, float fontSize,
                              const std::function<float(uint32_t, float)>& advanceOf)
{
    std::vector<uint32_t> cps;
    Utf8Decode(utf8, cps);
    float width = 0.0f;
    for (const uint32_t cp : cps)
    {
        if (cp == '\n')
            continue;
        width += advanceOf ? advanceOf(cp, fontSize) : 0.0f;
    }
    return width;
}
} // namespace BigHero::Ui
