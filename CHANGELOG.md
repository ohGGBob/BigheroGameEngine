# BigheroGameEngine 变更日志

本文档记录各轮升级中**已独立编译验证并落地**的功能增强。
所有条目均在沙箱以 `g++ -std=c++20 -Wall -Wextra` 编译运行验证通过后镜像到本仓库，
并保留同名验证驱动与输出说明。

## [0.17.1] - 2026-09-16 —— TransformHierarchy 生产接线 + 实体 Parent 层级（阶段一 · 1.1 兑现）

- **接线（兑现 0.17.0 的「只建不接」欠账）**：`TransformHierarchy` 此前仅测试引用，
  现被生产渲染路径真正消费。
- `scene/Transform.h`：`TransformHierarchy` 增加**矩阵局部模式**（`ResetMatrices` /
  `SetLocalMatrix` / `SetParentIndex`），以及 TRS/矩阵两模式统一的 `parentOf`/`localMatrixOf`
  与迭代式 `ForceRecomputeAll`（不依赖「父索引 < 子索引」的隐式约定）。
- `scene/EcsScene.h`：
  - 新增 `Parent` 组件（空句柄 = 根），`SetParent` / `ClearParents`。
  - 新增层级世界矩阵缓存 `EnsureWorld()`（懒构建；`ForEachRenderableWorld` 消费）与
    增量更新 API（`SetObjectPosition/Rotation/Scale/SpinAngle`、`RecomputeWorld`）。
    局部矩阵直接复用 `Scene::ComputeEntityModelMatrix`，与旧逐实体路径逐位一致。
  - `UpdateSpins` 改为**增量标记**：仅令有自转速度的实体局部矩阵失效，其余节点复用缓存；
    空闲帧 `UpdateWorld()` 为 O(1)。结构/变换变更（创建/删除/Sync）标记整层重建。
- `app/Application.cpp`（1 处）与 `app/Application_Record.cpp`（6 处）：渲染路径由逐实体
  `ComputeEntityModelMatrix` 切换为 `ForEachRenderableWorld` 的世界矩阵（无 Parent 时等价于
  旧值，零回归；有 Parent 时按父子级联）。
- 新增 `tests/test_parent_hierarchy.cpp`（4 用例 / 131 断言）：世界矩阵 == 父子级联逐元素对照、
  无 Parent 时与逐实体路径逐位一致、增量重算计数（空闲 0 / 叶子 1 / 中段 / 根全树）、
  真实场景帧计时。
- 基准（10k 节点 × 100 帧，0.5% 实体/帧变化）：逐实体全量重建 81.22 ms vs 层级脏标记
  0.94 ms ≈ **86×**，平均每帧重算 50 节点（O(受影响节点)）。
- `UPGRADE_PLAN.md` 第 2 节新增准入纪律：性能类模块须附生产接线 commit 才算完成。
- 验证：g++-14 `-std=c++20 -O2 -Wall -Wextra` 0 警告；9 个纯逻辑模块合计
  **67 用例 / 1756 断言全通过**；改动文件 clang-format-19 `--dry-run --Werror` 通过。

## [0.17.0] - 2026-09-16 —— Transform 层级脏标记与按需重算（阶段一 · 1.1）

- 新增 `scene/Transform::TransformHierarchy`：在 `Transform` 之上维护每节点局部 TRS、
  世界矩阵缓存、脏子树根集合与子节点邻接表。
  - **空闲帧 O(1)**：无任何局部修改时 `UpdateWorld()` 不做矩阵运算，直接复用缓存。
  - **局部修改仅重算受影响子树**：`SetTranslation/SetRotation/SetScale/SetLocal` 标记该节点
    及其子树；祖先脏根吸收后代脏根，保证各脏根子树两两不相交，单趟可解。
  - 语义与既有 `ComputeAllWorldMatrices` 完全一致（相同 T*R*S 组合、相同父级级联、
    悬空父索引回退局部矩阵）；为**增量 API**，不改动既有全量路径（零回归风险）。
