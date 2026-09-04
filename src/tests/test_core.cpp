// 核心系统（ECS / 引用计数资源缓存 / CPU 帧剖析器 / 线程池）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
#include "core/AssetCache.h"
#include "core/FrameProfiler.h"
#include "core/ecs.h"
#include "framework/test_common.h"
#include "render/ThreadPool.h"

using namespace BigHero;

TEST_CASE("Core.ECS")
{
    // ---- ECS 组件系统（纯CPU） ----
    {
        using namespace BigHero::Core;

        // 实体生命周期：Create / Alive / Destroy
        Registry reg;
        const Entity e0 = reg.Create();
        const Entity e1 = reg.Create();
        CHECK(e0.Index() == 1); // index 0 保留为空实体哨兵
        CHECK(e1.Index() == 2);
        CHECK(reg.Alive(e0));
        CHECK(reg.Alive(e1));
        CHECK(!e0.IsNull()); // 有效实体值与空实体哨兵不可混淆

        // 销毁后 Alive 为 false
        reg.Destroy(e0);
        CHECK(!reg.Alive(e0));

        // index 复用 + version 递增：重建 e0 的 index 得到新版本
        const Entity e0b = reg.Create();
        CHECK(e0b.Index() == e0.Index());         // 复用同一 index
        CHECK(e0b.Version() == e0.Version() + 1); // version 递增
        CHECK(reg.Alive(e0b));
        CHECK(!reg.Alive(e0)); // 旧句柄失效（版本不匹配）

        // 组件增删查
        struct Health
        {
            int hp = 0;
        };
        struct Position
        {
            float x = 0, y = 0, z = 0;
        };

        reg.Add<Health>(e1, 100);
        CHECK(reg.Has<Health>(e1));
        CHECK(reg.Get<Health>(e1).hp == 100);
        CHECK(!reg.Has<Position>(e1));
        reg.Add<Position>(e1, 1.0f, 2.0f, 3.0f);
        CHECK(reg.Has<Position>(e1));
        CHECK(std::fabs(reg.Get<Position>(e1).x - 1.0f) < 1e-4f);
        CHECK(std::fabs(reg.Get<Position>(e1).z - 3.0f) < 1e-4f);

        reg.Remove<Health>(e1);
        CHECK(!reg.Has<Health>(e1));
        CHECK(reg.Has<Position>(e1)); // 移除一个组件不影响其他

        // View 迭代：只遍历同时拥有全部组件的实体
        reg.Add<Health>(e0b, 50);
        reg.Add<Position>(e0b, 7.0f, 8.0f, 9.0f);
        // e1 只有 Position（Health 已移除）-> 不应出现在 View<Health, Position> 中
        int seen = 0;
        MakeView<Health, Position>(reg).Each(
            [&](Health& h, Position& p)
            {
                ++seen;
                CHECK(h.hp == 50);
                CHECK(std::fabs(p.x - 7.0f) < 1e-4f);
            });
        CHECK(seen == 1);

        // 单组件 View 迭代数量
        int posCount = 0;
        MakeView<Position>(reg).Each([&](Position&) { ++posCount; });
        CHECK(posCount == 2); // e0b 与 e1

        // swap-pop 紧凑性：移除中间实体后 dense 中剩余实体仍有效、大小收缩
        Registry reg2;
        const Entity a = reg2.Create();
        const Entity b = reg2.Create();
        const Entity c = reg2.Create();
        reg2.Add<Position>(a, 1.0f, 0.0f, 0.0f);
        reg2.Add<Position>(b, 2.0f, 0.0f, 0.0f);
        reg2.Add<Position>(c, 3.0f, 0.0f, 0.0f);
        CHECK(reg2.Get<Position>(a).x == 1.0f);
        CHECK(reg2.Get<Position>(c).x == 3.0f);
        reg2.Destroy(b); // 销毁中间实体，移除组件
        CHECK(!reg2.Alive(b));
        CHECK(!reg2.Has<Position>(b));
        // a/c 组件仍可访问，池中剩余 2 个
        CHECK(reg2.Get<Position>(a).x == 1.0f);
        CHECK(reg2.Get<Position>(c).x == 3.0f);
        CHECK(reg2.Pool<Position>().Size() == 2);
    }
}

