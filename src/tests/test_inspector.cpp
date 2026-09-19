// Inspector 属性元数据系统单元测试 —— Unity 对标清单 U1-E2。
// 覆盖验收线：
//   1. 元数据注册/查找（MetaRegistry 注册/覆盖/未命中 + 全局表 EnsureBuiltins 幂等）；
//   2. clamp/步进纯函数（ClampToRange / ClampIntRange / QuantizeStep / ClampEnum）；
//   3. 脏判定（ValuesEqual 按类型比较 / DirtyMask 基线表与长度防御）；
//   4. "属性行构建"往返（ReadValues -> 改表 -> WriteValues -> ReadValues，含 clamp、
//      可见性、偏移/指针/读写器三种绑定与字符串缓冲截断）。
#include "framework/test_common.h"

#include "editor/InspectorModel.h"

#include <cstring>

using namespace BigHero;

namespace
{
// 零值物体（聚合初始化清零无默认初始化器的字段，保证确定性）
Scene::SceneObject MakeObj()
{
    Scene::SceneObject o{};
    o.scale = 1.0f;
    o.tint = glm::vec3(1.0f);
    o.roughness = 0.5f;
    o.physicsMass = 1.0f;
    o.physicsFriction = 0.5f;
    return o;
}

// 按标签找属性下标（断言失败时返回 -1，调用方先 REQUIRE 命中）
int FindProp(const Editor::Inspector::ComponentMeta& meta, const char* label)
{
    for (size_t i = 0; i < meta.properties.size(); ++i)
        if (std::strcmp(meta.properties[i].label, label) == 0)
            return static_cast<int>(i);
    return -1;
}

// ---- 读写器路径的宿主与回调（验证 get/set 优先于 ptr/offset） ----
struct AccessorHost
{
    float decoy = 0.0f; // 诱饵：若实现误走 ptr 路径会改写它
    float hp = 50.0f;
    char name[16] = "默认";
};
Editor::Inspector::PropValue GetHp(const void* c)
{
    return Editor::Inspector::FloatValue(static_cast<const AccessorHost*>(c)->hp);
}
void SetHp(void* c, const Editor::Inspector::PropValue& v)
{
    static_cast<AccessorHost*>(c)->hp = v.f[0];
}
} // namespace

// ---------------------------------------------------------------------------
// 1. 元数据注册/查找
// ---------------------------------------------------------------------------

TEST_CASE("Inspector.MetaRegistryRegisterFind")
{
    Editor::Inspector::MetaRegistry reg;

    // 未命中 -> nullptr
    CHECK(reg.Find("Nope") == nullptr);
    CHECK(reg.Count() == 0);

    // 注册 + 查找
    Editor::Inspector::ComponentMeta a;
    a.typeName = "ScriptComp";
    a.properties.push_back(
        Editor::Inspector::PropertyDesc{.label = "生命", .type = Editor::Inspector::PropType::Float});
    reg.Register(std::move(a));
    const Editor::Inspector::ComponentMeta* found = reg.Find("ScriptComp");
    REQUIRE(found != nullptr);
    CHECK(found->properties.size() == 1);
    CHECK(std::strcmp(found->properties[0].label, "生命") == 0);
    CHECK(reg.Count() == 1);

    // 同名覆盖（脚本重载语义）：字段表被替换，数量不增
    Editor::Inspector::ComponentMeta b;
    b.typeName = "ScriptComp";
    b.properties.push_back(
        Editor::Inspector::PropertyDesc{.label = "护盾", .type = Editor::Inspector::PropType::Float});
    b.properties.push_back(
        Editor::Inspector::PropertyDesc{.label = "无敌", .type = Editor::Inspector::PropType::Bool});
    reg.Register(std::move(b));
    found = reg.Find("ScriptComp");
    REQUIRE(found != nullptr);
    CHECK(reg.Count() == 1);
    CHECK(found->properties.size() == 2);
    CHECK(std::strcmp(found->properties[0].label, "护盾") == 0);
}

