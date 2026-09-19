// 动画系统升级单元测试：A1 动画事件（AnimationEvent/AnimationEventTrack/AnimationEventPlayer）
// + A3 二维混合树（BlendSpace2D 及其在 AnimationStateMachine 中的集成）。
// 覆盖边界：正放/倒放/回绕跨 0/大 dt 多事件/同刻多事件/seek 不触发/暂停不触发/变速，
// 以及混合空间网格插值/越界 clamp/采样点增删/轴向退化/权重合并/状态机集成。
#include "framework/test_common.h"
#include "scene/Animation.h"
#include "scene/AnimationStateMachine.h"
#include "scene/GltfLoader.h"

using namespace BigHero;

namespace
{
// 构建单节点平移动画模型：x 从 0 线性到 2，时长 duration 秒（动画名 "Move"）
BigHero::Scene::GltfModel MakeMoveModel(float duration)
{
    using namespace BigHero::Scene;
    GltfModel model;
    model.nodeParents = {-1};
    model.nodeTranslations = {glm::vec3(0.0f)};
    model.nodeRotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    model.nodeScales = {glm::vec3(1.0f)};
    GltfAnimation anim;
    anim.name = "Move";
    GltfAnimationSampler sp;
    sp.times = {0.0f, duration};
    sp.values = {glm::vec4(0.0f), glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)};
    anim.samplers.push_back(sp);
    GltfAnimationChannel ch;
    ch.targetNode = 0;
    ch.path = "translation";
    ch.sampler = 0;
    anim.channels.push_back(ch);
    model.animations.push_back(anim);
    return model;
}
} // namespace

// ===================== A1 动画事件 =====================

TEST_CASE("AnimEvents.ForwardOrdered")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.clipName = "Move";
    track.AddEvent("zero", 0.0f);       // 起始事件：起播不触发
    track.AddEvent("half", 0.5f, 1.0f); // 带可选参数
    track.AddEvent("one", 1.0f);
    track.AddEvent("oneHalf", 1.5f, 2.0f);

    AnimationEventPlayer player(model, 0);
    CHECK(player.IsValid());
    CHECK(std::fabs(player.Duration() - 2.0f) < 1e-5f);

    // 轨匹配校验：clipIndex 不符 / clipName 不符 -> 拒绝绑定且不生效
    AnimationEventTrack wrongClip;
    wrongClip.clipIndex = 1;
    CHECK(player.BindTrack(&wrongClip) == false);
    AnimationEventTrack wrongName;
    wrongName.clipIndex = 0;
    wrongName.clipName = "Other";
    CHECK(player.BindTrack(&wrongName) == false);
    CHECK(player.BindTrack(&track));
    CHECK(player.BindTrack(nullptr)); // 解绑
    CHECK(player.BindTrack(&track));

    player.SetLoop(false);
    // 起播：time=0 的事件不触发（位于起始位置、未跨过）
    auto ev = player.Advance(0.25f);
    CHECK(ev.empty());
    // 精确到达 0.5：触发，参数随事件返回
    ev = player.Advance(0.25f);
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "half");
    CHECK(std::fabs(ev[0].time - 0.5f) < 1e-6f);
    CHECK(std::fabs(ev[0].param - 1.0f) < 1e-6f);
    ev = player.Advance(0.25f); // 0.75：无事件
    CHECK(ev.empty());
    ev = player.Advance(0.25f); // 1.0：精确到达
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "one");
    ev = player.Advance(0.75f); // 1.75：跨过 1.5
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "oneHalf");
    ev = player.Advance(0.25f); // 2.0：到末尾，无事件
    CHECK(ev.empty());
    CHECK(std::fabs(player.Time() - 2.0f) < 1e-5f);
    // 停在末尾：不重复触发
    ev = player.Advance(1.0f);
    CHECK(ev.empty());

    // 姿态采样联动：时间 2.0 -> x=2（x: 0->2 线性）
    std::vector<glm::vec3> T, S;
    std::vector<glm::quat> R;
    player.SamplePose(T, R, S);
    CHECK(T.size() == 1);
    CHECK(glm::distance(T[0], glm::vec3(2.0f, 0.0f, 0.0f)) < 1e-4f);
}

