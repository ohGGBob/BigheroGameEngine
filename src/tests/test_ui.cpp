// 运行时 UI 系统纯逻辑单元测试（U1-UI 第一增量）：
// 锚点解算（各象限/拉伸/嵌套树）、命中测试（遮挡顺序/穿透/隐藏）、
// 按钮状态机（hover/按下锁定/松开点击/移出取消）、文本测量（UTF-8 解码/advance 求和）、
// 字体图集纯逻辑（字形键编码/shelf 装箱/扩容触发/LRU 缓存）。
// 与 InspectorModel/HierarchyModel 同范式：全部用例不依赖 Vulkan/ImGui，可离线运行。
#include "framework/test_common.h"
#include "ui/UiFontCore.h"
#include "ui/UiModel.h"

#include <vector>

using namespace BigHero;
using namespace BigHero::Ui;

namespace
{
// 测试假度量表：ASCII 依码点线性，中文（>0x2E80）固定 16，回退 8
float FakeAdvance(uint32_t cp, float sizePx)
{
    if (cp == ' ')
        return sizePx * 0.5f;
    if (cp < 0x80)
        return sizePx * 0.6f;
    if (cp >= 0x2E80)
        return sizePx;
    return sizePx * 0.8f;
}
} // namespace

// ---------------- 锚点解算 ----------------

TEST_CASE("Ui.Rect.PointAnchorCenter")
{
    // 中心锚 + 中心 pivot：矩形以父中心为锚定点
    const UiSolvedRect parent{glm::vec2(0.0f), glm::vec2(1600.0f, 900.0f)};
    const UiRect r = UiRect::Anchored(glm::vec2(0.5f, 0.5f), glm::vec2(200.0f, 100.0f), glm::vec2(0.5f, 0.5f));
    const UiSolvedRect s = r.Solve(parent);
    CHECK_NEAR(s.pos.x, 700.0f, 1e-4f);
    CHECK_NEAR(s.pos.y, 400.0f, 1e-4f);
    CHECK_NEAR(s.size.x, 200.0f, 1e-4f);
    CHECK_NEAR(s.size.y, 100.0f, 1e-4f);
}

TEST_CASE("Ui.Rect.PointAnchorCorners")
{
    // 左上锚 + 左上 pivot → 矩形贴父左上角
    const UiSolvedRect parent{glm::vec2(0.0f), glm::vec2(100.0f, 100.0f)};
    {
        const UiRect tl = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 20.0f), glm::vec2(0.0f, 0.0f));
        const UiSolvedRect s = tl.Solve(parent);
        CHECK_NEAR(s.pos.x, 0.0f, 1e-4f);
        CHECK_NEAR(s.pos.y, 0.0f, 1e-4f);
    }
    // 右下锚（pivot 同为右下）→ 贴父右下角
    {
        const UiRect br = UiRect::Anchored(glm::vec2(1.0f, 1.0f), glm::vec2(40.0f, 20.0f), glm::vec2(1.0f, 1.0f));
        const UiSolvedRect s = br.Solve(parent);
        CHECK_NEAR(s.pos.x, 60.0f, 1e-4f);
        CHECK_NEAR(s.pos.y, 80.0f, 1e-4f);
    }
    // 偏移把锚定点推移：offset = (+10, -5)
    {
        const UiRect off = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 20.0f), glm::vec2(0.0f, 0.0f),
                                            glm::vec2(10.0f, -5.0f));
        const UiSolvedRect s = off.Solve(parent);
        CHECK_NEAR(s.pos.x, 10.0f, 1e-4f);
        CHECK_NEAR(s.pos.y, -5.0f, 1e-4f);
    }
}

