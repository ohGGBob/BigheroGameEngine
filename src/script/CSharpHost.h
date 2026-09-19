#pragma once
// C# 脚本系统宿主（U1-S1a/b/c 第一增量，对标 Unity 的核心差异化件）。
//
// 架构（experiments/csharp-host/DESIGN.md §3，spike 实测背书）：
//   C++ 主循环 ──每帧 1 次批量 dispatch──> hostfxr/CoreCLR ──> BigHero.Runtime.dll（默认 ALC，永不重载）
//                                                             └─> collectible ALC（用户 MyGame.dll，可热重载）
//   C# → C++ 上行走【原生函数指针表】（全 blittable，见 CSharpHost.cpp 的 NativeApiTable）；
//   C++ → C# 下行走 hostfxr load_assembly_and_get_function_pointer 的 delegate 形态。
//
// 实测坑对齐（DESIGN.md §2.4）：
//   - 禁止裸 LoadLibrary("hostfxr.dll")——PATH 上 Windows Performance Toolkit 副本会遮蔽；
//     本文件提供按 DOTNET_ROOT → 注册表 → 默认安装根顺序的显式解析，且解析逻辑拆为
//     纯函数（IsVersionDirectoryName / PickHighestVersionDirectory / CollectSearchRoots /
//     FindHostfxrUnderRoot / CollectDotnetCandidates），全部可离线单测（伪造目录结构）。
//   - 类库必须 <EnableDynamicLoading>true</EnableDynamicLoading> 生成 runtimeconfig.json。
//
// 优雅降级契约：Init() 任一步失败 → LOG_WARN 一次性说明 + 返回 false；引擎正常跑，
// Enabled()==false，Update() 为 no-op。绝不抛异常、绝不让 .NET 缺失拖垮引擎。
//
// 热重载（S1c）：每秒轮询用户脚本源码时间戳（未引入文件监听线程，成本报告见实现说明），
// 变更 → dotnet build -o 新版本目录（旧目录文件锁不影响新目录）→ 托管侧卸载旧
// collectible ALC（先解除全部 GCHandle，DESIGN.md §6.4 泄漏防线）→ 加载新程序集 → 重挂。

#include "script/ScriptFields.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BigHero::Scene
{
class EcsScene; // 前置声明：宿主只持指针，头文件不拖入 ECS 定义
}

namespace BigHero::Script
{
// ==================== 纯逻辑（可离线单测，不触碰注册表/环境变量/CLR） ====================

// 目录名是否为纯"数字+点"版本名（如 "8.0.31"）；空串/含其他字符/以点开头或结尾 → false
[[nodiscard]] bool IsVersionDirectoryName(const std::string& name);

// 从候选目录名挑最高版本（逐段数值比较）；无合法项返回空串
[[nodiscard]] std::string PickHighestVersionDirectory(const std::vector<std::string>& names);

// 候选根优先级排序：DOTNET_ROOT > 注册表根（依序）> 默认安装根；去重保序；空项跳过
[[nodiscard]] std::vector<std::string> CollectSearchRoots(const std::string& envDotnetRoot,
                                                          const std::vector<std::string>& registryRoots,
                                                          const std::string& defaultRoot);

// 文件系统探测（可用临时目录伪造目录结构单测）：
// 在 <root>/host/fxr/ 下枚举版本子目录取最高，命中 <root>/host/fxr/<ver>/hostfxr.dll
// 返回 UTF-8 绝对路径；未命中返回空串
[[nodiscard]] std::string FindHostfxrUnderRoot(const std::string& rootUtf8);

// dotnet.exe 候选目录（按优先级）：DOTNET_ROOT、默认安装根；供 FindDotnetExe 逐项探测
[[nodiscard]] std::vector<std::string> CollectDotnetCandidates(const std::string& envDotnetRoot,
                                                               const std::string& defaultRoot);

// ==================== 宿主 ====================

class CSharpHost
{
  public:
    CSharpHost() = default;
    ~CSharpHost();
    CSharpHost(const CSharpHost&) = delete;
    CSharpHost& operator=(const CSharpHost&) = delete;

