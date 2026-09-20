#pragma once
// 脚本公开字段 → Inspector（U1-S1d，C# 脚本系统第二增量第一部分）。
//
// 架构（experiments/csharp-host/DESIGN.md §5 "Inspector 反射集成思路"）：
//   - 反射全部在托管侧做：BigHero.Runtime.EditorSchema 扫描 Behaviour 子类的实例
//     字段/属性（public + [Editor] 标记的 private，属性要求 get;set;），把"字段描述表"
//     经上行 blittable 通道（NativeApiTable 第 9 入口）逐字段推给 C++；
//     System.Reflection 对象绝不越过边界。
//   - C++ 侧只持有两样东西：ScriptFieldSchema（描述表缓存，热重载失效重建）+
//     ScriptFieldView（挂接视图，getter/setter 上下文）；读值/写值走下行入口
//     ScriptApi.GetFieldValue / SetFieldValue（blittable ScriptFieldValue，20 字节）。
//   - 面板接线复用 InspectorModel 的读写器通道（PropertyDesc.get/set）：脚本字段的
//     PropertyDesc 不带 offset/ptr，只有 get/set，"component" 指针为逐字段
//     ScriptFieldContext（behaviourId + 字段下标 + clamp 范围），写字回 clamp 与滑杆语义一致。
//   - 撤销：字段值经 CSharpHost::CaptureFieldValues（Update 阶段逐帧拉取的绘制前基线）
//     与 ApplyFieldValues（Do/Undo 写回托管实例）进入既有手势命令栈。
//
// 本头全部为纯逻辑/纯数据（不触碰 CLR、Win32），可离线单测（test_inspector.cpp）。