TEST_CASE("Ui.Rect.StretchAnchors")
{
    // 全拉伸（0→1，offset 全 0）：矩形与父重合
    const UiSolvedRect parent{glm::vec2(10.0f, 20.0f), glm::vec2(300.0f, 200.0f)};
    UiRect full;
    full.anchorMin = glm::vec2(0.0f);
    full.anchorMax = glm::vec2(1.0f);
    const UiSolvedRect s = full.Solve(parent);
    CHECK_NEAR(s.pos.x, 10.0f, 1e-4f);
    CHECK_NEAR(s.pos.y, 20.0f, 1e-4f);
    CHECK_NEAR(s.size.x, 300.0f, 1e-4f);
    CHECK_NEAR(s.size.y, 200.0f, 1e-4f);

    // 水平拉伸 + 内缩：左入 10、右缩 20 → 宽 300-30=270；y 轴单点锚仍用 size/pivot
    UiRect h = full;
    h.anchorMin.y = h.anchorMax.y = 0.0f;
    h.offsetMin = glm::vec2(10.0f, 0.0f);
    h.offsetMax = glm::vec2(20.0f, 0.0f);
    h.size = glm::vec2(0.0f, 50.0f);
    h.pivot = glm::vec2(0.0f, 0.0f);
    const UiSolvedRect s2 = h.Solve(parent);
    CHECK_NEAR(s2.pos.x, 20.0f, 1e-4f);            // 10 + 10
    CHECK_NEAR(s2.size.x, 270.0f, 1e-4f);          // 310-20-20... = (300-20)-(10+10)
    CHECK_NEAR(s2.pos.y, 20.0f, 1e-4f);            // 单点锚 y：锚点 20 + offset 0 - pivot 0
    CHECK_NEAR(s2.size.y, 50.0f, 1e-4f);           // size.y 生效
}

TEST_CASE("Ui.Rect.PivotDoesNotMoveAnchorPoint")
{
    // pivot 改变只搬移矩形，不改变"锚定点"命中位置语义：锚定点 = 锚点 + offset
    const UiSolvedRect parent{glm::vec2(0.0f), glm::vec2(100.0f, 100.0f)};
    const glm::vec2 anchor(0.5f, 0.5f);
    const glm::vec2 offset(5.0f, 7.0f);
    const UiRect a = UiRect::Anchored(anchor, glm::vec2(50.0f, 50.0f), glm::vec2(0.0f, 0.0f), offset);
    const UiRect b = UiRect::Anchored(anchor, glm::vec2(50.0f, 50.0f), glm::vec2(1.0f, 1.0f), offset);
    const UiSolvedRect sa = a.Solve(parent);
    const UiSolvedRect sb = b.Solve(parent);
    // 两者的右/下边缘（pivot(1,1)）与左/上边缘（pivot(0,0)）应共享同一锚定点 (55, 57)
    CHECK_NEAR(sa.pos.x, 55.0f, 1e-4f);
    CHECK_NEAR(sb.Right(), 55.0f, 1e-4f);
    CHECK_NEAR(sa.pos.y, 57.0f, 1e-4f);
    CHECK_NEAR(sb.Bottom(), 57.0f, 1e-4f);
}

TEST_CASE("Ui.Canvas.NestedTreeSolve")
{
    // 嵌套树：屏幕 → 面板（子节点相对父解算矩形）→ 按钮（相对面板）
    UiCanvas canvas;
    UiNode panel;
    panel.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(400.0f, 300.0f), glm::vec2(0.0f, 0.0f));
    const int panelIdx = canvas.Add(kInvalidNode, panel);

    UiNode button;
    button.kind = UiKind::Button;
    button.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(100.0f, 40.0f), glm::vec2(0.0f, 0.0f),
                                   glm::vec2(30.0f, 50.0f));
    const int buttonIdx = canvas.Add(panelIdx, button);

    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 1920.0f, 1080.0f, rects);
    CHECK_EQ(rects.size(), canvas.nodes.size());
    CHECK_NEAR(rects[static_cast<size_t>(panelIdx)].pos.x, 0.0f, 1e-4f);
    CHECK_NEAR(rects[static_cast<size_t>(buttonIdx)].pos.x, 30.0f, 1e-4f);  // 相对面板
    CHECK_NEAR(rects[static_cast<size_t>(buttonIdx)].pos.y, 50.0f, 1e-4f);
}