TEST_CASE("Inspector.BuiltinSceneObjectMeta")
{
    // EnsureBuiltins 幂等：连续调用后全局表恰好有一个 SceneObject 元数据
    Editor::Inspector::EnsureBuiltins();
    Editor::Inspector::EnsureBuiltins();
    const Editor::Inspector::ComponentMeta* meta =
        Editor::Inspector::MetaRegistry::Global().Find(Editor::Inspector::kSceneObjectComponent);
    REQUIRE(meta != nullptr);
    REQUIRE(meta->properties.size() == 15);

    // 字段顺序与旧手写"场景"面板逐字一致（等价迁移的锚点断言）
    static const char* kExpected[] = {"网格",   "位置",    "缩放",      "色调",    "金属度", "粗糙度",
                                      "自转速度", "旋转X",  "旋转Y",     "旋转Z",  "刚体类型", "碰撞形状",
                                      "质量(kg)", "摩擦",   "弹性"};
    for (size_t i = 0; i < 15; ++i)
        CHECK(std::strcmp(meta->properties[i].label, kExpected[i]) == 0);

    // 关键描述抽查：类型/存储/范围/步长/UI 提示与旧控件调用一致
    const int pos = FindProp(*meta, "位置");
    REQUIRE(pos == 1);
    CHECK(meta->properties[pos].type == Editor::Inspector::PropType::Vec3);
    CHECK(meta->properties[pos].hasRange);
    CHECK_NEAR(meta->properties[pos].minV, -10.0f, 1e-6f);
    CHECK_NEAR(meta->properties[pos].maxV, 10.0f, 1e-6f);
    CHECK_NEAR(meta->properties[pos].step, 0.05f, 1e-6f);

    const int mesh = FindProp(*meta, "网格");
    REQUIRE(mesh == 0);
    CHECK(meta->properties[mesh].type == Editor::Inspector::PropType::Enum);
    CHECK(meta->properties[mesh].storage == Editor::Inspector::PropStorage::U32);
    CHECK(meta->properties[mesh].enumCount == 5);
    CHECK(std::strcmp(meta->properties[mesh].enumItems[2], "glTF 模型") == 0);

    const int metal = FindProp(*meta, "金属度");
    REQUIRE(metal == 4);
    CHECK(meta->properties[metal].uiHint == Editor::Inspector::UiHint::Slider);
    CHECK(meta->properties[metal].storage == Editor::Inspector::PropStorage::F32);

    const int body = FindProp(*meta, "刚体类型");
    REQUIRE(body == 10);
    CHECK(meta->properties[body].storage == Editor::Inspector::PropStorage::U8);
    CHECK(meta->properties[body].rebuildPhysics);
    CHECK(std::strcmp(meta->properties[body].group, "物理") == 0);
    CHECK(meta->properties[body].visibleIf == nullptr); // 刚体类型始终可见

    const int mass = FindProp(*meta, "质量(kg)");
    REQUIRE(mass == 12);
    CHECK(meta->properties[mass].visibleIf == &Editor::Inspector::PhysicsDynamicBody);
    CHECK(meta->properties[mass].rebuildPhysics);

    const int spin = FindProp(*meta, "自转速度");
    REQUIRE(spin == 6);
    CHECK(std::strcmp(meta->properties[spin].format, "%.1f deg/s") == 0);
    CHECK(!meta->properties[spin].rebuildPhysics); // 渲染属性不触发刚体重建
}

// ---------------------------------------------------------------------------
// 2. clamp/步进纯函数
// ---------------------------------------------------------------------------

