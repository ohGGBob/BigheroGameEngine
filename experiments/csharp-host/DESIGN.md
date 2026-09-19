# C# 脚本系统（对标 Unity 工作流）—— 最小可行预研 Spike 设计文档

> 日期：2026-09-19 ｜ 性质：探索性预研 spike，**不接入引擎根构建**，全部产物限于
> `experiments/csharp-host/`。
>
> **纪律声明**：本文严格区分两类内容——
> - **【实测】**：在本机（Windows 10 x64，VS 18 Community，.NET SDK 10.0.401）实际执行并得到输出验证的结论，可复现（复现方式见 §2）；
> - **【文档】**：来自微软官方文档/源码的结论，未在本机做对应实验；引用均附链接。
> - **【推断】**：基于前两者的工程经验判断，需在后续阶段验证。

---

## 1. 结论（TL;DR）

**技术路线走得通。【实测】** hostfxr 嵌入 CoreCLR + 加载 net8.0 类库 + 双形态互操作
（delegate 与 `[UnmanagedCallersOnly]`）在本机一次跑通：

```
MathUtils.Add(2, 3)      = 5   [OK]
Native.AddNative(20, 22) = 42  [OK]
SPIKE PASSED
```

主要坑不在"能不能跑"，而在：hostfxr.dll 的**定位可靠性**（§5.1，实测踩雷）、
**collectible ALC 卸载不干净**（§6，文档+排查工具）、**GC 与主循环交互**（§5.3）、
运行时**分发体积**（§5.4，实测 71–77MB）。热重载（collectible ALC）本身是微软官方
支持场景，但卸载是"协作式"的，泄漏排查有成熟工具链（SOS/gcroot）。

---

## 2. Spike 实测记录（可复现）

### 2.1 环境【实测】

| 项 | 值 |
|---|---|
| OS | Windows 10 x64（10.0.26200），Git Bash |
| .NET SDK | **10.0.401**（`dotnet --list-sdks` 唯一条目） |
| 已装运行时 | Microsoft.NETCore.App **8.0.31 / 9.0.20 / 10.0.12**（`dotnet --list-runtimes`） |
| C++ 编译器 | 无 g++；MSVC cl：VS 18 Community（14.51）与 VS 2022 Community（14.44），经 vswhere 定位 |
| hostfxr.h 参考（只读） | `C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\10.0.12\runtimes\win-x64\native\hostfxr.h`、同目录 `coreclr_delegates.h`（未拷贝进仓库，host.cpp 为手写最小声明） |
| hostfxr.dll 实际位置 | `C:\Program Files\dotnet\host\fxr\{10.0.12, 9.0.20, 8.0.31}\hostfxr.dll` |

### 2.2 文件与构建方式【实测】

```
experiments/csharp-host/
├── .gitignore            # bin/ obj/ *.exe *.pdb
├── build-and-run.sh      # 一键：dotnet build → cl → 运行（Git Bash）
├── managed/
│   ├── Hello.cs          # 无 NuGet 依赖类库：MathUtils.Add + [UnmanagedCallersOnly] AddNative
│   └── Hello.csproj      # net8.0 + <EnableDynamicLoading>true</EnableDynamicLoading>
└── host/
    ├── host.cpp          # hostfxr 嵌入（LoadLibrary + GetProcAddress 手写签名）
    └── build.cmd         # vswhere 找最新 VS → vcvars64 → cl（CRLF + 纯 ASCII，cmd 批处理要求）
```

构建命令：

```bash
dotnet build managed/Hello.csproj -c Release          # → managed/bin/Release/net8.0/Hello.dll
cmd //c host/build.cmd                                 # → cl /std:c++20 /O2 /EHsc /W4 → host/host.exe
./host/host.exe                                        # SPIKE PASSED (exit=0)
```

### 2.3 host.cpp 流程（手写最小声明，不依赖 nethost/导入库）【实测】

1. `LoadLibraryW(L"hostfxr.dll")`（先裸加载）→ 失败则枚举
   `$DOTNET_ROOT 或 C:\Program Files\dotnet\host\fxr\<最高版本>\hostfxr.dll`；
2. `GetProcAddress` 取 `hostfxr_initialize_for_runtime_config`（`__cdecl`）、
   `hostfxr_get_runtime_delegate`、`hostfxr_close`；