#include "editor/InspectorModel.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace BigHero::Script
{
// ---- 字段种类（与托管侧 EditorSchema.FieldKind 数值一一对应，勿动） ----
enum class FieldKind : int32_t
{
    Float = 0,
    Int = 1,
    Bool = 2,
    Vec3 = 3,  // BigHero.Runtime.Vec3（blittable，与 glm::vec3 逐字节对应）
    Color = 4, // BigHero.Runtime.Color（rgb，与引擎色调 vec3 同布局）
};

// ---- 跨界 blittable 值（与 C# NativeApi.FieldValueData 布局逐字节对应：3 float + 2 int） ----
struct ScriptFieldValue
{
    float f[3]; // Float 用 f[0]；Vec3/Color 用 f[0..2]
    int32_t i;  // Int
    int32_t b;  // Bool（0/1）
};
static_assert(sizeof(ScriptFieldValue) == 20,
              "ScriptFieldValue 必须与 C# NativeApi.FieldValueData 逐字节对齐（20 字节）");

// 单字段描述（托管侧反射收集，经上行通道按下标顺序累积）
struct ScriptFieldDesc
{
    std::string name;
    FieldKind kind = FieldKind::Float;
    float minV = 0.0f;
    float maxV = 0.0f;
    bool hasRange = false; // [Range] 特性缺失时 false（不限制）
};

// 一个脚本类型（Behaviour 子类）的字段描述表
struct ScriptFieldSchema
{
    std::string typeName; // 如 "MyGame.Spinner"
    std::vector<ScriptFieldDesc> fields;
};

// 单类型字段数上限（防御：异常/失控反射不会灌爆描述表；超出部分丢弃并计数）
inline constexpr size_t kMaxScriptFieldsPerType = 64;

// MetaRegistry 键前缀（脚本组件与内建组件共用一张全局注册表，键名隔离）
inline constexpr const char* kScriptMetaKeyPrefix = "Script:";

// 脚本组件在 MetaRegistry 中的键（"Script:" + 类型全名）
[[nodiscard]] inline std::string ScriptMetaKey(const std::string& typeName)
{
    return kScriptMetaKeyPrefix + typeName;
}

// "场景"面板分组标题（如 "脚本 (MyGame.Spinner)"）
[[nodiscard]] inline std::string ScriptGroupTitle(const std::string& typeName)
{
    return "脚本 (" + typeName + ")";
}

// ---- 上行累积（C# EditorRegisterScriptField 回调的纯逻辑落地，单测覆盖） ----
// 按 typeName 归组、按 fieldIndex 顺序追加；类型名/字段名空、kind 非法、下标跳变或
// 超出上限时吞掉返回 false（调用方计数告警，不影响其余字段）。
inline bool AccumulateScriptField(std::vector<ScriptFieldSchema>& schemas, const char* typeName, int32_t fieldIndex,
                                  const char* fieldName, int32_t kind, float minV, float maxV, int32_t hasRange)
{
    if (typeName == nullptr || fieldName == nullptr || typeName[0] == '\0' || fieldName[0] == '\0')
        return false;
    if (fieldIndex < 0 || fieldIndex >= static_cast<int32_t>(kMaxScriptFieldsPerType))
        return false;
    if (kind < 0 || kind > static_cast<int32_t>(FieldKind::Color))
        return false;
    ScriptFieldSchema* schema = nullptr;
    for (ScriptFieldSchema& s : schemas)
    {
        if (s.typeName == typeName)
        {
            schema = &s;
            break;
        }
    }
    if (schema == nullptr)
    {
        schemas.emplace_back();
        schema = &schemas.back(); // emplace 后取指针（vector 扩张安全）
        schema->typeName = typeName;
    }
    if (schema->fields.size() != static_cast<size_t>(fieldIndex))
        return false; // 顺序守卫：描述表必须按下标递增累积（重建时从 0 重新开始）
    ScriptFieldDesc d;
    d.name = fieldName;
    d.kind = static_cast<FieldKind>(kind);
    d.minV = minV;
    d.maxV = maxV;
    d.hasRange = hasRange != 0;
    schema->fields.push_back(std::move(d));
    return true;
}

// ---- 值转换（跨界值 ↔ PropValue；写回方向带 clamp，与滑杆/拖拽语义一致） ----
[[nodiscard]] inline Editor::Inspector::PropValue ScriptFieldToPropValue(FieldKind kind, const ScriptFieldValue& v)
{
    using Editor::Inspector::PropValue;
    switch (kind)
    {
    case FieldKind::Float:
        return Editor::Inspector::FloatValue(v.f[0]);
    case FieldKind::Int:
        return Editor::Inspector::IntValue(v.i);
    case FieldKind::Bool:
        return Editor::Inspector::BoolValue(v.b != 0);
    case FieldKind::Vec3:
    case FieldKind::Color:
    {
        PropValue p;
        p.f[0] = v.f[0];
        p.f[1] = v.f[1];
        p.f[2] = v.f[2];
        return p;
    }
    }
    return PropValue{};
}

[[nodiscard]] inline ScriptFieldValue PropValueToScriptField(FieldKind kind, bool hasRange, float minV, float maxV,
                                                             const Editor::Inspector::PropValue& v)
{
    using Editor::Inspector::ClampIntRange;
    using Editor::Inspector::ClampToRange;
    auto clamp1 = [hasRange, minV, maxV](float x) { return hasRange ? ClampToRange(x, minV, maxV) : x; };
    ScriptFieldValue out{};
    switch (kind)
    {
    case FieldKind::Float:
        out.f[0] = clamp1(v.f[0]);
        break;
    case FieldKind::Int:
    {
        std::int64_t c = v.i;
        if (hasRange)
            c = ClampIntRange(c, static_cast<std::int64_t>(minV), static_cast<std::int64_t>(maxV));
        out.i = static_cast<int32_t>(c);
        break;
    }
    case FieldKind::Bool:
        out.b = v.b ? 1 : 0;
        break;
    case FieldKind::Vec3:
    case FieldKind::Color:
        out.f[0] = clamp1(v.f[0]);
        out.f[1] = clamp1(v.f[1]);
        out.f[2] = clamp1(v.f[2]);
        break;
    }
    return out;
}

// ---- Inspector 每行绘制上下文（自由 getter/setter 的唯一上下文，随挂接视图重建） ----
struct ScriptFieldContext
{
    int behaviourId = -1; // 当前 GCHandle 表项（热重载后随视图重建更新）
    int fieldIndex = 0;   // schema 字段下标（与 ComponentMeta.properties 对齐）
    FieldKind kind = FieldKind::Float;
    float minV = 0.0f;
    float maxV = 0.0f;
    bool hasRange = false; // 从描述表拷贝：写回 clamp 不依赖 desc（读写器签名只见 component）
};

// 挂接视图：一个绑定（实体稳定序下标 + 类型名）的逐字段上下文表（面板绘制与撤销拉取用）
struct ScriptFieldView
{
    std::string typeName;
    size_t orderIndex = 0;
    std::vector<ScriptFieldContext> fields;
};

// 撤销命令定格的绑定身份（热重载后 behaviourId 会变，按 身份 重定位当前绑定）
struct BindingId
{
    size_t orderIndex = 0;
    std::string typeName;
};
[[nodiscard]] inline bool operator==(const BindingId& a, const BindingId& b) noexcept
{
    return a.orderIndex == b.orderIndex && a.typeName == b.typeName;
}

// 一份挂接的全部字段值（与 ScriptFieldView.fields 一一对应）
using ScriptFieldTable = std::vector<ScriptFieldValue>;

// 值表相等（撤销手势"是否真的改了"判定；形状不一致按不等处理——保守记录）
[[nodiscard]] inline bool ScriptFieldTablesEqual(const std::vector<ScriptFieldTable>& a,
                                                 const std::vector<ScriptFieldTable>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].size() != b[i].size())
            return false;
        for (size_t k = 0; k < a[i].size(); ++k)
        {
            const ScriptFieldValue& x = a[i][k];
            const ScriptFieldValue& y = b[i][k];
            if (x.f[0] != y.f[0] || x.f[1] != y.f[1] || x.f[2] != y.f[2] || x.i != y.i || x.b != y.b)
                return false;
        }
    }
    return true;
}