TEST_CASE("AnimEvents.LoopWrap")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("a", 0.25f);
    track.AddEvent("b", 1.0f);
    track.AddEvent("zero", 0.0f); // 跨 0 事件：回绕时触发

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);
    player.SetLoop(true);
    player.Seek(1.5f);

    auto ev = player.Advance(0.25f); // 1.75：无事件
    CHECK(ev.empty());
    // 回绕跨过 2.0 -> 0：触发 zero@0，并精确到达 a@0.25（按跨越顺序）
    ev = player.Advance(0.5f); // 1.75 + 0.5 = 2.25 ≡ 0.25
    CHECK(ev.size() == 2);
    CHECK(ev[0].name == "zero");
    CHECK(ev[1].name == "a");
    // 大 dt 跨整个周期：全部事件按跨越顺序各触发一次（不重复）
    ev = player.Advance(2.0f); // 0.25 -> 2.25 ≡ 0.25：跨 b@1.0、zero@2.0≡0、a@2.25≡0.25
    CHECK(ev.size() == 3);
    CHECK(ev[0].name == "b");
    CHECK(ev[1].name == "zero");
    CHECK(ev[2].name == "a");
    // 多圈播放：每圈各事件恰触发一次，不重不漏
    int zeroCount = 0, aCount = 0, bCount = 0;
    for (int i = 0; i < 8; ++i)
    {
        ev = player.Advance(1.0f);
        for (const AnimationEvent& e : ev)
        {
            if (e.name == "zero")
                ++zeroCount;
            else if (e.name == "a")
                ++aCount;
            else if (e.name == "b")
                ++bCount;
            else
                CHECK(false); // 不应出现未知事件
        }
    }
    CHECK(zeroCount == 4);
    CHECK(aCount == 4);
    CHECK(bCount == 4);
}

TEST_CASE("AnimEvents.Reverse")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("zero", 0.0f);
    track.AddEvent("half", 0.5f);
    track.AddEvent("one", 1.0f);
    track.AddEvent("oneHalf", 1.5f);

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);
    player.SetLoop(true);
    player.SetSpeed(-1.0f); // 倒放
    player.Seek(1.8f);

    auto ev = player.Advance(0.4f); // 1.4：跨过 1.5
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "oneHalf");
    ev = player.Advance(0.4f); // 1.0：精确到达
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "one");
    ev = player.Advance(0.4f); // 0.6：无事件
    CHECK(ev.empty());
    ev = player.Advance(0.4f); // 0.2：跨过 0.5
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "half");
    // 倒放回绕跨 0：触发 zero，且不触发未跨过的 1.5
    ev = player.Advance(0.4f); // -0.2 ≡ 1.8
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "zero");
    // 大步倒退跨多事件：按跨越顺序（时间降序）返回
    ev = player.Advance(1.6f); // 1.8 -> 0.2：跨 1.5、1.0、0.5
    CHECK(ev.size() == 3);
    CHECK(ev[0].name == "oneHalf");
    CHECK(ev[1].name == "one");
    CHECK(ev[2].name == "half");
}

TEST_CASE("AnimEvents.MultiEventOneFrame")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("e04", 0.4f);
    track.AddEvent("e08", 0.8f);
    track.AddEvent("e12", 1.2f);
    track.AddEvent("e16", 1.6f);
    track.AddEvent("end", 2.0f); // 末尾事件：非循环到达时长时触发

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);
    player.SetLoop(false);

    // 一帧跨多事件：全部按序返回
    auto ev = player.Advance(1.0f); // 0 -> 1.0
    CHECK(ev.size() == 2);
    CHECK(ev[0].name == "e04");
    CHECK(ev[1].name == "e08");
    ev = player.Advance(1.0f); // 1.0 -> 2.0（夹取到时长，末尾事件触发）
    CHECK(ev.size() == 3);
    CHECK(ev[0].name == "e12");
    CHECK(ev[1].name == "e16");
    CHECK(ev[2].name == "end");
    // 停在末尾：不再触发
    ev = player.Advance(1.0f);
    CHECK(ev.empty());
    CHECK(std::fabs(player.Time() - 2.0f) < 1e-5f);
    // seek 回起点重播：事件按新一圈重新触发
    player.Seek(0.0f);
    ev = player.Advance(2.0f);
    CHECK(ev.size() == 5);
    CHECK(ev.front().name == "e04");
    CHECK(ev.back().name == "end");
}