3. `init_for_runtime_config(L"Hello.runtimeconfig.json", nullptr, &ctx)` 启动 CoreCLR；
4. `get_runtime_delegate(ctx, hdt_load_assembly_and_get_function_pointer /*=5*/, ...)`；
5. delegate 形态：`loadAsm(dll, L"Hello.MathUtils, Hello", L"Add", L"Hello.IntIntInt, Hello", …)` → 调 `Add(2,3)`；
6. UnmanagedCallersOnly 形态：`loadAsm(dll, L"Hello.Native, Hello", L"AddNative", UNMANAGEDCALLERSONLY_METHOD, …)` → 调 `AddNative(20,22)`；
7. `hostfxr_close(ctx)` + `FreeLibrary`。

### 2.4 过程中实际踩到的坑【实测】

| # | 现象 | 根因与修复 |
|---|---|---|
| 1 | `build.cmd` 被 cmd 解析成碎片报错 | Write 工具产出 **LF 换行**，cmd 批处理必须 **CRLF**；且中文注释在 GBK(936) 控制台乱码 → 批处理改为纯 ASCII + `sed -i 's/$/\r/'` 转 CRLF |
| 2 | cl C4819 警告 + 大量"未声明的标识符"/"字符串字面量中的换行符" | **UTF-8 无 BOM + 中文注释**在代码页 936 下被误解码，多字节序列吞掉引号/换行破坏语法 → 源文件加 **UTF-8 BOM**（与引擎仓库现有源文件约定一致，如 src/core/ecs.h 带 BOM） |
| 3 | cl C2131：`constexpr const char_t* p = reinterpret_cast<const char_t*>(-1);` | 整型转指针不是常量表达式；官方头用宏 `((const char_t*)-1)` 规避 → 改为普通 `const char_t* const` |
| 4 | `hostfxr_initialize_for_runtime_config` 返回 **0x80008093**，hostfxr 自报 "runtimeconfig.json does not exist" | **类库默认不生成 runtimeconfig.json**（只生成 deps.json）→ csproj 加 `<EnableDynamicLoading>true</EnableDynamicLoading>`（官方宿主示例同款；这正是引擎用户程序集将来需要的插件形态开关） |
| 5 | 裸 `LoadLibraryW(L"hostfxr.dll")` 居然命中，`GetModuleFileNameW` 一查 → **`C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\hostfxr.dll`** | PATH 上有一份 Windows Performance Toolkit 自带的 hostfxr.dll。本机碰巧兼容能跑，但这是**版本错配地雷**：引擎绝不能依赖裸 LoadLibrary，必须显式解析路径（见 §5.1） |
| 6 | 干净 PATH 复跑（`PATH="/c/Windows/System32:/c/Windows" ./host/host.exe`） | 回退逻辑正确命中 `C:\Program Files\dotnet\host\fxr\10.0.12\hostfxr.dll`，SPIKE PASSED —— 受控解析路径**实测验证** |

### 2.5 实测数据备忘

- `Hello.runtimeconfig.json`（EnableDynamicLoading 生成）：`tfm=net8.0`、
  `rollForward=LatestMinor`、framework `Microsoft.NETCore.App 8.0.0` → 本机实际启动的 CLR 为 **8.0.31**。
- 运行时体积（`du -sm`）：`shared/Microsoft.NETCore.App/8.0.31` = **71MB**、
  `10.0.12` = **77MB**、`WindowsDesktop.App/10.0.12` = 96MB、`host/fxr/10.0.12`（hostfxr 本体）≈ 1MB。
- 热重载配置已就位：runtimeconfig 中 `System.Reflection.Metadata.MetadataUpdater.IsSupported=false`（默认类库值）——若走 .NET **Hot Reload/delta-patch** 路线需改为 true；本方案热重载走 collectible ALC 整体卸载路线，不依赖该开关。

---

## 3. 架构（文字版）【推断，边界机制已实测】

