#pragma once
// Inspector 属性元数据系统（纯逻辑）—— Unity 对标清单 U1-E2。
// 与 HierarchyModel.h 同一约定：纯函数、不依赖 ImGui/Vulkan，可离线单元测试。
//
// 设计：
//   - PropertyDesc 描述一条属性的中文标签 / 控件类型 / 底层存储 / 范围步长 / UI 提示；
//     数据源三选一：读写器（get/set，供脚本组件等非平凡存储）> 直接指针（ptr）>
//     成员偏移（offset，经 BindComponent 绑定组件基址后解析）。
//   - ComponentMeta 是一个组件的全部属性行（组件名 + 描述表）。
//   - MetaRegistry 按组件类型名注册/查找；全局注册表由引擎内建组件与后续
//     C# 脚本组件（U1-S1d）共用——脚本公开字段将来经同一元数据通道进面板。
//   - 数值 clamp/步进与"属性行脏判定"均为纯函数：面板据此渲染控件与提交撤销
//     手势（手势本身复用 Application 的 IsAnyItemActive 边沿 + SceneSnapshotCommand）。

#include "scene/Scene.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BigHero::Editor::Inspector
{
// ---- 属性种类：控件类型与底层存储解耦（Enum 的底层可以是 u8/u32/i32） ----
enum class PropType : uint8_t
{
    Float,  // DragFloat / SliderFloat
    Int,    // DragInt / SliderInt
    Bool,   // Checkbox
    Vec3,   // DragFloat3（三连浮点，glm::vec3 布局）
    Color,  // ColorEdit3（rgb 三连浮点）
    Enum,   // Combo（选项表见 PropertyDesc::enumItems）
    String  // InputText（char 缓冲，容量见 strCapacity）
};

// 底层存储布局（决定指针读写方式；与 SceneObject 字段类型一一对应）
enum class PropStorage : uint8_t
{
    F32, // float
    I32, // int32_t
    U32, // uint32_t
    U8,  // uint8_t
    B1,  // bool
    V3F, // 3 连 float（glm::vec3）
    Str  // char 缓冲
};

// 控件形态提示（Auto = 按类型默认：Float/Vec3→Drag，有范围且标记 Slider→Slider）
enum class UiHint : uint8_t
{
    Auto,
    Drag,
    Slider
};

// 统一属性值：属性行构建 / 脏判定的纯数据载体（与具体存储解耦）
struct PropValue
{
    float f[3] = {0.0f, 0.0f, 0.0f}; // Float 用 f[0]；Vec3/Color 用 f[0..2]
    std::int64_t i = 0;              // Int/Enum（u8/u32 存储提升后）
    bool b = false;                  // Bool
    std::string s;                   // String

    [[nodiscard]] friend bool operator==(const PropValue&, const PropValue&) = default;
};

// PropValue 便捷构造（面板与测试共用）
[[nodiscard]] inline PropValue FloatValue(float v)
{
    PropValue p;
    p.f[0] = v;
    return p;
}
[[nodiscard]] inline PropValue Vec3Value(const glm::vec3& v)
{
    PropValue p;
    p.f[0] = v.x;
    p.f[1] = v.y;
    p.f[2] = v.z;
    return p;
}
[[nodiscard]] inline PropValue IntValue(std::int64_t v)
{
    PropValue p;
    p.i = v;
    return p;
}
[[nodiscard]] inline PropValue BoolValue(bool v)
{
    PropValue p;
    p.b = v;
    return p;
}
[[nodiscard]] inline PropValue StringValue(std::string v)
{
    PropValue p;
    p.s = std::move(v);
    return p;
}

// 单条属性描述
struct PropertyDesc
{
    using Getter = PropValue (*)(const void* component);
    using Setter = void (*)(void* component, const PropValue& value);

    const char* label = nullptr;            // 中文名（兼作 ImGui 控件 ID）
    PropType type = PropType::Float;        // 控件种类
    PropStorage storage = PropStorage::F32; // 底层存储布局
    // 数据源三选一：读写器（get/set 优先）> 直接指针（ptr）> 成员偏移（offset，绑定后解析）
    Getter get = nullptr;
    Setter set = nullptr;
    void* ptr = nullptr;
    size_t offset = 0;
    // 范围与步长
    float minV = 0.0f;
    float maxV = 0.0f;
    float step = 0.0f;     // 步长：浮点=拖拽灵敏度（ImGui speed）；整数=吸附网格（QuantizeStep）
    bool hasRange = false; // 是否启用 min/max 范围（滑杆与写回 clamp）
    // UI 提示
    UiHint uiHint = UiHint::Auto; // Auto=按类型默认
    const char* format = nullptr; // 数值显示格式（nullptr=ImGui 默认）
    const char* hint = nullptr;   // 提示文本（预留 tooltip）
    const char* group = nullptr;  // 分组标题（非空时绘制分隔线 + 标题，如"物理"）
    // 枚举选项（type==Enum 时必填）
    const char* const* enumItems = nullptr;
    int enumCount = 0;
    // 条件可见 / 变更副作用
    bool (*visibleIf)(const void* component) = nullptr; // 依赖其他字段的可见性（物理子面板）
    bool rebuildPhysics = false;                        // 修改后需重建刚体（面板转 physicsRebuildRequested）
    int strCapacity = 0;                                // storage==Str 的缓冲容量（含 '\0'）
};

// 一个组件的元数据：组件类型名 + 全部属性行
struct ComponentMeta
{
    std::string typeName;
    std::vector<PropertyDesc> properties;
};

// 属性元数据注册表：按组件类型名注册/查找。
// 可实例化（测试用局部表）；Global() 为全局单例（内建组件 + 后续脚本组件共用）。
class MetaRegistry
{
  public:
    // 注册（同名覆盖，便于脚本重载后刷新字段表）
    void Register(ComponentMeta meta)
    {
        std::string key = meta.typeName;
        components_[key] = std::move(meta);
    }
    // 按组件类型名查找；不存在返回 nullptr
    [[nodiscard]] const ComponentMeta* Find(const std::string& typeName) const
    {
        const auto it = components_.find(typeName);
        return it == components_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] size_t Count() const noexcept { return components_.size(); }

    static MetaRegistry& Global()
    {
        static MetaRegistry instance;
        return instance;
    }

  private:
    std::unordered_map<std::string, ComponentMeta> components_;
};

// ---- 绑定：把"偏移描述"解析为"指针描述"（组件地址每帧可能变化，逐帧重绑） ----
// 数据源优先级：读写器 > ptr > offset+base。返回的描述仍引用原 enumItems 等静态数据。
[[nodiscard]] inline PropertyDesc BindDesc(const PropertyDesc& d, void* base)
{
    PropertyDesc b = d;
    if (b.ptr == nullptr && base != nullptr && b.get == nullptr)
        b.ptr = static_cast<std::byte*>(base) + b.offset;
    return b;
}
[[nodiscard]] inline ComponentMeta BindComponent(const ComponentMeta& meta, void* base)
{
    ComponentMeta bound;
    bound.typeName = meta.typeName;
    bound.properties.reserve(meta.properties.size());
    for (const PropertyDesc& d : meta.properties)
        bound.properties.push_back(BindDesc(d, base));
    return bound;
}

// 解析属性当前存储地址（读写器路径不走此处）
[[nodiscard]] inline void* ResolvePtr(const PropertyDesc& d, void* base)
{
    if (d.ptr != nullptr)
        return d.ptr;
    return static_cast<std::byte*>(base) + d.offset;
}
[[nodiscard]] inline const void* ResolvePtr(const PropertyDesc& d, const void* base)
{
    if (d.ptr != nullptr)
        return d.ptr;
    return static_cast<const std::byte*>(base) + d.offset;
}

// ---- 数值 clamp/步进纯函数（离线单测） ----
// 浮点 clamp：区间非法（lo>hi）时不处理（防御）。
[[nodiscard]] inline float ClampToRange(float v, float lo, float hi)
{
    if (lo > hi)
        return v;
    return v < lo ? lo : (v > hi ? hi : v);
}
// 整数 clamp（Enum 写回 / Int 字段共用）
[[nodiscard]] inline std::int64_t ClampIntRange(std::int64_t v, std::int64_t lo, std::int64_t hi)
{
    if (lo > hi)
        return v;
    return v < lo ? lo : (v > hi ? hi : v);
}
// 步进量化：吸附到最近 step 网格（step<=0 原样返回；四舍五入用 floor(x+0.5) 避免 half-away 歧义）
[[nodiscard]] inline float QuantizeStep(float v, float step)
{
    if (step <= 0.0f)
        return v;
    return std::floor(v / step + 0.5f) * step;
}
// 按描述 clamp（写回通道用；与 ImGui 滑杆/拖拽的 clamp 语义一致）
[[nodiscard]] inline float ClampDesc(float v, const PropertyDesc& d)
{
    return d.hasRange ? ClampToRange(v, d.minV, d.maxV) : v;
}
// 枚举值 clamp 到选项表范围（越界值防御为最后一项语义；无选项表不处理）
[[nodiscard]] inline std::int64_t ClampEnum(std::int64_t v, const PropertyDesc& d)
{
    return d.enumCount > 0 ? ClampIntRange(v, 0, d.enumCount - 1) : v;
}

// ---- 读值/写值（统一走 PropValue 通道；写值 clamp 与面板控件行为等价） ----
[[nodiscard]] inline PropValue ReadValue(const PropertyDesc& d, const void* base)
{
    if (d.get != nullptr)
        return d.get(base);
    PropValue v;
    const void* p = ResolvePtr(d, base);
    switch (d.storage)
    {
    case PropStorage::F32: v.f[0] = *static_cast<const float*>(p); break;
    case PropStorage::V3F:
    {
        const float* v3 = static_cast<const float*>(p);
        v.f[0] = v3[0];
        v.f[1] = v3[1];
        v.f[2] = v3[2];
        break;
    }
    case PropStorage::I32: v.i = static_cast<std::int64_t>(*static_cast<const int32_t*>(p)); break;
    case PropStorage::U32: v.i = static_cast<std::int64_t>(*static_cast<const uint32_t*>(p)); break;
    case PropStorage::U8: v.i = static_cast<std::int64_t>(*static_cast<const uint8_t*>(p)); break;
    case PropStorage::B1: v.b = *static_cast<const bool*>(p); break;
    case PropStorage::Str: v.s = static_cast<const char*>(p); break;
    }
    return v;
}

inline void WriteValue(const PropertyDesc& d, void* base, const PropValue& value)
{
    if (d.set != nullptr)
    {
        d.set(base, value);
        return;
    }
    void* p = ResolvePtr(d, base);
    switch (d.storage)
    {
    case PropStorage::F32: *static_cast<float*>(p) = ClampDesc(value.f[0], d); break;
    case PropStorage::V3F:
    {
        float* v3 = static_cast<float*>(p);
        for (int k = 0; k < 3; ++k)
            v3[k] = ClampDesc(value.f[k], d);
        break;
    }
    case PropStorage::I32:
    {
        std::int64_t c = value.i;
        if (d.hasRange)
            c = ClampIntRange(c, static_cast<std::int64_t>(d.minV), static_cast<std::int64_t>(d.maxV));
        *static_cast<int32_t*>(p) = static_cast<int32_t>(c);
        break;
    }
    case PropStorage::U32:
    {
        std::int64_t c = value.i < 0 ? 0 : value.i;
        if (d.type == PropType::Enum)
            c = ClampEnum(c, d);
        *static_cast<uint32_t*>(p) = static_cast<uint32_t>(c);
        break;
    }
    case PropStorage::U8:
    {
        std::int64_t c = value.i < 0 ? 0 : value.i;
        if (d.type == PropType::Enum)
            c = ClampEnum(c, d);
        *static_cast<uint8_t*>(p) = static_cast<uint8_t>(c);
        break;
    }
    case PropStorage::B1: *static_cast<bool*>(p) = value.b; break;
    case PropStorage::Str:
    {
        char* buf = static_cast<char*>(p);
        if (d.strCapacity > 0)
            snprintf(buf, static_cast<size_t>(d.strCapacity), "%s", value.s.c_str());
        break;
    }
    }
}

// 批量读/写：值表与元数据属性行一一对应（长度不足时按可用部分处理）
[[nodiscard]] inline std::vector<PropValue> ReadValues(const ComponentMeta& meta, const void* component)
{
    std::vector<PropValue> out;
    out.reserve(meta.properties.size());
    for (const PropertyDesc& d : meta.properties)
        out.push_back(ReadValue(d, component));
    return out;
}
inline void WriteValues(const ComponentMeta& meta, void* component, const std::vector<PropValue>& values)
{
    const size_t n = meta.properties.size() < values.size() ? meta.properties.size() : values.size();
    for (size_t i = 0; i < n; ++i)
        WriteValue(meta.properties[i], component, values[i]);
}

// ---- 脏判定：输入当前值表与基线值表，输出逐属性脏标记（供撤销手势判定/面板高亮） ----
// 按属性类型比较对应通道（未用到的通道不参与）；值表短于元数据时全部视为脏（防御表结构变化）。
[[nodiscard]] inline bool ValuesEqual(const PropertyDesc& d, const PropValue& a, const PropValue& b)
{
    switch (d.type)
    {
    case PropType::Float: return a.f[0] == b.f[0];
    case PropType::Vec3:
    case PropType::Color: return a.f[0] == b.f[0] && a.f[1] == b.f[1] && a.f[2] == b.f[2];
    case PropType::Int:
    case PropType::Enum: return a.i == b.i;
    case PropType::Bool: return a.b == b.b;
    case PropType::String: return a.s == b.s;
    }
    return false;
}
[[nodiscard]] inline std::vector<uint8_t> DirtyMask(const ComponentMeta& meta,
                                                    const std::vector<PropValue>& current,
                                                    const std::vector<PropValue>& baseline)
{
    const size_t n = meta.properties.size();
    if (current.size() < n || baseline.size() < n)
        return std::vector<uint8_t>(n, 1);
    std::vector<uint8_t> dirty(n, 0);
    for (size_t i = 0; i < n; ++i)
        dirty[i] = ValuesEqual(meta.properties[i], current[i], baseline[i]) ? 0 : 1;
    return dirty;
}

// 属性行：绑定后的描述 + 可见性 + 脏标记（"从选中对象构建属性行"的面板/测试载体）
struct PropertyRow
{
    const PropertyDesc* desc = nullptr; // 指向 meta 内的描述（meta 生命周期需覆盖行使用期）
    bool visible = true;
    bool dirty = false;
};

// 从选中对象构建属性行：逐属性绑定可见性谓词，并与基线值表比较输出脏标记。
// baseline 由手势起始帧经 ReadValues 抓取；任一行 dirty 即可提交撤销命令。
[[nodiscard]] inline std::vector<PropertyRow> BuildPropertyRows(const ComponentMeta& meta, const void* component,
                                                                const std::vector<PropValue>& baseline)
{
    std::vector<PropertyRow> rows;
    rows.reserve(meta.properties.size());
    for (size_t i = 0; i < meta.properties.size(); ++i)
    {
        PropertyRow r;
        r.desc = &meta.properties[i];
        r.visible = r.desc->visibleIf == nullptr || r.desc->visibleIf(component);
        r.dirty = baseline.size() > i && !ValuesEqual(*r.desc, ReadValue(*r.desc, component), baseline[i]);
        rows.push_back(r);
    }
    return rows;
}

// ---- 内建组件：SceneObject（编辑器兼容层）元数据 ----
// 字段顺序 / 控件种类 / 范围与步长 / 格式串与旧手写"场景"面板逐字等价（U1-E2 行为不变迁移）。
inline constexpr const char* kSceneObjectComponent = "SceneObject";
inline constexpr const char* kMeshKindNames[] = {"立方体", "圆环体", "glTF 模型", "球", "胶囊"};
inline constexpr const char* kBodyTypeNames[] = {"无", "静态", "动态", "运动学"};
inline constexpr const char* kShapeTypeNames[] = {"盒", "球", "胶囊"};

// 可见性谓词：物理子属性依赖刚体类型（与旧面板 if 分支一致）
inline bool PhysicsAnyBody(const void* component)
{
    return static_cast<const Scene::SceneObject*>(component)->physicsType != Physics::BodyType::None;
}
inline bool PhysicsDynamicBody(const void* component)
{
    return static_cast<const Scene::SceneObject*>(component)->physicsType == Physics::BodyType::Dynamic;
}

[[nodiscard]] inline ComponentMeta MakeSceneObjectMeta()
{
    using SO = Scene::SceneObject;
    ComponentMeta m;
    m.typeName = kSceneObjectComponent;
    auto& p = m.properties;
    p.reserve(15);

    // ---- 渲染 / 变换 ----
    p.push_back(PropertyDesc{.label = "网格",
                             .type = PropType::Enum,
                             .storage = PropStorage::U32,
                             .offset = offsetof(SO, meshId),
                             .enumItems = kMeshKindNames,
                             .enumCount = 5});
    p.push_back(PropertyDesc{.label = "位置",
                             .type = PropType::Vec3,
                             .storage = PropStorage::V3F,
                             .offset = offsetof(SO, position),
                             .minV = -10.0f,
                             .maxV = 10.0f,
                             .step = 0.05f,
                             .hasRange = true});
    p.push_back(PropertyDesc{.label = "缩放",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, scale),
                             .minV = 0.1f,
                             .maxV = 5.0f,
                             .step = 0.02f,
                             .hasRange = true});
    p.push_back(PropertyDesc{.label = "色调", .type = PropType::Color, .storage = PropStorage::V3F,
                             .offset = offsetof(SO, tint)});
    p.push_back(PropertyDesc{.label = "金属度",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, metallic),
                             .minV = 0.0f,
                             .maxV = 1.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider});
    p.push_back(PropertyDesc{.label = "粗糙度",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, roughness),
                             .minV = 0.045f,
                             .maxV = 1.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider});
    p.push_back(PropertyDesc{.label = "自转速度",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, spinSpeed),
                             .minV = -180.0f,
                             .maxV = 180.0f,
                             .step = 0.5f,
                             .hasRange = true,
                             .format = "%.1f deg/s"});
    p.push_back(PropertyDesc{.label = "旋转X",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, rotation) + 0 * sizeof(float),
                             .minV = -180.0f,
                             .maxV = 180.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider,
                             .format = "%.0f deg"});
    p.push_back(PropertyDesc{.label = "旋转Y",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, rotation) + 1 * sizeof(float),
                             .minV = -180.0f,
                             .maxV = 180.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider,
                             .format = "%.0f deg"});
    p.push_back(PropertyDesc{.label = "旋转Z",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, rotation) + 2 * sizeof(float),
                             .minV = -180.0f,
                             .maxV = 180.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider,
                             .format = "%.0f deg"});

    // ---- 物理（group 标题与旧面板的 Separator+"物理"一致） ----
    p.push_back(PropertyDesc{.label = "刚体类型",
                             .type = PropType::Enum,
                             .storage = PropStorage::U8,
                             .offset = offsetof(SO, physicsType),
                             .group = "物理",
                             .enumItems = kBodyTypeNames,
                             .enumCount = 4,
                             .rebuildPhysics = true});
    p.push_back(PropertyDesc{.label = "碰撞形状",
                             .type = PropType::Enum,
                             .storage = PropStorage::U8,
                             .offset = offsetof(SO, physicsShape),
                             .group = "物理",
                             .enumItems = kShapeTypeNames,
                             .enumCount = 3,
                             .visibleIf = &PhysicsAnyBody,
                             .rebuildPhysics = true});
    p.push_back(PropertyDesc{.label = "质量(kg)",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, physicsMass),
                             .minV = 0.01f,
                             .maxV = 1000.0f,
                             .step = 0.1f,
                             .hasRange = true,
                             .visibleIf = &PhysicsDynamicBody,
                             .rebuildPhysics = true});
    p.push_back(PropertyDesc{.label = "摩擦",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, physicsFriction),
                             .minV = 0.0f,
                             .maxV = 1.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider,
                             .visibleIf = &PhysicsAnyBody,
                             .rebuildPhysics = true});
    p.push_back(PropertyDesc{.label = "弹性",
                             .type = PropType::Float,
                             .storage = PropStorage::F32,
                             .offset = offsetof(SO, physicsRestitution),
                             .minV = 0.0f,
                             .maxV = 1.0f,
                             .hasRange = true,
                             .uiHint = UiHint::Slider,
                             .visibleIf = &PhysicsAnyBody,
                             .rebuildPhysics = true});
    return m;
}

// 注册内建组件元数据（可重复调用，同名覆盖）
inline void RegisterBuiltins(MetaRegistry& registry)
{
    registry.Register(MakeSceneObjectMeta());
}
// 幂等确保：全局注册表已含内建组件则跳过（面板每帧调用零开销）
inline void EnsureBuiltins()
{
    MetaRegistry& reg = MetaRegistry::Global();
    if (reg.Find(kSceneObjectComponent) == nullptr)
        RegisterBuiltins(reg);
}
} // namespace BigHero::Editor::Inspector
