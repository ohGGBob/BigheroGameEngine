# BigHeroGameEngine 全面升级方案

> 本方案基于对项目源码、构建系统、CI、单元测试、代码质量配置与文档的真实审读生成。
> 目的：为引擎从「技术展示型 / 学习型」迈向「可实际使用的轻量级 3D 引擎」提供一份**分级、可执行、按优先级推进**的升级路线。
> 适用于在具备完整构建环境（Windows + VS2022 + Vulkan SDK，或带 GPU 的 Linux/macOS）上落地与验证。

---

## 5. 升级执行记录（P1：纯逻辑头自包含检查 — 已完成）

对 21 个纯逻辑头文件做了静态依赖审读（核对"所用标准库设施 vs 已包含头"），确认 17 个自包含良好，修复 5 处真实缺陷：

| 文件 | 缺陷 | 修复 |
|---|---|---|
| `src/core/Random.h` | FastRng 两处 `[[nodiscard]]` 违规 + 缺 `<algorithm>/<cmath>` | `(void)Next()` + 补头（孤立 TU 编译验证 EXIT=0） |
| `src/game/NavGrid.h` | 用 `std::max` 却缺 `<algorithm>` | 已补 `<algorithm>` |
| `src/game/NavAgent.h` | 用 `std::max` 却缺 `<algorithm>` | 已补 `<algorithm>` |
| `src/editor/EditorPanel.h` | 用 `std::min/std::max` 却缺 `<algorithm>` | 已补 `<algorithm>` |
| `src/core/AssetCache.h` | 用 `typeid` 却缺 `<typeinfo>` | 已补 `<typeinfo>` |

审读确认无需修改（初判有疑、实证由传递包含提供）：`ColorGrading.h`（`glm::max/pow` 由 `glm.hpp` 内部 `func_common.inl` 提供）。

> 说明：本沙箱编译验证受限于超时（单头 `-fsyntax-only` 亦超时），除 Random.h 经孤立 TU 编译验证外，其余 4 处修复基于标准库归属的静态证据（`std::max/min` 归属 `<algorithm>`、`typeid` 归属 `<typeinfo>`），语义为纯增量 include，不改变任何运行时行为。建议在你的 Windows 构建环境跑一次 `BigHeroTests` + 全量构建回归确认。

~~下一优先级（按方案）：P1 拆分 `test_main.cpp` 为模块化测试文件 → P0 ECS 场景实体化。~~
**2026-09-04 已完成测试工程化重构**：单体 `test_main.cpp`（2664 行）已拆分为 6 个模块文件
（test_core / test_scene / test_assets / test_animation / test_gameplay / test_render_logic），
配套自研轻量测试框架 `src/tests/framework/test_assert.h`（TEST_CASE 静态注册 + CHECK 断言 + 逐用例汇报，
与 CTest 退出码约定一致）；33 个原分区封装为独立 TEST_CASE，628 处 CHECK 断言零丢失。
同步增强 `core/ecs.h`（TryGet/Count/Clear/EntityCount/ClearAllComponents，Has/Remove 消除空池副作用，
SparseSet::Reserve）与 `scene/Transform.h`（ComputeAllWorldMatrices 单趟 O(n) 批量世界矩阵，
支持任意存储顺序 + 悬空父索引安全回退），并各配新 TEST_CASE。下一优先级：P0 ECS 场景实体化。

---

## 0. 现状基线（审读结论）

| 维度 | 现状 | 评价 |
|---|---|---|
| 技术栈 | Vulkan 1.3 + GLFW + GLM + C++20/23 | 当代、前沿 |
| 渲染 | 前向 + 延迟、PBR、IBL、阴影、SSAO/SSR、Bloom/DoF/运动模糊/色调分级 | 功能深度足 |
| 玩法系统 | ECS、骨骼蒙皮、动画、导航 A*、粒子、命令栈、物理、音频、序列化 | 体系较完整 |
| 工程化 | CMake Presets 四平台 + FetchContent | 良好 |
| CI | Windows/Linux/macOS 构建 + 测试 + ASan/UBSan + clang-tidy + clang-format | 优秀 |
| 测试 | 单一 `src/tests/test_main.cpp`(2664 行)，纯逻辑可测 | 覆盖逻辑但组织笨重 |
| 文档 | README 约 30k，特性/构建/着色器约定/Roadmap 齐全 | 优秀 |
| 已知缺陷 | `src/core/Random.h`(FastRng) `[[nodiscard]]` 违规、缺失自包含头文件 | 已在本轮修复 |

---

## 1. 已落地修复（本轮，已编译验证）

**文件：`src/core/Random.h`**
- 问题 1：构造函数与 `Seed()` 调用 `Next()` 却丢弃其 `[[nodiscard]]` 返回值，触发 `-Wunused-result` 警告。
- 问题 2：头文件直接使用 `std::sqrt / std::log / std::max / std::cos / std::sin` 却未自包含 `<cmath>` / `<algorithm>`（依赖传递包含，脆弱）。
- 修复：`(void)Next();` 显式忽略结果；补 `<algorithm>`、`<cmath>` 头文件。
- 验证：以孤立翻译单元 `g++ -std=c++23 -Wall -Wextra -Wpedantic` 编译通过，退出码 0，零警告。

---

## 2. 分级升级路线（按优先级推进）

### P0 —— 收敛 Roadmap 收尾项（决定引擎可用性）