```
┌────────────────────────────────────────────────────────────┐
│ C++ 引擎主循环（src/game, src/app）                          │
│   Frame(dt): Input → FixedUpdate → Update → Render (Vulkan) │
└──────────────┬─────────────────────────────────────────────┘
               │ 每帧 1~4 次跨界调用（批量 dispatch，非每实体一次）
┌──────────────▼─────────────────────────────────────────────┐
│ CSharpHost（C++ 薄封装层）                                   │
│  · 启动期：解析 hostfxr.dll → init_for_runtime_config        │
│    (BigHero.Runtime.runtimeconfig.json) → 取函数指针         │
│  · 暴露 C API 给引擎：ScriptHost_Init / Update / Reload /    │
│    Shutdown；错误码映射为引擎日志                             │
└──────────────┬─────────────────────────────────────────────┘
               │
┌──────────────▼─────────────────────────────────────────────┐
│ hostfxr / CoreCLR（进程内单实例，官方限制：单进程单运行时）     │
└──────────────┬─────────────────────────────────────────────┘
               │
┌──────────────▼─────────────────────────────────────────────┐
│ BigHero.Runtime.dll —— 引擎 API 投影（默认 ALC，永不热重载）   │
│  · C# 侧 API：Entity / Transform / Log / Input / Time / …    │
│  · [UnmanagedCallersOnly] 回调表 ←→ 引擎 exe 导出的 C API     │
│  · ScriptHost 托管侧：collectible ALC 管理器、脚本注册表       │
└──────────────┬─────────────────────────────────────────────┘
               │ AssemblyLoadContext(isCollectible: true)
┌──────────────▼─────────────────────────────────────────────┐
│ MyGame.dll —— 用户脚本程序集（可热重载）                      │
│   class MyPlayer : Behaviour { OnCreate/OnUpdate/OnDestroy } │
└────────────────────────────────────────────────────────────┘
```

要点：

- **两层托管程序集**：`BigHero.Runtime.dll`（引擎 API 投影）放默认上下文，随引擎进程存活；
  用户 `MyGame.dll` 放 collectible ALC。这样热重载只丢弃用户代码，引擎 API 层与已加载的
  基础库不被反复卸载/重载。
- **跨界双向**：
  - 下行（C++→C#）：hostfxr `load_assembly_and_get_function_pointer` 拿静态入口函数指针；
    高频路径用 `[UnmanagedCallersOnly]` 纯函数指针（无 delegate 分配，spike 已验证两种形态）。
  - 上行（C#→C++）：引擎启动时把一张 **C 函数指针表**（`UnmanagedCallersOnly` 委托封送
    或 `Marshal.GetFunctionPointerForDelegate` 固定后的指针）注册给托管侧；C# 用
    `[DllImport]`/函数指针调用。全程值类型/Blittable，无运行时 marshal 开销。
- **单次跨界批量 dispatch**：每帧一次 `ScriptSystem.Update(dt)` 进托管，由托管侧遍历
  脚本实例调用 `OnUpdate`——比"每实体一次 C++/C# 切换"少 2 个数量级的过渡开销【推断】。

## 4. Behaviour API 如何映射到自研 ECS【推断，基于 src/core/ecs.h 与 src/scene/EcsScene.h 实读】

引擎 ECS 现状（实读源码）：`Core::Entity` 为 **32 位打包句柄**（低 20 位 index、高位
version，防悬垂复用）；`Core::Registry` 用 **SparseSet 组件池**（dense+sparse，O(1)
增删查、swap-pop 移除），`View<Ts...>::Each` 迭代；`Transform`（position/欧拉角/均匀
scale）等组件在 `EcsScene` 中齐套创建（`CreateObject`），并有 `DestroyAll`/`Reserve`
等商业化路径。

映射方案：

| Unity 概念 | 本引擎映射 |
|---|---|
| `GameObject` | `Core::Entity`（32 位句柄值传给 C#，无指针跨界） |
| `MonoBehaviour` 基类 | `BigHero.Runtime.Behaviour`（C# 抽象类，持 `Entity Entity` 字段） |
| 生命周期 Awake/Start/Update/OnDestroy | `OnCreate`（CreateObject 齐套后）/ `OnUpdate(dt)` / `OnDestroy`（第一版三段即可） |
| 组件附加 | 引擎侧组件 `ScriptComponent { uint32 behaviourId }` 进 Registry 稀疏集池，脚本实体与普通组件用同一 `View`/池迭代 |
| Inspector 序列化字段 | 见 §5 |

关键设计约束：

1. **C++ 永远持数据，C# 持句柄**。托管对象实例（Behaviour 子类）由托管侧
   `Dictionary<int, Behaviour>`（behaviourId → 实例）持有；C++ 侧 `ScriptComponent`
   只存 `behaviourId`。C++ 对托管实例的强引用通过 **`GCHandle`（Normal）** 持有，
   销毁实体时 `Free`——天然 GC 移动安全，杜绝指针固化。
