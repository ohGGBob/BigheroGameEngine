// 玩法逻辑（A* 寻路 / 导航代理 / 粒子预设与模拟 / 命令栈 / 场景快照命令）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
#include "framework/test_common.h"
#include "game/CommandStack.h"
#include "game/EmitterPresets.h"
#include "game/NavAgent.h"
#include "game/NavGrid.h"
#include "game/ParticleSystem.h"
#include "game/SceneCommand.h"
#include "scene/Scene.h"

using namespace BigHero;

TEST_CASE("Game.NavGrid")
{
    // ---- A* 导航寻路（纯CPU，网格 + 启发式开放集） ----
    {
        using namespace BigHero::Game;

        // 1) 直线可达（4 邻接，空网格）
        {
            NavGrid grid(8, 8);
            const PathResult r = grid.FindPath(0, 0, 5, 0);
            CHECK(r.found);
            CHECK(r.path.size() == 6); // (0,0)..(5,0) 共 6 格
            CHECK((r.path.front() == Cell{0, 0}));
            CHECK((r.path.back() == Cell{5, 0}));
            // 路径必须逐步且仅沿轴移动（相邻格曼哈顿距离=1）
            for (size_t i = 1; i < r.path.size(); ++i)
            {
                const int d = std::abs(r.path[i].x - r.path[i - 1].x) + std::abs(r.path[i].y - r.path[i - 1].y);
                CHECK(d == 1);
            }
        }

        // 2) 绕障：竖墙 (x=3, y=0..6) 留 y=7 缺口，路径须绕过
        {
            NavGrid grid(8, 8);
            for (int y = 0; y < 7; ++y)
                grid.SetBlocked(3, y);
            const PathResult r = grid.FindPath(0, 0, 7, 0);
            CHECK(r.found);
            CHECK((r.path.front() == Cell{0, 0}));
            CHECK((r.path.back() == Cell{7, 0}));
            // 路径中不得经过任何被阻挡格
            for (const Cell& c : r.path)
                CHECK(!grid.IsBlocked(c));
            // 绕行使长度 > 直线 8 格（直线本为 8，绕过缺口至少 10）
            CHECK(r.path.size() >= 10);
        }

        // 3) 不可达：整列墙完全封死
        {
            NavGrid grid(8, 8);
            for (int y = 0; y < 8; ++y)
                grid.SetBlocked(3, y);
            const PathResult r = grid.FindPath(0, 0, 7, 0);
            CHECK(!r.found);
            CHECK(r.path.empty());
        }

        // 4) 边界：越界 / 起终点自身阻挡 -> 空
        {
            NavGrid grid(8, 8);
            CHECK(grid.FindPath(-1, 0, 5, 5).path.empty());
            CHECK(grid.FindPath(0, 0, 9, 9).path.empty());
            grid.SetBlocked(0, 0);
            CHECK(grid.FindPath(0, 0, 5, 5).path.empty()); // 起点被阻挡
            grid.Clear();
            grid.SetBlocked(5, 5);
            CHECK(grid.FindPath(0, 0, 5, 5).path.empty()); // 终点被阻挡
        }

        // 5) 起点即终点：返回仅含该格的路径
        {
            NavGrid grid(8, 8);
            const PathResult r = grid.FindPath(2, 3, 2, 3);
            CHECK(r.found);
            CHECK(r.path.size() == 1);
            CHECK((r.path[0] == Cell{2, 3}));
        }

        // 6) 8 邻接对角：曼哈顿距离缩减为切比雪夫步数，且禁止切角
        {
            NavGrid grid(8, 8, /*allowDiagonal=*/true);
            grid.SetHeuristic(NavHeuristic::Octile);
            const PathResult r = grid.FindPath(0, 0, 4, 4);
            CHECK(r.found);
            // 对角移动：4 步对角可达，长度 = 5
            CHECK(r.path.size() == 5);
            // 全对角路径相邻格曼哈顿距离应为 2（除首格）
            for (size_t i = 1; i < r.path.size(); ++i)
            {
                const int d = std::abs(r.path[i].x - r.path[i - 1].x) + std::abs(r.path[i].y - r.path[i - 1].y);
                CHECK(d == 2);
            }

            // 切角防护（不变量）：返回路径中任何对角步的两正交邻格不得同时被阻挡。
            // 构造一处障碍簇，迫使路径出现对角步但不会穿角。
            NavGrid cg(8, 8, /*allowDiagonal=*/true);
            cg.SetHeuristic(NavHeuristic::Octile);
            cg.SetBlocked(3, 3);
            cg.SetBlocked(3, 4);
            cg.SetBlocked(4, 3);
            const PathResult cr = cg.FindPath(0, 0, 7, 7);
            CHECK(cr.found);
            for (const Cell& c : cr.path)
                CHECK(!cg.IsBlocked(c)); // 路径不穿任何阻挡格
            for (size_t i = 1; i < cr.path.size(); ++i)
            {
                const Cell a = cr.path[i - 1];
                const Cell b = cr.path[i];
                const int dx = b.x - a.x;
                const int dy = b.y - a.y;
                if (std::abs(dx) == 1 && std::abs(dy) == 1) // 对角步
                {
                    const bool orthH = cg.IsBlocked(a.x + dx, a.y); // 水平正交邻格
                    const bool orthV = cg.IsBlocked(a.x, a.y + dy); // 垂直正交邻格
                    CHECK(!(orthH && orthV));                       // 不得切角
                }
            }
        }

        // 7) 启发式等价性：4 邻接下曼哈顿与欧氏得到相同最优长度
        {
            NavGrid m(10, 10);
            NavGrid e(10, 10);
            e.SetHeuristic(NavHeuristic::Euclidean);
            for (int y = 0; y < 9; ++y) // 一道留缺口的竖墙
                m.SetBlocked(5, y), e.SetBlocked(5, y);
            const PathResult rm = m.FindPath(0, 0, 9, 0);
            const PathResult re = e.FindPath(0, 0, 9, 0);
            CHECK(rm.found && re.found);
            CHECK(rm.path.size() == re.path.size()); // 最优长度一致
        }

        // 8) 动态障碍：先阻挡后清除，路径由不可达变为可达
        {
            NavGrid grid(8, 8);
            for (int y = 0; y < 8; ++y)
                grid.SetBlocked(3, y);
            CHECK(!grid.FindPath(0, 0, 7, 0).found);
            for (int y = 0; y < 8; ++y)
                grid.SetBlocked(3, y, false); // 清除
            CHECK(grid.FindPath(0, 0, 7, 0).found);
        }

        // 9) 调试线：障碍与路径均有对应线段产出
        {
            NavGrid grid(4, 4);
            grid.SetBlocked(1, 1);
            const PathResult r = grid.FindPath(0, 0, 3, 3);
            const auto lines = grid.GetDebugLines(1.0f, glm::vec2(0.0f), &r);
            CHECK(!lines.empty());
            // 网格线数量 = (w+1)+(h+1) = 5+5 = 10
            size_t gridLines = 0;
            for (const auto& l : lines)
                if (glm::distance(l.color, glm::vec3(0.25f, 0.55f, 0.85f)) < 1e-3f)
                    ++gridLines;
            CHECK(gridLines == 10);
            // 障碍叉线：1 个障碍 -> 2 段
            size_t blockLines = 0;
            for (const auto& l : lines)
                if (glm::distance(l.color, glm::vec3(0.85f, 0.25f, 0.25f)) < 1e-3f)
                    ++blockLines;
            CHECK(blockLines == 2);
        }
    }
}