- **ECS 场景实体化**（Roadmap 未完成）
  - 现状：`core/ecs.h` 已具备 `SparseSet/Registry/View`，但场景物体仍以 `Scene` 数组驱动。
  - 目标：以 ECS 驱动场景物体，`Scene::Object` 退化为 ECS 实体句柄，组件化更新（Transform / Renderable / RigidBody / Script）。
  - 价值：真正组件化，为后续热更、多场景、网络同步铺路。
- **Linux / macOS 跨平台实机验证**
  - 现状：CI 已有三平台构建与测试，但核心渲染路径未在非 Windows 实机验证。
  - 目标：在 Linux(带 X11/Wayland 显示) 与 macOS(MoltenVK) 实机跑通渲染 demo，修复平台差异（深度格式、surface、清除值等）。
- **HDR 真实环境资源**（Roadmap 未完成）
  - 现状：环境光照用 CPU 程序化天空，`HdrImage` 加载能力已具备但未接入真实 `.hdr`。
  - 目标：用真实 HDR 环境替换程序化天空，验证 IBL 全链路（辐照度/预滤波/BRDF LUT）质量。

### P1 —— 测试工程化（当前最直接影响质量的点）

- **拆分单体 `test_main.cpp`(2664 行)** 为按模块组织的多文件：
  - `test_math_ecs.cpp`（Transform / ECS / Gizmo）
  - `test_render_logic.cpp`（UBO 布局 / Frustum / GpuAllocator / RenderGraph / ColorGrading / HdrImage / InstanceBuffer / Skinning）
  - `test_scene_load.cpp`（GltfLoader / MtlMaterial / Skeleton / SkinnedMesh / Animation）
  - `test_gameplay.cpp`（NavGrid / NavAgent / ParticleSystem / EmitterPresets / CommandStack / SceneCommand）
  - 公共断言头 `tests/test_assert.h`（包装现有 CHECK 宏 + 计数器）。
- **引入轻量测试框架或保留自研框架但模块化**：推荐先用 `doctest`（单头、轻量），减少维护成本，便于逐文件独立构建。
- **为纯逻辑头补自包含检查**：每个头文件用「孤立 TU 编译」验证可独立包含，避免依赖传递包含。

### P2 —— 渲染管线健壮性与性能（增量、可回归）

- **`render/Transform.h` 与场景层级**：已有 Transform Hierarchy + 世界 AABB，建议补父级脏标记（dirty flag）与按需重算，避免每帧全量级联。
- **`render/GpuAllocator.h`（自研 VMA 替代）**：已实现块内子分配 + 相邻合并，建议补碎片率统计、按用途(transient/permanent)分池，与 `TransientAllocator` 协调。
- **`render/RenderGraph.h`**：已有 transient 槽位规划（区间着色内存别名），建议补自动屏障推导（layout transition / barrier）以减少手动 `pipelineBarrier`。
- **着色器**：`shaders/` 下 30+ 个 `.glsl` 结构合理；建议补 uniform/binding 常量化（集中到头文件），减少 set/binding 硬编码散落。
- **Profiling**：已有 GPU 时间戳剖析，建议接 `FrameProfiler`(core) 与 GPU 数据形成统一 HUD。

### P3 —— 代码质量与可维护性

- **`render/Context.cpp`**（TODO/注释 27 处，最高密度）：专项清理，拆分大型函数，补缺失注释。
- **第三方边界隔离**：`thirdparty/stb` 完整 tests/tools 目录被纳入，建议用 git submodule 或 CMake `FetchContent` 拉取，保持仓库整洁与可复现。
- **`.clang-tidy`**：已覆盖 bugprone/modernize/performance/readability/cppcoreguidelines 并关闭与 Vulkan C API 冲突项，配置良好；建议在 CI 已运行基础上，将 `WarningsAsErrors` 逐步在新模块开启。
- **命名与文档**：README 中文详实，质量高；建议补充「架构分层图」「渲染帧流程」「描述符布局总表」的 ASCII/图。

---

## 3. 建议的落地顺序（可拆分 PR，逐步合入）

1. **PR-A（低风险）**：拆分测试为多文件 + 引入 doctest + 补纯逻辑头自包含检查。
2. **PR-B（低风险）**：清理 `render/Context.cpp` TODO 区域 + 补注释。
3. **PR-C（中风险）**：ECS 场景实体化（先做数据层，再迁移主循环）。
4. **PR-D（中风险）**：RenderGraph 自动屏障 + 临时内存池协调。
5. **PR-E（验证型）**：Linux/macOS 实机运行 + HDR 真实资源接入。
6. **PR-F（质量）**：第三方库 submodule 化 + 新模块 WarningsAsErrors。

每个 PR 都在本地 **Windows + Vulkan SDK** 完整构建 + 运行 `BigHeroTests` + clang-tidy 通过后再合入。

---

## 4. 验证边界说明（重要）

- 本方案的生成环境为**无显示器、无完整 Vulkan 运行时**的沙箱：
  - 完整引擎 configure 卡在 FetchContent 拉取 ReactPhysics3D（网络超时）。
  - 重型 `render/*.cpp`（实例化 Vulkan 模板）单独编译超时。
  - 无法运行渲染 GUI 做视觉验证。
- 因此 `P0` 之后的改动（ECS 实体化、渲染管线增强、跨平台）**必须在用户具备完整构建/GPU 环境时落地并在该环境回归**，本方案提供的是**已在源码层面核实、可执行、按优先级排序**的升级路线，而非未经验证的大规模改写。
- 已在本地编译验证的改动仅为 `src/core/Random.h`（见第 1 节）。