2. **Transform 读写走 API 调用**：`Transform`（glm::vec3/float，Blittable）经 C API
   `Transform_GetPosition(entity, vec3* out)` / `SetPosition` 读写；第一版不做共享内存
   映射，避免锁步 GC 与内存布局【推断】。`rotation` 是欧拉角（度），文档里写明与
   Unity 的四元数语义差异。
3. **生命周期调度挂在现有更新系统旁**：`EcsScene` 的组件化更新循环里，脚本更新独立成
   系统；实体销毁路径（`Registry::Destroy` → 组件池 `RemoveEntity`）需要回调托管侧
   释放 GCHandle 与调用 `OnDestroy`——在 `ScriptComponent` 池上挂移除通知即可，不动
   ecs.h 模板。
4. **句柄版本校验沿用引擎机制**：脚本缓存旧 Entity 句柄时，`Registry::Alive` 的
   version 检查天然防悬垂；C# API 层把失效句柄映射为异常或 `default`，与 Unity 的
   "destroyed object" 语义对齐。

## 5. Inspector 反射集成思路【推断】

1. **schema 导出（托管侧，一次性）**：`MyGame.dll` 加载后，`BigHost.Runtime` 用反射
   枚举 `Behaviour` 子类，读字段/属性与标注（`[Range]`、`[Tooltip]`、`[Header]`、
   `[HideInInspect]`……自建 Attribute 体系），生成扁平 schema（类型名/字段名/字段类型/
   约束），经上行回调注册到 C++ 编辑器。编辑器 UI 只依赖 schema，**不依赖 .NET**，
   与现有编辑器解耦。
2. **值读写**：编辑器路径用 `FieldInfo.GetValue/SetValue`（慢、无所谓）；运行时热路径
   若需，按字段缓存编译好的 getter/setter 委托【推断】。
3. **序列化**：场景里脚本字段落成 YAML/JSON 属性包（schema 驱动），对齐 Unity 场景
   文件形态；实体上的 `ScriptComponent` 增加 per-entity 的属性覆盖表。
4. **热重载时的 schema 比对**：新程序集加载后重新导出 schema，字段名+类型匹配的值
   恢复，缺失字段丢弃并告警——与 Unity 域重载行为一致。

## 6. 热重载：collectible AssemblyLoadContext【文档，官方文档实读核实】

来源：微软官方《How to use and debug assembly unloadability in .NET》
（https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability ，2026-03 更新，本次已抓取核实）。

### 6.1 卸载完成条件

`Unload()` 只是**发起**（协作式），真正完成需同时满足：

1. **没有任何线程**的调用栈上还挂着该 ALC 程序集的方法帧；
2. ALC 中的**程序集、类型、类型实例**没有被以下东西强引用（弱引用 `WeakReference`
   不算）：外部常规引用（栈槽/寄存器——含 **JIT 隐式局部**、静态变量）、**强
   GCHandle（Normal/Pinned）**。

检测完成的标准姿势（官方示例）：

```csharp
[MethodImpl(MethodImplOptions.NoInlining)]      // 防栈槽引用把 ALC 钉活
static void ExecuteAndUnload(string path, out WeakReference alcWeakRef) { … }

for (int i = 0; alcWeakRef.IsAlive && i < 10; i++) {
    GC.Collect();
    GC.WaitForPendingFinalizers();
}   // WeakReference 死亡 = 卸载完成
```

### 6.2 Unloading 事件

ALC 内代码可在 `alc.Unloading += …` 里做清理：停自建线程、释放强 GCHandle、注销全局
事件——官方明确这是"清强句柄/停线程"的挂点。托管侧重载管理器（在不可卸载的
`BigHero.Runtime.dll`）在 `Unloading` 里触发脚本 `OnDestroy` 与状态序列化。

### 6.3 C++ 侧重入流程【推断，机制为文档】

```
文件监视发现 MyGame.dll 变更（编译产出后触发）
→ 暂停脚本 dispatch（本帧起脚本不再执行）
→ 调托管重载入口：alc.Unload() + Unloading 里跑 OnDestroy/状态保存
→ 轮询 WeakReference 直至死亡（GC.Collect 循环，≤10 次）
→ ★ 卸载确认完成前不得覆盖写 DLL 文件（Windows 文件锁；LoadFromAssemblyPath
    会内存映射并持句柄）。规避方案：改用 hostfxr load_assembly_bytes 从内存字节
    加载，或先写临时文件再加载【文档+推断】
→ 新 collectible ALC 加载新 MyGame.dll → 反射重建 Behaviour 实例 → 恢复序列化状态
→ 恢复 dispatch
```

