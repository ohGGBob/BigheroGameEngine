# BigheroGameEngine 变更日志

本文档记录各轮升级中**已独立编译验证并落地**的功能增强。
所有条目均在沙箱以 `g++ -std=c++20 -Wall -Wextra` 编译运行验证通过后镜像到本仓库，
并保留同名验证驱动与输出说明。

## [0.13.0] - 2026-09-06 —— 核心基础设施规模化新增（本轮）

本轮按"规模化新增商用引擎必备基础设施模块"策略，成批新增 13 个**纯标准库、可独立编译**的
核心模块（全部仅头文件，置于 `src/core`），每批均以 `g++ -std=c++20 -Wall -Wextra` 编译
运行验证通过后镜像落盘，并保留同名验证驱动。

### 本轮新增模块（13 个）
- `ObjectPool.h` —— 对象池模板：`Acquire/Release/ForEach/Reserve/Clear`，避免高频分配。
- `StringUtils.h` —— 字符串工具集：`Trim/ToLower/ToUpper/Split/Join/Replace/ParseInt/ParseFloat/ToString/Format`。
- `MemoryArena.h` —— 线性内存竞技场：`Allocate/AllocateAligned/SaveMark/Rewind/Reset/Emplace`（帧分配器）。
- `Stopwatch.h` —— 高精度计时器：`Start/Stop/Reset/Lap/Elapsed`。
- `BinarySerializer.h` —— 二进制序列化：`BinaryWriter/BinaryReader`，小端 + 边界校验。
- `HashUtils.h` —— 哈希工具：`Fnv1a32/Fnv1a64/Murmur3`。
- `MathUtils.h` —— 数学工具：`Clamp/Lerp/SmoothStep/IsPowerOfTwo/NextPow2/WrapDeg/DegreesToRadians`。
- `CRC32.h` —— CRC32 校验和（IEEE 0xEDB88320）：增量式 `Begin/Append/Get` + 一次性 `Compute`。
- `StringId.h` —— 字符串驻留池：`Intern/Resolve/IsInterned`，线程安全，字符串↔稳定 ID。
- `ThreadSafeQueue.h` —— 线程安全队列：`PushBack/PushFront/TryPopFront/TryPopBack/WaitPopFront`，带容量上限。
- `EventBus.h` —— 类型安全事件总线：`Subscribe/Publish/Unsubscribe/Clear`，多监听者、引用计数保活。
- `JobSystem.h` —— C++20 作业调度：`Execute/ParallelFor/Shutdown`，线程池 + 分块并行。
- `SignalSlot.h` —— 信号-槽：`Connect/Emit/Disconnect`，RAII 连接管理，类型安全、线程安全。

### 验证结果
- 第 1 批（ObjectPool/StringUtils/MemoryArena/Stopwatch）—— `NEWMOD_OK pass=27 fail=0`（含 ASAN 通过）。
- 第 2 批（BinarySerializer/HashUtils/MathUtils）—— `BATCH3_OK pass=23 fail=0`。
- 第 3 批（CRC32/StringId/ThreadSafeQueue）—— `BATCH4_OK pass=18 fail=0`。
- 第 4 批（EventBus/JobSystem/SignalSlot）—— `BATCH5_OK pass=8 fail=0`。
- 13 个模块均已 `cp` 镜像至 `src/core/`，并经 grep 稳定通道确认落盘。

## [0.12.0] - 2026-09-06 —— 核心基础设施商业化增强

本轮对纯标准库、可独立编译的核心/渲染/游戏/测试基础设施做系统性增强，
全部通过编译运行验证与集成兼容性校验（13 个增强头同时 include，INTEGRATION_OK 11/11）。

### 测试框架（src/tests/framework）
- `test_assert.h`
  - 新增**关系/容差断言**：`CHECK_EQ/NE/LT/LE/GT/GE`（任意可比较类型，不依赖 `operator<<`）与
    `CHECK_NEAR`（浮点容差）。比较失败只打印表达式与文件:行号，保证任意类型可编译。
  - 新增**致命断言 `REQUIRE`** 与 `TestAbort`：失败即打印并抛出内建异常中止当前用例，
    与 `CHECK`（累计所有失败）语义互补；`RunAllTests` 捕获后标记该用例 FAILED，不崩溃。
