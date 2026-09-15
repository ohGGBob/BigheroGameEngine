# ECS 渲染端收敛：Renderable 组件直接批次化

## Context

引擎已确立 EcsScene 为权威场景存储，但渲染路径仍每帧消费 `ECS → SceneObject 包` 投影（scene_）：UpdateVisibility、FillMeshInstances/FillGltfPrimInstances（O(网格×对象) 双重遍历）、两个阴影预通道逐对象遍历包。本轮收敛后渲染直读 ECS 组件并单趟批次化；scene_ 包保留为编辑器数据模型（Gizmo/拾取/撤销/序列化/关节锚点继续使用，职责边界写入注释）。用户已确认范围：**仅渲染端收敛**。

行为约束：**零行为变化**——实例顺序、剔除结果、阴影绘制、统计口径全部保持现状（包括现存怪癖：阴影不剔射、glTF 阴影不乘 GltfOffset、hasTorus_=false 时 meshId==1 剔除回退立方体球）。

## 实施步骤

### 1. src/scene/Scene.h —— 矩阵核心拆分
- `ComputeObjectModelMatrix` 拆出核心重载 `ComputeObjectModelMatrix(position, rotation, scale, spinAngleDeg)`，乘法顺序 T·Ry(spin)·Rx·Ry·Rz·S 逐项保持；SceneObject 重载一行委托。

### 2. src/scene/EcsScene.h —— 渲染遍历 API
- 新增模板 `ForEachRenderable(fn)`：**手写 `for (Entity e : order_)` 循环**取 (const Transform&, const Renderable&, const Spin&) 传入 fn。不可用 `View<T...>::Each`（dense 序受 swap-pop 打乱，core/ecs.h:351-361，稳定序语义会被破坏）。
- 新增 inline `ComputeEntityModelMatrix(const Transform&, float spinAngleDeg)` 委托 Scene 核心函数。

### 3. src/app/Application.h —— 成员与声明调整
- 删除 `UpdateVisibility`/`FillInstanceBuffers`/`FillMeshInstances`/`FillGltfPrimInstances` 声明，新增 `UpdateRenderables()`。
- 删除 `visible_`；新增 `firstGltfModel_`（首个可见 glTF 实体矩阵，供 SortedGltfBlendPrims）与 `gltfPrimScratch_`（`std::vector<std::vector<Render::InstanceData>>`，尺寸随 gltfPrims_ 重建对齐）及 cube/torus 桶 scratch。
- `AppendInstanceUpload` 泛化为 `(InstanceBuffer&, const InstanceData*, uint32_t count)`；删除仅被旧 Fill 使用的 `instanceScratch_`（先确认无其他消费者）。

### 4. src/app/Application.cpp —— 单趟 UpdateRenderables()
- 顺序复刻现流程：清空 `pendingUploads_`/`instanceUploadStash_` → **stash 按 ObjectCount×(2+prim 数) reserve**（AppendInstanceUpload 以 `stash.data()+offset` 登记指针，防 insert realloc 悬垂）→ 预取 frustum 与三种包围球参数（cube/torus/gltf，同 kCullMargin 与 hasTorus_/hasGltf_ 回退口径）→ 单趟 `ForEachRenderable`：算模型矩阵 → 视锥测试（**每实体一次**，culledCount_ 口径不变，Application_Record.cpp:142 消费）→ 按 r.meshId 入桶（glTF：每 prim 各生成一条，model 复用，tint=obj.tint×baseColorFactor.rgb，metallic/roughness 取 prim 材质）→ 记录首个可见 glTF 实体矩阵 → 遍历后逐桶 AppendInstanceUpload 更新 cubeInstanceCount_/torusInstanceCount_/gltfPrimCounts_。
- `RepackScene` 删 visible_ resize，注释更新为"编辑器兼容层（渲染已直读 ECS）"。

### 5. src/app/Application_Assets.cpp
- 删除 `FillGltfPrimInstances`；`SortedGltfBlendPrims` 改读 `firstGltfModel_`（无可见 glTF 实体时保持单位矩阵语义）。

### 6. src/app/Application_Record.cpp
- `DrawShadowCasters`/`DrawCubeShadowCasters` 的 scene_ 循环换 `ForEachRenderable` + meshId 过滤 + `ComputeEntityModelMatrix`（不乘 GltfOffset、不剔射——现状保持）；地面 drawOne 位置与 `hasGltf_` gate 原样保留；遍历序=稳定序=包序，逐 mesh 过滤后实例序与绘制结果不变。

### 7. src/game/SceneCommand.h + Application.cpp 559-580 + src/tests/test_gameplay.cpp —— visibility 快照清理
- 删 `SceneSnapshot::visibility`（SceneCommand.h:32）、`SceneSnapshotsDiffer` 两处比较（88, 103-104）、Snapshot/RestoreScene 读写（Application.cpp 564, 574）——可见性是每帧渲染派生值，本就不该进撤销快照。
- 同步更新 test_gameplay.cpp 撤销测试中对 `visibility` 的引用（578-665）。

### 8. 单测（src/tests/test_ecs_scene.cpp）
- 矩阵一致性：随机字段下 `ComputeEntityModelMatrix(t, angle)` == `ComputeObjectModelMatrix(SceneObject{同字段}, angle)`。
- 稳定序：创建 5 实体收集 meshId 序 == 包序；`DestroyAt(1)` 后遍历剩余序正确且组件不错位（证明未误用 dense 序）。

## 验证

1. `cmake --build out/build/x64-Debug --config Debug --target BigHeroTests` → 运行 BigHeroTests.exe，全部测试通过（现 55 用例基线 + 新增）。
2. 全量构建 `--target BigHeroGameEngine` 无新告警。
3. 冒烟点：默认场景画面不变；相机移开物体消失且 culled 统计正常；添加/删除/撤销后渲染正确；阴影含视锥外物体、glTF 阴影位置同现状；glTF 半透明批次排序不变。

## 明确不做

- 不动 scene_/spinAngles_/SyncSceneEdits/RepackScene 语义（仅注释职责边界）；Gizmo/拾取/SceneIoHost/物理关节读包路径不改。
- 不给阴影加剔除、不改 GltfOffset 与透明排序算法、不 ECS 化地面/粒子/导航。
- 不改 InstanceData 布局、着色器、SceneSnapshot 命令机制本身。
- 每模块带测试、独立 commit（UPGRADE_PLAN 第 2 节约定）。