TEST_CASE("Inspector.ClampPureFunctions")
{
    // 浮点 clamp：两端/中间/区间非法防御
    CHECK_NEAR(Editor::Inspector::ClampToRange(5.0f, 0.0f, 1.0f), 1.0f, 1e-6f);
    CHECK_NEAR(Editor::Inspector::ClampToRange(-3.0f, 0.0f, 1.0f), 0.0f, 1e-6f);
    CHECK_NEAR(Editor::Inspector::ClampToRange(0.5f, 0.0f, 1.0f), 0.5f, 1e-6f);
    CHECK_NEAR(Editor::Inspector::ClampToRange(7.0f, 2.0f, 2.0f), 2.0f, 1e-6f);  // 退化区间
    CHECK_NEAR(Editor::Inspector::ClampToRange(7.0f, 3.0f, 1.0f), 7.0f, 1e-6f);  // lo>hi 不处理

    // 整数 clamp
    CHECK(Editor::Inspector::ClampIntRange(9, 0, 4) == 4);
    CHECK(Editor::Inspector::ClampIntRange(-1, 0, 4) == 0);
    CHECK(Editor::Inspector::ClampIntRange(3, 0, 4) == 3);

    // 步进量化：吸附最近网格；step<=0 原样返回
    CHECK_NEAR(Editor::Inspector::QuantizeStep(1.234f, 0.05f), 1.25f, 1e-4f);
    CHECK_NEAR(Editor::Inspector::QuantizeStep(0.049f, 0.05f), 0.05f, 1e-4f);
    CHECK_NEAR(Editor::Inspector::QuantizeStep(-0.026f, 0.05f), -0.05f, 1e-4f); // 最近网格为 -0.05
    CHECK_NEAR(Editor::Inspector::QuantizeStep(0.37f, 0.0f), 0.37f, 1e-6f);
    CHECK_NEAR(Editor::Inspector::QuantizeStep(0.37f, -0.1f), 0.37f, 1e-6f);
    CHECK(Editor::Inspector::ClampEnum(99, Editor::Inspector::PropertyDesc{.enumCount = 5}) == 4);
    CHECK(Editor::Inspector::ClampEnum(-1, Editor::Inspector::PropertyDesc{.enumCount = 4}) == 0);
    CHECK(Editor::Inspector::ClampEnum(2, Editor::Inspector::PropertyDesc{.enumCount = 0}) == 2); // 无选项表不处理
}

TEST_CASE("Inspector.WriteValueClamp")
{
    const Editor::Inspector::ComponentMeta meta = Editor::Inspector::MakeSceneObjectMeta();

    // 浮点写回 clamp（与 ImGui 滑杆/拖拽范围一致）
    Scene::SceneObject obj = MakeObj();
    const int metal = FindProp(meta, "金属度");
    Editor::Inspector::WriteValue(meta.properties[metal], &obj, Editor::Inspector::FloatValue(5.0f));
    CHECK_NEAR(obj.metallic, 1.0f, 1e-6f);
    Editor::Inspector::WriteValue(meta.properties[metal], &obj, Editor::Inspector::FloatValue(-1.0f));
    CHECK_NEAR(obj.metallic, 0.0f, 1e-6f);
    const int rough = FindProp(meta, "粗糙度");
    Editor::Inspector::WriteValue(meta.properties[rough], &obj, Editor::Inspector::FloatValue(0.5f));
    CHECK_NEAR(obj.roughness, 0.5f, 1e-6f); // 区间内不动

    // Vec3 写回逐轴 clamp（位置 [-10,10]）
    const int pos = FindProp(meta, "位置");
    Editor::Inspector::WriteValue(meta.properties[pos], &obj,
                                  Editor::Inspector::Vec3Value(glm::vec3(20.0f, -20.0f, 3.0f)));
    CHECK_NEAR(obj.position.x, 10.0f, 1e-6f);
    CHECK_NEAR(obj.position.y, -10.0f, 1e-6f);
    CHECK_NEAR(obj.position.z, 3.0f, 1e-6f);

    // 枚举写回 clamp：越界吸附到选项表边界；负值防御为 0
    const int mesh = FindProp(meta, "网格");
    Editor::Inspector::WriteValue(meta.properties[mesh], &obj, Editor::Inspector::IntValue(2));
    CHECK(obj.meshId == 2);
    Editor::Inspector::WriteValue(meta.properties[mesh], &obj, Editor::Inspector::IntValue(99));
    CHECK(obj.meshId == 4);
    Editor::Inspector::WriteValue(meta.properties[mesh], &obj, Editor::Inspector::IntValue(-1));
    CHECK(obj.meshId == 0);
    const int body = FindProp(meta, "刚体类型");
    Editor::Inspector::WriteValue(meta.properties[body], &obj, Editor::Inspector::IntValue(7));
    CHECK(obj.physicsType == Physics::BodyType::Kinematic);
}