TEST_CASE("Ui.Rect.DeepNestingAccumulates")
{
    // 三层嵌套：每层 +10 偏移 → 孙节点屏幕位置 = 30
    UiCanvas canvas;
    UiNode a;
    a.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(500.0f, 500.0f), glm::vec2(0.0f, 0.0f),
                              glm::vec2(10.0f, 10.0f));
    const int ia = canvas.Add(kInvalidNode, a);
    const int ib = canvas.Add(ia, a);   // 同配置子节点
    const int ic = canvas.Add(ib, a);
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 1920.0f, 1080.0f, rects);
    CHECK_NEAR(rects[static_cast<size_t>(ia)].pos.x, 10.0f, 1e-4f);
    CHECK_NEAR(rects[static_cast<size_t>(ib)].pos.x, 20.0f, 1e-4f);
    CHECK_NEAR(rects[static_cast<size_t>(ic)].pos.x, 30.0f, 1e-4f);
}

// ---------------- 命中测试 ----------------

namespace
{
// 构造两节点画布：下层面板 0..200，上层按钮 80..120（遮挡面板中央）
UiCanvas MakeOverlayCanvas()
{
    UiCanvas canvas;
    UiNode panel;
    panel.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(200.0f, 200.0f), glm::vec2(0.0f, 0.0f));
    canvas.Add(kInvalidNode, panel);
    UiNode button;
    button.kind = UiKind::Button;
    button.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, 40.0f), glm::vec2(0.0f, 0.0f),
                                   glm::vec2(80.0f, 80.0f));
    canvas.Add(kInvalidNode, button);
    return canvas;
}
} // namespace

TEST_CASE("Ui.HitTest.TopmostOcclusion")
{
    // 上层按钮截胡命中（z 序 = 插入序）；面板区域命中面板
    const UiCanvas canvas = MakeOverlayCanvas();
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);

    const UiHitResult hitButton = HitTest(canvas, rects, glm::vec2(100.0f, 100.0f)); // 按钮内
    CHECK_EQ(hitButton.node, 1);
    CHECK(hitButton.blocked);
    const UiHitResult hitPanel = HitTest(canvas, rects, glm::vec2(30.0f, 30.0f)); // 按钮外、面板内
    CHECK_EQ(hitPanel.node, 0);
    CHECK(hitPanel.blocked);
}

TEST_CASE("Ui.HitTest.PassThroughDecor")
{
    // raycastBlock=false 的装饰层：命中穿透到下层交互节点
    UiCanvas canvas = MakeOverlayCanvas();
    UiNode decor;
    decor.raycastBlock = false;
    decor.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(400.0f, 400.0f), glm::vec2(0.0f, 0.0f));
    canvas.Add(kInvalidNode, decor); // 最上层装饰，覆盖全屏
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 800.0f, 800.0f, rects);
    const UiHitResult hit = HitTest(canvas, rects, glm::vec2(100.0f, 100.0f));
    CHECK_EQ(hit.node, 1); // 穿透装饰，命中按钮
    CHECK(hit.blocked);    // 按钮仍阻断场景拾取
}

TEST_CASE("Ui.HitTest.InvisibleSkipped")
{
    // 隐藏节点不参与命中：上层按钮隐藏 → 命中落到面板
    UiCanvas canvas = MakeOverlayCanvas();
    canvas.nodes[1].visible = false;
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const UiHitResult hit = HitTest(canvas, rects, glm::vec2(100.0f, 100.0f));
    CHECK_EQ(hit.node, 0);
    CHECK(hit.blocked);
}

TEST_CASE("Ui.HitTest.EmptyCanvasMiss")
{
    // 空画布/无命中：node 无效且不阻断（场景拾取照常）
    UiCanvas canvas;
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const UiHitResult hit = HitTest(canvas, rects, glm::vec2(10.0f, 10.0f));
    CHECK_EQ(hit.node, kInvalidNode);
    CHECK(!hit.blocked);

    const UiCanvas overlay = MakeOverlayCanvas();
    std::vector<UiSolvedRect> rects2;
    SolveCanvas(overlay, 400.0f, 400.0f, rects2);
    const UiHitResult miss = HitTest(overlay, rects2, glm::vec2(350.0f, 350.0f));
    CHECK_EQ(miss.node, kInvalidNode);
    CHECK(!miss.blocked);
}

// ---------------- 按钮状态机 ----------------

