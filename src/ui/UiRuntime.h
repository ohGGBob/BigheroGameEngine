#pragma once
// 运行时 UI 系统第一增量：每帧拼装层（UiModel 纯逻辑 + UiFontAtlas 字集图集 + UiRenderer 批渲染）。
// 职责：
//   - Update：SolveCanvas → UpdateInteraction（按钮状态机/命中遮挡）→ 顶点展开（面板圆角/文本
//     字形/按钮态配色）；返回本帧点击事件与 blocked（Application 据此让引擎拾取不穿透）。
//   - Record：顶点直写本帧槽位 + 单 draw call 渲染通道录制（场景之后、编辑器 ImGui 之前）。
//   - BuildDemoCanvas：--ui-demo 演示画布（半透明面板 + 标题 + 实时统计 + "生成方块/清空"两按钮）。
// 持 GPU 资源（图集/渲染器）：Application 中须声明于 ctx_/renderer_ 之后，析构逆序释放。

#include "ui/UiFontAtlas.h"
#include "ui/UiModel.h"
#include "ui/UiRenderer.h"

#include <functional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
class Swapchain;

namespace Ui
{
class UiRuntime
{
  public:
    // 演示按钮业务 id（--ui-demo 画布）
    static constexpr UiNodeId kDemoBtnSpawn = 1;
    static constexpr UiNodeId kDemoBtnClear = 2;

    UiRuntime() = default;
    ~UiRuntime() = default;
    UiRuntime(const UiRuntime&) = delete;
    UiRuntime& operator=(const UiRuntime&) = delete;

    // enabled=false（headless / --no-ui）时整系统惰性：Update/Record 均为 no-op
    void Init(const Context& ctx, const Swapchain& swapchain, bool enabled);
    void Shutdown();

    // 加载字体（TTF/TTC；失败优雅降级为无文本）。画布含文本时必须先于 BuildDemoCanvas 调用
    void LoadFont(const std::string& ttfPath);
    [[nodiscard]] bool FontLoaded() const noexcept { return atlas_.FontLoaded(); }

    // 管线创建（依赖 Init 的渲染通道；外部在着色器生成后调用一次）
    void CreatePipeline(VkDevice dev);
    // 交换链重建：帧缓冲重建
    void OnSwapchainRecreated(const Swapchain& swapchain);

    // --ui-demo 演示画布（可重复调用重建画布）
    void BuildDemoCanvas();
    // 演示实时文本（如实体统计行）；节点不存在时忽略
    void SetDemoStatsText(const std::string& text);

    // 点击回调（Update 内同步派发；Application 接管场景操作）
    void SetClickHandler(std::function<void(const UiEvent&)> fn) { onClick_ = std::move(fn); }

    // 每帧更新（输入喂入 → 解算 → 命中/状态机 → 顶点展开）。返回点击事件与 blocked 标志
    struct FrameEvents
    {
        std::vector<UiEvent> clicked;
        bool blocked = false; // 本帧鼠标命中 UI（点击不穿透）
    };
    FrameEvents Update(const Context& ctx, glm::vec2 mousePx, bool leftDown, float screenW, float screenH);

    // 录制 UI 渲染通道（RecordUi 内、editorOverlay_.Render 之前调用）
    void Record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);

    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool Blocked() const noexcept { return lastEvents_.blocked; } // 上一 Update 结果
    [[nodiscard]] size_t VertexCount() const noexcept { return vertices_.size(); }

  private:
    void BuildVertices(const Context& ctx);
    // 单个四边形（2 三角形）展开；radius>0 时写入 local/rect 供片元 SDF 圆角裁剪
    void PushQuad(glm::vec2 posPx, glm::vec2 sizePx, glm::vec4 color, glm::vec2 uvMin, glm::vec2 uvMax, float radius);
    // 字形四边形（radius=0，local=0 → 片元全覆盖分支）
    void PushGlyph(glm::vec2 posPx, glm::vec2 sizePx, glm::vec4 color, glm::vec2 uvMin, glm::vec2 uvMax);
    // 文本串展开（topLeftPx 为文本框左上角；返回整串像素宽度）
    float PushText(const Context& ctx, std::string_view utf8, float fontSize, glm::vec4 color, glm::vec2 topLeftPx,
                   bool centered, float boxWidth);
    void DrawNode(const Context& ctx, const UiNode& node, const UiSolvedRect& rect, const UiButtonRuntime* state);

    bool enabled_ = false;
    const Context* ctx_ = nullptr; // Update/Record 阶段的 Context 引用（Init 捕获）
    UiCanvas canvas_;
    UiInteractState interact_;
    std::vector<UiSolvedRect> solved_;
    UiFontAtlas atlas_;
    UiRenderer renderer_;
    std::vector<UiVertex> vertices_;
    FrameEvents lastEvents_;
    std::function<void(const UiEvent&)> onClick_;

    // 演示画布节点句柄（SetDemoStatsText 用）
    UiNodeId statsNode_ = kInvalidNode;
};
} // namespace Ui
} // namespace BigHero