TEST_CASE("Core.ECS.Advanced")
{
    // ---- ECS 注册表增强（TryGet / Count / Clear / EntityCount，且查询无副作用） ----
    using namespace BigHero::Core;

    struct Health
    {
        int hp = 0;
    };
    struct Position
    {
        float x = 0, y = 0, z = 0;
    };

    // 1) 只读查询不创建组件池（无副作用）
    {
        Registry reg;
        const Entity e = reg.Create();
        CHECK(!reg.Has<Health>(e)); // 未注册过 Health，应安全返回 false
        CHECK(reg.TryGet<Health>(e) == nullptr);
        CHECK(reg.Count<Health>() == 0);
        reg.Remove<Health>(e); // 无副作用：不应崩溃或建池
        reg.Clear<Health>();   // 无副作用
        CHECK(reg.Count<Health>() == 0);
    }

    // 2) TryGet 读写往返 + Count 统计
    {
        Registry reg;
        const Entity a = reg.Create();
        const Entity b = reg.Create();
        reg.Add<Health>(a, 100);
        reg.Add<Health>(b, 50);
        CHECK(reg.Count<Health>() == 2);
        Health* h = reg.TryGet<Health>(a);
        CHECK(h != nullptr && h->hp == 100);
        h->hp = 80; // 通过指针修改
        CHECK(reg.Get<Health>(a).hp == 80);
        CHECK(reg.TryGet<Position>(a) == nullptr); // 未添加 Position
    }

    // 3) EntityCount 随生命周期增减
    {
        Registry reg;
        CHECK(reg.EntityCount() == 0);
        const Entity a = reg.Create();
        const Entity b = reg.Create();
        const Entity c = reg.Create();
        CHECK(reg.EntityCount() == 3);
        reg.Destroy(b);
        CHECK(reg.EntityCount() == 2);
        const Entity d = reg.Create(); // 复用 b 的 index
        CHECK(reg.EntityCount() == 3);
        (void)a;
        (void)c;
        (void)d;
    }

    // 4) Clear<T> 清空单类组件，实体保持存活
    {
        Registry reg;
        const Entity a = reg.Create();
        const Entity b = reg.Create();
        reg.Add<Health>(a, 1);
        reg.Add<Health>(b, 2);
        reg.Add<Position>(a);
        CHECK(reg.Count<Health>() == 2);
        CHECK(reg.Count<Position>() == 1);
        reg.Clear<Health>();
        CHECK(reg.Count<Health>() == 0);
        CHECK(!reg.Has<Health>(a));
        CHECK(reg.Alive(a) && reg.Alive(b)); // 实体仍存活
        CHECK(reg.Count<Position>() == 1);   // 其他组件不受影响
    }

    // 5) ClearAllComponents 清空所有组件，实体句柄仍有效
    {
        Registry reg;
        const Entity a = reg.Create();
        reg.Add<Health>(a, 7);
        reg.Add<Position>(a);
        CHECK(reg.EntityCount() == 1);
        reg.ClearAllComponents();
        CHECK(reg.Count<Health>() == 0);
        CHECK(reg.Count<Position>() == 0);
        CHECK(reg.Alive(a)); // 实体未被销毁
        CHECK(reg.EntityCount() == 1);
        // 清空后仍可重新添加
        reg.Add<Health>(a, 42);
        CHECK(reg.Get<Health>(a).hp == 42);
    }

    // 6) Reserve 预分配不破坏正确性
    {
        Registry reg;
        reg.Pool<Health>().Reserve(64);
        const Entity a = reg.Create();
        reg.Add<Health>(a, 9);
        CHECK(reg.Get<Health>(a).hp == 9);
        CHECK(reg.Count<Health>() == 1);
    }
}