- 新增测试模块 `tests/test_transform_cache.cpp`（8 用例 / 91 断言）：
  首帧全量、叶子/中段编辑的精确重算计数、祖先-后代脏根吸收、随机编辑序列与全量级联
  逐元素对照、悬空父索引、`SetParent` 拓扑一致性、空层级，以及 10k 节点基准。
- 基准（10k 节点 × 200 帧，每帧改 1 节点）：全量级联 66.95 ms vs 脏标记 0.95 ms ≈ **70×**，
  平均每帧重算 182 节点（占 10000）。
- 验证：g++-14 `-std=c++20 -O2 -Wall -Wextra` 编译 0 警告；8 个纯逻辑模块合计
  **63 用例 / 1625 断言全通过**；改动文件 clang-format-19 `--dry-run --Werror` 通过。

## [0.16.2] - 2026-09-16 —— core/ 纯逻辑基础设施运行时测试补齐 + SmallVector 缺陷修复

- 修复 `core/SmallVector.h` 真实缺陷：`ptr()` 原先按 `size_ <= N` 判断取址，导致从堆
  （`heap_`）“回退”到内嵌容量区间时误读**已被迁移搬空**的内嵌缓冲，返回悬垂/脏数据
  （已用独立最小复现证明：旧实现 pop 回 size==N 后读到空串）。现改为以显式 `onHeap_`
  标志严格区分存储阶段，`Clear()`/`pop_back()` 归零时同步清空堆并复位标志。
- 新增 3 个纯逻辑测试模块（此前这些基础设施仅有 HeaderCheck 自包含编译检查，无运行时回归）：
  - `tests/test_containers.cpp`：SmallVector（含上述缺陷的回归断言）/ SparseSet / FixedArray /
    BitFlags / Result / Variant / Any / PropertyBag / StringBuilder —— 9 用例 / 136 断言。
  - `tests/test_utilities.cpp`：Easing / EasingCurve / CurveKey / StringUtils / PathUtils /
    Uuid（v4 位与 round-trip）/ ScopeGuard / Stopwatch / Hash(FNV/Murmur3) / StringId / Time
    —— 11 用例 / 134 断言。
  - `tests/test_memory_math.cpp`：MemoryArena（对齐/SaveMark-Rewind/块复用）/ PoolAllocator /
    ObjectPool / RingBuffer / BitVector / Vector3 / MathUtils —— 7 用例 / 122 断言。
- 清理 `core/MemoryArena.h` 的 `-Wunused-but-set-variable`（`Rewind` 中未使用的 `prev`）。
- CMakeLists `BigHeroTests` 源列表注册以上 3 个 cpp。
- 验证：g++-14 `-std=c++20 -Wall -Wextra` 编译 0 警告；7 个纯逻辑模块合计 **55 用例 / 1534 断言
  全通过**；改动文件 clang-format-19 `--dry-run --Werror` 全通过。

## [0.16.1] - 2026-09-15 —— 帧瞬态上传池 + core/ 二轮清理

- `render/FrameStaging`（新增）：每帧槽位一块常驻 host-visible arena，帧内 bump 分配切片，
  帧栅栏等待后整帧回收；拷贝并入帧命令缓冲（首个 pass 内 + TRANSFER→VERTEX_INPUT 屏障）。
  实例/粒子每帧上传从"逐帧 staging Buffer 创建-绑定-销毁 + 一次性提交（vkQueueWaitIdle
  全队列停顿 ×4/帧）"改为帧内瞬态拷贝；地面实例（数据恒定）改为初始化上传一次。
  TransientAllocator 至此不再是"测试专用"：图像别名复用（device-local）待接入，
  帧内主机→设备中转（host-visible）已接入。
- Application 拆分收尾：2125 → 1102 行（`Application_Record/Pipelines/Assets.cpp` 三翻译单元），
  达成 ≤1200 行目标。