TEST_CASE("Game.NavAgent")
{
    // ---- AI 导航代理 NavAgent（纯CPU，沿 A* 路径插值移动 + 环形巡逻） ----
    {
        using namespace BigHero::Game;

        // 0) 无网格：规划必然失败，且无有效路径
        {
            NavAgent agent(nullptr);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            CHECK(!agent.Plan(Cell{0, 0}, Cell{5, 0}));
            CHECK(!agent.HasPath());
        }

        // 1) 空心网格上直线规划 + 单次小步插值
        {
            NavGrid grid(8, 8);
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            agent.SetSpeed(2.0f); // 2 格/秒

            CHECK(agent.Plan(Cell{0, 0}, Cell{5, 0}));
            CHECK(agent.HasPath());
            CHECK((agent.CurrentCell() == Cell{0, 0}));
            CHECK(!agent.Arrived());
            // 起点世界坐标 = 格中心 (0.5, 0, 0.5)
            CHECK(glm::distance(agent.Position(), glm::vec3(0.5f, 0.0f, 0.5f)) < 1e-3f);

            // dt=0.25 -> 剩余距离 0.5 世界单位，沿 +x 插值到 x=1.0
            agent.Step(0.25f, 1.0f, glm::vec2(0.0f));
            CHECK(glm::distance(agent.Position(), glm::vec3(1.0f, 0.0f, 0.5f)) < 1e-3f);
            CHECK((agent.CurrentCell() == Cell{0, 0})); // 尚未抵达下一格中心
            CHECK(!agent.Arrived());
        }

        // 2) 大步长一次抵达终点：position == 终点格中心，currentCell == 终点，arrived
        {
            NavGrid grid(8, 8);
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            agent.SetSpeed(2.0f);
            agent.Plan(Cell{0, 0}, Cell{5, 0});
            // 5 格世界距离 = 5.0，speed*cellSize*dt = 2*dt，dt=3.0 -> remaining 6.0 > 5.0
            agent.Step(3.0f, 1.0f, glm::vec2(0.0f));
            CHECK(agent.Arrived());
            CHECK((agent.CurrentCell() == Cell{5, 0}));
            CHECK(glm::distance(agent.Position(), NavAgent::CellToWorld(Cell{5, 0}, 1.0f, glm::vec2(0.0f))) < 1e-3f);
            // 抵达后继续 Step 不应改变状态
            agent.Step(1.0f, 1.0f, glm::vec2(0.0f));
            CHECK((agent.CurrentCell() == Cell{5, 0}));
        }

        // 3) 不可达：规划失败，无路径
        {
            NavGrid grid(8, 8);
            for (int y = 0; y < 8; ++y)
                grid.SetBlocked(3, y); // 封死的竖墙
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            CHECK(!agent.Plan(Cell{0, 0}, Cell{7, 0}));
            CHECK(!agent.HasPath());
        }

        // 4) 绕障路径跟随：代理逐格前进，最终落在目标点（不穿障碍即可）
        {
            NavGrid grid(8, 8);
            for (int y = 0; y < 7; ++y)
                grid.SetBlocked(3, y); // 留 y=7 缺口
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            agent.SetSpeed(4.0f);
            CHECK(agent.Plan(Cell{0, 0}, Cell{7, 0}));
            int guard = 0;
            while (!agent.Arrived() && guard++ < 10000)
                agent.Step(0.05f, 1.0f, glm::vec2(0.0f));
            CHECK(agent.Arrived());
            CHECK((agent.CurrentCell() == Cell{7, 0}));
        }

        // 5) 环形巡逻：points[0]->points[1]->points[2]->points[0]... 闭环
        {
            NavGrid grid(8, 8);
            grid.SetHeuristic(NavHeuristic::Octile);
            grid.Resize(8, 8, /*allowDiagonal=*/true);
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            agent.SetSpeed(4.0f);

            const Cell A{0, 0};
            const Cell B{7, 0};
            const Cell C{0, 7};
            agent.SetPatrolPoints({A, B, C});

            // 起始于 A（点[0]），第一次 PlanToNext 前往 B（点[1]）
            agent.Plan(A, A);
            CHECK(agent.Arrived());
            CHECK(agent.PlanToNext()); // -> B
            int guard = 0;
            while (!agent.Arrived() && guard++ < 10000)
                agent.Step(0.05f, 1.0f, glm::vec2(0.0f));
            CHECK((agent.CurrentCell() == B));
            CHECK(agent.PlanToNext()); // -> C
            guard = 0;
            while (!agent.Arrived() && guard++ < 10000)
                agent.Step(0.05f, 1.0f, glm::vec2(0.0f));
            CHECK((agent.CurrentCell() == C));
            CHECK(agent.PlanToNext()); // -> A（绕回）
            guard = 0;
            while (!agent.Arrived() && guard++ < 10000)
                agent.Step(0.05f, 1.0f, glm::vec2(0.0f));
            CHECK((agent.CurrentCell() == A));
            // 再一轮仍闭合：B
            CHECK(agent.PlanToNext());
            guard = 0;
            while (!agent.Arrived() && guard++ < 10000)
                agent.Step(0.05f, 1.0f, glm::vec2(0.0f));
            CHECK((agent.CurrentCell() == B));
        }

        // 6) 调试线：GetDebugLines 含朝向线 + 代理位置十字标记
        {
            NavGrid grid(8, 8);
            NavAgent agent(&grid);
            agent.SetWorldMapping(1.0f, glm::vec2(0.0f));
            agent.Plan(Cell{0, 0}, Cell{5, 0});
            const auto lines = agent.GetDebugLines();
            // 朝向线 1 + 十字 2 = 3 段
            CHECK(lines.size() == 3);
        }
    }
}