TEST_CASE("AnimEvents.SameTimeOrder")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("first", 1.0f, 1.0f);
    track.AddEvent("second", 1.0f, 2.0f);
    track.AddEvent("third", 1.0f, 3.0f);
    // AddEvent 稳定排序：同刻事件保持插入顺序
    CHECK(track.events.size() == 3);
    CHECK(track.events[0].name == "first");
    CHECK(track.events[1].name == "second");
    CHECK(track.events[2].name == "third");

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);
    auto ev = player.Advance(1.0f);
    CHECK(ev.size() == 3);
    CHECK(ev[0].name == "first");
    CHECK(ev[1].name == "second");
    CHECK(ev[2].name == "third");
}

TEST_CASE("AnimEvents.SeekSkips")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("a", 0.5f);
    track.AddEvent("b", 1.5f);

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);
    player.SetLoop(false);

    // seek 向前跳过 a：不触发（与 Unity 对齐：seek 是重新定位而非播放跨越）
    player.Seek(1.0f);
    auto ev = player.Advance(0.2f); // 1.2：无事件
    CHECK(ev.empty());
    ev = player.Advance(0.5f); // 1.7：只触发 b
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "b");

    // seek 向后跳过 b（同样不触发），再从 0 正放：a 正常触发
    player.Seek(0.0f);
    ev = player.Advance(0.5f);
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "a");
    CHECK(std::fabs(player.Time() - 0.5f) < 1e-5f);

    // 循环状态下 seek 越界回绕
    player.SetLoop(true);
    player.Seek(2.75f); // 2.75 mod 2.0 = 0.75
    CHECK(std::fabs(player.Time() - 0.75f) < 1e-5f);
    ev = player.Advance(0.5f); // 1.25：无事件
    CHECK(ev.empty());
}

TEST_CASE("AnimEvents.PauseAndSpeed")
{
    using namespace BigHero::Scene;
    const GltfModel model = MakeMoveModel(2.0f);
    AnimationEventTrack track;
    track.clipIndex = 0;
    track.AddEvent("mid", 0.5f);

    AnimationEventPlayer player(model, 0);
    player.BindTrack(&track);

    // 暂停：不推进、不触发
    player.SetPaused(true);
    CHECK(player.IsPaused());
    auto ev = player.Advance(1.0f);
    CHECK(ev.empty());
    CHECK(std::fabs(player.Time()) < 1e-6f);
    // dt=0：同样不触发
    player.SetPaused(false);
    ev = player.Advance(0.0f);
    CHECK(ev.empty());
    // 恢复后正常触发
    ev = player.Advance(0.5f);
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "mid");

    // 2 倍速：第一步不到、第二步跨过
    player.Seek(0.0f);
    player.SetSpeed(2.0f);
    ev = player.Advance(0.2f); // 0.4：无
    CHECK(ev.empty());
    ev = player.Advance(0.2f); // 0.8：跨过 0.5
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "mid");

    // 半速：第二步精确到达
    player.Seek(0.0f);
    player.SetSpeed(0.5f);
    ev = player.Advance(0.5f); // 0.25：无
    CHECK(ev.empty());
    ev = player.Advance(0.5f); // 0.5：到达
    CHECK(ev.size() == 1);
    CHECK(ev[0].name == "mid");
}

// ===================== A3 二维混合空间 =====================