    // 初始化：校验脚本目录 → 定位 dotnet → 显式解析 hostfxr → 启动 CoreCLR（以
    // BigHero.Runtime.runtimeconfig.json）→ 注册原生函数表 + API 版本校验 →
    // 编译/加载用户脚本程序集（collectible ALC）。任一步失败返回 false（优雅降级）。
    bool Init(Scene::EcsScene* scene, const std::string& scriptsDir);

    // 关闭：卸载用户程序集（OnDestroy + GCHandle 全释放 + ALC unload）→ hostfxr_close。
    // 幂等；未初始化时为 no-op。
    void Shutdown();

    // .NET 是否可用且脚本系统已就绪（Init 失败后恒为 false —— 优雅降级标志）
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }

    // 挂接脚本：把 typeName（如 "MyGame.Spinner"）实例绑定到稳定序第 orderIndex 个实体。
    // 返回 behaviourId（失败 -1）。绑定记录保留，热重载后按同一份记录自动重挂。
    int AttachToOrderIndex(size_t orderIndex, const std::string& typeName);

    // 当前挂接中的脚本数
    [[nodiscard]] uint32_t AttachedCount() const noexcept { return attachedCount_; }

    // 每帧调用：热重载轮询（每 1s 比对源码时间戳）+ 批量派发 OnStart/OnUpdate。
    // 未启用时为 no-op；不抛异常。
    void Update(float dt);

    // 显式热重载：重编译用户脚本（新版本目录，不动被锁旧文件）→ 卸载旧 ALC →
    // 加载新程序集 → 按绑定记录重挂。编译失败时保留旧脚本继续运行并返回 false。
    bool ReloadScripts();

    // 上一帧脚本派发耗时（毫秒，含跨界开销；未启用/无脚本时为 0）
    [[nodiscard]] float LastFrameScriptMs() const noexcept { return lastFrameScriptMs_; }

    // ==================== 脚本公开字段 → Inspector（U1-S1d） ====================
    // 描述表在程序集装载成功后由托管侧反射导出（上行通道累积进进程级缓存并注册
    // MetaRegistry）；字段值经下行 blittable 通道读写。热重载后描述表/挂接视图整体重建。

    // 读一个脚本实例字段值（behaviourId 失效/重载过渡/下标越界 → false，*out 清零）
    bool GetFieldValue(int behaviourId, int fieldIndex, ScriptFieldValue* out) const;
    // 写一个脚本实例字段值（Inspector 写回通道；clamp 由读写器上下文负责）
    bool SetFieldValue(int behaviourId, int fieldIndex, const ScriptFieldValue& value) const;

    // 拉取全部挂接绑定的当前字段值（撤销手势基线；未启用/无挂接时输出空表）
    void CaptureFieldValues(std::vector<ScriptFieldTable>& out) const;
    // 最近一份逐帧拉取的值表（Update 阶段抓取 = 本帧 UI 绘制前的值，手势起始快照来源）
    [[nodiscard]] const std::vector<ScriptFieldTable>& PolledFieldValues() const noexcept { return polledFieldValues_; }
    // 挂接身份表（撤销命令提交时定格，热重载后按身份重定位）
    [[nodiscard]] std::vector<BindingId> BindingIdentities() const;
    // 按（实体稳定序下标, 类型名）身份应用值表（撤销 Do/Undo；缺失绑定与越界字段跳过）。
    // 返回成功写入的字段数。
    int ApplyFieldValues(const std::vector<BindingId>& bindings, const std::vector<ScriptFieldTable>& values) const;

    // 描述表 / 挂接视图重建（Init / 挂接 / 热重载共用，实现在 CSharpHost.cpp）：
    // RebuildScriptSchemas = 清缓存 → 托管反射上行累积 → 注册 MetaRegistry（同名覆盖）；
    // RebuildScriptViews = 按挂接记录重建逐字段绘制上下文（behaviourId 取当前绑定）。
    void RebuildScriptSchemas();
    void RebuildScriptViews();

  private:
    struct Impl; // PIMPL：隔离 hostfxr 函数指针 / Win32 句柄 / 构建状态
    Impl* impl_ = nullptr;
    bool enabled_ = false;
    uint32_t attachedCount_ = 0;
    float lastFrameScriptMs_ = 0.0f;
    std::vector<ScriptFieldTable> polledFieldValues_; // 撤销手势基线（Update 阶段逐帧拉取）
};
} // namespace BigHero::Script
