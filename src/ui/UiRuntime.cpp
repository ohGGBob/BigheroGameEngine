// 运行时 UI 每帧拼装层实现：解算/交互/顶点展开（Update）与单 draw call 录制（Record）。
#include "ui/UiRuntime.h"

#include "core/Log.h"
#include "render/Context.h"
#include "render/Swapchain.h"

#include <algorithm>
#include <cmath>

namespace BigHero::Ui
{
namespace
{
// 按钮态配色：基色 × 悬停提亮 / 按下压暗（uGUI ColorTint 语义简化）
glm::vec4 TintButtonColor(const glm::vec4& base, const UiButtonRuntime* state)
{
    if (state == nullptr)
        return base;
    float factor = 1.0f;
    if (state->pressed)
        factor = 0.72f;
    else if (state->hover)
        factor = 1.28f;
    return glm::vec4(glm::clamp(glm::vec3(base) * factor, 0.0f, 1.0f), base.a);
}
} // namespace

void UiRuntime::Init(const Context& ctx, const Swapchain& swapchain, bool enabled)
{
    Shutdown();
    enabled_ = enabled;
    if (!enabled_)
        return;
    ctx_ = &ctx;
    atlas_.Init(ctx);
    renderer_.Init(ctx, swapchain);
    LOG_INFO("运行时 UI 系统初始化完成（图集 " << UiFontAtlas::kInitialSize << "² 起步）");
}

void UiRuntime::Shutdown()
{
    renderer_.Destroy();
    atlas_.Destroy();
    canvas_.nodes.clear();
    canvas_.parent.clear();
    solved_.clear();
    vertices_.clear();
    statsNode_ = kInvalidNode;
    ctx_ = nullptr;
    enabled_ = false;
    lastEvents_ = {};
}

void UiRuntime::LoadFont(const std::string& ttfPath)
{
    if (!enabled_)
        return;
    atlas_.LoadTtf(ttfPath);
}

void UiRuntime::CreatePipeline(VkDevice dev)
{
    if (!enabled_)
        return;
    renderer_.CreatePipeline(dev, atlas_.Layout(), atlas_.Set());
}

void UiRuntime::OnSwapchainRecreated(const Swapchain& swapchain)
{
    if (!enabled_)
        return;
    renderer_.OnSwapchainRecreated(swapchain);
}

void UiRuntime::BuildDemoCanvas()
{
    if (!enabled_)
        return;
    // 面板：顶部水平居中（anchor (0.5,0) + pivot (0.5,0)），半透明深色 + 圆角
    UiNode panel;
    panel.kind = UiKind::Panel;
    panel.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(360.0f, 236.0f), glm::vec2(0.5f, 0.0f),
                                  glm::vec2(0.0f, 28.0f));
    panel.color = glm::vec4(0.07f, 0.08f, 0.11f, 0.85f);
    panel.cornerRadius = 12.0f;
    panel.interactable = true; // 面板本身截留点击（其上拖拽不穿透到场景）
    canvas_.nodes.clear();
    canvas_.parent.clear();
    solved_.clear();
    statsNode_ = kInvalidNode;
    const UiNodeId panelNode = canvas_.Add(kInvalidNode, panel);