TEST_CASE("Ui.Button.HoverAndPressed")
{
    // hover 进入/退出；按下锁定后 pressed 保持
    const UiCanvas canvas = MakeOverlayCanvas();
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const glm::vec2 inside(100.0f, 100.0f);
    const glm::vec2 outside(300.0f, 300.0f);

    UiInteractState st;
    UiInteractOutput out = UpdateInteraction(canvas, rects, outside, false, st);
    CHECK(!out.state.buttons[1].hover);
    st = out.state;

    out = UpdateInteraction(canvas, rects, inside, false, st); // 移入
    CHECK(out.state.buttons[1].hover);
    CHECK(!out.state.buttons[1].pressed);
    st = out.state;

    out = UpdateInteraction(canvas, rects, inside, true, st); // 按下
    CHECK(out.state.buttons[1].hover);
    CHECK(out.state.buttons[1].pressed);
    st = out.state;

    out = UpdateInteraction(canvas, rects, outside, true, st); // 按住移出：锁定保持（uGUI 语义）
    CHECK(!out.state.buttons[1].hover);
    CHECK(out.state.buttons[1].pressed);
    CHECK_EQ(out.state.pressedNode, 1);
}

TEST_CASE("Ui.Button.ClickOnReleaseInside")
{
    // 在按钮上按下 → 原按钮上松开：触发一次 clicked 事件（携带业务 id）
    UiCanvas canvas = MakeOverlayCanvas();
    canvas.nodes[1].id = 77;
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const glm::vec2 inside(100.0f, 100.0f);

    UiInteractState st;
    UiInteractOutput out = UpdateInteraction(canvas, rects, inside, true, st); // 按下
    st = out.state;
    out = UpdateInteraction(canvas, rects, inside, false, st);                 // 松开
    CHECK_EQ(out.clicked.size(), 1u);
    CHECK_EQ(out.clicked[0].node, 1);
    CHECK_EQ(out.clicked[0].id, 77);
    CHECK_EQ(out.state.pressedNode, kInvalidNode);
}

TEST_CASE("Ui.Button.ReleaseOutsideCancelled")
{
    // 按下后移出按钮再松开：不触发点击（uGUI PointerClick 语义）
    const UiCanvas canvas = MakeOverlayCanvas();
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const glm::vec2 inside(100.0f, 100.0f);
    const glm::vec2 outside(300.0f, 300.0f);

    UiInteractState st;
    st = UpdateInteraction(canvas, rects, inside, true, st).state;            // 按下
    const UiInteractOutput out = UpdateInteraction(canvas, rects, outside, false, st); // 移出后松开
    CHECK(out.clicked.empty());
}

TEST_CASE("Ui.Button.OccludedButtonNotClickable")
{
    // 最上层按钮截胡：下层按钮同位置不可点击（遮挡顺序作用于状态机）
    UiCanvas canvas;
    UiNode lower;
    lower.kind = UiKind::Button;
    lower.id = 1;
    lower.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(100.0f, 100.0f), glm::vec2(0.0f, 0.0f));
    canvas.Add(kInvalidNode, lower);
    UiNode upper;
    upper.kind = UiKind::Button;
    upper.id = 2;
    upper.rect = UiRect::Anchored(glm::vec2(0.0f, 0.0f), glm::vec2(100.0f, 100.0f), glm::vec2(0.0f, 0.0f));
    canvas.Add(kInvalidNode, upper);
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);

    const glm::vec2 p(50.0f, 50.0f);
    UiInteractState st;
    st = UpdateInteraction(canvas, rects, p, true, st).state;
    CHECK_EQ(st.pressedNode, 1); // 插入序末尾 = 最上层（upper）
    const UiInteractOutput out = UpdateInteraction(canvas, rects, p, false, st);
    CHECK_EQ(out.clicked.size(), 1u);
    CHECK_EQ(out.clicked[0].id, 2);
}