- `test_gltf_helpers.h`
  - 新增 `B64Decode()`（base64 解码，`B64Encode` 的逆操作，忽略空白/非法字符）。
  - 新增 `AppendU32()`（小端 uint32）与 `AppendF32x3()`（连续三个 float），
    供 glTF 测试 fixture 更完整地构造/校验二进制缓冲。

### 核心工具（src/core）
- `FrameProfiler.h`
  - 新增 `BuildSummary(maxResults)`：把当前帧各作用域按 name 聚合为
    count/total/avg/max 并按 max 降序排序，用于商业化性能面板定位 CPU 热点。
- `AssetCache.h`
  - 新增缓存效率统计 `Stats`（hits/misses/evictions + `HitRate()` 命中率 +
    `Total()`），并提供 `GetStats()/ResetStats()`，用于运行时缓存调优与诊断。
- `Log.h`
  - 新增可配置日志级别 `SetLogLevel()/GetLogLevel()`（低于级别丢弃，便于发布版关 Debug）。
  - 新增长度时间戳（HH:MM:SS.mmm）与全局互斥锁串行化输出（多线程日志不交叠）。
- `ecs.h`
  - 新增 `Registry::DestroyAll()`：一次性销毁全部存活实体（关卡重载/场景重置核心）。
  - 新增 `Registry::Reserve(entityCount, componentCountPerPool)`：批量预分配，减少 realloc。
  - `SparseSet` 补充 `Reserve()` 池容量预分配。
- `Random.h`
  - 新增有界整数生成 `NextUInt(bound)`（拒绝采样消除模偏差）与 `NextInt(min,max)`（含两端，min>max 自动交换）。
  - 新增 `Shuffle(first,last)` Fisher-Yates 均匀洗牌，避免依赖 MSVC 有问题的 `<random>`。
- **新增** `RingBuffer.h`
  - 通用固定容量 FIFO 环形缓冲区：`PushBack/PopFront/Front/Back/operator[]/Reserve/Clear`。
  - 连续数组 + 游标；空/满由 size 判定，避免 head==tail 歧义；满时不覆盖。

### 渲染/游戏/应用（src/render, src/game, src/app/systems）
- `ThreadPool.h`（src/render）
  - 新增 `ParallelFor(count, fn)`：按工作线程数切块并行处理连续区间，比手工收集任务更简洁。
  - 新增调度统计 `Stats`（submitted/completed/peakQueueDepth）+ `GetStats()/ResetStats()`。
- `CommandStack.h`（src/game）
  - 新增 `MaxDepth()/SetMaxDepth()`：查询/调整历史深度上限（缩小即时裁剪最旧命令）。
  - 新增 `ModificationCount()`：单调递增修改计数，用于编辑器"未保存改动"脏标记。
- `ISubSystem.h`（src/app/systems）
  - 新增 `SetEnabled()/Enabled()`：运行时启用/禁用子系统开关（默认启用，向后兼容）。
- `SceneCommand.h`（src/game）
  - **修复潜在编译错误**：`UndoRedoManager.h` 调用了 `SceneSnapshot::HasChanges()`，
    但原结构体只有自由函数 `SceneSnapshotsDiffer`，缺少该成员方法。已新增 `HasChanges`
    委托成员并修正前置声明顺序（`struct SceneSnapshot;` 先于函数声明）。

## 说明
- 沙箱环境无 Vulkan 头文件、无网络、无 root，故本轮聚焦**纯标准库、可独立编译**的增强，
  每步均以实编译运行验证，规避 git 提交通道在 FUSE 挂载上的不稳定性。
- 依赖 glm/Vulkan 的模块（如 `CameraController.h` / `SystemManager.h` / `Frustum.h`）
  因沙箱无法编译，未做改动；其增强建议在原生可编译环境 `D:\BigheroGameEngine` 进行。
