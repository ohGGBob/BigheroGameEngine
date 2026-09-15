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
| core/ | 70 个头文件（两轮清理 679 → 432 → 70，保留集=引用闭包 ∪ 工具集） | 已收敛 |

---

## 1. 剩余路线（按优先级）

### P0 —— 复评建议收尾（已完成 2026-09-15）

- **Application.cpp 继续拆分（2125 → 1102 行）** ✅
  - 渲染录制路径抽至 `Application_Record.cpp`，管线管理抽至 `Application_Pipelines.cpp`，
    资产装配抽至 `Application_Assets.cpp`，主文件聚焦生命周期与更新逻辑。
- **core/ 剩余未引用头清理** ✅
  - 依赖闭包分析列 362 文件删除清单，432 → 70 个头文件；
    保留集 = engine+tests 引用闭包 ∪ bighero:: 工具集（曲线/噪声/数学/序列化/容器/性能剖析）。

### P1 —— 显存与渲染收敛

- **TransientAllocator 接入帧资源生命周期** ✅（2026-09-15，`render/FrameStaging`）
  - 新增帧瞬态上传池：每帧槽位一块常驻 host-visible arena，帧内 bump 分配切片，
    帧栅栏等待后整帧回收；拷贝并入帧命令缓冲（首个 pass 内 + TRANSFER→VERTEX_INPUT 屏障）。
  - 替代旧路径：实例/粒子每帧 staging Buffer 创建-绑定-销毁 + 一次性提交
    （SubmitOneTime 内含 vkQueueWaitIdle 全队列停顿 ×4/帧）。
  - 分工：`TransientAllocator` 面向渲染图瞬态图像的 device-local 别名复用（待接入）；
    `FrameStaging` 面向每帧主机→设备数据中转（已接入）。逐帧 UBO 保持 MemoryPools 常驻
    子分配（无逐帧分配-释放，描述符一次性绑定，刻意不迁移）。
- **ECS 渲染端收敛**
  - 渲染路径目前消费 EcsScene 包投影；下一步将 Renderable 组件批次化抽离，
    消除 SceneObject 投影中间层（架构重构计划中预留的下一轮方向）。
- **TransientAllocator 图像别名复用（待做）**
  - RenderGraph 已有 PlanTransientSlots 区间着色与内存报告；下一步在资源创建期
    对生命周期不重叠的离屏图像（SSAO/SSR/后处理中间图）做 `Image::CreateBound`
    池化绑定，并在渲染图中补齐别名覆写屏障。

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
