# BigHeroGameEngine 升级路线（2026-09-14 修订）

> 本文档自 2026-09-14 起替代重构前的旧路线图（该路线中的 P0 ECS 实体化、P1 测试工程化、
> 跨平台预设等均已落地，详见 `CHANGELOG.md` 0.16.0 与 README"架构重构（2026-09）"小节）。
> 依据 2026-09-14 外部复评（评分 8.8/10）与本仓库实际状态制定剩余路线。

---

## 0. 现状基线

| 维度 | 现状 | 评价 |
|---|---|---|
| 架构 | Application 拆 6 子系统（2752→2125 行）、Renderer 拆 4 TU（1540→718 行）、MemoryPools 统一显存 | 重构落地，仍有收尾 |
| 场景 | EcsScene 权威存储 + SceneObject 包双向投影 | 已落地 |
| 渲染 | 前向+延迟、CSM、IBL、SSAO/SSR、雾/TAA/自动曝光/GodRays 后处理链 | 功能深度足 |
| 跨平台 | Win/Linux/macOS/Android 预设 + CI；Android 实机待验证 | 代码齐备 |
| 测试 | 7 个模块文件、54 用例 / 1741 断言、HeaderCheck 自包含检查、ASan | 组织良好 |
| core/ | 432 个头文件（已清 247 个错位/零引用） | 仍有一批未引用工具头 |

---

## 1. 剩余路线（按优先级）

### P0 —— 复评建议收尾（进行中）

- **Application.cpp 继续拆分（2125 → ≤1200 行）**
  - 下一抽离目标：渲染录制路径（六个 Record* 回调中与子系统重复的场景录制逻辑）与资源装配段。
  - 形态沿用现有约定：构造注入、编辑器参数公有字段化、不引入框架基类。
- **core/ 剩余未引用头清理**
  - 方法：HeaderCheck（孤立 TU 编译）+ include 依赖闭包分析列清单 → 分类分轮删除
    （沿用 2026-09-14 Round A-D 的"每轮构建 + ctest 全绿"流程）。
  - 已知保留特例：`EasingCurve_v2/Plane3_v2/Sphere3_v2` 为 `test_foundation.cpp` 直接引用，非垃圾。

### P1 —— 显存与渲染收敛

- **TransientAllocator 接入帧资源生命周期**
  - 现状：有测试覆盖但为"测试专用"，逐帧 UBO 仍走 MemoryPools 常驻子分配。
  - 目标：接入帧内瞬态缓冲（逐帧 UBO/暂存上传），帧栅栏后整帧回收，替代逐帧分配-释放。
- **ECS 渲染端收敛**
  - 渲染路径目前消费 EcsScene 包投影；下一步将 Renderable 组件批次化抽离，
    消除 SceneObject 投影中间层（架构重构计划中预留的下一轮方向）。

### P2 —— 渲染管线健壮性与性能

- RenderGraph：在现有自动布局转换基础上补齐跨队列/所有权转移场景的屏障推导。
- 着色器 binding/set 常量化（集中头文件，消除散落硬编码）。
- Transform Hierarchy 脏标记与按需重算（当前每帧全量级联）。
- FrameProfiler(core) 与 GPU 时间戳数据统一 HUD。

### P3 —— 平台与工程化

- Linux(X11/Wayland) / macOS(MoltenVK) 实机渲染验证（预设与 CI 已就绪，缺实机回归）。
- Android 真机渲染冒烟（触摸输入、CSM/后处理在移动 GPU 上的性能档位）。
- 第三方边界隔离（stb 目录 submodule/FetchContent 化）。
- clang-tidy `WarningsAsErrors` 在新模块渐进开启。

---

## 2. 执行约定（沿用 2026-09 重构总原则）

1. 每项独立可构建、ctest 全绿后推进下一项；纯重构不改行为。
2. 每完成一项按模块独立 commit（ conventional commits：feat/refactor/chore/docs(scope) ）。
3. 涉及 Android 的改动需保持：imgui_impl_android 后端、128 字节 push constant、
   VK_USE_PLATFORM_ANDROID_KHR、Application 层不接触 android_app* 原生句柄。