TEST_CASE("Core.AssetCache")
{
    // ---- 引用计数 LRU 资源缓存（纯CPU） ----
    {
        using namespace BigHero::Core;

        struct Texture
        {
            int id = 0;
        };

        int loads = 0;
        AssetCache<Texture> cache(2,
                                  [&](const std::string& key)
                                  {
                                      ++loads;
                                      auto t = std::make_shared<Texture>();
                                      t->id = std::atoi(key.c_str());
                                      return t;
                                  });

        // 未命中加载并缓存
        auto a = cache.Load("10");
        CHECK(a != nullptr);
        CHECK(a->id == 10);
        CHECK(loads == 1);
        CHECK(cache.Size() == 1);

        // 命中：不重复加载，返回同一对象
        auto a2 = cache.Load("10");
        CHECK(loads == 1);          // 未再次调用工厂
        CHECK(a2.get() == a.get()); // 同一对象

        // 容量淘汰：容量 2，再加载 2 个（软上限：被引用条目不淘汰）
        auto b = cache.Load("20");
        auto c = cache.Load("30");
        CHECK(loads == 3);
        // 因 a/b/c 仍被外部引用，实际无法淘汰，Size 超容量（软上限）——验证引用保护：
        CHECK(cache.Size() == 3); // 三者均被引用，软容量不强制淘汰

        // 释放外部引用后再加载新键，应淘汰最旧的未引用条目
        a.reset();
        a2.reset();
        auto d = cache.Load("40");
        CHECK(loads == 4);
        CHECK(cache.Size() == 3);     // 淘汰 "10"（已无引用），保留 20/30/40
        CHECK(!cache.Contains("10")); // "10" 已被淘汰
        CHECK(cache.Contains("20"));
        CHECK(cache.Contains("30"));
        CHECK(cache.Contains("40"));

        // Get 不触发加载
        auto g = cache.Get("20");
        CHECK(g != nullptr && g->id == 20);
        CHECK(loads == 4);
        // 不存在的键 Get 返回 nullptr
        CHECK(cache.Get("nope") == nullptr);

        // Remove 手动移除
        cache.Remove("30");
        CHECK(!cache.Contains("30"));
        CHECK(cache.Size() == 2);

        // SetCapacity 缩小触发淘汰：释放全部外部引用后，应淘汰 LRU 端条目
        g.reset();
        b.reset();
        d.reset();
        cache.SetCapacity(1);
        CHECK(cache.Size() == 1);
        // 释放后仅剩一个未引用条目；LRU 端 "20" 被淘汰，MRU 端 "40" 保留
        CHECK(cache.Contains("40"));
        CHECK(!cache.Contains("20"));

        // 工厂返回 nullptr（加载失败）→ 不缓存
        AssetCache<Texture> failCache(4, [](const std::string&) { return std::shared_ptr<Texture>(); });
        auto f = failCache.Load("x");
        CHECK(f == nullptr);
        CHECK(failCache.Size() == 0); // 失败不缓存

        // Clear
        cache.Clear();
        CHECK(cache.Size() == 0);
        CHECK(cache.Empty());

        // AssetManager：按类型注册多个缓存并统一加载
        struct Mesh2
        {
            int m = 0;
        };
        AssetManager mgr;
        mgr.Cache<Texture>(4,
                           [&](const std::string& key)
                           {
                               auto t = std::make_shared<Texture>();
                               t->id = std::atoi(key.c_str());
                               return t;
                           });
        mgr.Cache<Mesh2>(2, [](const std::string&) { return std::make_shared<Mesh2>(); });

        auto ta = mgr.Load<Texture>("7");
        CHECK(ta != nullptr && ta->id == 7);
        auto ma = mgr.Load<Mesh2>("m0");
        CHECK(ma != nullptr);
        // 命中复用
        auto ta2 = mgr.Load<Texture>("7");
        CHECK(ta2.get() == ta.get());
        // 类型隔离：Texture 与 Mesh2 各自独立缓存
        CHECK(mgr.Cache<Texture>().Size() == 1);
        CHECK(mgr.Cache<Mesh2>().Size() == 1);
    }
}