- core/ 二轮清理：432 → 70 个头文件（删除 362 个未引用头，保留集 = engine+tests 引用闭包
  ∪ bighero:: 工具集）。

## [0.16.0] - 2026-09-14 —— 跨平台 + 渲染特性链 + 架构重构

本轮为引擎迄今最大一轮变更（339 个文件），全部经全量构建 + CTest（54 用例 / 1741 断言）验证，
已按模块拆分为 9 个 commit 落库（9501a6f..dd261d5）。

### 跨平台（Android / Linux / macOS）
- `platform/` 窗口抽象层：桌面 `GlfwWindow` 后端 + `android/AndroidWindow`（native_app_glue）后端，
  触摸→键鼠映射、双指滚轮、APK 资产经 AndroidAssets 落地；应用层经 AndroidAppSession 感知会话。
- 新增 `BigHero::Time` 稳定时间源替代 `glfwGetTime` 直调；CMake 预设补 linux-x64 / macos-arm64 /
  android-arm64；Gradle 打包工程（根 `platform/`）+ CI Android 编译校验 job。

### 渲染特性链
- 级联阴影贴图 CSM：4 级联 2x2 深度图集、实用分割法、纹素对齐、级间混合，前向/延迟/透明统一接入。
- 后处理扩展：体积雾（光线步进高度雾 + HG 相函数）、自动曝光（亮度金字塔 + eye adaptation）、
  暗角/胶片颗粒、雾效阴影采样 + God Rays、TAA（重投影 + 邻域 AABB 钳制）。
- glTF PBR 纹理映射与透明/自发光材质；glTF 动画编辑器整合（播放控制/时间轴/属性写回）。
- RenderGraph 声明式 pass 链 + 跨 pass 自动布局转换/同步。

### ECS
- `scene/EcsScene.h`：场景物体 = Registry 实体 + 组件，权威存储 + 与 SceneObject 双向包投影，
  渲染/编辑器/Gizmo/序列化/物理零改动消费投影；6 个专测覆盖。

### 架构重构（四项架构债清偿）
- 删除 src/app/systems/ 15 个未接线骨架，提取六个真实子系统（PostProcessSync/SceneIoHost/
  AnimationHost/NavHost/ParticleHost/PhysicsHost）；Application.cpp 2752 → 2125 行。
- Renderer.cpp 拆分 4 个翻译单元（1540 → 718 行）：主帧循环+渲染图 / 帧资源 / 延迟三通道 / 后处理。
- GpuAllocator 收敛：新增 `Render::MemoryPools` 双池门面（deviceLocal 2GB + hostVisible 512MB，
  块级持久映射），Buffer/Image/UboBuffer 统一子分配，消除裸 vkAllocateMemory。
- core/ 最小清理：679 → 432 头文件，删除 247 个错位/零引用头（UI/2D、渲染空壳、玩法物理、_v2）。

## [0.15.0] - 2026-09-06 —— 核心基础设施规模化新增·第3轮

本轮继续规模化新增 **纯标准库、可独立编译** 的核心模块（仅头文件，置于 `src/core`），
分两批验证后镜像落盘，确保 UTF-8 BOM。

### 第 1 批（7 模块）
- `StringBuilder.h` —— 高效字符串构建器：`Append/AppendLine/AppendRepeated/Str/Clear/Reserve`。
- `ScopeGuard.h` —— 作用域守卫：`MakeScopeGuard/MakeScopeExit`，RAII 退出时执行回调。
- `Delegate.h` —— 多播委托：`Bind/Broadcast/Unbind/UnbindAll`，类型安全多回调。
- `PropertyBag.h` —— 属性包：`Set/Has/Get/GetOr/Erase`，字符串键 + Any 值。
- `Color.h` —— 颜色容器：RGBA float + 8 位/HEX 转换 + Lerp/Blend。
- `Rect.h` —— 矩形：`Contains/Overlaps/Intersect/Inflate`，2D 几何。
- `BitVector.h` —— 位向量：`Set/Clear/Test/Toggle/CountSetBits/SetAll`，紧凑位存储。