TEST_CASE("Game.EmitterPresets")
{
    // ---- 粒子发射器预设 EmitterPresets（纯CPU，配方可离线构造与校验） ----
    {
        using namespace BigHero::Game;

        // 1) 预设数量与名称匹配
        CHECK(EmitterPresetCount() == 4);

        // 2) 各预设构造不抛、字段合理（喷泉向上、爆发强扩散、烟雾慢升、火花极短寿命）
        {
            const EmitterPreset fountain = MakeEmitterPreset(0);
            CHECK(fountain.emitter.initialVelocity.y > 0.0f); // 向上喷
            CHECK(fountain.gravity.y < 0.0f);

            const EmitterPreset burst = MakeEmitterPreset(1);
            CHECK(burst.emitter.rate == 0.0f);           // 靠手动 Emit
            CHECK(burst.emitter.spawnRadius > 0.3f);     // 强扩散
            CHECK(burst.gravity.y < fountain.gravity.y); // 下坠更快

            const EmitterPreset smoke = MakeEmitterPreset(2);
            CHECK(smoke.emitter.rate > 0.0f);         // 连续发射
            CHECK(smoke.gravity.y > 0.0f);            // 轻微上浮
            CHECK(smoke.emitter.lifetimeMax >= 2.5f); // 长寿命

            const EmitterPreset spark = MakeEmitterPreset(3);
            CHECK(spark.emitter.lifetimeMax <= 0.8f); // 极短寿命
            CHECK(spark.emitter.sizeMax <= 0.2f);     // 极小尺寸
        }

        // 3) 越界索引回退到 0 号（喷泉）
        {
            const EmitterPreset oob = MakeEmitterPreset(99);
            CHECK(oob.emitter.initialVelocity.y > 0.0f);
            const EmitterPreset neg = MakeEmitterPreset(-1);
            CHECK(neg.emitter.initialVelocity.y > 0.0f);
        }

        // 4) 预设间应彼此不同（至少喷泉与火花在寿命/尺寸上区分明显）
        {
            const Emitter a = MakeEmitter(0);
            const Emitter b = MakeEmitter(3);
            CHECK(a.lifetimeMax != b.lifetimeMax);
            CHECK(a.sizeMax != b.sizeMax);
        }
    }
}