注意限制（官方）：**collectible ALC 内 ReadyToRun 代码会被忽略**（全 JIT），无
C++/CLI。故热重载程序集首帧有 JIT 开销；引擎 API 投影层放默认 ALC 不参与重载。

### 6.4 "卸载不干净"排查手段【文档】

泄漏根因清单（官方列表）：栈槽/寄存器隐式引用、静态变量、强 GCHandle、
**还活着的线程在跑 ALC 代码**、ALC 内部创建的非 collectible ALC 子类实例、
带 ALC 内回调的 `RegisteredWaitHandle`、**自定义 ALC 子类自己的字段**（卸载期间运行时
强持 ALC，必须把字段置空）。

工具链：

- **WinDbg + SOS**（官方推荐）：`!dumpheap -type LoaderAllocator` 找到该 ALC 的
  `LoaderAllocator` 对象地址 → `!gcroot <addr>` 列出钉活它的根链（栈槽/句柄/静态）→
  `~*e !clrstack` 查所有托管线程栈找残留帧；
- **dotnet-gcdump / dotnet-counters**（更友好）：重载前后各拍一份 gcdump 比对
  LoaderAllocator/残留类型实例；
- 工程化防线：重载管理器内置"卸载超时 + WeakReference 存活告警 + 自动 dump 根链"
  的自检日志【推断】。

## 7. 已知坑与风险汇总

| # | 坑 | 依据 | 对策 |
|---|---|---|---|
| 1 | **hostfxr.dll 定位**：裸 LoadLibrary 会命中 PATH 上任意副本（实测命中 Windows Performance Toolkit 的拷贝）；nethost/hostfxr 官方只支持 framework-dependent 部署 | 【实测】§2.4#5 + 【文档】hosting 文章 | 引擎显式解析（自带版本枚举逻辑，spike 已验证）或链接 nethost 的 `get_hostfxr_path`；发布形态随引擎分发 hostfxr.dll 与运行时，开发形态探测安装根 |
| 2 | 类库不生成 runtimeconfig.json，hostfxr 初始化报 0x80008093 | 【实测】§2.4#4 | 用户程序集 csproj 加 `<EnableDynamicLoading>true</EnableDynamicLoading>` |
| 3 | **ALC 卸载泄漏**（栈槽/静态/句柄/线程/ALC 字段） | 【文档】§6.4 | 强纪律：脚本不得自建常驻线程、不得把实例挂到非 ALC 静态；托管管理器统一持 GCHandle；自检日志+gcdump |
| 4 | **GC 线程与主循环**：GC 在独立线程跑，STW 会暂停正在托管代码里的引擎线程；GC 协程/finalizer 不在主线程 | 【文档+推断】 | 脚本热路径零分配约定（避免 Gen0 频繁触发）；上行 API 不在 finalizer/线程池回调里调引擎；引擎自建线程跨界前需 attach（托管回调自动 attach）；Workstation GC 起步，Server GC 作为选项 |
| 5 | **体积**：基础运行时 71MB(8.0.31)/77MB(10.0.12)，加桌面栈 96MB+ | 【实测】§2.5 | 随安装包分发选定版本运行时（或引导安装）；裁剪（trimming）对动态加载场景基本无效，按 71MB 规划安装包 |
| 6 | **调试**：vsdbg 需可移植 PDB；混合模式（native+managed）附加；collectible ALC 重载后旧断点失效 | 【文档+推断】 | 构建保持 PDB 随 DLL；编辑器一键"附加 vsdbg"；调试热重载时断点重下；泄漏用 SOS/gcdump 而非断点 |
| 7 | 单进程单 CLR；初始化参数不兼容直接失败 | 【文档】hosting 文章 Limitations | 引擎与脚本 SDK 版本强绑定；多游戏域不支持（Unity 同样是单域+重载） |
| 8 | collectible ALC 忽略 ReadyToRun | 【文档】§6.3 | 重载只影响用户程序集，JIT 首帧开销可控；性能关键内层逻辑下沉引擎 |
| 9 | MSVC + UTF-8 无 BOM 中文注释 → C4819/语法破坏；批处理必须 CRLF；整型转指针不能 constexpr | 【实测】§2.4#1-3 | 引擎 C++ 源码统一带 BOM（仓库现状即如此）；新写 .cmd 用 ASCII+CRLF |