### 第 2 批（7 模块）
- `Bezier.h` —— 贝塞尔曲线：二次/三次求值与切线。
- `PoolAllocator.h` —— 固定块内存池：O(1) Allocate/Free，消除碎片。
- `Profiler.h` —— 帧剖析器：命名区间 CPU 耗时统计 + `ScopedProfiler` RAII。
- `Logger.h` —— 分级日志器：`Trace/Debug/Info/Warn/Error` 分级过滤 + 线程安全。
- `FileSystemUtils.h` —— 文件系统工具：`ListFiles/ReadText/WriteText/GetSize/CreateDirs`。
- `Buffer.h` —— 字节缓冲：`Write/Read/Append/Resize`，读写位置指针。
- `Tween.h` —— 补间动画值：`Start/Update/SetEasing`，配合 Easing 驱动值过渡。

### 验证结果
- 第 1 批（7 模块）—— `BATCH8_OK pass=35 fail=0`。
- 第 2 批（7 模块）—— `BATCH9_OK pass=36 fail=0`。
- 14 个模块均已镜像至 `src/core/` 并确保 UTF-8 BOM（`src/core` 现共 48 个头文件）。

## [0.14.0] - 2026-09-06 —— 核心基础设施规模化新增·第2轮

本轮继续按"规模化新增商用引擎必备基础设施模块"策略，新增 13 个**纯标准库、可独立编译**的
核心模块（仅头文件，置于 `src/core`），分两批验证后镜像落盘，确保 UTF-8 BOM。

### 第 1 批（7 模块）
- `Variant.h` —— 类型安全变体：`Emplace/Has/Get/GetOr/TryGet/Type`，运行时类型安全读写。
- `Any.h` —— 类型擦除容器：`Set/Has/Get/TryGet`，任意值装箱。
- `PathUtils.h` —— 跨平台路径工具：`Normalize/Join/GetFileName/GetExtension/GetParentDir/ChangeExtension`。
- `Uuid.h` —— 通用唯一标识：`Generate(v4)/FromString/ToString/IsNil`，128 位。
- `BitFlags.h` —— 位标志容器：`Set/Unset/Test/Toggle/HasAny/HasAll/Count`。
- `Easing.h` —— 缓动函数库：`Linear/InQuad/OutQuad/InOutCubic/OutBack/OutElastic/SmoothStep`。
- `Base64.h` —— Base64 编解码：`Encode/Decode`。

### 第 2 批（6 模块）
- `Result.h` —— 错误处理结果类型：`Ok/Err/IsOk/IsErr/Value/ValueOr/ErrorOr/TryValue`。
- `FixedArray.h` —— 固定容量数组：`push_back/pop_back/At/Size/Capacity/Full`，零堆分配。
- `SmallVector.h` —— 小缓冲向量：容量小时用栈内嵌缓冲，超出转堆分配。
- `SparseSet.h` —— 稀疏集合：O(1) 插入/删除/查询，紧凑迭代。
- `ScopedTimer.h` —— 作用域计时器：RAII，析构输出耗时。
- `TypeTraits.h` —— 类型特性工具：`IsRangeV/IsCopyable/IsMovable/IsArithmetic/IsHashableV/MakeUnique`。

### 验证结果
- 第 1 批（7 模块）—— `BATCH6_OK pass=48 fail=0`。
- 第 2 批（6 模块）—— `BATCH7_OK pass=43 fail=0`。
- 13 个模块均已镜像至 `src/core/` 并确保 UTF-8 BOM（`src/core` 现共 34 个头文件）。

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
  - **[fixed] `WaitAll()` 原为假实现**（仅递增无用计数器即返回），已重写为基于 `pending_` 计数与条件变量的真正阻塞等待（任务完成时 `cv_.notify_all()` 唤醒等待者）；`WAITALL_OK pass=5 fail=0` 验证通过。
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