TEST_CASE("Game.ParticleSystem")
{
    // ---- 粒子系统（纯CPU模拟：发射/寿命/积分/容量/速率） ----
    {
        using namespace BigHero::Game;

        // 1) 手动爆发：发射数量等于存活数量（容量充足）
        {
            ParticleSystem ps(100);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 5.0f; // 长寿命，测试期间不消亡
            e.jitter = 0.0f;                      // 确定性
            ps.SetEmitter(e);
            ps.Emit(10);
            CHECK(ps.AliveCount() == 10);
            CHECK(ps.Capacity() == 100);
        }

        // 2) 寿命衰减：发射 1 个寿命 1.0s，更新 1.5s 后消亡
        {
            ParticleSystem ps(16);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 1.0f;
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.Emit(1);
            CHECK(ps.AliveCount() == 1);
            ps.Update(1.5f);
            CHECK(ps.AliveCount() == 0); // 寿命耗尽，回收
        }

        // 3) 速度积分（无重力、无阻尼）：位置 = 初速度 * dt
        {
            ParticleSystem ps(16);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 10.0f;
            e.initialVelocity = glm::vec3(0.0f, 10.0f, 0.0f);
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.SetGravity(glm::vec3(0.0f)); // 关闭重力，单独观察速度积分
            ps.Emit(1);
            ps.Update(1.0f);
            const auto& parts = ps.GetParticles();
            CHECK(parts[0].active);
            CHECK(std::fabs(parts[0].position.y - 10.0f) < 1e-3f);
            CHECK(std::fabs(parts[0].position.x) < 1e-4f);
        }

        // 4) 重力积分（显式欧拉）：v = g*dt，pos = v*dt
        {
            ParticleSystem ps(16);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 10.0f;
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.SetGravity(glm::vec3(0.0f, -10.0f, 0.0f));
            ps.Emit(1);
            ps.Update(1.0f);
            const auto& parts = ps.GetParticles();
            CHECK(std::fabs(parts[0].velocity.y - (-10.0f)) < 1e-3f);
            CHECK(std::fabs(parts[0].position.y - (-10.0f)) < 1e-3f);
        }

        // 5) 阻尼：速度随时间衰减
        {
            ParticleSystem ps(16);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 10.0f;
            e.initialVelocity = glm::vec3(10.0f, 0.0f, 0.0f);
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.SetGravity(glm::vec3(0.0f)); // 关闭重力，单独观察阻尼
            ps.SetDamping(1.0f);            // 每秒衰减比例 1.0
            ps.Emit(1);
            ps.Update(1.0f);
            const auto& parts = ps.GetParticles();
            CHECK(std::fabs(parts[0].velocity.x - 0.0f) < 1e-2f); // (1-1*1)=0
        }

        // 6) 对象池上限：容量 5，发射 10 个，存活不超过容量
        {
            ParticleSystem ps(5);
            Emitter e{};
            e.lifetimeMin = e.lifetimeMax = 10.0f;
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.Emit(10);
            CHECK(ps.AliveCount() == 5); // 环形覆盖最旧，数量封顶
            ps.Clear();
            CHECK(ps.AliveCount() == 0);
        }

        // 7) 速率发射：rate=10/s，更新 1.0s 生成约 10 个
        {
            ParticleSystem ps(64);
            Emitter e{};
            e.rate = 10.0f;
            e.lifetimeMin = e.lifetimeMax = 100.0f; // 长寿命，更新期间不消亡
            e.jitter = 0.0f;
            ps.SetEmitter(e);
            ps.Update(1.0f);
            CHECK(ps.AliveCount() == 10); // 10/s * 1s
            // 跨帧累积：再更新 0.5s 再多 5 个（已达 15）
            ps.Update(0.5f);
            CHECK(ps.AliveCount() == 15);
        }
    }
}