TEST_CASE("BlendSpace.GridBilinear")
{
    using namespace BigHero::Scene;
    BlendSpace2D bs;
    bs.AddSample({0.0f, 0.0f}, 0);
    bs.AddSample({1.0f, 0.0f}, 1);
    bs.AddSample({0.0f, 1.0f}, 2);
    bs.AddSample({1.0f, 1.0f}, 3);
    CHECK(bs.SampleCount() == 4);

    // 中心点：四角各 0.25（按 animIndex 升序输出）
    auto w = bs.Evaluate({0.5f, 0.5f});
    CHECK(w.size() == 4);
    CHECK(w[0].animIndex == 0);
    CHECK(std::fabs(w[0].weight - 0.25f) < 1e-5f);
    CHECK(w[1].animIndex == 1);
    CHECK(std::fabs(w[1].weight - 0.25f) < 1e-5f);
    CHECK(w[2].animIndex == 2);
    CHECK(std::fabs(w[2].weight - 0.25f) < 1e-5f);
    CHECK(w[3].animIndex == 3);
    CHECK(std::fabs(w[3].weight - 0.25f) < 1e-5f);

    // 轴线上：只取该行两角 0.75 / 0.25
    w = bs.Evaluate({0.25f, 0.0f});
    CHECK(w.size() == 2);
    CHECK(w[0].animIndex == 0);
    CHECK(std::fabs(w[0].weight - 0.75f) < 1e-5f);
    CHECK(w[1].animIndex == 1);
    CHECK(std::fabs(w[1].weight - 0.25f) < 1e-5f);

    // 精确命中角点：仅该点基线权重
    w = bs.Evaluate({1.0f, 1.0f});
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 3);
    CHECK(std::fabs(w[0].weight - 1.0f) < 1e-5f);

    // 权重基线：非 1 基线按插值系数缩放
    BlendSpace2D bs2;
    bs2.AddSample({0.0f, 0.0f}, 0, 0.5f);
    bs2.AddSample({1.0f, 0.0f}, 1, 2.0f);
    w = bs2.Evaluate({0.5f, 0.0f}); // x 中点、y 轴退化（单行）
    CHECK(w.size() == 2);
    CHECK(std::fabs(w[0].weight - 0.25f) < 1e-5f); // 0.5 * 0.5
    CHECK(std::fabs(w[1].weight - 1.0f) < 1e-5f);  // 2.0 * 0.5
}

TEST_CASE("BlendSpace.Clamp")
{
    using namespace BigHero::Scene;
    BlendSpace2D bs;
    bs.AddSample({0.0f, 0.0f}, 0);
    bs.AddSample({1.0f, 0.0f}, 1);
    bs.AddSample({0.0f, 1.0f}, 2);
    bs.AddSample({1.0f, 1.0f}, 3);

    // 默认轴范围 = 采样点包围盒 [0,1]^2：越界 clamp 到边缘
    auto w = bs.Evaluate({-5.0f, 0.5f}); // clamp 到 (0, 0.5)：x=0 列上下角各半
    CHECK(w.size() == 2);
    CHECK(w[0].animIndex == 0);
    CHECK(std::fabs(w[0].weight - 0.5f) < 1e-5f);
    CHECK(w[1].animIndex == 2);
    CHECK(std::fabs(w[1].weight - 0.5f) < 1e-5f);

    w = bs.Evaluate({3.0f, 9.0f}); // clamp 到 (1,1)：角点精确命中
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 3);
    CHECK(std::fabs(w[0].weight - 1.0f) < 1e-5f);

    // 显式轴范围：扩展 clamp 区域（范围超出采样包围盒时取到边缘采样）
    BlendSpace2D bs2;
    bs2.AddSample({0.0f, 0.0f}, 0);
    bs2.AddSample({1.0f, 0.0f}, 1);
    bs2.SetAxisRange(0, -1.0f, 2.0f);
    bs2.SetAxisRange(1, 0.0f, 0.0f);
    w = bs2.Evaluate({-1.0f, 0.0f}); // x=-1 在采样左侧 -> 取 x=0 边缘列
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 0);
    CHECK(std::fabs(w[0].weight - 1.0f) < 1e-5f);
    w = bs2.Evaluate({2.0f, 0.0f}); // x=2 在采样右侧 -> 取 x=1 列
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 1);
    // 非法 axis 忽略
    bs2.SetAxisRange(5, 0.0f, 1.0f);
    CHECK(bs2.HasAxisRange(0));
    CHECK(!bs2.HasAxisRange(5));
}