TEST_CASE("Core.FrameProfiler")
{
    // ---- CPU 帧剖析器 ----
    {
        using namespace BigHero::Core;

        FrameProfiler profiler;

        // 空帧：BeginFrame 后无 Scope，EndFrame 记录总耗时
        profiler.BeginFrame();
        CHECK(profiler.Records().empty());
        profiler.EndFrame();
        CHECK(profiler.TotalMs() >= 0.0f);
        CHECK(profiler.HistoryCount() == 1);

        // 单 Scope：记录名称和正耗时
        profiler.BeginFrame();
        {
            FrameProfiler::Scope s(profiler, "TestScope");
            volatile int x = 0;
            for (int i = 0; i < 1000; ++i)
                x += i;
            (void)x;
        }
        profiler.EndFrame();
        CHECK(profiler.Records().size() == 1);
        CHECK(std::string(profiler.Records()[0].name) == "TestScope");
        CHECK(profiler.Records()[0].ms >= 0.0f);
        CHECK(profiler.Records()[0].ms <= profiler.TotalMs() + 0.01f); // scope 耗时不超过总耗时

        // 多 Scope：按析构顺序记录
        profiler.BeginFrame();
        {
            FrameProfiler::Scope s1(profiler, "Outer");
            {
                FrameProfiler::Scope s2(profiler, "Inner");
            }
        }
        profiler.EndFrame();
        CHECK(profiler.Records().size() == 2);
        CHECK(std::string(profiler.Records()[0].name) == "Inner"); // 内层先析构
        CHECK(std::string(profiler.Records()[1].name) == "Outer");

        // 帧率历史环形缓冲：连续多帧后历史数增长，不超过 kHistorySize
        for (int i = 0; i < 200; ++i)
        {
            profiler.BeginFrame();
            profiler.EndFrame();
        }
        CHECK(profiler.HistoryCount() == FrameProfiler::kHistorySize);

        // GetHistoryChronological：按时间顺序输出，最旧在前
        std::array<float, FrameProfiler::kHistorySize> buf{};
        const size_t n = profiler.GetHistoryChronological(buf.data(), buf.size());
        CHECK(n == FrameProfiler::kHistorySize);
        // 所有值非负
        for (size_t i = 0; i < n; ++i)
            CHECK(buf[i] >= 0.0f);

        // Fps() 计算
        CHECK(profiler.Fps() >= 0.0f);
    }
}

TEST_CASE("Core.ThreadPool")
{
    // ---- 多线程命令录制基础：线程池（纯逻辑） ----
    {
        using namespace BigHero::Render;
        // 并行执行 64 个任务：每个任务把其索引写入独立槽位，并递增执行计数
        ThreadPool pool(6);
        std::vector<int> slots(64, -1);
        std::atomic<int> executed{0};
        std::vector<std::function<void(uint32_t)>> tasks;
        tasks.reserve(64);
        for (int i = 0; i < 64; ++i)
            tasks.emplace_back(
                [i, &slots, &executed](uint32_t)
                {
                    slots[i] = i;
                    ++executed;
                });
        pool.Run(tasks);
        CHECK(executed.load() == 64);
        int correct = 0;
        for (int i = 0; i < 64; ++i)
            if (slots[i] == i)
                ++correct;
        CHECK(correct == 64);

        // 空任务安全
        pool.Run({});

        // 单线程退化（threadCount=0）：在当前线程顺序执行
        ThreadPool solo(0);
        std::atomic<int> n{0};
        std::vector<std::function<void(uint32_t)>> two;
        two.emplace_back([&n](uint32_t) { ++n; });
        two.emplace_back([&n](uint32_t) { ++n; });
        solo.Run(two);
        CHECK(n.load() == 2);
    }
}