TEST_CASE("Game.CommandStack")
{
    // ---- 编辑器撤销/重做命令栈（纯逻辑） ----
    {
        using namespace BigHero::Game;

        // 测试命令：对外部计数器执行 +/-delta，撤销时反向
        struct CounterCmd : public Command
        {
            int* counter;
            int delta;
            const char* label;
            CounterCmd(int* c, int d, const char* l = "Counter") : counter(c), delta(d), label(l) {}
            void Do() override { *counter += delta; }
            void Undo() override { *counter -= delta; }
            const char* Name() const noexcept override { return label; }
        };

        // 1) 执行 -> 撤销：状态回到初始
        {
            CommandStack stack;
            int v = 0;
            stack.Execute(std::make_unique<CounterCmd>(&v, +1, "Inc"));
            CHECK(v == 1);
            CHECK(stack.CanUndo());
            CHECK(!stack.CanRedo());
            CHECK(std::string(stack.TopUndoName()) == "Inc");
            stack.Undo();
            CHECK(v == 0);
            CHECK(!stack.CanUndo());
            CHECK(stack.CanRedo());
            CHECK(std::string(stack.TopRedoName()) == "Inc");
        }

        // 2) 撤销 -> 重做：状态往返
        {
            CommandStack stack;
            int v = 0;
            stack.Execute(std::make_unique<CounterCmd>(&v, +5));
            stack.Undo();
            CHECK(v == 0);
            stack.Redo();
            CHECK(v == 5);
            CHECK(stack.CanUndo());
            CHECK(!stack.CanRedo());
        }

        // 3) 多命令连续撤销/重做链
        {
            CommandStack stack;
            int v = 0;
            stack.Execute(std::make_unique<CounterCmd>(&v, +1));
            stack.Execute(std::make_unique<CounterCmd>(&v, +2));
            stack.Execute(std::make_unique<CounterCmd>(&v, +4));
            CHECK(v == 7);
            stack.Undo();
            stack.Undo();
            CHECK(v == 1);
            stack.Redo();
            CHECK(v == 3);
            stack.Redo();
            CHECK(v == 7);
        }

        // 4) 新执行清空重做栈
        {
            CommandStack stack;
            int v = 0;
            stack.Execute(std::make_unique<CounterCmd>(&v, +1));
            stack.Undo();
            CHECK(stack.CanRedo());
            stack.Execute(std::make_unique<CounterCmd>(&v, +10));
            CHECK(!stack.CanRedo()); // 重做历史被新命令冲掉
            CHECK(v == 10);
        }

        // 5) 深度裁剪：超过 maxDepth 丢弃最旧命令
        {
            CommandStack stack(3);
            int v = 0;
            for (int i = 0; i < 5; ++i)
                stack.Execute(std::make_unique<CounterCmd>(&v, +1));
            CHECK(v == 5);
            CHECK(stack.UndoCount() == 3); // 仅保留最近 3 条
            // 撤销 3 次后栈空（最旧 2 条已被裁剪，无法撤销到初始 0）
            stack.Undo();
            stack.Undo();
            stack.Undo();
            CHECK(v == 2);
            CHECK(!stack.CanUndo());
        }

        // 6) Clear：清空全部历史
        {
            CommandStack stack;
            int v = 0;
            stack.Execute(std::make_unique<CounterCmd>(&v, +1));
            stack.Undo();
            CHECK(stack.CanRedo());
            stack.Clear();
            CHECK(!stack.CanUndo());
            CHECK(!stack.CanRedo());
            CHECK(v == 0);
        }
    }
}