TEST_CASE("BlendSpace.AddRemove")
{
    using namespace BigHero::Scene;
    BlendSpace2D bs;
    bs.AddSample({0.0f, 0.0f}, 0); // 0
    bs.AddSample({1.0f, 0.0f}, 1); // 1
    bs.AddSample({0.0f, 1.0f}, 2); // 2
    bs.AddSample({1.0f, 1.0f}, 3); // 3
    CHECK(bs.SampleCount() == 4);

    // 移除角点 (1,1)：x=1 列仍存在（(1,0)），但角点 (1,1) 无采样 -> 空权重
    bs.RemoveSample(3);
    auto w = bs.Evaluate({1.0f, 1.0f});
    CHECK(w.empty());
    // 重新添加恢复覆盖
    bs.AddSample({1.0f, 1.0f}, 3);
    w = bs.Evaluate({1.0f, 1.0f});
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 3);

    // 移除中间点 (1,0)：其余下标稳定前移；y=0 行只剩 (0,0) -> 半权重（未归一化，交由 blender 归一化）
    bs.RemoveSample(1);
    CHECK(bs.SampleCount() == 3);
    CHECK(bs.GetSample(1).animIndex == 2);
    CHECK(bs.GetSample(2).animIndex == 3);
    w = bs.Evaluate({0.5f, 0.0f});
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 0);
    CHECK(std::fabs(w[0].weight - 0.5f) < 1e-5f);

    // 越界移除 no-op；清空后求值为空
    bs.RemoveSample(99);
    CHECK(bs.SampleCount() == 3);
    bs.Clear();
    CHECK(bs.SampleCount() == 0);
    CHECK(bs.Evaluate({0.5f, 0.5f}).empty());
}

TEST_CASE("BlendSpace.DegenerateAxes")
{
    using namespace BigHero::Scene;
    // 单行（唯一 Y）：沿 X 线性插值，Y 参数越界 clamp 到行
    BlendSpace2D row;
    row.AddSample({0.0f, 3.0f}, 0);
    row.AddSample({1.0f, 3.0f}, 1);
    auto w = row.Evaluate({0.5f, 99.0f});
    CHECK(w.size() == 2);
    CHECK(std::fabs(w[0].weight - 0.5f) < 1e-5f);
    CHECK(std::fabs(w[1].weight - 0.5f) < 1e-5f);

    // 单采样点：任意参数 clamp 后精确命中，返回该点基线
    BlendSpace2D single;
    single.AddSample({0.5f, 0.5f}, 7, 0.8f);
    w = single.Evaluate({-9.0f, 9.0f});
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 7);
    CHECK(std::fabs(w[0].weight - 0.8f) < 1e-5f);
}

TEST_CASE("BlendSpace.MergeAndOrder")
{
    using namespace BigHero::Scene;
    BlendSpace2D bs;
    // 同一动画占据多个采样点：权重合并
    bs.AddSample({0.0f, 0.0f}, 1);
    bs.AddSample({1.0f, 0.0f}, 1);
    bs.AddSample({0.0f, 1.0f}, 0);
    bs.AddSample({1.0f, 1.0f}, 0);
    auto w = bs.Evaluate({0.5f, 0.5f});
    CHECK(w.size() == 2);
    CHECK(w[0].animIndex == 0); // 按 animIndex 升序
    CHECK(std::fabs(w[0].weight - 0.5f) < 1e-5f);
    CHECK(w[1].animIndex == 1);
    CHECK(std::fabs(w[1].weight - 0.5f) < 1e-5f);

    // 零基线权重不输出（剩余角点按双线性系数输出：1.0 基线 × 0.5 系数）
    BlendSpace2D bs2;
    bs2.AddSample({0.0f, 0.0f}, 5, 0.0f);
    bs2.AddSample({1.0f, 0.0f}, 6);
    w = bs2.Evaluate({0.5f, 0.0f});
    CHECK(w.size() == 1);
    CHECK(w[0].animIndex == 6);
    CHECK(std::fabs(w[0].weight - 0.5f) < 1e-5f);
}