// ---------------------------------------------------------------------------
// 3. 脏判定
// ---------------------------------------------------------------------------

TEST_CASE("Inspector.ValuesEqualAndDirtyMask")
{
    const Editor::Inspector::ComponentMeta meta = Editor::Inspector::MakeSceneObjectMeta();
    Scene::SceneObject obj = MakeObj();
    const std::vector<Editor::Inspector::PropValue> baseline = Editor::Inspector::ReadValues(meta, &obj);

    // 未修改：全干净
    std::vector<uint8_t> dirty = Editor::Inspector::DirtyMask(meta, baseline, baseline);
    for (const uint8_t d : dirty)
        CHECK(d == 0);

    // 只改金属度：仅该行脏
    obj.metallic = 0.9f;
    dirty = Editor::Inspector::DirtyMask(meta, Editor::Inspector::ReadValues(meta, &obj), baseline);
    for (size_t i = 0; i < dirty.size(); ++i)
        CHECK(dirty[i] == (meta.properties[i].label == std::string("金属度") ? 1 : 0));

    // 按类型比较通道：Vec3 改单轴也算脏；Float 只比 f[0]（f[1..2] 不串扰）
    CHECK(!Editor::Inspector::ValuesEqual(meta.properties[FindProp(meta, "位置")],
                                          Editor::Inspector::Vec3Value(glm::vec3(1.0f)),
                                          Editor::Inspector::Vec3Value(glm::vec3(1.0f, 0.0f, 2.0f))));
    Editor::Inspector::PropValue a = Editor::Inspector::FloatValue(1.0f);
    Editor::Inspector::PropValue b = Editor::Inspector::FloatValue(1.0f);
    b.f[1] = 9.0f; // Float 不使用 f[1]，不应影响相等性
    CHECK(Editor::Inspector::ValuesEqual(meta.properties[FindProp(meta, "缩放")], a, b));

    // 布尔/字符串按自身通道比较
    CHECK(Editor::Inspector::ValuesEqual(Editor::Inspector::PropertyDesc{.type = Editor::Inspector::PropType::Bool},
                                         Editor::Inspector::BoolValue(true),
                                         Editor::Inspector::BoolValue(true)));
    CHECK(!Editor::Inspector::ValuesEqual(
        Editor::Inspector::PropertyDesc{.type = Editor::Inspector::PropType::String},
        Editor::Inspector::StringValue("甲"), Editor::Inspector::StringValue("乙")));

    // 长度防御：基线表短于元数据 -> 全部视为脏（表结构变化时保守提交）
    dirty = Editor::Inspector::DirtyMask(meta, baseline, std::vector<Editor::Inspector::PropValue>(3));
    for (const uint8_t d : dirty)
        CHECK(d == 1);
}