TEST_CASE("Ui.Button.InteractableFalseIgnored")
{
    // interactable=false：不成为命中节点、不触发点击，但仍阻断（装饰性面板语义）
    UiCanvas canvas = MakeOverlayCanvas();
    canvas.nodes[1].interactable = false;
    std::vector<UiSolvedRect> rects;
    SolveCanvas(canvas, 400.0f, 400.0f, rects);
    const glm::vec2 p(100.0f, 100.0f);

    const UiHitResult hit = HitTest(canvas, rects, p);
    CHECK_EQ(hit.node, kInvalidNode);
    CHECK(hit.blocked);

    UiInteractState st;
    st = UpdateInteraction(canvas, rects, p, true, st).state;
    CHECK_EQ(st.pressedNode, kInvalidNode);
    const UiInteractOutput out = UpdateInteraction(canvas, rects, p, false, st);
    CHECK(out.clicked.empty());
}

// ---------------- 文本测量 ----------------

TEST_CASE("Ui.Text.Utf8Decode")
{
    std::vector<uint32_t> cps;
    Utf8Decode("Hi", cps);
    CHECK_EQ(cps.size(), 2u);
    CHECK_EQ(cps[0], static_cast<uint32_t>('H'));

    // 中文三字节序列：U+4E2D U+6587
    Utf8Decode("中文", cps);
    CHECK_EQ(cps.size(), 2u);
    CHECK_EQ(cps[0], 0x4E2Du);
    CHECK_EQ(cps[1], 0x6587u);

    // 四字节（emoji U+1F600）
    Utf8Decode("\xF0\x9F\x98\x80", cps);
    CHECK_EQ(cps.size(), 1u);
    CHECK_EQ(cps[0], 0x1F600u);

    // 非法续字节 → U+FFFD 回退，不崩溃
    Utf8Decode("\xE4\xB8", cps); // 截断三字节
    CHECK_EQ(cps.size(), 1u);
    CHECK_EQ(cps[0], 0xFFFDu);
    Utf8Decode("\xFF", cps); // 非法首字节
    CHECK_EQ(cps.size(), 1u);
    CHECK_EQ(cps[0], 0xFFFDu);
}

TEST_CASE("Ui.Text.MeasureWidth")
{
    // 宽度 = 逐字 advance 和（假度量表：ASCII 0.6×，中文 1×）
    const float size = 20.0f;
    CHECK_NEAR(MeasureTextWidth("Hi", size, FakeAdvance), (0.6f + 0.6f) * size, 1e-4f);
    CHECK_NEAR(MeasureTextWidth("中", size, FakeAdvance), size, 1e-4f);
    CHECK_NEAR(MeasureTextWidth("中文UI", size, FakeAdvance), (1.0f + 1.0f + 0.6f + 0.6f) * size, 1e-4f);
    CHECK_NEAR(MeasureTextWidth("", size, FakeAdvance), 0.0f, 1e-6f);
    // 无 advance 回调：宽度为 0（不崩溃）
    CHECK_NEAR(MeasureTextWidth("abc", size, nullptr), 0.0f, 1e-6f);
}

// ---------------- 字体图集纯逻辑 ----------------

TEST_CASE("Ui.Font.GlyphKeyEncoding")
{
    // 编码/解码往返；1/4 px 量化
    const GlyphKey k1 = MakeGlyphKey(0x4E2D, 18.0f);
    CHECK_EQ(GlyphKeyCodepoint(k1), 0x4E2Du);
    CHECK_NEAR(GlyphKeySizePx(k1), 18.0f, 1e-3f);
    // 同码点不同字号 → 不同键
    CHECK(MakeGlyphKey(0x4E2D, 18.0f) != MakeGlyphKey(0x4E2D, 24.0f));
    // 同字号不同码点 → 不同键
    CHECK(MakeGlyphKey(0x4E2D, 18.0f) != MakeGlyphKey(0x6587, 18.0f));
    // 量化：18.05 与 18.0 同键（1/4 px 内）
    CHECK_EQ(MakeGlyphKey(0x41, 18.05f), MakeGlyphKey(0x41, 18.0f));
    // 非法字号钳 0
    CHECK_EQ(GlyphKeySizePx(MakeGlyphKey(0x41, -1.0f)), 0.0f);
}