TEST_CASE("BlendSpace.StateMachineIntegration")
{
    using namespace BigHero::Scene;
    // 模型：3 条单节点动画。anim0 静止于 x=0，anim1 静止于 x=2，anim2 静止于 x=4。
    GltfModel model;
    model.nodeParents = {-1};
    model.nodeTranslations = {glm::vec3(0.0f)};
    model.nodeRotations = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    model.nodeScales = {glm::vec3(1.0f)};
    const float animXs[3] = {0.0f, 2.0f, 4.0f};
    for (const float x : animXs)
    {
        GltfAnimation anim;
        anim.name = "Static";
        GltfAnimationSampler sp;
        sp.times = {0.0f, 1.0f};
        sp.values = {glm::vec4(x, 0.0f, 0.0f, 0.0f), glm::vec4(x, 0.0f, 0.0f, 0.0f)};
        anim.samplers.push_back(sp);
        GltfAnimationChannel ch;
        ch.targetNode = 0;
        ch.path = "translation";
        ch.sampler = 0;
        anim.channels.push_back(ch);
        model.animations.push_back(anim);
    }

    // 混合空间：Speed(0..1) × Turn(0..1)；(0,0)->anim0、(1,0)->anim1、(0,1)/(1,1)->anim2
    BlendSpace2D blend;
    blend.AddSample({0.0f, 0.0f}, 0);
    blend.AddSample({1.0f, 0.0f}, 1);
    blend.AddSample({0.0f, 1.0f}, 2);
    blend.AddSample({1.0f, 1.0f}, 2); // 右上角复用 anim2

    AnimationStateMachine sm;
    sm.BindModel(&model);
    const int idle = sm.AddState("Idle", 0, 1.0f, true);  // 固定动画状态
    const int move = sm.AddState("Move", -1, 1.0f, true); // BlendSpace 状态（无固定动画）
    sm.SetInitialState(idle);
    sm.SetStateBlendSpace(move, &blend, "Speed", "Turn");

    // 未进入 BlendSpace 状态：固定动画采样（x=0）
    std::vector<glm::vec3> T, S;
    std::vector<glm::quat> R;
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(0.0f, 0.0f, 0.0f)) < 1e-4f);

    // 过渡进入 BlendSpace 状态
    sm.ForceTransition(move, 0.1f);
    sm.Update(1.0f);
    CHECK(sm.CurrentState() == move);
    CHECK(!sm.IsTransitioning());

    // 参数驱动权重：Speed=0.5、Turn=0 -> anim0/anim1 各半 -> x=1
    sm.SetFloat("Speed", 0.5f);
    sm.SetFloat("Turn", 0.0f);
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-4f);

    // Speed=1 -> anim1 -> x=2
    sm.SetFloat("Speed", 1.0f);
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(2.0f, 0.0f, 0.0f)) < 1e-4f);

    // Turn=1、Speed=0 -> anim2 -> x=4
    sm.SetFloat("Speed", 0.0f);
    sm.SetFloat("Turn", 1.0f);
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(4.0f, 0.0f, 0.0f)) < 1e-4f);

    // 中心点：anim0/anim1/anim2 各 0.25/0.25/0.5 -> x = 0.25*0 + 0.25*2 + 0.5*4 = 2.5
    sm.SetFloat("Speed", 0.5f);
    sm.SetFloat("Turn", 0.5f);
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(2.5f, 0.0f, 0.0f)) < 1e-4f);

    // 空混合空间：权重为空 -> 回退绑定姿态（x=0）
    BlendSpace2D empty;
    sm.SetStateBlendSpace(move, &empty, "Speed", "Turn");
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(0.0f, 0.0f, 0.0f)) < 1e-4f);

    // 解除绑定：恢复 animationIndex=-1 的绑定姿态
    sm.SetStateBlendSpace(move, nullptr, "", "");
    sm.SamplePose(model, T, R, S);
    CHECK(glm::distance(T[0], glm::vec3(0.0f, 0.0f, 0.0f)) < 1e-4f);
}