TEST_CASE("Inspector.PropertyRowsVisibility")
{
    const Editor::Inspector::ComponentMeta meta = Editor::Inspector::MakeSceneObjectMeta();

    // physicsType == None：碰撞形状/质量/摩擦/弹性不可见，其余可见
    Scene::SceneObject obj = MakeObj();
    const std::vector<Editor::Inspector::PropValue> baseline = Editor::Inspector::ReadValues(meta, &obj);
    std::vector<Editor::Inspector::PropertyRow> rows = Editor::Inspector::BuildPropertyRows(meta, &obj, baseline);
    REQUIRE(rows.size() == 15);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CHECK(rows[i].desc == &meta.properties[i]);
        const char* label = meta.properties[i].label;
        const bool physicsExtra = std::strcmp(label, "碰撞形状") == 0 || std::strcmp(label, "质量(kg)") == 0 ||
                                  std::strcmp(label, "摩擦") == 0 || std::strcmp(label, "弹性") == 0;
        CHECK(rows[i].visible == !physicsExtra);
        CHECK(!rows[i].dirty);
    }

    // 改为动态刚体：物理子属性全部可见；金属度修改 -> 对应行脏
    obj.physicsType = Physics::BodyType::Dynamic;
    obj.metallic = 0.9f;
    rows = Editor::Inspector::BuildPropertyRows(meta, &obj, baseline);
    for (const Editor::Inspector::PropertyRow& r : rows)
        CHECK(r.visible);
    CHECK(rows[FindProp(meta, "金属度")].dirty);
    CHECK(!rows[FindProp(meta, "位置")].dirty);
}

// ---------------------------------------------------------------------------
// 4. "属性行构建"往返
// ---------------------------------------------------------------------------

TEST_CASE("Inspector.ValueTableRoundTrip")
{
    const Editor::Inspector::ComponentMeta meta = Editor::Inspector::MakeSceneObjectMeta();
    Scene::SceneObject obj = MakeObj();

    // 读表 -> 改表 -> 写回 -> 再读：往返一致（表内值均在范围内，不走 clamp 分支）
    std::vector<Editor::Inspector::PropValue> table = Editor::Inspector::ReadValues(meta, &obj);
    table[FindProp(meta, "位置")] = Editor::Inspector::Vec3Value(glm::vec3(1.0f, 2.0f, 3.0f));
    table[FindProp(meta, "缩放")] = Editor::Inspector::FloatValue(2.5f);
    table[FindProp(meta, "色调")] = Editor::Inspector::Vec3Value(glm::vec3(0.2f, 0.4f, 0.6f));
    table[FindProp(meta, "金属度")] = Editor::Inspector::FloatValue(0.75f);
    table[FindProp(meta, "网格")] = Editor::Inspector::IntValue(3);
    table[FindProp(meta, "旋转X")] = Editor::Inspector::FloatValue(30.0f);
    table[FindProp(meta, "刚体类型")] = Editor::Inspector::IntValue(2);
    table[FindProp(meta, "摩擦")] = Editor::Inspector::FloatValue(0.8f);
    Editor::Inspector::WriteValues(meta, &obj, table);

    const std::vector<Editor::Inspector::PropValue> again = Editor::Inspector::ReadValues(meta, &obj);
    CHECK(again == table);

    // 物体字段确实落位；未涉及的字段保持原值
    CHECK_NEAR(obj.position.y, 2.0f, 1e-5f);
    CHECK_NEAR(obj.scale, 2.5f, 1e-6f);
    CHECK_NEAR(obj.tint.z, 0.6f, 1e-5f);
    CHECK_NEAR(obj.metallic, 0.75f, 1e-6f);
    CHECK(obj.meshId == 3);
    CHECK_NEAR(obj.rotation.x, 30.0f, 1e-5f);
    CHECK(obj.physicsType == Physics::BodyType::Dynamic);
    CHECK_NEAR(obj.physicsFriction, 0.8f, 1e-6f);
    CHECK_NEAR(obj.roughness, 0.5f, 1e-6f); // 未改
    CHECK(obj.parentIndex == -1);           // 未改
    CHECK_NEAR(obj.spinSpeed, 0.0f, 1e-6f); // 未改

    // 短值表：只写前缀，不越界（WriteValues 按可用长度截断）
    std::vector<Editor::Inspector::PropValue> shortTable{Editor::Inspector::IntValue(1)};
    Editor::Inspector::WriteValues(meta, &obj, shortTable);
    CHECK(obj.meshId == 1);
    CHECK_NEAR(obj.position.y, 2.0f, 1e-5f); // 其余字段不受影响
}