TEST_CASE("Game.SceneCommand")
{
    // ---- 场景快照命令 SceneCommand（纯CPU：Do/Undo 还原场景 + 差异判定） ----
    {
        using namespace BigHero::Game;

        // 测试用快照目标：持有场景物体/自转/可见性，可还原到任意快照
        struct MockTarget : public SceneSnapshotTarget
        {
            std::vector<Scene::SceneObject> objects;
            std::vector<float> spins;
            std::vector<uint8_t> visibility;

            SceneSnapshot Snapshot() const override
            {
                SceneSnapshot s;
                s.objects = objects;
                s.spins = spins;
                s.visibility = visibility;
                return s;
            }
            void RestoreScene(const SceneSnapshot& snap) override
            {
                objects = snap.objects;
                spins = snap.spins;
                visibility = snap.visibility;
            }
        };

        // 构造两个物体（a=红、b=蓝）
        Scene::SceneObject a{};
        a.position = glm::vec3(0.0f, 0.0f, 0.0f);
        a.tint = glm::vec3(1.0f, 0.0f, 0.0f);
        a.scale = 1.0f;
        Scene::SceneObject b{};
        b.position = glm::vec3(5.0f, 0.0f, 0.0f);
        b.tint = glm::vec3(0.0f, 0.0f, 1.0f);
        b.scale = 2.0f;

        // 1) Do 还原 after、Undo 还原 before：移动 a + 改 b 颜色可完整往返
        {
            MockTarget t;
            t.objects = {a, b};
            t.spins = {0.0f, 30.0f};
            t.visibility = {1, 1};
            const SceneSnapshot before = t.Snapshot();

            Scene::SceneObject a2 = a;
            a2.position = glm::vec3(3.0f, 0.0f, 0.0f);
            Scene::SceneObject b2 = b;
            b2.tint = glm::vec3(0.0f, 1.0f, 0.0f); // 改绿
            SceneSnapshot after;
            after.objects = {a2, b2};
            after.spins = {0.0f, 30.0f};
            after.visibility = {1, 1};

            CHECK(SceneSnapshotsDiffer(before, after)); // before/after 确有差异

            SceneSnapshotCommand cmd(&t, before, after, "编辑物体属性");
            cmd.Do();
            CHECK(glm::distance(t.objects[0].position, glm::vec3(3.0f, 0.0f, 0.0f)) < 1e-4f);
            CHECK(t.objects[1].tint.g > 0.5f); // b 变绿

            cmd.Undo();
            CHECK(glm::distance(t.objects[0].position, glm::vec3(0.0f)) < 1e-4f);
            CHECK(t.objects[1].tint.b > 0.5f); // b 回到蓝
            CHECK(std::string(cmd.Name()) == "编辑物体属性");
        }

        // 2) 对象数变化的快照应被判为"差异"（增删场景也走此命令）
        {
            MockTarget t;
            t.objects = {a};
            t.spins = {0.0f};
            t.visibility = {1};
            const SceneSnapshot before = t.Snapshot();
            SceneSnapshot after = before;
            after.objects.push_back(b);
            after.spins.push_back(0.0f);
            after.visibility.push_back(1);
            CHECK(SceneSnapshotsDiffer(before, after));
        }

        // 3) 完全相同的快照应判为"无差异"（手势提交时跳过，避免空命令）
        {
            MockTarget t;
            t.objects = {a, b};
            t.spins = {0.0f, 30.0f};
            t.visibility = {1, 1};
            const SceneSnapshot s = t.Snapshot();
            CHECK(!SceneSnapshotsDiffer(s, s));
        }

        // 4) 经 CommandStack 撤销/重做闭环：Do→Undo→Redo 状态正确往返
        {
            MockTarget t;
            t.objects = {a};
            t.spins = {0.0f};
            t.visibility = {1};
            const SceneSnapshot before = t.Snapshot();
            Scene::SceneObject a2 = a;
            a2.position = glm::vec3(9.0f, 0.0f, 0.0f);
            SceneSnapshot after = before;
            after.objects[0] = a2;

            CommandStack stack;
            stack.Execute(std::make_unique<SceneSnapshotCommand>(&t, before, after, "移动物体"));
            CHECK(glm::distance(t.objects[0].position, glm::vec3(9.0f, 0.0f, 0.0f)) < 1e-4f);
            stack.Undo();
            CHECK(glm::distance(t.objects[0].position, glm::vec3(0.0f)) < 1e-4f);
            stack.Redo();
            CHECK(glm::distance(t.objects[0].position, glm::vec3(9.0f, 0.0f, 0.0f)) < 1e-4f);
            CHECK(std::string(stack.TopUndoName()) == "移动物体");
        }
    }
}