    // 标题文本（面板内顶部居中）
    UiNode title;
    title.kind = UiKind::Text;
    title.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(320.0f, 34.0f), glm::vec2(0.5f, 0.0f),
                                  glm::vec2(0.0f, 14.0f));
    title.text = "BigHero 运行时 UI";
    title.fontSize = 22.0f;
    title.textColor = glm::vec4(0.95f, 0.96f, 1.0f, 1.0f);
    title.textCentered = true;
    title.raycastBlock = false; // 纯展示：点击穿透到面板层
    canvas_.Add(panelNode, title);

    // 实时统计行（Application 每帧 SetDemoStatsText）
    UiNode stats;
    stats.kind = UiKind::Text;
    stats.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(320.0f, 24.0f), glm::vec2(0.5f, 0.0f),
                                  glm::vec2(0.0f, 52.0f));
    stats.text = "实体数: -";
    stats.fontSize = 15.0f;
    stats.textColor = glm::vec4(0.72f, 0.76f, 0.82f, 1.0f);
    stats.textCentered = true;
    stats.raycastBlock = false;
    statsNode_ = canvas_.Add(panelNode, stats);

    // 按钮 1：生成方块（点击走编辑器"添加物体"同一路径）
    UiNode spawn;
    spawn.kind = UiKind::Button;
    spawn.id = kDemoBtnSpawn;
    spawn.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(300.0f, 44.0f), glm::vec2(0.5f, 0.0f),
                                  glm::vec2(0.0f, 92.0f));
    spawn.color = glm::vec4(0.16f, 0.45f, 0.86f, 1.0f);
    spawn.cornerRadius = 8.0f;
    spawn.text = "生成方块";
    spawn.fontSize = 18.0f;
    spawn.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    spawn.textCentered = true;
    canvas_.Add(panelNode, spawn);

    // 按钮 2：清空场景
    UiNode clear;
    clear.kind = UiKind::Button;
    clear.id = kDemoBtnClear;
    clear.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(300.0f, 44.0f), glm::vec2(0.5f, 0.0f),
                                  glm::vec2(0.0f, 146.0f));
    clear.color = glm::vec4(0.55f, 0.22f, 0.24f, 1.0f);
    clear.cornerRadius = 8.0f;
    clear.text = "清空";
    clear.fontSize = 18.0f;
    clear.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    clear.textCentered = true;
    canvas_.Add(panelNode, clear);

    // 提示行
    UiNode hint;
    hint.kind = UiKind::Text;
    hint.rect = UiRect::Anchored(glm::vec2(0.5f, 0.0f), glm::vec2(320.0f, 22.0f), glm::vec2(0.5f, 0.0f),
                                 glm::vec2(0.0f, 200.0f));
    hint.text = "按钮点击真实生效（复用编辑器物体增删路径）";
    hint.fontSize = 13.0f;
    hint.textColor = glm::vec4(0.6f, 0.64f, 0.7f, 1.0f);
    hint.textCentered = true;
    hint.raycastBlock = false;
    canvas_.Add(panelNode, hint);

    LOG_INFO("UI 演示画布已构建: " << canvas_.Count() << " 个节点（面板/标题/统计/2 按钮/提示）");
}

void UiRuntime::SetDemoStatsText(const std::string& text)
{
    if (statsNode_ < 0 || statsNode_ >= static_cast<UiNodeId>(canvas_.Count()))
        return;
    canvas_.nodes[static_cast<size_t>(statsNode_)].text = text;
}

UiRuntime::FrameEvents UiRuntime::Update(const Context& ctx, glm::vec2 mousePx, bool leftDown, float screenW,
                                         float screenH)
{
    lastEvents_ = {};
    if (!enabled_)
        return lastEvents_;
    ctx_ = &ctx;

    SolveCanvas(canvas_, screenW, screenH, solved_);
    const UiInteractOutput out = UpdateInteraction(canvas_, solved_, mousePx, leftDown, interact_);
    interact_ = out.state;
    lastEvents_.clicked = out.clicked;
    lastEvents_.blocked = out.blocked;

    for (const UiEvent& ev : out.clicked)
        if (onClick_)
            onClick_(ev);

    BuildVertices(ctx);
    return lastEvents_;
}

void UiRuntime::Record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent)
{
    if (!enabled_ || ctx_ == nullptr || vertices_.empty())
        return;
    const uint32_t slot = frameIndex % UiRenderer::kMaxFrames;
    renderer_.UploadVertices(*ctx_, slot, vertices_.data(), vertices_.size());
    renderer_.Record(cmd, imageIndex, slot, static_cast<uint32_t>(vertices_.size()), extent);
}

// ---- 顶点展开 ----

void UiRuntime::PushQuad(glm::vec2 posPx, glm::vec2 sizePx, glm::vec4 color, glm::vec2 uvMin, glm::vec2 uvMax,
                         float radius)
{
    if (sizePx.x <= 0.0f || sizePx.y <= 0.0f || color.a <= 0.0f)
        return;
    const glm::vec2 half = sizePx * 0.5f;
    UiVertex v{};
    v.color = color;
    v.rect = glm::vec4(half.x, half.y, radius, 0.0f);
    const glm::vec2 corners[4] = {{0.0f, 0.0f}, {sizePx.x, 0.0f}, {sizePx.x, sizePx.y}, {0.0f, sizePx.y}};
    const glm::vec2 localCorners[4] = {{-half.x, -half.y}, {half.x, -half.y}, {half.x, half.y}, {-half.x, half.y}};
    const glm::vec2 uvs[4] = {uvMin, {uvMax.x, uvMin.y}, uvMax, {uvMin.x, uvMax.y}};
    // 两三角形 0-1-2 / 0-2-3（Vulkan y 向下；管线 cullMode=NONE，序号不敏感）
    constexpr uint16_t kIdx[6] = {0, 1, 2, 0, 2, 3};
    for (const uint16_t i : kIdx)
    {
        v.pos = posPx + corners[i];
        v.local = localCorners[i];
        v.uv = uvs[i];
        vertices_.push_back(v);
    }
}