TEST_CASE("Inspector.BindOffsetAndPtr")
{
    const Editor::Inspector::ComponentMeta meta = Editor::Inspector::MakeSceneObjectMeta();
    Scene::SceneObject obj = MakeObj();
    obj.position = glm::vec3(4.0f, 5.0f, 6.0f);
    obj.meshId = 1;

    // 偏移绑定 -> 指针绑定：BindComponent 后以空基址读取应与直接偏移读取一致
    const Editor::Inspector::ComponentMeta bound = Editor::Inspector::BindComponent(meta, &obj);
    REQUIRE(bound.properties.size() == meta.properties.size());
    for (size_t i = 0; i < bound.properties.size(); ++i)
        CHECK(bound.properties[i].ptr != nullptr);
    const std::vector<Editor::Inspector::PropValue> viaOffset = Editor::Inspector::ReadValues(meta, &obj);
    const std::vector<Editor::Inspector::PropValue> viaPtr = Editor::Inspector::ReadValues(bound, nullptr);
    CHECK(viaPtr == viaOffset);
    CHECK_NEAR(viaPtr[FindProp(meta, "位置")].f[1], 5.0f, 1e-6f);
    CHECK(viaPtr[FindProp(meta, "网格")].i == 1);

    // 经指针绑定写回同样生效
    Editor::Inspector::WriteValue(bound.properties[FindProp(meta, "网格")], nullptr,
                                  Editor::Inspector::IntValue(4));
    CHECK(obj.meshId == 4);

    // 无基址且未绑定时 ptr/offset 均为空 -> ResolvePtr 依赖基址，null 基址仅供防御路径（不读取）
    const Editor::Inspector::PropertyDesc& unbound = meta.properties[0];
    CHECK(unbound.ptr == nullptr);
    CHECK(Editor::Inspector::ResolvePtr(unbound, &obj) == &obj.meshId);
}

TEST_CASE("Inspector.AccessorAndStringStorage")
{
    // 读写器优先于 ptr：诱饵 ptr 指向 decoy，若误走指针路径会改写 decoy 而非 hp
    AccessorHost host;
    Editor::Inspector::PropertyDesc hp;
    hp.label = "生命";
    hp.type = Editor::Inspector::PropType::Float;
    hp.get = &GetHp;
    hp.set = &SetHp;
    hp.ptr = &host.decoy; // 诱饵

    CHECK_NEAR(Editor::Inspector::ReadValue(hp, &host).f[0], 50.0f, 1e-6f);
    Editor::Inspector::WriteValue(hp, &host, Editor::Inspector::FloatValue(88.0f));
    CHECK_NEAR(host.hp, 88.0f, 1e-6f);
    CHECK_NEAR(host.decoy, 0.0f, 1e-6f); // 诱饵未被触碰

    // 字符串存储：char 缓冲往返 + 容量截断（含 '\0'）
    Editor::Inspector::PropertyDesc name;
    name.label = "名字";
    name.type = Editor::Inspector::PropType::String;
    name.storage = Editor::Inspector::PropStorage::Str;
    name.ptr = host.name;
    name.strCapacity = 16;

    Editor::Inspector::WriteValue(name, &host, Editor::Inspector::StringValue("小引擎"));
    CHECK(std::strcmp(host.name, "小引擎") == 0);
    CHECK(Editor::Inspector::ReadValue(name, &host).s == "小引擎");

    std::string longName(64, 'x');
    Editor::Inspector::WriteValue(name, &host, Editor::Inspector::StringValue(longName));
    CHECK(std::strlen(host.name) == 15); // 截断到容量-1，保证终止符
    CHECK(Editor::Inspector::ReadValue(name, &host).s.size() == 15);
}