TEST_CASE("Ui.Font.AtlasShelfAllocAndGrow")
{
    AtlasShelf shelf;
    shelf.width = shelf.height = 64;
    shelf.Reset();

    AtlasShelf::Slot slot{};
    CHECK(shelf.Alloc(20, 10, slot) == AtlasShelf::Result::Ok); // (1,1) 起
    CHECK_EQ(slot.u, 1u);
    CHECK_EQ(slot.v, 1u);
    CHECK(shelf.Alloc(20, 10, slot) == AtlasShelf::Result::Ok); // 同行 (22,1)
    CHECK_EQ(slot.u, 22u);
    CHECK_EQ(slot.v, 1u);

    // 空白字形不占图集
    CHECK(shelf.Alloc(0, 0, slot) == AtlasShelf::Result::Ok);
    CHECK_EQ(slot.u, 0u);

    // 行放不下 → 换行；剩余高度不足 → NeedGrow（扩容触发）
    shelf.Reset();
    shelf.width = shelf.height = 32;
    shelf.Alloc(30, 12, slot);
    CHECK(shelf.Alloc(30, 12, slot) == AtlasShelf::Result::Ok); // 换到第二行
    CHECK_EQ(slot.v, 14u);                                      // 1 + 12 + 1
    CHECK(shelf.Alloc(30, 12, slot) == AtlasShelf::Result::NeedGrow);

    // 单字形超图集：NeedGrow
    shelf.Reset();
    CHECK(shelf.Alloc(64, 4, slot) == AtlasShelf::Result::NeedGrow);
    CHECK(shelf.Alloc(4, 64, slot) == AtlasShelf::Result::NeedGrow);
}

TEST_CASE("Ui.Font.GlyphCacheHitAndLru")
{
    GlyphCache cache;
    GlyphInfo a;
    a.advance = 10;
    GlyphInfo b;
    b.advance = 20;
    cache.Insert(MakeGlyphKey(0x41, 16.0f), a);
    cache.Insert(MakeGlyphKey(0x42, 16.0f), b);
    CHECK_EQ(cache.Size(), 2u);

    // 命中
    const GlyphInfo* hit = cache.Find(MakeGlyphKey(0x41, 16.0f));
    REQUIRE(hit != nullptr);
    CHECK_EQ(hit->advance, 10);

    // 未命中
    CHECK(cache.Find(MakeGlyphKey(0x43, 16.0f)) == nullptr);

    // 已存在 Insert：原地更新且插入序不变（扩容重光栅化依赖）
    a.advance = 11;
    cache.Insert(MakeGlyphKey(0x41, 16.0f), a);
    CHECK_EQ(cache.Size(), 2u);
    CHECK_EQ(cache.Order().size(), 2u);
    CHECK_EQ(cache.Order()[0], MakeGlyphKey(0x41, 16.0f));

    // LRU：先访问 0x41 再访问 0x42 → 0x41 最新，修剪到 1 个时驱逐 0x42
    (void)cache.Find(MakeGlyphKey(0x41, 16.0f));
    (void)cache.Find(MakeGlyphKey(0x42, 16.0f));
    (void)cache.Find(MakeGlyphKey(0x41, 16.0f));
    const size_t evicted = cache.Trim(1);
    CHECK_EQ(evicted, 1u);
    CHECK_EQ(cache.Size(), 1u);
    CHECK(cache.Find(MakeGlyphKey(0x41, 16.0f)) != nullptr);
    CHECK(cache.Find(MakeGlyphKey(0x42, 16.0f)) == nullptr);
    // 修剪后插入序与缓存一致
    CHECK_EQ(cache.Order().size(), 1u);
}

TEST_CASE("Ui.Canvas.AddPreservesZOrder")
{
    // Add 不重排：z 顺序 = 插入序；parent 平行数组对齐
    UiCanvas canvas;
    UiNode n;
    for (int i = 0; i < 5; ++i)
    {
        n.kind = (i % 2 == 0) ? UiKind::Panel : UiKind::Text;
        const int idx = canvas.Add(i == 0 ? kInvalidNode : 0, n);
        CHECK_EQ(idx, i);
    }
    CHECK_EQ(canvas.Count(), 5u);
    CHECK_EQ(canvas.parent[0], kInvalidNode);
    CHECK_EQ(canvas.parent[4], 0);
}