## 8. 分项工作量估计【推断（经验估算，非实测）】

单位：人日。**生成** = 写出可编译代码/配置；**验证** = 测试、调通、踩坑修复、文档。
按"1 名熟悉引擎的 C++ 工程师 + 现有编辑器组配合"估算。

| 分项 | 内容 | 生成 | 验证 |
|---|---|---:|---:|
| P0 CSharpHost 封装层 | hostfxr 解析/加载/错误码、C API（Init/Update/Reload/Shutdown）、host.cpp 工程化入引擎构建 | 3 | 2 |
| BigHero.Runtime.dll 引擎 API 投影 | Entity/Transform/Log/Input/Time C# API + 上行 C 函数指针表 + 字符串/marshal 边界 | 5 | 3 |
| Behaviour 框架 | C# 基类、生命周期三段、GCHandle 表、批量 dispatch、实体销毁钩子 | 4 | 3 |
| 程序集加载/重载管理器 | collectible ALC + AssemblyDependencyResolver、WeakReference 完成检测、load_assembly_bytes 内存加载、卸载自检日志 | 5 | 5 |
| 热重载工程化 | 文件监视、重载时序、脚本状态序列化/恢复、schema 比对 | 6 | 6 |
| Inspector 反射 | Attribute 体系、schema 导出、值读写、编辑器 UI 对接、YAML/JSON 落盘 | 8 | 5 |
| 调试与工作流 | vsdbg 附加一键化、脚本项目模板、构建管线（dotnet build 集成 CI）、PDB 分发 | 3 | 2 |
| **合计** | | **34** | **26** |

≈ **60 人日**：1 人全职约 3 个月 MVP（对标 Unity 最小工作流：改脚本 → 保存 → 编译 →
热重载 → Inspector 调参），2 人并行约 1.5–2 个月。**前提风险**：#3/#4 的泄漏与 GC 问题
可能吃掉验证余量，建议先做"重载管理器+自检日志"再做编辑器集成。

## 9. 官方文档链接

已抓取核实（本次 spike 实际读取）：

1. Write a custom .NET runtime host（hostfxr 三步流程、单运行时限制、FDD 要求）
   https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting
2. How to use and debug assembly unloadability in .NET（collectible ALC 条件/事件/排查）
   https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability

官方参考（未逐字核验，指向 GitHub main 分支）：

3. hostfxr.h（函数签名权威来源）https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h
4. coreclr_delegates.h（UNMANAGEDCALLERSONLY_METHOD 等委托签名）
   https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h
5. 官方宿主示例仓库 https://github.com/dotnet/samples/tree/main/core/hosting
6. native hosting 设计文档 https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md
7. dotnet-gcdump（泄漏诊断）https://learn.microsoft.com/en-us/dotnet/core/diagnostics/dotnet-gcdump
8. UnmanagedCallersOnlyAttribute
   https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute

## 10. 实测 vs 文档推断 一览

| 结论 | 类别 |
|---|---|
| SDK 10.0.401 / 运行时 8.0.31–10.0.12 就绪；hostfxr.dll 位置 | 实测 |
| hostfxr 嵌入全流程 + Add(2,3)=5 + AddNative(20,22)=42 | 实测 |
| delegate 与 UnmanagedCallersOnly 两种互操作形态可用 | 实测 |
| 裸 LoadLibrary 的 PATH 地雷（WPT 副本）；fxr 版本枚举回退可用 | 实测 |
| EnableDynamicLoading 必要性（0x80008093） | 实测 |
| UTF-8 BOM / CRLF / constexpr 三个编译期坑 | 实测 |
| 运行时体积 71/77MB；runtimeconfig rollForward=LatestMinor | 实测 |
| collectible ALC 卸载条件、Unloading 事件、泄漏清单、SOS 排查 | 文档（官方，未本机实验） |
| 单进程单 CLR、FDD 限制、collectible 忽略 R2R | 文档（官方 hosting 文章） |
| 架构分层、Behaviour→ECS 映射、Inspector 方案、工作量数字 | 推断（后续阶段验证） |