// ---- 查询（CSharpHost.cpp 实现维护进程级缓存；未启用/无挂接返回空） ----
// 按实体稳定序下标查挂接视图（"场景"面板逐物体树节点调用；可变指针——写回路径把
// 逐字段上下文作为可变 component 传给 PropertyDesc.set）
[[nodiscard]] ScriptFieldView* FindScriptFieldView(size_t orderIndex);
// 按类型名查描述表（ApplyFieldValues 重定位字段数上限用）
[[nodiscard]] const ScriptFieldSchema* FindScriptFieldSchema(const std::string& typeName);
// 全部描述表（单测断言热重载重建用；重建前为空）
[[nodiscard]] const std::vector<ScriptFieldSchema>& AllScriptFieldSchemas();

// ---- ComponentMeta 构建：脚本字段 → Inspector 读写器通道 ----
// PropertyDesc.get/set 由宿主注入（跨 CLR 的自由函数）；label 指向 schema 持有的字段名
// ——schema 缓存与元数据同批重建（RebuildSchemas），label 生命周期由 schema 覆盖。
[[nodiscard]] inline Editor::Inspector::ComponentMeta MakeScriptComponentMeta(
    const ScriptFieldSchema& schema, Editor::Inspector::PropertyDesc::Getter get,
    Editor::Inspector::PropertyDesc::Setter set)
{
    using Editor::Inspector::PropertyDesc;
    using Editor::Inspector::PropStorage;
    using Editor::Inspector::PropType;
    Editor::Inspector::ComponentMeta m;
    m.typeName = ScriptMetaKey(schema.typeName);
    m.properties.reserve(schema.fields.size());
    for (const ScriptFieldDesc& f : schema.fields)
    {
        PropertyDesc d;
        d.label = f.name.c_str(); // 指向 schema 缓存的字段名（同批重建，生命周期覆盖使用期）
        switch (f.kind)
        {
        case FieldKind::Float:
            d.type = PropType::Float;
            d.storage = PropStorage::F32;
            break;
        case FieldKind::Int:
            d.type = PropType::Int;
            d.storage = PropStorage::I32;
            break;
        case FieldKind::Bool:
            d.type = PropType::Bool;
            d.storage = PropStorage::B1;
            break;
        case FieldKind::Vec3:
            d.type = PropType::Vec3;
            d.storage = PropStorage::V3F;
            break;
        case FieldKind::Color:
            d.type = PropType::Color;
            d.storage = PropStorage::V3F;
            break;
        }
        d.get = get;
        d.set = set;
        d.minV = f.minV;
        d.maxV = f.maxV;
        d.hasRange = f.hasRange;
        m.properties.push_back(d);
    }
    return m;
}
} // namespace BigHero::Script