void UiRuntime::PushGlyph(glm::vec2 posPx, glm::vec2 sizePx, glm::vec4 color, glm::vec2 uvMin, glm::vec2 uvMax)
{
    // 字形四边形：radius=0（local 填 0，frag 走全覆盖分支）
    const glm::vec2 corners[4] = {{0.0f, 0.0f}, {sizePx.x, 0.0f}, {sizePx.x, sizePx.y}, {0.0f, sizePx.y}};
    const glm::vec2 uvs[4] = {uvMin, {uvMax.x, uvMin.y}, uvMax, {uvMin.x, uvMax.y}};
    UiVertex v{};
    v.color = color;
    v.rect = glm::vec4(0.0f);
    constexpr uint16_t kIdx[6] = {0, 1, 2, 0, 2, 3};
    for (const uint16_t i : kIdx)
    {
        v.pos = posPx + corners[i];
        v.local = glm::vec2(0.0f);
        v.uv = uvs[i];
        vertices_.push_back(v);
    }
}

float UiRuntime::PushText(const Context& ctx, std::string_view utf8, float fontSize, glm::vec4 color,
                          glm::vec2 topLeftPx, bool centered, float boxWidth)
{
    if (!atlas_.FontLoaded() || utf8.empty())
        return 0.0f;

    // 逐字 advance 与 = 整串宽度（居中对齐用；与 MeasureTextWidth 同口径，advance 由图集度量提供）
    std::vector<uint32_t> cps;
    Utf8Decode(utf8, cps);
    float totalWidth = 0.0f;
    for (const uint32_t cp : cps)
        totalWidth += atlas_.Advance(cp, fontSize);

    float penX = centered ? topLeftPx.x + (boxWidth - totalWidth) * 0.5f : topLeftPx.x;
    const float baseline = topLeftPx.y + atlas_.Ascent(fontSize);
    const float texW = static_cast<float>(atlas_.TextureWidth());
    const float texH = static_cast<float>(atlas_.TextureHeight());

    for (const uint32_t cp : cps)
    {
        if (cp == '\n')
            continue; // 第一增量单行文本
        const GlyphInfo& g = atlas_.Glyph(ctx, cp, fontSize);
        if (!g.Empty())
        {
            // stbtt 坐标：bearingY 自基线向上为正 → 屏幕 y = baseline - bearingY
            const glm::vec2 glyphTopLeft(penX + static_cast<float>(g.bearingX),
                                         baseline - static_cast<float>(g.bearingY));
            const glm::vec2 uvMin(static_cast<float>(g.u) / texW, static_cast<float>(g.v) / texH);
            const glm::vec2 uvMax(static_cast<float>(g.u + g.w) / texW, static_cast<float>(g.v + g.h) / texH);
            PushGlyph(glyphTopLeft, glm::vec2(static_cast<float>(g.w), static_cast<float>(g.h)), color, uvMin, uvMax);
        }
        penX += g.advance;
    }
    return totalWidth;
}

void UiRuntime::DrawNode(const Context& ctx, const UiNode& node, const UiSolvedRect& rect, const UiButtonRuntime* state)
{
    switch (node.kind)
    {
    case UiKind::Panel:
    {
        const glm::vec2 white = atlas_.WhitePixelUv();
        PushQuad(rect.pos, rect.size, node.color, white, white, node.cornerRadius);
        break;
    }
    case UiKind::Button:
    {
        // 底板（态配色）+ 中心标签
        const glm::vec2 white = atlas_.WhitePixelUv();
        PushQuad(rect.pos, rect.size, TintButtonColor(node.color, state), white, white, node.cornerRadius);
        if (!node.text.empty())
            PushText(ctx, node.text, node.fontSize, node.textColor, rect.pos, node.textCentered, rect.size.x);
        break;
    }
    case UiKind::Text:
    {
        if (!node.text.empty())
            PushText(ctx, node.text, node.fontSize, node.textColor, rect.pos, node.textCentered, rect.size.x);
        break;
    }
    }
}

void UiRuntime::BuildVertices(const Context& ctx)
{
    vertices_.clear();
    if (solved_.size() != canvas_.nodes.size())
        return;
    for (size_t i = 0; i < canvas_.nodes.size(); ++i)
    {
        const UiNode& node = canvas_.nodes[i];
        if (!node.visible)
            continue;
        const UiButtonRuntime* state =
            (i < interact_.buttons.size() && node.kind == UiKind::Button) ? &interact_.buttons[i] : nullptr;
        DrawNode(ctx, node, solved_[i], state);
    }
}
} // namespace BigHero::Ui
