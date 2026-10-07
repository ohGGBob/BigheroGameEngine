# BigheroGameEngine 变更日志

本文档记录各轮升级中**已独立编译验证并落地**的功能增强。
所有条目均在沙箱以 `g++ -std=c++20 -Wall -Wextra` 编译运行验证通过后镜像到本仓库，
并保留同名验证驱动与输出说明。

> 里程碑（2026-10-07）：最新版本 0.22.33，CHANGELOG / CMakeLists 版本号已对齐（README / UPGRADE_PLAN 无逐版本引用）。

## [0.22.33] - 2026-10-07 —— 编辑器烘焙按钮：光照贴图 + 反射探针重烘焙入口（U2 编辑器化补全）

> 0.22.27/0.22.28 的光照贴图管线与 0.22.25 的 GPU 反射探针捕捉都只有 CLI/启动期入口，
> 编辑器里没有重烘焙手段。本轮把两个入口补进 ProjectPanel，并保持「面板只发请求、
> Application 帧末消费」的既有契约（同遮挡剔除 / 探针控件的纯 UI 先例）。

- **折叠区与控件**：ProjectPanel 剔除面板尾部新增「光照烘焙（Lightmap / 反射探针）」
  CollapsingHeader，内含 `烘焙光照贴图` / `烘焙反射探针` 两个按钮 + 两条状态行
  （悬停提示说明生效范围：lightmap 走前向主通道，延迟模式回退实时光照）。
- **帧末消费（RunPendingBakes）**：两个按钮只置
  `lightmapBakeRequested` / `reflectionBakeRequested` 标志，在帧末统一消费；
  lightmap 请求先清 `lightmapBatchReady_` 幂等标志再走 `BuildStaticLightmapRuntime()`
  整套自适应图集阶梯强制重烘焙（含 BVH 阴影、dilation、静态批次重建）；
  reflection 请求清空重捕全部探针，与离线 `--bake` 共用同一捕捉/编码管线。
- **状态反馈行**：分别汇报「已重烘焙（前向主通道生效；延迟模式回退实时）」
  与「已重烘焙（N 个探针，解析环境 SH L2）」；失败明示语义与离线路径一致
  （无静止立方体 / 图集 2048² 仍放不下时显示跳过原因）。

验证：static_lm 管线与反射捕捉零警告重编译；全套件 359 用例 / 142,171 断言全绿；
clang-format 门 0 违规；按钮请求所路由的每一条腿（BuildStaticLightmapRuntime 强制重烘焙 /
BakeReflectionProbes 全量重捕）即 0.22.27/0.22.28/0.22.25 已独立验证的路径，
新增部分仅为标志置位、帧末分发与状态行字符串（本机约束不启动引擎，未做窗口内点击级验证）。

## [0.22.32] - 2026-09-30 —— 延迟天空缺 skyTint 修正（夜晚氛围失效修复）

> 0.22.31 的 deferred 回归实锤「夜间场景通体无暗部」后，静态比对发现首个成因：
> 延迟光照的 `sampleSky()` 直出环境立方图，**缺 skyTint 乘数**（前向 skybox 是
> `envMap × skyTint.rgb × skyTint.w`）。夜晚氛围（ApplyAtmosphere 的 skyTint 压暗）在
> 延迟模式下完全失效——白天亮度天空 + 湿润沥青的亮蓝反射撑起全图基调。

### 修复（shaders/deferred_light.frag.glsl）
- `sampleSky()` 与前向 skybox 逐位对齐：乘 `lightUbo.skyTint.rgb × skyTint.w`。
  延迟/前向的背景天空亮度语义从此一致；白天默认 skyTint=(1,1,1,1) 行为逐位不变。

### 验证
- glslc 离线预检 0 error；MSVC Release 构建 0 error；全量套件 359 用例 / 142,171 断言
  0 失败（渲染着色器改动不进单测）。
- ⚠️ 窗口视觉回归（def+cybercity 复验全图暗部恢复）按授权协议未执行（未获运行时授权），
  留待下轮；修复正确性由「与前向逐位对齐」的代码比对承担。若复验仍有残留问题
  （静态无法覆盖显示管线时序），继续按同法排查。

## [0.22.31] - 2026-09-30 —— 延迟模式静止立方体消失修复 + --deferred CLI

> 0.22.28 的 Static 语义过滤在**所有渲染模式**下把静止立方体从实例路径剔除——而光照贴图
> 批次只在**前向**主通道绘制（v1 门控），延迟模式下静止立方体既不进批次也不进实例 →
> **彻底消失**。本轮修正为仅前向过滤；并补上欠账已久的 `--deferred` CLI（等价编辑器
> "渲染统计"面板勾选），使该路径可命令行回归。

### 变更（src/app + src/main）
- **`UpdateRenderables`**：静止立方体过滤条件加 `!renderer_.IsDeferred()`——延迟模式实例
  路径保留全部立方体（光照回退实时，v1 门控不变）；前向模式行为逐位不变。
- **`--deferred`**（AppConfig + main.cpp 帮助）：主循环前 `postProcessSync_.deferred = true`，
  首帧 UpdateDeferredState 消费（renderer_.SetDeferred + GBuffer 描述符重建，与面板同构）。
- 注：追加说明「首帧 UpdateRenderables 先于 UpdateDeferredState 运行」导致帧 1 一帧的
  过渡差，30 帧截图口径不可见，30 帧外无感。

### 验证
- 全量套件 359 用例 / 142,171 断言 / 0 失败；构建 0 error；格式门全绿。
- 窗口回归（cybercity --deferred，exit 0）：「命令行启动延迟渲染通道」日志确认开关生效；
  但**回归截图揭示一桩疑似预存在的更大问题**——deferred + cybercity 画面通体无暗部
  （全图 0 暗像素、亮度分布集中在 180~360 桶；同场景前向基线暗部/夜色齐全、色数 7618
  vs 3000）——夜间城市缺暗色几何。deferred 从未做过窗口验收（此前仅编辑器勾选、无 CLI），
  已列为独立待办（下轮诊断）。本修复的过滤逻辑正确性由构造与套件承担。

## [0.22.30] - 2026-09-30 —— 地形坡度物理 v1.5：台阶上限阻挡 + 缓坡贴地 + 跳跃不被打断

> 0.22.26 的「走在山地上」是隐式地面跟随——陡崖瞬移、缓坡贴地不精确。本轮给
> FpController 加**地形采样器**：无碰撞体路径下按目标点地面高度做行走语义——
> 超过台阶上限（stepHeight 0.45m）的陡坡撤销位移（不可硬爬），缓坡/台阶贴地抬升，
> 空中（跳跃/下落）完全跳过（绝不打断跳跃弧线）。纯 CPU、零 GPU，沙箱单测全绿。

### 变更（src/game/FpController.h + src/app）
- **`SetGroundSampler(std::function<float(x,z)>)`**：无碰撞体路径的地面高度查询回调
  （空 = 纯隐式地面旧行为，逐位不变）。消费于 MoveAndCollide 的 XZ 段、**仅 onGround_
  为真**时：目标点地面 > 脚底 + stepHeight → 撤销 XZ 位移并清零水平速度（陡墙阻挡）；
  ≤ stepHeight 且 > 脚底 → 贴地抬升（缓坡/台阶行走）。
- Application：terrainMode 每帧喂 `SampleWorld` 采样器 + 隐式地面（同源高度场）；
  其他场景清空采样器（`SetGroundSampler({})`，行为与旧逐位一致）。

### 验证
- 新增测试 **`FpController.TerrainSlopeWalk`**（+9 断言）：缓坡 h=0.5x 行走 2 秒
  （贴坡 y≈0.5x、onGround 保持）、3m 陡墙阻挡（x 停在墙前、y 恒 0 不上墙）、
  0.3m 缓台阶跨越（站上台面继续前行）、跳跃不被打断（起跳有初速、空中不被拉回地面）。
  接线口径与引擎一致（每帧按脚底喂隐式地面）。全量套件 **359 用例 / 142,171 断言 /
  0 失败**（上一版 358/142,162 + 本版）。
- MSVC Release 构建 0 error、格式门（19.1.5）全绿。
- 引擎侧 terrain+FP 遥测运行按授权协议未执行（未获运行时授权），留待下一轮回归；
  新逻辑正确性由单测全量承担，引擎侧改动为同模式 lambda 接线（与已验证的
  SetGroundHeight 一致）。

## [0.22.29] - 2026-09-30 —— 烘焙 BVH 加速：阴影射线 O(tris)→O(log tris)，cybercity 烘焙 16.4s→0.245s（65×）

> 0.22.28 实测 cybercity 光照贴图烘焙 16.4 秒（场景加载卡顿）——根因是阴影射线对**全部**
> 三角形暴力遍历。本轮给 LightmapBaker 加中位分割 BVH（header-only、确定性构建），烘焙
> 提速 **65 倍**；顺带修正阴影射线的一个语义 bug（近距命中压制远距真实遮挡体）。

### 新增（src/render/LightmapBaker.h）
- **`detail::TriBvh`**：中位分割 BVH（质心最长轴 + 排序取中位，叶子 ≤4 三角形，构建
  O(n log n) 确定性）；递归扩容安全（子节点按下标配回，不持有引用）；共面片轴向退化
  退化为叶子防无限递归。**任意命中查询**（阴影射线可早退）：double 精度 slab 剪枝 +
  叶内逐三角形过滤（selfIndex / castShadow / cullBackfaces / t ∈ [minHitT, maxT)）。
- **BakeLightmap**：构建一次 BVH 全图共享；三个阴影射线调用点（方向光/点光/AO）切换至
  `bvh.AnyHit(...)`。
- **语义修正（任意命中替代最近命中判区间）**：旧实现对射线取**最近**命中再判
  [minHitT, maxT)——当一根射线同时存在 sub-bias 近距命中（共面假阳性）与远距真实
  遮挡体时，近距命中会错误压制远距遮挡（判为不遮挡）。任意命中版按根过滤 t ∈ 区间，
  物理语义正确且可早退。既有测试场景不受影响（共面邻居与射线平行无交）。

### 验证
- 新增测试 **`Lightmap.BvhEquivalence`**：随机场景（地面 + 40 块随机遮挡板含 20% 不投影）
  × 2000 随机射线 × cullBackfaces 两种口径 = **4000 次判定 BVH ≡ 暴力遍历逐位一致**
  （含 selfIndex/minHitT/maxT 变化）。全量套件 **358 用例 / 142,162 断言 / 0 失败**。
- **提速实测**（headless cybercity）：升档警告 → 批次就绪 16.4 秒（0.22.28）→ **0.245 秒**
  （0.22.29），65×；烘焙结果（1024² 图集 / 2928 三角形 / 244 物体）与 0.22.28 一致。
- 修复过程中的两处自查（编译/测试暴露）：AnyHit 遗漏 tris 参数；BuildRecursive 递归
  扩容后左/右子按下标配回（引用悬垂）；平行轴 invDir 初版写反（1.0/1e30 ≈ 0 令 slab
  塌缩、轴向射线整树误剪枝——debug 程序实锤后修正）。

## [0.22.28] - 2026-09-30 —— 光照贴图正确性收口：Static 语义 + 输出变换对齐 + 图集自适应升档

> 0.22.27 接线后的三轮 A/B 验证暴露/确认了三个问题，本轮全部收口：①自转体不该吃烘焙
> 光照（批次冻结其姿态，语义债两次标注）；②static_lm 直出线性辐射度、缺主管线的
> exposure+ACES 输出变换（PP 关时过曝）；③cybercity A/B 逐位相同——诊断实锤 2928 三角形
> 放不下 512² 图集（约 420K texel 需求 > 262K），批次静默旁路。

### 修复
- **Static 语义**（CollectStaticLightmapTris + UpdateRenderables + 两处阴影 caster）：
  仅**静止**立方体（spinSpeed==0）进光照贴图批次；自转体从实例路径保留（UpdateRenderables
  过滤， cube 实例批次只含自转体），阴影 caster 分两路打光（批次相位对齐矩阵 + 自转体
  ECS 实时变换）——「烘焙姿态冻结」语义债清偿。空批次场景（如 default 全自转）优雅回退
  实时路径并留观测日志。
- **输出变换对齐**（static_lm.frag + 管线推送）：批次片元补齐与 frag.glsl 逐位一致的
  LightUBO/ObjectPush 声明与 acesFilm——PP 关=片元内 exposure+ACES 直通交换链，
  PP 开=线性 HDR 交合成端；管线增 FRAGMENT PushObject 推送段，绘制时按
  IsPostProcessing() 推送 outputTarget。
- **图集自适应升档**：512² 放不下逐级翻倍至 2048²（上限 32MB RGBA16F）；PackAtlas
  失败发生在打光之前，重试零烘焙浪费。

### 验证
- headless 诊断实锤 cybercity 根因（512² 放不下 2928 三角形）；升档后
  「**批次就绪: 244 个物体 / 2928 三角形 / 图集 1024²**」，烘焙一次性成本约 16 秒
  （O(texels×rays×tris) 无加速结构，降频/异步/ BVH 记入待办）。
- 窗口 A/B（cybercity，on/off 均 exit 0）：楼宇带差异 124.75、天空 76.66、街面 43.33
  （后两者来自楼宇入画与其湿润路面反射）；**ACES 对齐确认**——纯白像素 ON 319 < OFF 516
  （推送失效将表现为大面积过曝白块），亮像素 ON 略高（烘焙直射 vs 实时含阴影衰减），
  方向合理。全量套件 357 用例 / 142,160 断言 / 0 失败。

## [0.22.27] - 2026-09-30 —— 光照贴图渲染接线 v1（U2-L1 收官）：静态合并批次 + 图集采样替代实时立方体光照

> U2-L1 的最后一块拼图：0.22.17 烘焙的光照贴图从此真实进入渲染管线。场景加载即烘焙
> （与 --bake-lightmap 离线 CLI 同源采集/参数）→ 静态立方体合并为世界空间批次（顶点带
> 图集 UV）→ 图集 RGBE 解码上传 RGBA16F（双线性安全）；前向主通道以批次替代共享立方体
> 实例的光照，Unity lightmap 静态几何语义（贴图含 albedo/直接光/天光 AO，出射辐射度直出）。

### 接线（src/render + shaders + src/app）
- **烘焙端两处正确性修正**（chartless 运行时采样的前提，单测驱动）：
  ①chart 内容区**内膨胀**——三角形外 texel 重心钳制到三角形内打光（原「跳过留零」让
  斜边角落顶角在运行时采到黑）；②**margin 膨胀**（written 掩码 + 两轮 4 邻域扩散，
  RGBE 零值与真黑需显式区分）——零值 margin 会让双线性在边缘混入黑边。
- **`BuildStaticLightmapBatch`**（LightmapBaker.h 新公开 API）：静态三角形 → 合并批次
  （`LightmapBatchVertex{pos/normal/color/tangent/lmUV}`，世界空间、逐三角形独立顶点），
  顶点 UV = chart 世界→texel 精确映射（无中心偏移假设）；失配输入整体拒绝。
- **着色器对**：`static_lm.vert/frag`——仅消耗 pos+lmUV 子集（管线声明全顶点结构的
  消费子集，其余属性不声明、无 VUID 告警）；frag 直接输出图集采样（出射辐射度）。
- **管线与描述符**：`staticLmPipeline_`（静态批次顶点布局 + 相机/光照双布局、零推送）；
  set1 binding13（图集 sampler2D，双端常量同步，布局扩至 14 绑定）。
- **Application**：`BuildStaticLightmapRuntime`（幂等；烘焙 512² 图集 → 批次上传 →
  RGBE→RGBA16F → `Texture::CreateFromFloatPixels` → 描述符 binding13）；采集助手
  `CollectStaticLightmapTris`（相位对齐：烘焙/批次/阴影 caster 同用**初始相位姿态**，
  A/B 免姿态污染；caster 在批次就绪时改掷相位矩阵防阴影与可见几何脱节）与
  `CollectBakeLights` 供离线/运行时共用；前向主通道批次分支（延迟模式保持实时，v1 门控）；
  `--no-lightmap` A/B 开关。

### 验证
- 新增测试 **`Lightmap.StaticBatch`**（+9/14 断言）：顶点数/索引数/绕序保持；**UV 往返**
  ——每顶点图集采样值 == 该点烘焙值（±0.02）；失配输入拒绝。全量套件 **357 用例 /
  142,160 断言 / 0 失败**（上一版 356/142,136 + 本版）。
- MSVC Release 构建 0 error、本章零新警告；glslc 离线预检双 shader 0 error；格式门
  （19.1.5）全绿。
- 窗口 A/B（default，on/off 各一张，均 exit 0）：ON 运行日志「静态光照贴图批次就绪:
  5 个物体 / 60 三角形 / 图集 512²」；**立方体区独立色 on=1302 vs off=8205**——烘焙
  平滑梯度与实时六面色照明的结构性差异，证明批次路径在管线中真实生效。分带差值
  （sky 40 / cube 158 / ground 119）与自转圆环/相位的独立运动混叠，不作定量口径（诚实
  记录）；离线定量保证由 CPU 端 UV 往返**解析校验**承担。
- 已知取舍（v1）：静态批次不含 torus/glTF/人物（实时路径不变）；延迟模式批次门控关闭；
  自转立方体按初始相位烘焙后冻结（spinner 语义标注 Static 标记后恢复实时）；ICL 说明：
  margin 膨胀固定两轮、图集 512² 上限放不下 slice 万级几何（烘焙失败优雅旁路）。

## [0.22.26] - 2026-09-30 —— headless FP 崩溃修复（预存在）+ 地形第一人称碰撞（走在山地上）

> 两件事：①修掉 0.22.25 检视实锤的 headless+FP 场景段错误（pre-existing，0.22.23 起即有）；
> ②借修复解锁的 headless+FP 验证能力，把地形 FP 碰撞接上——`--scene terrain --camera fp`
> 真实站在高度场上。另附一次「首帧插桩二分」诊断法的完整记录。

### 修复：headless + FP 场景必崩（ImGui 空上下文）
- **定位**（首帧插桩二分，标记法）：主循环 begin→camera 之间崩溃 → 收窄至 UpdateCamera →
  FP 分支 → `UpdateFirstPersonMovement` 首行 `ImGui::GetIO().WantCaptureMouse`——**无条件调用**，
  而 headless（无窗口）不初始化编辑器覆盖层/ImGui 上下文 → 解引用空上下文即段错误。
- **为何其他场景不崩**：全库共 6 处 `GetIO()`，其余 5 处全靠短路求值保命（headless 下前置
  条件为 false，GetIO 不被求值）；本处是唯一无条件调用点。影响面：cybercity / voxel /
  terrain 等一切 FP 场景 `--headless` 必崩。
- **修复**：`editorOverlay_.IsInitialized()` 守卫（与其他 GetIO 点口径一致）；
  诊断插桩全部移除。

### 地形第一人称碰撞（U2-T1 接线 v1.5）
- **`FpController::SetGroundHeight(h)`**：隐式地面高度成员（默认 y=0，无碰撞体路径的
  兜底地面——旧行为逐位一致）；`terrainMode` 下 Application 每帧喂
  `terrainHeightmap_.SampleWorld(脚底 x/z)`，实现「走在山地上」（v1 无坡度滑落/陡坡阻挡，
  隐式地面跟随；盒碰撞体路径不受影响）。
- **一次性落地遥测**：FP 首帧落地后输出脚底坐标（对照 LightProbe ambient 遥测先例）。

### 验证
- 三跑全绿（exit 0）：`--headless --scene terrain --camera fp --bench-frames 10`
  （**遥测 y=0.5 @ (0,0)**——正是高度场基准高，证实玩家站在高度场而非 y=0 平面）、
  `--headless --scene cybercity`（原崩溃组合）、`--headless --scene default`（无回归）。
- 全量测试套件重跑：356 用例 / 142,136 断言 / 0 失败。

## [0.22.25] - 2026-09-30 —— 反射探针 GPU 立方图捕获 v1（U2-L2 收口段）：真实场景反射替代解析环境

> 0.22.23 把反射探针接进了 specular，但环境是解析的（天空+太阳+点光源瓣）。本轮补上
> U2-L2 的最后一段：**GPU 真实场景捕获**——每帧把「天空 + 静态几何」从探针位置渲进
> 128² RGBA16F 彩色立方图（6 面 + 4 级 mip），着色器按粗糙度取 mip 采样，建筑/霓虹
> 从此可入反射。机制全部复用既有先例：立方图渲染对照 CubeShadowMap、mip 链 vkCmdBlit
> 逐级降采样、捕获管线顶点变换走推送常量（规避相机 UBO 同帧读写冲突）。

### 新增（src/render + shaders + src/app）
- **`ReflectionCapture`**（render/ReflectionCapture.h/.cpp，新 TU 走 render GLOB 自动入库）：
  RGBA16F 彩色立方图（COLOR|SAMPLED|TRANSFER_SRC|DST，CUBE 兼容）+ D32 深度立方图 +
  6 面帧缓冲 + 独立渲染通道；布局状态机显式管理（PrepareFrame SHADER_READ→COLOR_ATTACHMENT /
  FinishFrame 逐级 blit mip + 全 mip 转 SHADER_READ）；深度附件 initialLayout=UNDEFINED
  （loadOp=CLEAR 逐面清屏）、finalLayout=DEPTH_STENCIL_ATTACHMENT_OPTIMAL。
- **`shaders/capture.vert.glsl`**：与主 vert 同构（同一顶点布局/实例属性/varying 契约），
  唯一差异 = 视图投影走顶点阶段推送常量（每面一次 PushCaptureVP 64B），与主管线共用
  set0/set1 描述符与实例缓冲，规避相机 UBO 的同帧读写冲突。
- **捕获管线 ×2**（CreatePipelines）：capture.vert+frag.glsl（顶点 64B + 片元 40B 双推送段）
  与 skybox 双着色器（PushSky 复用），均指向捕获渲染通道；`--no-reflection-probes` /
  `--no-probe-capture` 时整体旁路。
- **录制**：`RecordProbeCapture`（RecordPrePass 末尾；渲染图 shadow pass 无条件登记 →
  前向/延迟模式均生效）：90° 透视 6 面（Y 翻转与主管线 NDC 一致）→ 天空 + 共享立方体 +
  地形分块 + 圆环 + glTF 不透明/MASK；片元 outputTarget=1（线性 HDR，捕获不做色调映射）；
  首次捕获完成置脏重传 UBO（见修复③）。
- **着色器双路**：set1 binding12 捕获立方图（双端常量同步，布局扩至 13 绑定）；
  `SampleReflectionProbe` 在 `countPad.y>0.5` 时走 `textureLod(capture, rd, roughness*4)`
  （视差方向取权重最高探针），否则回退解析 SH——两路同享同一权重几何。
- **CLI**：`--no-probe-capture` 旁路捕获（探针回退解析 SH；A/B 对照用）。

### 验证（含三轮如实记录的修复迭代）
- MSVC Release：0 error、本章零新警告；glslc 离线预检三 shader 0 error。
- 运行期修复（headless 诊断一次命中并留档）：①描述符布局 12→**13** 槽（binding12 写入
  越界 VUID-00315 → 驱动 UB 段错误）；②深度附件改用合并 DEPTH_STENCIL_ATTACHMENT 布局
  （VUID-03313）且 finalLayout 不得为 UNDEFINED（VUID-00843）；③捕获标志时序——脏帧打包
  发生在首次捕获之前，countPad.y 恒 0：首帧捕获完成后置脏重传（一次性，之后零开销）。
- **预存在 bug 实锤（stash 二分）**：headless + cybercity 组合段错误在 0.22.23 旧代码
  同样复现（该组合此前从未测过：cybercity 第一人称 + 无窗口）——与本章改动无关，
  单独记入待办。
- **窗口 A/B**（cybercity，捕获 vs --no-probe-capture，两跑 exit 0）：**3 探针布点回归
  通过**（0.22.23 的 currentSceneKind_ 时序修复在此验证）；分带差异 天空 0.36 /
  建筑 12.56 / 街面 64.28——捕获路径确定生效且运行确定（对照 0.22.23 的零差异校准组）；
  霓虹直接发光像素两跑逐位一致（排除非探针来源）。捕获立方图内的场景内容观感
  （建筑/霓虹入反射）留待目检两张 PNG（build/bin/Release/out/refl_capture_on/off.png）。
- 已知取舍（v1）：单探针捕获（probes[0]，其余探针仍走解析 SH）；每帧 6 面 128² 全场景
  绘制（与点光源立方阴影同级成本，降频/事件驱动留后续）；捕获不含人物/透明/粒子；
  roughness→LOD 为 blit mip 链近似（非 GGX 预过滤）。

## [0.22.24] - 2026-09-28 —— 根运动循环提取器（跨圈自动取模）+ CubeMesh Vulkan 依赖解耦

> 在审阅 0.22.17~0.22.23 用户改动（LightmapBaker / Terrain / RootMotion / ReflectionProbe
> 四模块，沙箱 g++ 复验全绿）之后的一轮「修缮 + 拓展」：修掉 CubeMesh 对 Vulkan SDK 的
> 硬依赖（纯 CPU 消费方被传递包含误伤），并给根运动提取器补上循环时间轴语义——跨圈
> 区间不再要求调用方手动拆分。

### 修缮（src/scene/CubeMesh.h）
- **问题**：CubeMesh.h 无条件 `#include <vulkan/vulkan.h>`，Vertex 的绑定/属性描述方法
  直接暴露 Vulkan 类型——RootMotion 等纯 CPU 模块经传递包含后，在无 SDK 环境（Linux
  沙箱 / CI）编译即失败。
- **方案**：新增 `BIGHERO_CUBE_MESH_NO_VULKAN` 宏守卫——Vulkan 头与两个描述方法整体
  条件编译，守卫通过时定义 `BIGHERO_CUBE_MESH_HAS_VULKAN 1`；默认（宏未定义）行为与
  原版等价，MSVC 引擎构建零影响；纯 CPU 单测加 `-DBIGHERO_CUBE_MESH_NO_VULKAN` 即可。
- 沙箱实证：加宏后 root_motion 套件编译通过（此前卡 `<vulkan/vulkan.h>` 不存在）。

### 拓展（src/scene/RootMotion.h）
- **新增 `ExtractRootMotionDeltaLooped`**：循环时间轴版根运动提取。t0/t1 为全局时刻
  （可越过 clip 时长、可为负——负时刻按 floor 语义向过去回绕），内部按 `Duration()`
  自动取模并补齐跨圈增量：
  - 位移 = 圈内位移 + 单圈净位移 (P(D)−P(0)) × 整圈数，最终表达回 t0 时刻根节点朝向
    的本体系（与精确版同一约定；horizontalOnly 依旧丢弃垂直分量）；
  - 偏航 = 尾段 + 首段 + 整圈偏航 × (loops−1) 分段累加；
  - 同圈区间与 `ExtractRootMotionDelta` 完全等价（直接复用）；t1≤t0 / Duration()≤0 /
    player 无效 / rootNode 越界 → 零增量；
  - 语义边界写入头注释：循环自洽（首尾姿态连续）仍是 clip 制作责任，提取器不掩藏
    不连续——「时刻语义」条目同步更新。
- **测试**（src/tests/test_root_motion.cpp，+55 行，新增 2 用例 21 断言）：
  - `RootMotion.LoopedWrapAround`（duration=1s、单圈 +X 3m 平移 clip）：圈内等价
    [0.2,0.7]→(1.5,0,0)；跨圈 [0.9,1.1]→(0.6,0,0)；两整圈 [0,2]→(6,0,0)；平移窗口
    [0.5,2.5]→(6,0,0)；第二圈内 [1.2,1.7]→(1.5,0,0)；退化 t1≤t0→零增量；
  - `RootMotion.LoopedYawAccumulates`（单圈 90° 偏航 clip）：圈内 [0.2,0.6]=36°；
    跨圈 [0.9,1.1]=尾段 9°+首段 9°=18°；两整圈 [0,2]=180°。

### 验证
- 沙箱 `g++ -std=c++20 -Wall -Wextra -O0`（glm 头文件库）：root_motion **9 用例
  68 断言 0 失败**（原 7 用例 47 断言 + 新增 2 用例 21 断言），既有用例零回归；
  terrain / lightmap / reflection_probe 套件同环境复验保持全绿（297 / 108 / 281 断言）。
- 挂载同步：`cp` 三文件（CubeMesh.h / RootMotion.h / test_root_motion.cpp）exit 0，
  并经文件 API 逐段读回比对一致。**如实记录**：同步后 FUSE 挂载对这三文件出现间歇性
  Input/output error，shell 侧 md5 校验和多轮重试不可用（文件 API 路径正常）——建议
  本机侧抽验一眼后再 commit；本轮未在挂载上执行任何 git 写操作。

## [0.22.23] - 2026-09-27 —— 反射探针运行时接线（U2-L2）：SH UBO + 前向/延迟 specular 混合 + A/B 验证开关

> 0.22.19 落了反射探针 CPU 核心（SH 存储/带通预滤波/box 视差校正/混合），本轮接进渲染
> 管线——GI 双柱闭环：漫反射侧 LightProbe 已接线，镜面侧金属/光滑表面从此吃到烘焙级
> 局部反射。零新纹理、零新 Pass：set1 新增 binding11 UBO（与既有 ProbeUBO binding10 同构）。

### 接线（src/render + src/app + shaders）
- **UBO 布局**：`GpuReflectProbe`（12 vec4 = 位置/有效、盒 min/max、9 个未预滤波 SH 系数）
  × `kMaterialReflectProbeMax`(4) 槽 + `countPad` 头；`GetUboByteSize<ReflectProbeUBO>` 特化
  （首轮链接错误：漏特化——如实记录）；`shader_bindings.h`/`bindings.glsl` 双端同步 binding11。
- **描述符**：set1 布局 11→12 绑定；`reflectProbeUbos_` 按帧并行槽创建并 UpdateSet 绑定 11；
  脏标记延迟上传（`reflectProbeDirty_`，与光探针同款零开销路径）。
- **烘焙**：`BakeReflectionProbes()`——解析环境（天空常量 × skyTint + 太阳瓣 + 点光源半球瓣
  1/(1+d²k) 衰减）注入式 RadianceFn、512 样本投影 SH L2，场景加载即烘（幂等 Clear 重放）。
- **着色器**：新增 `shaders/include/reflect_probe.glsl`（前向/延迟共用）——SH 基、盒内起点
  slab 视差校正（各轴出口 t 取最小）、带通衰减 `f_l=exp(-l(l+1)·α²/2)`、三角窗权重归一化；
  `frag.glsl` 与 `deferred_light.frag.glsl` 的 specular 段以 `mix(specularIbl, probe, w)` 拼接，
  盒外/未烘焙权重 0 → 渲染零变化。所有循环上界编译期常量（unrollable，兼容动态均匀索引受限平台）。
- **`--no-reflection-probes`**：禁用烘焙（A/B 对照验证与性能对比用）。
- **修复（随本轮）**：`currentSceneKind_` 原在 BuildAndLoadScene 末尾赋值，烘焙布点看不到
  kind → cybercity 三探针分支未命中；赋值提前至函数头（读取点仅下拉框防重复，安全）。

### 验证
- MSVC Release 增量构建：0 error、本章零新警告（含一次链接错误修复迭代）。
- **glslc 离线预检**：`frag.glsl` / `deferred_light.frag.glsl` 双双 0 error 0 warning（不起引擎）。
- **窗口 A/B 对照**（--scene cybercity，带/不带 --no-reflection-probes 各一张 1600×900 截图，
  两跑均 exit 0）：分带差异分析——天空带 0.35（探针不触及天空盒 ✓）、建筑带 12.8、
  **金属湿滑街面带 62.9**；compare_images 全图 diff_ratio 19.7%、平均亮度 58.98→68.80
  （烘焙反射注入 specular 能量）。差异梯度恰好集中在反射面 = 接线生效的铁证。
- 已知口径：本轮 A/B 以**单探针**完成（布点 bug 时序，修复已随本轮落地）——cybercity
  三探针布点口径留待下轮回归确认。GLSL v1 盒外不走 CPU 的「最近中心兜底」（回退 IBL
  保持连续感）；真实立方图捕获（GPU 渲 6 面，替代解析环境）为 U2-L2 的下一段。

## [0.22.22] - 2026-09-27 —— 地形渲染接线 v1（U2-T1）：--scene terrain 分块网格实时绘制 + 顶点色 splat

> 0.22.20 落了地形 CPU 核心（高度场/笔刷/分块网格/splat 规则），本轮把核心接进渲染
> 管线——`--scene terrain` 加载笔刷地貌（山/洼/台地/双轮平滑），4×4 分块网格（129²
> 高度场 × 2m = 256²m 世界，32K 三角形）作为静态网格直接绘制，splat 四通道混合烧进
> 顶点色（零 shader / 零描述符 / 零新纹理改动）。

### 接线（src/app）
- **`--scene terrain`**（main.cpp 帮助文本 + BuildAndLoadScene 分支）：物体列表留空、
  不掺 glTF 演示体，地貌由 `InitTerrainScene()` 构建；取景 `SetTarget((0,6,0)) / 55m`。
- **新 TU `src/app/Application_Terrain.cpp`**（Application 第四分立 TU）：
  - `InitTerrainScene()`：高度场构建（Stamp×4 + Smooth×2 + FlattenTo）→ 逐块
    `BuildTerrainChunkVertices` 打包（位置/解析法线/UV 1/16 平铺/顶点色=splat 四层加权/
    切线沿 +X 坡度）→ `Render::Mesh::Create` 上传；幂等（重复进入不重建）。
  - `DrawTerrainChunks(cmd)`：恒等模型矩阵 + 恒等实例缓冲（`terrainInstances_`，
    **主管线顶点着色器的逐实例属性在 binding1 是必需绑定**——对照地面 `groundInstances_`
    同构），`DrawIndexedInstanced(..., 1)`。
- **主通道与阴影全路径接地**：前向/延迟（GBuffer）两根主通道与 CSM/点光源立方体阴影
  caster 共 4 处地面绘制点，`terrainMode_` 下地形分块**替代**地面平面（绘制与投影一致）；
  三角统计（Stats 面板）纳入分块。
- CMakeLists：`Application_Terrain.cpp` 同时注册进桌面 exe 与 Android main 目标。

### 验证
- MSVC Release 增量构建：0 error、本章零新警告（仅存量 C4834/C4100 噪音）。
- headless 冒烟（`--headless --scene terrain --bench-frames 5`）：**exit 0**，
  「地形场景构建完成: 129×129 顶点场 / 4×4 块 / 32768 三角形 / 场景已加载: terrain」。
- 窗口化截图（1600×900 PNG，exit 0，路径 `build/bin/Release/out/terrain_v1.png`）：
  像素统计交叉验证——下半屏主导色 = 橄榄暗绿 (32,32,16)（草）× 中性灰 (64,64,64)（岩）×
  暖褐 (32,16,16)（沙）splat 三通道经 tiles 反照率调制后的签名，另有亮蓝灰天际斑块；
  全图采样色彩数较"仅有天空盒"的对照运行 2704→1032（地形遮挡天空），42% 下半屏采样
  亮度 >130（受光面正常）。**两次验证迭代修复的问题如实记录**：①初始版本地形未绑实例
  缓冲（顶点着色器 binding1 逐实例属性缺失 → 全天空盒画面）；②forward 主通道的地面绘制
  替换点因缩进不匹配未命中（8 空格 vs 4 空格）——均已修复并回归。
- 已知取舍（v1）：splat 仅以顶点色呈现（与暗色 tiles 贴图相乘，整体色调偏暗，独立
  反照率槽留待后续）；地形无 FP 高度碰撞（轨道/第一人称相机仍走 y=0 地面物理）；
  不参与 LightProbe 逐对象探针辐照度包装（环境光走常量通道）；编辑器场景下拉框
  暂未收录 terrain（CLI 专属，同 voxel 先例）。

## [0.22.21] - 2026-09-26 —— 动画根运动（A2）：根节点帧间增量提取（本体系位移 + Y 轴偏航）

> 动画线收尾件：A1 事件（0.21.9）、A3 二维混合空间（0.20.x）已在，本轮补 A2 根运动——
> 把 Walk/Run clip 中根节点（含父链动画）的帧间位移/朝向变化提取出来交角色控制器施加，
> 动画成为运动来源，杜绝「原地踏步 + 控制器硬推」的滑步。纯 CPU（AnimationPlayer::Sample
> 局部 TRS + GltfModel 父链级联），可离线单测。

### 新增文件（src/scene/RootMotion.h，header-only，`BigHero::Scene`）
- **`WorldTrsAt(model, player, node, t, ...)`**：节点世界 TRS = 沿 `nodeParents` 自根向下
  级联局部 TRS（父节点的动画通道一并生效，级联 M = M_parent·T·R·S；链长以节点数为上限防环）。
- **`ExtractRootMotionDelta(model, player, cfg, t0, t1)`**：
  - ΔP_local = inv(R(root,t0))·(P(t1)−P(t0))——**本体系位移**，控制器应用时乘当前角色
    朝向即可，与帧率/世界朝向缓存无关。
  - ΔYaw = 水平前向（R·(0,0,-1) 的 XZ 投影）绕 +Y 的叉积-点积转角（右手定则，从上方
    看逆时针为正）；纯俯仰/滚转不产生偏航；前向水平投影退化（竖直）取 0。
  - `RootMotionConfig.horizontalOnly` 丢弃垂直位移（贴地角色保持 grounded）。
  - player 无效 / 根节点越界 → 零增量（全程良定义）。
- **时刻语义（关键设计）**：提取器对给定具体时刻**精确求值、不取模**——循环/回绕策略
  属于时间轴层，跨循环边界的帧增量由调用方把时间轴展开为末段时刻对表达。这与
  `AnimationPlayer::Sample(loop=true)` 的行为有本质差异：后者在 t=Duration 处 fmod
  回绕首帧（t=1.0 ≡ t=0），直接复用会把「循环终点姿态」静默替换成「首帧姿态」，
  根运动在循环边界永远提取出零位移——这正是本轮把 loop 参数从提取器签名中剥离的原因
  （配套用例 `ConcreteTimeSemantics` 锁死 t=1.0 精确取末帧）。

### 验证
- 新增测试 **src/tests/test_root_motion.cpp 7 用例 / 47 断言**：位移+偏航复合解析值
  （全程/半程/世界 TRS 直查）、父链级联（子节点继承父节点通道）、水平投影裁决
  （贴地 vs 空中）、静止与非法输入零增量、具体时刻语义（含循环终点末帧）、俯仰不
  产生偏航且 Y90·X30 / X30·Y90 两种组合顺序下偏航分量均精确分离为 +90°、确定性。
- 沙箱离线编译运行（-std=c++20 -Wall -Wextra -O0；本机无 g++，以 LLVM clang++ 同参数；
  GltfLoader 依赖链含 vulkan/vulkan.h，沙箱需追加 `-I<VULKAN_SDK>/Include`）：
  **compile 0 error 0 warning，7 用例 47 断言 0 失败，exit 0**。
- ⚠️ 引擎内未接线：FpController/PersonHost 消费根运动增量、与 BlendSpace/AnimationBlender
  的混合联动（各自提取后按权重合成）留待后续——与 A1/A3 同款「先落地后接线」节奏。

## [0.22.20] - 2026-09-26 —— 地形核心（U2-T1）：高度场 + 笔刷编辑 + 分块网格 + splat 混合规则

> U2-T1（地形：高度图 / splat / 植被实例化 / 笔刷）中需要 GPU/窗口验证的是渲染接线与
> 交互体验，而「高度场本身」是一套纯数据与采样数学。本轮落地该 CPU 核心（延续 U2-L1/L2
> 「先核心后接线」节奏）：规则网格高度场、双线性采样、解析法线、四类笔刷编辑、
> 分块网格化（块间天然缝合）、四通道 splat 层次混合、纯文本序列化。

### 新增文件（src/scene/Terrain.h，header-only，`BigHero::Scene`）
- **`TerrainHeightmap`**：
  - 网格：`Resize(nx, nz, cellSize, origin, initialHeight)`（各维 >= 2 与正 cell 校验，
    失败保留原状态）；`Height/ClampedHeight/VertexWorld/MinHeight/MaxHeight`。
  - 查询：`WorldToGrid`（越界钳制）、`SampleWorld`（双线性）、`NormalAt`（隐式曲面
    F=h(x,z)-y 的梯度解析法线，中心差分/边界单侧）、`SlopeAt`。
  - 编辑：`Stamp`（峰值恰为 maxRise、(1-(d/r)²)² 单调衰减、半径外不变——笔刷手感
    三不变量）、`Smooth`（同步单趟四邻域平均，不逐点交叉污染）、`FlattenTo`（硬缘整平）、
    `SetHeight`（逐格直写，高度图导入/测试用）。全部经统一脏矩形登记，
    `ChunkDirty(cx,cz,chunkQuads)/ClearDirty` 供分块重建。
  - `SaveTerrainHeightmap`/`LoadTerrainHeightmap`：纯文本快照（%.9g 十进制精确往返，
    缺 data 段/未知字段/数据不足整体拒绝）。
- **`TerrainSplatRule` + `TerrainSplatWeights(height, slope, rule)`**：四通道层次混合
  （雪带 > 沙带 > 草/岩按坡度 smoothstep 过渡），构造保证权重非负且和为 1；
  颜色随规则可配（后续每通道接纹理采样）。
- **`TerrainChunkMesh` + `BuildTerrainChunkMesh(hm, out, cx, cz, chunkQuads, rule)`**：
  分块网格（顶点位置/法线/UV/splat 权重 + CCW 索引）。块间共享同一高度场网格顶点，
  邻块边界位置逐位一致——缝合零后期处理；UV 按网格坐标 1/16 平铺（每 16 米重复）。

### 验证
- 新增测试 **src/tests/test_terrain.cpp 7 用例 / 297 断言**：网格重建拒绝/映射与双线性
  精确值/线性斜坡解析法线（normalize(-dh/dx,1,-dh/dz) 与坡度解析解）、笔刷峰值与
  衰减曲线/反向压平、平滑极值收缩与圈外不变/整平精确、分块网格计数/平面法线/UV/
  邻块边界逐位一致/边界块截断/重复构建确定、splat 四种地貌各自主导 + 任意采样点
  权和恒 1、脏区域波及判定、序列化逐位往返与 6 类损坏拒绝。
- 沙箱离线编译运行（-std=c++20 -Wall -Wextra -O0；本机无 g++，以 LLVM clang++ 同参数）：
  **compile 0 error 0 warning，7 用例 297 断言 0 失败，exit 0**。
- ⚠️ 引擎内未接线：地形渲染（分块网格上传 + splat 采样）、笔刷交互面板、植被实例化、
  高度图纹理导入（核心已预留 `SetHeight` 直写入口）留待后续——与 VoxelWorld/Lightmap
  同款「先落地后接线」节奏。

## [0.22.19] - 2026-09-26 —— 反射探针核心（U2-L2）：SH 存储 + 粗糙度带通预滤波 + box 视差校正 + 混合采样

> GI 双柱的另一半。漫反射侧已有 LightProbe.h（SH 辐照度体 + 体插值）；镜面侧此前只有
> SSR（屏幕空间，背面/遮挡处全黑）与 IBL（无限远天空，局部墙面/霓虹反射不到）。
> 本轮落地 U2-L2 的反射探针 CPU 核心：场景局部环境的镜面反射离线烘焙成 SH 辐射亮度，
> 运行时按位置选探针、按反射方向经 box 视差校正求值。
> 纯 CPU、复用 LightProbe.h 的 Sh9 机制、不触碰 Vulkan，可离线单测。

### 新增文件（src/render/ReflectionProbe.h，header-only，`BigHero::Render`）
- **`ReflectionProbe`**：探针数据 = 位置 + 影响盒 + 原始入射辐射亮度 SH（未预滤波）+ 有效性。
- **`ReflectionProbeSet`**：
  - `AddProbe`/`SetProbeSh`/`Bake(radianceFn, samples)`——烘焙注入 RadianceFn（与
    LightProbeVolume::Bake 同构），Fibonacci 球面投影成 SH（确定性）。
  - `PrefilterForRoughness(sh, α)`——采样时粗糙度带通滤波：`f_l(α)=exp(-l(l+1)·α²/2)`
    （GGX(α) ↔ Phong(n≈2/α²) 转化近似）。α=0 精确锐利（L2 截断精度内），α→1 只留底色。
    **一颗探针一套系数服务所有粗糙度，无需逐档烘焙 mip 链**。能量不变量：均匀环境
    （辐射亮度恒 L）任意粗糙度恒得 L（f_0≡1），由单测锁死。
  - `ParallaxCorrectedDir(probe, p, R)`——经典 box projection（S. Lagarde）：求 p+tR 与
    影响盒的出口交点（盒内起点版 slab 法：各轴出口 t 取**最小**者 = 最先命中的面），
    反射像钉在盒壁上；探针中心处无唯一解回退原方向；盒外点先钳入盒内，全程良定义。
  - `Sample(p, R, α)`——完整链路：盒内候选 → 三轴三角衰减窗（盒中心 1/盒壁 0）权重
    归一化混合（无效探针剔除）→ 无候选退化「盒中心最近的有效探针」（与 LightProbe
    同哲学、不闪黑）→ 全空回退 SH（默认黑）。
  - `SaveReflectionProbes`/`LoadReflectionProbes`——纯文本快照（%.9g 十进制精确往返，
    仿 LightmapBaker::SaveLightmap 先例）；未知字段/非法值/截断整体拒绝。

### 验证
- 新增测试 **src/tests/test_reflection_probe.cpp 7 用例 / 281 断言**：均匀环境不变量
  （3 点×4 向×4 粗糙度恒得 L）、全链路与直接 SH 参考自洽（探针中心、3 向×3 粗糙度）、
  粗糙度模糊峰值下降、视差几何（中心回退/轴向出口/东北向首出 x 面/盒外钳制）、
  双探针重叠中点=均值与无效剔除、等距决胜确定性 + 最近中心兜底 + 退化输入拒绝、
  序列化逐位往返（含采样一致性）与 6 类损坏拒绝。
- 沙箱离线编译运行（-std=c++20 -Wall -Wextra -O0；本机无 g++，以 LLVM clang++ 同参数）：
  **compile 0 error 0 warning，7 用例 281 断言 0 失败，exit 0**。
- ⚠️ 引擎内未接线：运行时金属表面接入（SSR 缺失区回退反射探针）、探针放置与捕获烘焙
  （渲染 6 面立方图需 GPU）留待后续——与 LightmapBaker/NavMesh 同款「先落地后接线」节奏。

## [0.22.18] - 2026-09-26 —— 光照贴图接线 2a：--bake-lightmap 离线 CLI + Windows headless 初始化修复（U2-L1）

> 0.22.17 落地了纯 CPU 的 LightmapBaker 核心但「引擎内未接线」。本轮接通离线管道的第一段：
> 命令行 `--bake-lightmap` 在引擎完整初始化后、主循环前，把场景几何收集成三角形汤交给
> LightmapBaker 烘焙（chartless 直接光 + 天光 AO），RGBE 文本快照写盘并打印统计后立即退出
> （0=成功 1=失败）。验证过程中揭露并修复了一个预存在的 headless 初始化崩溃——详见下文。

### 接线（src/app + src/main）
- **`--bake-lightmap [p]` CLI**（main.cpp + AppConfig）：可选覆盖输出路径（默认 `lightmap.lm`，
  写于引擎工作目录）。帮助文本同步更新。
- **`Application::BakeLightmapOffline()`**（Application_Record.cpp）：场景 `scene_` 投影包中
  全部 meshId 0 共享立方体（12 三角形/个，父链合成世界矩阵、≤8 跳防环），albedo=tint；
  太阳取编辑器 `LightParams`（direction + color×intensity），点光源取场景 `pointLights_`
  （color×intensity + 半径窗口），天光走烘焙器默认 skyColor；参数 `atlas=512 / texel=1.0m`。
- **退出语义**：烘焙请求经既有 `RunPendingBakes()` 分发（与 --bake-probes/--bake-occlusion
  同构），主循环前同步兑现；失败（图集放不下/写盘失败）返回 1，成功返回 0。
- **测试注册**：CMakeLists 的 BigHeroTests 补入 `src/tests/test_lightmap.cpp`。

### 修复：Windows/Linux 真 headless 完整初始化崩溃（预存在）
- `Renderer::createFrameResources` 无条件读 `swapchain_.Extent()/Format()/ImageCount()`——
  headless 模式从不创建交换链，拿到 {0,0} 尺寸、`VK_FORMAT_UNDEFINED` 格式去建 MSAA 深度/
  颜色图像，触发 `VUID-VkImageCreateInfo-extent-00944/00945` 与 `pNext-01975` 后创建失败崩溃。
  此前任何平台都未真正走过 headless 完整初始化（CI 的 Linux 截图回归在 xvfb 虚拟显示器上
  开真实窗口；`--headless` 仅与 `--validate-only`（不初始化 Vulkan）组合使用过）。
- 修复（Renderer_FrameResources.cpp）：headless 分支用占位 extent（1280×720）与占位格式
  （B8G8R8A8_SRGB，与 headless 渲染通道一致），并跳过挂交换链视图的直通帧缓冲（headless
  的 DrawFrame 本就不消费）；窗口化路径行为逐位不变。顺带使本机所有离线验证不再弹窗口。

### 验证
- MSVC Release 增量构建：0 error；本轮改动零新警告（仅有存量 C4834/C4100 噪音）。
- 本机（AMD 780M）单次 `--headless --bake-lightmap --scene default` 真机运行：
  `[lightmap] objects=5 triangles=60 charts=60 atlas=512x512 litTexels=63 bakeMs=0.724
  output=lightmap.lm`，exit 0；产物 2.1 MB RGBE 文本快照（60 chart 基向量与立方体对角线
  剖分几何吻合），RAII 资源清理干净（PhysicsEngine/MemoryPools 正常销毁）。
- 范围内已知取舍：默认演示场景立方体全部带自转，2a 按相位 0 姿态烘焙（不区分静态）；
  地面（1000×1000 局部四边形）、torus/glTF、体素世界不参与烘焙（chartless 逐三角形图集
  对超大四边形与万级面几何不经济）。**渲染端光照贴图采样、Static 标记、编辑器面板按钮
  留待接线 2b。**

## [0.22.17] - 2026-09-26 —— 光照贴图烘焙核心：chartless 直接光 + 图集打包 + 运行时采样 + RGBE 序列化（U2-L1）

> U2-L1 的 light probes 半部（LightProbe.h，SH 系数字段 + 体插值）已在 0.21.0~0.21.3 落地并接线；
> 本轮补齐 lightmap 半部——把静态场景接收的「直接光 + 天光」离线烘焙成图集纹理的 CPU 核心。
> 参数化走 **chartless**（每个三角形独立正交投影进图集），不依赖 xatlas 类网格展开库：
> 全局 UV 展开正是 U2-L1 验证列「漏光/seam 长尾」预算的大头，逐三角形投影配合内容区边缘
> 钳制采样直接把这类长尾绕开；代价是图集利用率略低（三角形外 texel 浪费），小体量场景可接受。
> 阴影射线/天光 AO 全在 CPU 完成（Möller–Trumbore 双精度求交 + 确定性 Fibonacci 半球方向集，
> 零随机数），烘焙结果可完全离线单测。

### 新增文件（src/render/LightmapBaker.h，header-only，`BigHero::Render`）
- **输入/输出结构**：`LightmapTri`（世界三角形 + albedo + 投射开关）、`DirectionalLightDesc`、
  `PointLightDesc`（平方衰减 + 半径窗口，与 PBR 多光源口径一致）、`LightmapBakeParams`、
  `LightmapChart`、`LightmapResult`（atlas 尺寸 + RGBE texel 缓冲 + 逐三角形 chart）。
- **`BakeLightmap(tris, dirs, points, params, out)`**：chart 参数化（最长边定 U 轴、内容区紧裹
  三角形）→ guillotine 图集打包（高度降序白盒确定）→ 逐 texel 光线烘焙（方向光/点光可见性 +
  天光半球因子 × AO）→ albedo 吸收出射辐射度（Unity lightmap 语义，运行时直接累加不再乘 albedo）。
- **`SampleLightmap(lm, chartIndex, worldPos)`**：运行时查询——世界坐标投影进 chart 内容区
  双线性过滤，越界按边缘钳制（等于 dilation 外扩防漏光），O(1) + 4 次 RGBE 解码。
- **`RgbeEncode`/`RgbeDecode`**：RGBE 编解码，与 `HdrImage::RGBEToLinear` 的 radiance 约定逐位兼容。
- **`SaveLightmap`/`LoadLightmap`**：纯文本快照序列化（仿 AssetDatabase::SaveCache 先例，
  可重现、便于 diff；%.9g 精确保真，RGBE 数据为 16 进制块）。损坏/截断/未知字段整体拒绝。

### 关键设计
- **保守阴影**：默认不剔除背向面——纸片式单面几何（墙壁/挡板）无论哪面朝向射线都遮挡，
  宁可多遮不错遮；严格双面闭合网格可开 `cullBackfaces` 提速。阴影射线起点沿法线偏移
  `shadowBias`，t < bias 的近距命中忽略（防共面邻居假阳性）。
- **确定性**：图集打包按「高度降序 → 宽度降序 → 三角形下标」白盒排序、AO 用固定方向集
  （Fibonacci 球面点列，与 test_light_probe 验证驱动同构），同一输入必然得到逐字节一致的贴图。
- **chartless 已知取舍**：中心落在三角形外的浪费 texel 保持零值；运行时采样点全在三角形内，
  仅长边附近半 texel 渐变区可能混入零值，texel 密度越高影响越小（文件头契约已注明）。
- **RGBE 精度契约**：共享指数使暗通道绝对误差可达 max/512 量级——这是格式固有语义，
  测试按「相对最亮通道 1%」口径核对。

### 验证
- 新增测试 **src/tests/test_lightmap.cpp 11 用例 / 108 断言**：图集打包（边界/两两不重叠/
  margin=rect−content 不变量/重复烘焙逐字节确定）、方向光解析解（±0.01）、albedo 吸收、
  点光源平方衰减解析解（±0.01）、硬阴影（板下全黑/板外全亮）、AO 贴墙 vs 开阔（开阔恒等于
  天光 L、贴墙显著衰减）、双线性过滤已知 gradient、多光源求和、退化输入/参数拒绝、RGBE
  量化口径、序列化往返逐字节一致与 7 类损坏输入拒绝。
- 沙箱离线编译运行（-std=c++20 -Wall -Wextra -O0；本机未装 g++，本次以 LLVM clang++ 同参数）：
  **compile 0 error 0 warning，11 用例 108 断言 0 失败，exit 0**。
- ⚠️ 引擎内未接线：渲染管线的光照贴图采样、`--bake-lightmap` CLI、ProjectPanel 烘焙按钮
  留待第二阶段（需可开引擎验证 GPU 环境）——与 NavMesh.h「先落地后接线」同款节奏，
  接口按接线零改动设计。

## [0.22.16] - 2026-09-26 —— 启动加速：资产库快照缓存（AssetDatabase 扩展）

> 冷启动 `ImportAll()` 会重读并重解析**全部**资产文件内容来重建引用表（kinds_/sizes_/
> mtimes_/refs_ 均只在内存）。项目一大，这就是启动时最重的瓶颈。本轮新增**快照缓存**：
> 把完整内存状态持久化到磁盘，热启动只做 N 次文件 stat 校验、零内容读取。配合既有的
> `ScanForChanges`/`ApplyChanges`，构成「读缓存 → 校验 → 增量应用变更」的完整快速启动路径。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`SaveCache(path)`**：把完整内存状态（每资产的 guid/rel/kind/size/mtime + 引用图）写成
  纯文本快照（按 GUID 排序、可重现）。引用条目序列化为 `raw → resolvedHex`（断链记 `-`）。
- **`LoadCache(path)`**：逐行校验文件存在性与 (mtime,size) 一致性，全对才重建内存。
  任一不符（文件被改/被删/.meta 被改写/格式损坏）即 Clear 并返回 false——调用方干净降级
  `ImportAll()`。**全程仅 stat、零内容读取**，这是热启动提速的核心。引用断链在加载末尾
  由既有 `ReresolveAll()` 自愈。
- **`WarmStart(cachePath)`**：便捷入口——先尝试 `LoadCache`，失败则降级 `ImportAll`，
  返回是否走了缓存。

### 关键设计
- **正确性优先于速度**：缓存只在「文件存在 + size + mtime + .meta 身份」四项全对时才
  被接受，任何一项漂移都整体回退到全量导入——宁可慢，不可错。
- **零副作用登记**：热启动登记走 `GuidForPath`（读现有 .meta）并校验与缓存一致，绝不
  改写 .meta；不一致即视为库外改动、缓存作废。
- **与 SaveIndex 分工**：SaveIndex 持久化 GUID 映射（供无 .meta 的打包/只读场景），
  SaveCache 持久化完整内存状态（供编辑器/工具热启动），两者互补。

### 验证
- 新增测试用例 **`AssetDb.CachePersistence`**（src/tests/test_asset_database.cpp）：
  冷启动存缓存 → 热启动 LoadCache 重建内存（引用图/类别/体积完整）→ 文件被改后 LoadCache
  拒绝并干净降级 → 文件被删后拒绝 → 无缓存时 WarmStart 降级且计数正确（缓存置于资产根
  之外，避免被 ImportAll 当作资产）——共 18 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  17 用例 459 断言 0 失败，exit 0**。全部既有 16 用例保持通过，无回归。

## [0.22.15] - 2026-09-26 —— GUID 完整性校验：复制粘贴撞车的检测与确定性修复（AssetDatabase 扩展）

> 资产在 Explorer / 编辑器外「复制-粘贴」会连同 .meta 一起复制 → 两个文件共享同一 GUID。
> 虽然 `GuidForPath` 的撞车自愈会在导入时为后登记者重新生成身份，但「谁保住原 GUID」
> 取决于导入顺序——原文件的既有引用可能被静默转移到副本上。本轮补上不依赖导入状态的
> 磁盘级检测与确定性修复，并把第五类问题接入 `Audit()`。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`DuplicateGuidGroup`**：一组共享同一 GUID 的资产（≥2 个路径，字典序；`paths[0]`
  为修复保留者）。
- **`FindDuplicateGuids()`**：扫描磁盘 Root 全部 .meta，按 GUID 归组，仅报告有效 GUID
  的多路径撞车（缺失/损坏的 .meta 由导入自愈负责，不混入）。结果按首路径字典序，确定性。
- **`RepairDuplicateGuids()`**：每组保留**字典序最小路径**的 GUID（复制场景下通常即原
  文件），其余资产重新生成身份并重新导入，返回被重分配身份的资产数。顺序设计：先摘除
  副本登记（连带删其共享 .meta），再兜底导入保留者——即使「保留者未登记而副本已登记」，
  身份归属依旧确定。**已登记身份永不被剥夺；保留者 GUID 不变 ⇒ 指向它的既有引用零影响。**

### 审计接入
- `Audit()` 新增第五类问题 `duplicate-guid`（Error），每个涉案路径一条，`detail` 带
  共享 GUID 的十六进制串，便于定位是哪一组撞车。

### 验证
- 新增测试用例 **`AssetDb.DuplicateGuids`**（src/tests/test_asset_database.cpp）：
  初始无撞车 → 模拟连同 .meta 复制 → 检测出 1 组 2 路径（字典序）→ 审计报 2 条
  duplicate-guid Error → 修复后原文件保住 GUID、副本得新身份、引用不断 → 三方撞车
  （brick3/brick4）一次修复 2 个、原文件身份依旧——共 40 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  16 用例 441 断言 0 失败，exit 0**。全部既有 15 用例保持通过，无回归。

## [0.22.14] - 2026-09-26 —— 资产健康审计：一次调用拿到全部问题（AssetDatabase 扩展）

> 0.22.10~0.22.13 陆续补齐了断链检测、引用环检测、孤儿分析与孤儿 .meta 清理四件单点
> 工具。本轮把它们拧成一个收口能力：**`Audit()` 健康审计**——编辑器「项目健康」面板
> 或 CI 门禁不再需要逐个调用再自行拼接，一次调用即得按严重度分级的全量问题清单。
> 纯组合既有已验证原语，零新算法、零外部依赖，可离线单测。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`AssetIssue`**：一条审计发现，含 `severity`（Error/Warning/Info）、`category`
  （`broken-ref`/`cycle`/`orphan`/`stale-meta`）、`path`（主资产相对路径）与 `detail`
  （断链时带被引用的原始写法，便于定位修复）。
- **`Audit()`**：聚合四类检测——断链（Error，阻断加载必须先修）、引用环（Error，无合法
  加载顺序，以全部已登记资产为根跑 `ComputeLoadOrder` 检出）、孤儿 .meta（Warning）与
  孤儿资产（Warning，以全部场景资产为根）。输出按 severity → category → path → detail
  确定性排序：同一库状态必然得到同一份报告，可直接做 CI 断言或快照对比。
- 审计是**只读快照**，不做任何修复；修复仍走各单点 API（`Import` 愈断链 /
  `SweepStaleMetaFiles` 清孤儿 .meta / `Move` 保引用等），职责边界清晰。

### 关键设计
- **「文件丢失」与「孤儿」严格区分**：被引用的资产文件在库外被删，其 GUID 仍解析到已
  登记路径，可达性判定不受影响——体现为 `stale-meta`（旁车残留）而非 `orphan`（无人
  引用）。审计聚合时两类问题各归其位，互不混淆。
- **复用而非重写**：四类检测完全复用 0.22.10~0.22.13 的既有实现，新增代码只做聚合与
  排序——检测语义不变、回归风险为零。

### 验证
- 新增测试用例 **`AssetDb.Audit`**（src/tests/test_asset_database.cpp）：初始单断链 →
  Import 愈合后审计清空 → 人为制造四类问题（断链/互引环/孤儿资产/孤儿 .meta）验证聚合
  数量与严重度排序（Error 全部在前）→ 同状态重复审计逐位一致——共 63 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  15 用例 401 断言 0 失败，exit 0**。全部既有 14 用例保持通过，无回归。

## [0.22.13] - 2026-09-26 —— 打包清单 + 孤儿 .meta 清理（AssetDatabase 扩展）

> 引用图、断链、变更检测、热重载闭环、孤儿分析、拓扑序之后，补两块「发布与维护」侧
> 的常用工具：打包前的内容枚举与体积预估，以及资产在库外被删后残留的 .meta 旁车清理。
> 依旧纯 `core/` + 标准库，零外部依赖，可离线单测。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`CollectBundle(roots)`**：给定一组根资产，收集「根自身 + 传递依赖」的完整打包清单
  （`AssetBundleEntry{path, kind, size}`，按路径字典序）并给出体积合计 `totalBytes`——
  回答「这个关卡到底要带哪些资产、一共多大」。未登记的根静默跳过；断链引用没有可打包
  目标，天然不入清单（断链另见 BrokenReferences）。与 `FindOrphans` 互补：一个列「要带
  什么」，一个列「可以删什么」。
- **`FindStaleMetaFiles()`**：只读扫描磁盘 Root，找出「主资产已不存在」的孤儿 .meta
  旁车（相对路径、字典序）。
- **`SweepStaleMetaFiles()`**：删除全部孤儿 .meta，返回删除数量（先报后删，同一扫描口径）。

### 关键设计
- **孤儿 .meta 的成因**：库内 `Move`/`Remove` 都会同步处理 .meta（写新删旧），因此孤儿
  只能来自「库外直接删/移动资产文件」。这类残留不参与索引（`ImportAll`/`ScanForChanges`
  本就跳过 .meta），却会污染版本控制与打包目录——故单独提供查找与清扫，而不混入既有流程。
- **体积口径诚实**：`totalBytes` 是各资产**源文件**体积的简单求和（复用 `sizes_`），
  不含打包容器开销/压缩收益/对齐填充，定位为「下限预估」而非精确包体。

### 验证
- 新增测试用例 **`AssetDb.BundleAndMetaSweep`**（src/tests/test_asset_database.cpp）：
  场景根清单枚举（4 资产、字典序、断链贴图不入）→ 体积合计与逐资产 SizeOf 一致 →
  子集根/空根/未登记根/幂等 → 库外删主文件后孤儿 .meta 被识别 → 清扫后孤儿消失且存活
  资产的 .meta 不受影响——共 28 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  14 用例 338 断言 0 失败，exit 0**。全部既有 13 用例保持通过，无回归。

## [0.22.12] - 2026-09-26 —— 拓扑加载顺序 + 引用环检测（AssetDatabase 扩展）

> 引用图（0.22.8）、断链检测、变更检测（0.22.10）、热重载闭环与孤儿分析（0.22.11）之后，
> 资产管线还缺最后一块常用拼图：**加载顺序**。资源系统按依赖先后加载（材质引用贴图时
> 贴图必须已就绪），打包写入也同理。本轮补上 `ComputeLoadOrder`，并顺手把「引用环」这一
> 坏内容信号显影出来。依旧纯 `core/` + 标准库，零外部依赖，可离线单测。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`AssetLoadOrder`**：一次拓扑计算的结果，含 `order`（加载顺序：每个资产的依赖都排在
  它之前，同层按字典序决胜、可重现）与 `cycles`（处于引用环上的资产，字典序），并提供
  `HasCycles()`。
- **`ComputeLoadOrder(roots)`**：对根资产集的**传递依赖闭包**做 Kahn 拓扑排序（小顶堆
  保证同层字典序出队 → 同一引用图必然得到同一顺序，便于测试与问题复现）。引用环上的
  资产不存在合法加载顺序，一律不进 `order`，单独列入 `cycles`——环几乎总是内容出错的
  信号，显影给工具链而不是静默跳过。未登记的根静默跳过；断链引用没有可加载目标，天然
  不进序。

### 关键设计
- **确定性输出**：同层资产之间没有依赖约束时按字典序决胜（`std::greater` 小顶堆），
  避免 `unordered_map` 遍历序导致的「同图不同序」，测试结果可断言、可复现。
- **环隔离而非拒绝**：环上资产被单独列出，干净子图照常得到合法加载顺序，互不污染。
- **环安全复用**：可达集收集复用 `CollectDependencies`（自带 visited，遇环收敛），
  环的判定交给 Kahn 出队余量——两种机制各自简单，不相互渗透。

### 验证
- 新增测试用例 **`AssetDb.LoadOrder`**（src/tests/test_asset_database.cpp）：
  场景根的四资产全序（依赖在前 + 同层字典序）→ 不变式逐对检查 → 同图重复计算逐位一致
  → 空根/未登记根静默跳过 → `a.mat ↔ b.mat` 互引成环入 `cycles` → 自环同样被检出 →
  干净子图与环隔离共存——共 34 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  13 用例 310 断言 0 失败，exit 0**。全部既有 12 用例保持通过，无回归。

## [0.22.11] - 2026-09-26 —— 热重载闭环 + 资产可达性分析（AssetDatabase 扩展）

> 在 0.22.10 的变更检测之上，把「感知 → 应用」补成闭环，并顺手修复一个会在热重载中
> 暴露的隐患。三件事：①修复引用表「只增不删」累积缺陷，②新增 `FindOrphans` 可达性
> 分析，③新增 `ApplyChanges` 一键热重载。依旧纯 `core/` + `std::filesystem`，零外部
> 库依赖，可离线单测。

### 修复：引用表「只增不删」→ 真·刷新（src/core/AssetDatabase.h `Register`）
- 旧实现把新扫描到的引用**追加**到既有条目后，从不移除文件里已被删掉的引用——热重载
  删掉一行 `normal: ...`，那条旧引用会残留成永远的假断链。
- 现改为以当前文件内容为准**真·刷新**：同一写法的既有条目保留其已解析 GUID（移动后
  不退化成断链），文件中已不存在的旧引用则丢弃。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`FindOrphans(roots)`**：从一组根资产（通常传入所有场景）出发做传递可达性分析，
  返回未被任何根直接或间接引用的孤儿资产（相对路径、字典序）。用于资产清理 / 打包
  瘦身。⚠️ 只负责「引用图上不可达」这一客观事实，运行时按路径硬编码加载的资产需人工确认。
- **`ApplyChanges()`**：一键应用 `ScanForChanges` 结果（added/modified→`Import`，
  removed→`Remove`），返回本次实际处理的变更集。文件监视器触发后一句 `db.ApplyChanges()`
  即可令索引与磁盘重新一致。

### 验证
- 新增测试用例 **`AssetDb.OrphansAndApplyChanges`**（src/tests/test_asset_database.cpp）：
  全可达→无孤儿 → 加孤儿资产被识别 → 换根集→旧根变孤儿 → 空根集→一切皆孤儿 →
  ApplyChanges 一键同步（新增/修改/删除各一）→ 处理后 `ScanForChanges` 干净、引用表
  真·刷新使断链消失——共 24 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  12 用例 276 断言 0 失败，exit 0**。全部既有 11 用例保持通过，无回归。

## [0.22.10] - 2026-09-26 —— 资产变更检测：热重载的增量刷新基础（AssetDatabase 扩展）

> 在 0.22.9 的引用传递闭包之上，补上资产管线的「感知层」：磁盘上哪些资产**新增 / 修改 / 删除**了？
> 这是编辑器热重载的第一步——文件监视器触发后，不再无脑 `ImportAll` 全量重扫，而是精准地
> 只刷新发生变化的资产（大型资产工程里，全量重扫是帧卡顿与编辑中断的主要来源）。
> 不引入新外部库；纯 `std::filesystem` + 既有 `core/` 工具，零 GPU/窗口依赖，可离线单测。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`AssetChangeSet`**：一次变更检测的结果，含 `added` / `modified` / `removed` 三组相对路径
  （各自字典序、互不重叠），并提供 `Empty()` / `TotalCount()`。
- **`ScanForChanges()`**：递归扫描磁盘 Root 与内存索引的差异，列出三向变更。只读、不落盘、
  不改动库状态；调用方拿到结果后自行决定 `Import` / `Remove`。
- **`SnapshotNow()`**：把内存索引的 (mtime, 体积) 基线对齐到磁盘现状，**不重新解析引用**
  （区别于 `Import`：只同步时间戳/体积，轻量）。用于外部批量处理后声明新基线。

### 关键设计
- **`mtimes_` 成员**：导入时记录文件 `last_write_time`，与体积一起作为「修改」的双重判定
  ——写入通常会更新 mtime；即便文件系统 mtime 粒度太粗，体积变化也能兜底。`mtimes_`
  与 `kinds_`/`sizes_` 一样，在 `Register`/`Move`/`Remove`/`Clear` 中同步维护。
- **`LastWriteTime`** 私有辅助：`last_write_time` 读取失败返回零值，使比对必然视为「不同」
  （保守策略：宁可误判为已修改而多刷新，不可漏刷新）。
- 三组判定互不重叠且各自字典序，便于编辑器直接渲染「新增/修改/删除」三栏。

### 验证
- 新增测试用例 **`AssetDb.ChangeDetection`**（src/tests/test_asset_database.cpp）：
  基线干净 → 新增资产入 `added` → 修改内容入 `modified` → 删除文件入 `removed` →
  模拟热重载循环（added/modified→Import、removed→Remove）后索引与磁盘重新一致 →
  `SnapshotNow` 重置基线——共 24 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  11 用例 252 断言 0 失败，exit 0**。全部既有 10 用例保持通过，无回归。

## [0.22.9] - 2026-09-26 —— 引用传递闭包：打包/删除评估/分类筛选的依赖全景（AssetDatabase 扩展）

> 在 AssetGuid 引用追踪（0.19.2）与四情形/去重/指针契约硬化（0.21.3）之上，为 AssetDatabase
> 补齐**引用图的传递展开**能力：一处引用解析成 GUID 后，本模块把「这张图的所有邻居」按层级
> 递归收拢成集合。三类典型资产管线动作由此有了可靠支撑——资产打包要「这个关卡到底需要
> 哪些东西」、删除前评估要「删掉它会波及哪些资产」、资产浏览器要「按类别筛选」。
> 不引入新外部库；纯 `std` + 既有 `core/` 工具，零 GPU/窗口依赖，可离线单测。

### 新增 API（src/core/AssetDatabase.h，header-only，`BigHero::Core`）
- **`CollectDependencies(rel)`**：递归收集某资产**直接+间接**依赖的全部资产（跨多级），
  返回相对路径、字典序、**不含自身**。用途：资产打包时收集一个关卡的完整依赖闭包；
  「本资产到底依赖了什么」的全景视图。
- **`CollectDependents(rel)`**：递归收集**直接+间接**引用此资产的全部引用者。
  用途：删除前的完整**影响面评估**（删一张贴图，会波及哪些材质、哪些场景）。
- **`FindAssetsByKind(kind)`**：按类别（Texture/Mesh/Material/…）筛选已登记资产，
  返回相对路径、字典序。用途：资产浏览器的分类列表。

### 关键设计
- **循环引用安全**：visited 集合（以相对路径为键）保证递归收敛，a↔b 互引不会死循环。
- **裸指针生命周期守 A2 契约**：`PathFor` / `GuidFor` 返回的内部裸指针一律**立即拷贝**
  再用——`PathFor` 命中缓存可能向 `relCache_` 插入触发 rehash 令指针悬空。
- **断链目标不入闭包**：只有解析成功的引用（`IsValid` 且能反查当前路径）才参与传递；
  缺失/断链目标没有可入集的路径，由 `BrokenReferences()` 单独显影。
- 三个公共查询为只读、不修改库状态，可在任意时刻安全调用。

### 验证
- 新增测试用例 **`AssetDb.DependencyClosure`**（src/tests/test_asset_database.cpp）：
  场景→材质→贴图的两级传递依赖（缺失的 normal 断链不入集）、贴图的传递引用者
  （材质+场景）、叶子无引用、类别筛选、a↔b 循环收敛——共 24 断言。
- 沙箱离线编译运行（g++ -std=c++20 -Wall -Wextra -O0）：**compile 0 error 0 warning，
  10 用例 228 断言 0 失败，exit 0**。全部既有 9 用例保持通过，无回归。

## [0.22.8] - 2026-09-23 -- stb 第三方单头库隔离：CMake FetchContent 化（P3 工程化）

> 把 stb 从仓库内 vendor 快照（整份 stb 仓库，含 tests/data/.github 等约 430 文件 + 嵌套 .git）
> 迁移为 CMake FetchContent 按需拉取，锁定 commit 2c980bb（与原 vendor 副本逐字节一致）。
> 业务源码 include 写法与 STB_*_IMPLEMENTATION 宏位置均未改动，零行为变化。
> 不引入新外部库（stb 为既有依赖，仅变更获取方式）。

### 隔离方案
- 采用 CMake `FetchContent`（方案 A），不再把 stb 仓库源码直接提交进本仓库。
- 锁定版本：`GIT_REPOSITORY https://github.com/nothings/stb.git`，`GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20`（即原 vendor 副本所在 commit，2026-08-01）。
- 已用 SHA256 逐字节比对 `stb_image.h` / `stb_truetype.h` / `stb_image_write.h`：FetchContent 落盘结果与原 vendor 副本完全一致，二进制行为不变。

### 目录结构变化
- 删除 `thirdparty/stb/`（原为本仓库直接提交的整份 stb 仓库快照，含 tests/data/deprecated/tools/.github 等约 430 个文件及嵌套 `.git`）。
- stb 源码改为配置期由 FetchContent 拉取到 `build/_deps/stb-src/`（build/ 已被 .gitignore 忽略，不进版本库）。

### CMake target 变化
- 新增 `stb` INTERFACE target 的正式化定义，并补充别名 `stb::stb`；include 目录指向 `${stb_SOURCE_DIR}`（FetchContent 落盘目录）。
- 移除 `BigHeroEngine` 上冗余的 `$<BUILD_INTERFACE:.../thirdparty/stb>`（原先与 `stb` PUBLIC 链接重复），include 路径统一由 `stb` target 经 PUBLIC 链接传递。

### include 路径变化
- 业务源码 `#include <stb_image.h>` / `<stb_image_write.h>` / `<stb_truetype.h>` 写法保持不变（include 目录仍指向 stb 仓库根），无需改动任何 .cpp。
- `STB_IMAGE_IMPLEMENTATION` / `STB_IMAGE_WRITE_IMPLEMENTATION` / `STB_TRUETYPE_IMPLEMENTATION` 编译宏位置不变，仍分别唯一定义在 `src/render/Texture.cpp`、`src/render/Renderer.cpp`、`src/ui/UiFontAtlas.cpp`。
- imgui 内对 `../stb/stb_image_write.h` 的相对引用位于 `#if 0` 死代码块，不参与编译，不受影响。

### 验证
- cmake --build build --config Release：0 error，全部 target 产出。
- ctest --test-dir build -C Release：通过，0 failure。
- BigHeroHeaderCheck：构建通过。
- 冒烟 `--scene default --bench-frames 30`：exit 0，30 帧渲染、RAII 资源正常释放。

### 备注
- 首次配置需要网络访问 github 拉取 stb（与既有 nlohmann_json / reactphysics3d FetchContent 一致）；配置完成后后续构建离线可用。

## [0.22.7] - 2026-09-23 -- P3 工程化第七步：clang-tidy WarningsAsErrors 门禁扩展至 EditorPanel.h

> 不改游戏逻辑。在上轮 41 模块基础上新增 1 个此前排除的大模块：EditorPanel.h（63 KB 编辑器面板头）。
> 摸底发现该头文件经根 .clang-tidy 配置过滤后为 0 告警，无需改代码。不引入新外部库。

### 纳入模块（1 个新增 + 41 个既有 = 42 个）
- `src/editor/EditorPanel.h`：编辑器面板头（63 KB），驱动翻译单元 `src/app/Application.cpp`。

### 修复的告警
- 无需修复——EditorPanel.h 经根 .clang-tidy 配置过滤后为 0 clang-tidy 告警。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，42 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_step7.txt`。

## [0.22.6] - 2026-09-23 -- P3 工程化第六步：clang-tidy WarningsAsErrors 门禁扩展至 Application.h 与 EditorOverlay.h

> 不改游戏逻辑。在上轮 39 模块基础上新增 2 个此前排除的大模块：Application.h（42 KB 主循环头）
> 与 EditorOverlay.h（编辑器叠加层）。修复 Application.h 中 4 处 offsetof 常量表达式问题
> （沿用 __builtin_offsetof 先例），并修复 compile_commands.json 的 arguments 数组格式以正确
> 传递 BIGHERO_SOURCE_ROOT 宏引号。不引入新外部库。

### 纳入模块（2 个新增 + 39 个既有 = 41 个）
- `src/app/Application.h`：主循环头（42 KB），驱动翻译单元 `src/app/Application.cpp`。
- `src/editor/EditorOverlay.h`：编辑器叠加层（1.6 KB），驱动翻译单元 `src/editor/EditorOverlay.cpp`。

### 修复的告警
- `Application.h` 4 处 `offsetof` → `__builtin_offsetof`（PushObject 结构体偏移 static_assert，
  与此前 InstanceBuffer.h 相同问题：UCRT offsetof 宏展开含 reinterpret_cast，clang 在常量表达式中拒绝）。
- compile_commands.json 格式从 `command` 字符串改为 `arguments` 数组，修复 BIGHERO_SOURCE_ROOT
  宏引号被 shell 解析器剥离导致的 ProjectPanel.h:310 编译错误。

### 排除的模块
- `src/editor/EditorPanel.h`（63 KB 编辑器面板头）：文件过大且依赖 ImGui 复杂耦合，留待后续轮次。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，41 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_step6.txt`。

## [0.22.5] - 2026-09-23 -- P3 工程化第五步：clang-tidy WarningsAsErrors 门禁扩展至 39 个模块（音频/导航/脚本/示例场景）

> 不改游戏逻辑。在上轮 20 模块基础上新增 19 个模块纳入门禁：音频（AudioEngine/AudioMixer/
> Sound/SoundSource）、导航 Host（NavHost）、系统 Host（AnimationHost/PostProcessSync/
> SceneIoHost/ShowcaseHost）、场景（AvatarRetarget/PersonHost/Camera/FirstPersonCamera）、
> 脚本（CSharpHost/ScriptFields）、示例场景（OpenWorld/CyberCity/Slice/VoxelWorld）。
> Application.h（42 KB 主循环头）留待后续轮次单独评估。不引入新外部库。

### 纳入模块（19 个新增 + 20 个既有 = 39 个）
- 音频：`src/audio/AudioEngine.h`、`src/audio/AudioMixer.h`、`src/audio/Sound.h`、
  `src/audio/SoundSource.h`。
- Host 层：`src/app/systems/NavHost.h`、`src/app/systems/AnimationHost.h`、
  `src/app/systems/PostProcessSync.h`、`src/app/systems/SceneIoHost.h`、
  `src/app/systems/ShowcaseHost.h`。
- 场景：`src/scene/AvatarRetarget.h`、`src/scene/PersonHost.h`、`src/scene/Camera.h`、
  `src/scene/FirstPersonCamera.h`。
- 脚本：`src/script/CSharpHost.h`、`src/script/ScriptFields.h`。
- 示例：`samples/open_world/OpenWorldScene.h`、`samples/showcase/CyberCity.h`、
  `samples/vertical_slice/SliceScene.h`、`samples/voxel/VoxelWorld.h`。

### 排除的模块
- `src/app/Application.h`（42 KB 主循环头）：文件过大且含平台/ImGui 耦合，留待后续轮次单独评估。

### 修复的告警
- 19 个新增头文件经摸底均为 0 clang-tidy 告警，无需改代码。
- 未排除新的检查项。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，39 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_step5.txt`。

## [0.22.4] - 2026-09-23 -- P3 工程化第四步：clang-tidy WarningsAsErrors 门禁扩展至 20 个模块（渲染管线 + 后处理 + 物理 Host）

> 不改游戏逻辑。在上轮 9 模块基础上新增 11 个模块纳入门禁：Renderer.h、pipeline.h、
> PostProcessor.h、FrameStaging.h、MemoryPools.h、TransientAllocator.h、
> TransientMemoryPool.h、ParallelCommandRecorder.h、ubo_structs.h（渲染管线/后处理/内存池）、
> ParticleHost.h、PhysicsHost.h（粒子/物理 Host）。不引入新外部库。

### 纳入模块（11 个新增 + 9 个既有 = 20 个）
- 渲染管线：`src/render/Renderer.h`、`src/render/pipeline.h`。
- 后处理：`src/render/PostProcessor.h`。
- 帧资源/内存：`src/render/FrameStaging.h`、`src/render/MemoryPools.h`、
  `src/render/TransientAllocator.h`、`src/render/TransientMemoryPool.h`、
  `src/render/ParallelCommandRecorder.h`、`src/render/ubo_structs.h`。
- Host 层：`src/app/systems/ParticleHost.h`、`src/app/systems/PhysicsHost.h`。
- 既有 9 模块保持 0 告警。

### 修复的告警
- 11 个新增头文件经摸底均为 0 clang-tidy 告警，无需改代码。
- 未排除新的检查项；未引入新的代码修复。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，20 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_step4.txt`。

## [0.22.3] - 2026-09-23 -- P3 工程化第三步：clang-tidy WarningsAsErrors 门禁扩展至 9 个模块（渲染核心 + 场景 + UI）

> 不改游戏逻辑。在上轮 5 模块基础上新增 4 个模块纳入门禁：descriptor_set.h（渲染核心
> 描述符布局）、EcsScene.h（ECS 场景）、Transform.h（变换层级）、UiModel.h（UI 运行时）。
> 另修复 InstanceBuffer.h 中 2 处 offsetof 兼容性问题（MSVC offsetof 宏展开含
> reinterpret_cast，clang 在常量表达式中拒绝；改用 `__builtin_offsetof`，MSVC/clang
> 均支持，零行为变化）。不引入新外部库。

### 纳入模块（4 个新增 + 5 个既有 = 9 个）
- `src/render/descriptor_set.h`（描述符集布局，渲染核心）——头文件 0 clang-tidy 告警。
- `src/scene/EcsScene.h`（ECS 场景）——0 告警。
- `src/scene/Transform.h`（变换层级）——0 告警。
- `src/ui/UiModel.h`（UI 运行时模型）——0 告警。
- 既有 5 模块（LodGroup/FrameProfiler/OcclusionCulling/LightProbe/NavMesh）保持 0 告警。

### 修复的告警
- `src/render/InstanceBuffer.h`：2 处 `offsetof` 替换为 `__builtin_offsetof`
  （clang 兼容性修复，非 clang-tidy 检查告警；MSVC 编译不受影响）。
- 4 个新增头文件经摸底均为 0 clang-tidy 告警，无需改代码。

### 验证
- cmake --build build --config Release：0 error（仅既有 C4834 警告）。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，9 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_step3.txt`。

## [0.22.2] - 2026-09-23 -- P3 工程化第二步：clang-tidy WarningsAsErrors 门禁扩展至 5 个模块

> 不改任何游戏逻辑。在上轮 LodGroup.h 单模块门禁基础上，新增 4 个已验证零告警的
> 头文件模块纳入门禁：FrameProfiler.h、OcclusionCulling.h、LightProbe.h、NavMesh.h。
> 全部模块通过 `tools/run-clang-tidy.ps1` 的 line-filter + WarningsAsErrors 门禁，
> 翻译单元与测试框架告警不参与判定。不引入新外部库。

### 纳入模块（4 个新增 + 1 个既有 = 5 个）
- `src/core/FrameProfiler.h`（帧剖析器基础设施）——0 告警。
- `src/render/OcclusionCulling.h`（遮挡剔除）——0 告警。
- `src/render/LightProbe.h`（光照探针）——0 告警。
- `src/navigation/NavMesh.h`（导航网格，65 KB）——0 告警。
- `src/render/LodGroup.h`（既有，保持 0 告警）。

### 修复的告警
- 4 个新增头文件经 clang-tidy 摸底均为零告警，无需改代码。
- 未排除新的检查项；根 `.clang-tidy` 排除列表与上轮一致。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，5 个目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_expanded.txt`。

## [0.22.1] - 2026-09-23 -- 动画事件运行期可见：--demo-events 程序化 clip 注入 + 自动进 Play 态

> 上一轮（commit 316dfe5）已把 AnimationEventPlayer 接入生产路径，但默认道具
> `assets/models/model.gltf`（96 顶点）无动画，`UpdateEventPlayer` 在 `animations.empty()`
> 时复位清空，运行期事件不可见。本轮新增 `--demo-events` CLI 钩子：无外部动画资产时向
> 模型注入一条 2s 循环程序化 clip，并自动进入 Play 态，使事件播放器在正常仿真时间轴上
> 推进、经 `DrainFiredEvents()` 派发——事件来自动画时间轴，未绕过正常流程。
> 不引入新外部库，只改现有代码。

### 启动方式
```
BigHeroGameEngine.exe --scene default --demo-events --bench-frames 600
BigHeroGameEngine.exe --scene default --demo-events --screenshot <path>.png
```
- `--demo-events`：无动画资产时注入 `DemoLoop`（2.0s，根节点 x: 0→1→0 振荡），
  内置轨事件名设为 `click`，并在 InitScene 末尾 `EnterPlayMode()`（自动进 Play 态）。
- 配合 `--bench-frames N` 跑有限帧后自动退出；`--screenshot <p>` 取图。
- 单独 `--demo-events` 不开时行为与既往逐位一致（不注入、不进 Play、内置轨仍发 "tick"）。

### 事件触发机制
- 注入发生在 `LoadGltfAsset` 成功后、状态机绑定前：`animations.empty()` 时补一个根节点
  并 push 一条 `GltfAnimation{name="DemoLoop"}`，LINEAR 采样器 times={0,1,2}。
  随后既有的"动画绑定"块自然运行，把状态机状态绑到 animIndex 0。
- `AnimationHost::SetDemoEventName("click")` 非空时，`UpdateEventPlayer` 构建的内置轨
  在 clip 25%/75% 发 `click`（默认空串仍发 `tick`，单测 `AnimEvents.HostWiring` 不受影响）。
- 自动进 Play 态后，`playMode_.ShouldSimulate()==true`，仿真段内
  `animationHost_.Update(...)` → `eventPlayer_.Advance(dt)` → `DrainFiredEvents()` →
  逐条记日志；`click` 映射 `audioEngine_.PlaySfx(SfxId::Click)`（无音频设备时 `PlaySfx`
  返回 false 优雅降级，日志记录"无音频设备，优雅降级跳过"）。

### 运行期证据（build/bin/Release/out/）
- `demo_anim_events.log`：关键行
  - `--demo-events: 已注入程序化循环 clip 'DemoLoop'（2.0s，根节点 x 振荡）`
  - `进入 Play 模式`（自动进运行态）
  - `动画事件播放器绑定: clip=[DemoLoop] dur=2s 事件名=click（25%/75% 触发）`
  - `动画事件: [click] clipTime=0.5`（25%）与 `clipTime=1.5`（75%）跨循环重复触发
  - `动画事件 'click' -> PlaySfx(Click): 已播放音效`（本机有声卡，实际播放）
- `demo_anim_events.png`：Play 态（面板"运行中"）下场景渲染正常，gltf 模型加载（资源面板
  12/12、0 失败），退出码 0。
- 注：本机约 165fps，`--bench-frames 120` 仅走约 0.9s 只命中 25%；取 600 帧可同时看到
  25%/75% 两拍并跨循环重复。

### 改动文件（6 个）
- src/app/systems/AnimationHost.h：新增 `SetDemoEventName(std::string)` 与成员
  `demoEventName_`（空 = 生产约定 "tick"，非空 = 演示事件名）。
- src/app/systems/AnimationHost.cpp：`UpdateEventPlayer` 构建内置轨时按 `demoEventName_`
  取事件名，并新增一条绑定日志（clip 名/时长/事件名）。
- src/app/Application.h：AppConfig 新增 `bool demoEvents = false;`。
- src/main.cpp：解析 `--demo-events`，补 `--help` 文本。
- src/app/Application_Assets.cpp：`LoadGltfAsset` 成功后按 `config_.demoEvents` 注入
  程序化 `DemoLoop` clip（补根节点 + 一条平移通道）。
- src/app/Application.cpp：InitScene 末尾按 `config_.demoEvents` 调
  `SetDemoEventName("click") + EnterPlayMode()`；事件消费段 `click` 分支记录 PlaySfx 结果。

### 验证
- cmake --build build --config Release：0 error（仅既有 C4834/C4100 警告，无新增）。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。

### 设计取舍
- 选方案 A（程序化 clip 注入）而非方案 B（绕过 clip 手动触发）：事件仍来自
  `AnimationEventPlayer` 在真实时间轴上的 `Advance`，保留"事件绑定 clip 时间点"的语义。
- 注入只动 `gltfModel_.animations` 与必要的节点数组，不改网格/材质/渲染路径；默认道具
  无节点时补一个根节点使 clip 合法，`UpdateGltfOffset` 据此驱动根节点增量（道具可见微动）。
- 事件名开关默认关闭（空串 → "tick"），生产路径与既有单测逐位不变。

## [0.22.0] - 2026-09-23 -- P3 工程化：clang-tidy WarningsAsErrors 在 render/LodGroup 模块渐进开启

> 不改任何游戏逻辑。首个静态分析门禁落地：以 `tools/run-clang-tidy.ps1` 脚本对
> `src/render/LodGroup.h`（LOD 选档模块）运行 clang-tidy，通过 `--line-filter`
> 精确限定该头文件、`-warnings-as-errors=*` 将其告警视为失败（exit!=0）。
> 翻译单元、测试框架、第三方代码的告警不参与判定。不引入新外部库。

### 背景
- 根 `.clang-tidy` 此前 `WarningsAsErrors: ''`（空），全量开启会引爆存量误报。
  本轮选告警最干净的小模块 `LodGroup.h`（纯 CPU、仅依赖 glm 与标准库、有离线单测）作为首个门禁目标。
- 工具链：VS2022 自带 LLVM 19.1.5；因系统并存 VS18 STL（14.51，要求 Clang 20+），
  脚本内通过 `VCToolsInstallDir` 指向兼容的 VS2022 MSVC 14.44，并加
  `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` 绕过版本检查。

### 改动文件（2 个）
- `.clang-tidy`：Checks 排除列表新增 `-bugprone-macro-parentheses`（该检查对 MSVC STL
  系统头文件宏持续误报，无文件级位置可修，与既有排除项同类）。
- `tools/run-clang-tidy.ps1`（新增）：渐进门禁脚本。`$TargetModules` 表登记受闸门
  模块与驱动翻译单元；逐模块跑 clang-tidy，line-filter 限定目标头文件，
  收集诊断到 `build/bin/Release/out/clang_tidy_target.txt`，有诊断则 exit 1。
  后续新模块只需在表中追加一行。

### 修复的告警
- 目标模块 `LodGroup.h` 本身 clang-tidy 零告警，无需改代码。
- 仅排除了 `bugprone-macro-parentheses` 系统头误报 1 类。

### 验证
- cmake --build build --config Release：0 error。
- ctest --test-dir build -C Release --output-on-failure：全绿（313/313，无回归）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- clang-tidy 复查：`tools/run-clang-tidy.ps1` exit 0，目标头文件 0 诊断；
  证据存于 `build/bin/Release/out/clang_tidy_target.txt`（19 条经 line-filter 过滤的
  非目标文件告警，42612 条系统头文件告警）。

## [0.21.9] - 2026-09-23 -- A1 动画事件播放器生产接线：AnimationEventPlayer 由"只建不接"接入 AnimationHost

> 纯接线 + 薄派发层，不改任何既有渲染/姿态采样行为。把此前仅有头文件与单测、
> 生产路径零引用的 `AnimationEventPlayer`（scene/Animation.h）接入 `AnimationHost`
> 每帧 Update：主 clip 循环推进，触发事件经 `DrainFiredEvents()` 交生产层消费。
> 不引入新外部库，只改现有代码。

### 背景
- `AnimationEventPlayer`（A1，见 test_anim_events.cpp 7 个 TEST_CASE）此前已完整实现
  （正放/倒放/回绕/seek/变速/同刻多事件），但生产代码从未实例化——属 UPGRADE_PLAN §2
  明令禁止的"只建不接"模式。本轮按执行约定接线投产。

### 改动文件（5 个）
- src/app/systems/AnimationHost.h：新增事件播放器成员（unique_ptr）、内置事件轨、
  本帧事件缓冲；新增 `DrainFiredEvents()`；私有 `UpdateEventPlayer()`。
- src/app/systems/AnimationHost.cpp：Update() 末尾调 `UpdateEventPlayer()`；
  实现主 clip（animIndex 0）绑定——首次/clip 重载时重建播放器、置循环、挂内置轨，
  每帧 `Advance(dt)` 收集事件；无 gltf/无动画时复位并清空缓冲。
- src/app/Application.cpp：Animation 仿真段内 `animationHost_.Update(...)` 之后消费
  `DrainFiredEvents()`——逐条记日志，约定事件名 "click" 映射 `audioEngine_.PlaySfx(Click)`
  （顺带把此前只生成从不播放的 Click 音效接入派发）。
- src/tests/test_anim_events.cpp：新增 `AnimEvents.HostWiring`——用 2.0s 单节点动画驱动
  AnimationHost::Update 120 帧，断言内置轨 25%/75% 各发一个 "tick"（>=2）；
  hasGltf=false 时缓冲为空。
- CMakeLists.txt：BigHeroTests 源列表补 `src/app/systems/AnimationHost.cpp`。

### 设计要点（诚实口径）
- v1 事件派发绑定主 clip（animIndex 0）循环推进，与状态机 crossfade 解耦：
  事件流独立于视觉姿态时间。后续可按当前激活动画下标切换播放器，留作后续深化。
- 内置轨在 clip 25%/75% 发 "tick" 事件，使派发流在无内容管线时仍可被日志/单测观察。
- 模型重载（成功/失败）经 clip 名比对触发播放器重建；模型为 Application 侧常驻成员，
  地址与生命周期稳定。
- shipped 默认模型（assets/models/model.gltf，96 顶点道具）无动画，故默认场景运行时
  不产生运行期事件日志（`animations.empty()` 早退）；带动画的 gltf 模型加载后即按帧派发。

### 验证
- cmake --build build --config Release：0 error（仅既有 C4834/C4100 警告，无新增）。
- ctest --test-dir build -C Release --output-on-failure：全绿（含新增 AnimEvents.HostWiring）。
- cmake --build build --config Release --target BigHeroHeaderCheck：通过。
- 冒烟：--scene default --no-ui --screenshot 运行退出码 0，stderr 无错误，截图正常产出；
  gltf 模型与粒子系统初始化日志正常。

## [0.21.8] - 2026-09-23 -- SyncFromPacket indexOf 映射缓存（纯数据结构优化，零行为变化）

> 纯数据结构优化：EcsScene::SyncFromPacket 每帧重建 unordered_map（实体ID→稳定序下标），
> 稳态（无增删/读档）下 order_ 完全不变，映射结果恒定。缓存为成员变量，仅 order_ 结构变更时
> 重建，避免每帧 9516 实体的 hash insert 开销。语义与数值逐位一致，不改任何行为。

### 优化点
- EcsScene 新增 `indexOfCache_`（unordered_map）+ `indexOfDirty_`（bool）成员及 `RebuildIndexOfCache()`
  私有方法。SyncFromPacket 仅当缓存脏时重建，否则直接复用。
- 脏标记失效时机覆盖全部 order_ 修改路径：CreateObject（push_back）、DestroyAt（erase）、
  LoadPacket（clear + push_back）。SetParent 不改 order_，无需失效。
- EnsureWorld 内的 indexOf 重建不动（仅在 hierarchyDirty_ 时运行，不在热路径）。

### 改动文件（1 个）
- src/scene/EcsScene.h：新增 indexOfCache_/indexOfDirty_ 成员 + RebuildIndexOfCache() 方法；
  SyncFromPacket 替换局部 map 为缓存引用；CreateObject/DestroyAt/LoadPacket 后置 indexOfDirty_=true。

### 验证
- cmake --build build --config Release → 0 error
- ctest → 313/313 全绿（0 failure）
- BigHeroHeaderCheck → 通过

### 性能结论
- 未经复测/估算：移除每帧 9516 实体 hash insert 的固定开销，预期改善 Update 阶段
  SyncFromPacket 占比，但具体毫秒收益未测量。

## [0.21.7] - 2026-09-23 -- 编辑器 HUD 统一展示：CPU/GPU 帧剖析同轴对比（零渲染行为变化）

> 纯编辑器 UI 改动：Stats 面板性能段由"GPU 文本块 -> CPU 文本块 -> FPS 折线"的顺序展示
> 重设计为 CPU/GPU 并排同轴对比。仅触碰编辑器绘制与统计数据装配，渲染路径、FrameProfiler、
> GpuProfiler 录制逻辑零行为变化，不引入新外部库。

### 展示改进点
- CPU vs GPU 整帧同轴对比：两条 ProgressBar 共享 max 刻度（取 cpuTotalMs / gpuFrameMs 较大者，
  下限 1ms），分别标注 CPU/GPU 毫秒值，并在下方给出"差值 CPU-GPU"。GPU 时间戳不可用
  （gpuFrameMs==0）时优雅降级为仅 CPU bar + 黄色提示"GPU 时间戳不可用"，不崩溃。
- CPU 细分 Scope 与 GPU 阶段统一两列（ImGui::Columns）：左列 CPU 各作用域（名 + ms + 占比），
  右列 GPU 三阶段（阴影预通道/场景通道/UI 通道 + 占 GPU 整帧比例）；每行内嵌小 ProgressBar
  直观显示占比。
- 帧耗时历史升级为 CPU(蓝)/GPU(橙) 双曲线同轴叠加：新增 GPU 整帧耗时环形历史缓冲
  （与 CPU 历史同尺寸 180 帧），与 CPU 历史共用动态峰值刻度，经 ImDrawList 双线绘制；
  下方标注当前 CPU/GPU ms 与峰值刻度。
- 其余信息（FPS/帧耗时/GPU 名/分辨率/MSAA/三角形/物体数/批次）保留，位置随新布局微调。

### 改动文件（3 个）
- src/editor/EditorPanel.h：EditorStats 新增 gpuHistory/gpuHistoryCount 指针；
  DrawStatsWindow 性能段重写为同轴对比 + 两列细分 + 双线历史。
- src/app/Application.h：新增 gpuHistory_ 环形缓冲、gpuHistoryChrono_ 时序拷贝缓冲及
  RecordGpuFrameHistory()/GetGpuHistoryChronological() 内联辅助。
- src/app/Application_Record.cpp：每帧 RecordGpuFrameHistory(stats.gpuFrameMs) 推进环形缓冲，
  并将时序 GPU 历史指针填入 EditorStats。

### 验证
- cmake --build build --config Release：0 error（仅既有 C4834/C4100 警告）。
- ctest -C Release：单测全绿；BigHeroHeaderCheck 通过。
- --scene default --screenshot：运行 30 帧退出，无 stderr 错误，Stats 面板正常渲染、
  GPU 时间戳数据正确显示。

## [0.21.6] - 2026-09-23 -- 着色器 set/binding 常量化收尾：残留硬编码清零

> 纯重构零行为变化：将 UPGRADE_PLAN P2 阶段遗留的残留硬编码 set/binding 字面量
> 全部替换为中央常量（C++ ShaderBindings / GLSL BH_* 宏），数值逐位不变。
> 主渲染管线（set0/1/2/3）的 binding 现全部由 shader_bindings.h 单一来源驱动；
> GLSL 侧 ui.frag / equirect_to_cube.frag 补齐 include 并改用 BH_SET_POST/BH_PP_SLOT0；
> PostProcessor 6 槽位新增 C++ 侧 kPostSlot0~5 与 GLSL BH_PP_SLOT0~5 逐位对应。

### 改动文件（7 个）
- src/render/shader_bindings.h：新增 kPostSlot0~kPostSlot5（0~5）及 static_assert 交叉校验。
- src/render/descriptor_set.h：5 处字面量替换--camBinding->kCameraUBO、
  lightBindings[0]->kMaterialLightUBO、lightBindings[9]->kMaterialObjectTex、
  cubeShadowBinding->kPointShadowUBO、aoBinding->kAOTex；gbuffer 循环加常量引用注释。
- shaders/ui.frag.glsl：补 include，layout(set=0,binding=0)->BH_SET_POST/BH_PP_SLOT0。
- shaders/equirect_to_cube.frag.glsl：补 include，layout(binding=0)->BH_SET_POST/BH_PP_SLOT0。
- src/render/PostProcessor.cpp：6 处 binding 字段->kPostSlot0~5；加 shader_bindings.h include。
- src/render/Skinning.h：顶点输入绑定 0->kSkinningUBO；加 shader_bindings.h include。
- src/render/Renderer_PostFx.cpp：合成 Pass 2 绑定循环加 kPostSlot0/1 引用注释。

### 常量覆盖范围
- set 索引：kSetCamera/Post/Material/GBuffer/PointShadow/AO/Skinning（已有，本次未改）。
- set CAMERA：kCameraUBO；set MATERIAL：kMaterialLightUBO~kMaterialObjectTex + kMaterialProbeUBO。
- set GBUFFER：kGBufferAlbedo/Normal/Position；set AO：kAOTex；
  set SKINNING：kSkinningUBO；set POINT_SHADOW：kPointShadowUBO。
- set POST：kPostSlot0~kPostSlot5（本次新增，对应 GLSL BH_PP_SLOT0~5）。
- 子系统独立 set layout（UI/SSAO/SSR/EnvironmentLighting/Particle）的 binding=0
  为自包含本地布局，与全局 set 契约不冲突，按约定保留字面量并加注释说明。

### 验证
- cmake --build build --config Release -> 0 error（仅既有 C4834/C4100 提示，无新增警告）；
  glslc 全量重编译通过（ui.frag / equirect_to_cube.frag 改后编译无误）。
- ctest --test-dir build -C Release --output-on-failure -> 313/313 全绿，无回归。
- cmake --build build --config Release --target BigHeroHeaderCheck -> 通过。

## [0.21.5] - 2026-09-23 —— Update 阶段细分：FrameProfiler 子 Scope 落地 + openworld 实测构成

> 给 Update 主循环补 9 个细粒度 CPU Scope（纯计时、零行为变化），在 openworld（9,516 实体）
> --no-ui 120 帧基准上首次量化 Update 内部构成。诚实结论与原假设相反：基准实际跑在
> 编辑态（PlayMode=Editor，ShouldSimulate()=false），物理/重建包/动画/粒子/导航代理
> 五个仿真门控子系统本帧**未运行**（未出现在输出）；Update 的真实大头是
> `SyncSceneEdits`（包→ECS 写回）2.10ms（占 Update 56%），而非预期的 UpdateRenderables。
> 未为消除该写回开销引入跨编辑源的脏标记（风险高、收益不明），如实记录、不做无谓改动。

### 新增细粒度 Scope（src/app/Application.cpp）
- 在 Update Scope 内补 9 个 `Core::FrameProfiler::Scope`：UpdateCamera / SyncSceneEdits /
  Physics / RepackScene / Animation / UpdateRenderables / Particles / NavAgent / UpdateUniforms。
- 纯计时 RAII，不含任何行为改动，渲染输出逐位一致。
- 内层局部变量命名 `sc`（遮蔽外层 Update 的 `s`），消除新增 C4456 警告。
- 未加 Scope：UpdateTime / UpdateFpsTitle（极快）、UpdateShowcase / UpdateGizmo（--no-ui 空操作）。

### openworld Update 实测构成（1280×720 / --no-ui / --bench-frames 120 / Radeon 780M）
帧 avg=26.63ms（Render 22.83ms / Update 3.76ms / PollEvents 0.04ms / Picking 0.01ms）。

| 子系统 Scope | avg ms | 占 Update 比例 | 说明 |
|---|---|---|---|
| SyncSceneEdits | 2.100 | 55.9% | 包→ECS 写回（每帧无条件），编辑态无操作仍跑 9,516 实体 diff |
| UpdateRenderables | 0.887 | 23.6% | 渲染收敛：视锥剔除+批次化（每帧必调，合理热路径） |
| UpdateCamera | 0.742 | 19.7% | 相机输入/移动/抖动 |
| UpdateUniforms | 0.010 | 0.3% | Camera/Light UBO 更新（脏标记后近零） |
| 未覆盖间隙 | 0.018 | 0.5% | UpdateTime/Showcase/Gizmo/PlayModeRequests/FpsTitle |
| Physics / RepackScene / Animation / Particles / NavAgent | — | — | **未运行**（基准为编辑态，ShouldSimulate()=false，仿真门关闭） |

### 诚实分析与不修复决定
- **仿真门澄清**：原勘察假设 `--no-ui` 基准默认 `simulating=true`，实测为否——`PlayModeController`
  默认态为 Editor，`EnterPlayMode()` 仅由 Ctrl+P / 播放按钮触发，基准不自动进 Play。故 Physics /
  RepackScene / Animation / Particles / NavAgent 在本组基准中**根本未被调用**，非"空转"，
  无需修复；其耗时需在手动进 Play 后另测。
- **SyncSceneEdits 为何是大头**：编辑态 `scene_` 是编辑器数据模型，Gizmo/属性面板直接改它，
  每帧须经 `SyncFromPacket` 写回 ECS 供渲染读取。该函数已做逐实体 TRS diff（未改不写、不置脏），
  但仍每帧重建 9,516 项 `indexOf` 哈希表并无条件写 Renderable/Spin 字段——空闲无编辑时为冗余往返。
- **不修复理由**：跳过该写回需覆盖 Gizmo 拖拽 / ImGui 属性编辑 / 撤销重做 / 增删 / 改父 / 读档
  全部编辑源的脏标记，正是应避免的跨路径复杂缓存机制；漏任一编辑源即静默丢编辑、渲染不同步，
  风险高于 ~2ms 收益（占整帧仅 ~4%）。故如实记录，不做改动。
- UpdateRenderables / UpdateUniforms 为渲染必需热路径，不做无谓优化。
- 日志存档：`build/bin/Release/out/bench_matrix/bench_update_profile.log`。

### 验证
- `cmake --build build --config Release` → 0 error（仅既有 C4834 提示，无新增警告）。
- `ctest --test-dir build -C Release --output-on-failure` → 全绿（基线 313 断言无回归）。
- `cmake --build build --config Release --target BigHeroHeaderCheck` → 通过。
- 基准 exit=0，9 个新 Scope 中 4 个（UpdateCamera/SyncSceneEdits/UpdateRenderables/
  UpdateUniforms）出现在 [BENCH] 输出，5 个仿真门控 Scope 因编辑态未进入而不出现（符合预期）。

## [0.21.4] - 2026-09-23 —— LOD 收益量化：--lod-off CLI 开关 + openworld 对照组

> 首次将 LOD 选档从"已接线未量化"状态拉到有实测数据：新增 `--lod-off` 旁路开关，
> 在 openworld（9,516 实体）上跑 LOD 开/关两组对照。数据如实呈现：在 780M 核显上，
> LOD 开/关差异 ~1.8ms（在 iGPU 噪声级），LOD 收益不显著——球/胶囊实体占比极小
> （~260 球 + ~200-400 胶囊，占总实体 ~5-7%），且 LOD 路径本身的双批次管理开销
> 抵消了低模顶点节省。未强行解读为正收益。

### --lod-off CLI 开关（src/app/Application.h / src/main.cpp / src/app/Application.cpp）
- **新增 `AppConfig::lodOff`**（默认 false）：`--lod-off` 置 true 时，`UpdateRenderables`
  对球（meshId 3）/胶囊（meshId 4）**跳过 `LodGroup::SelectLevel` / `kCulled` 检查**，
  直接 push 高模桶（`sphereScratch_` / `capsuleScratch_`）。
- **不影响**：视锥剔除（`frustum.IntersectsSphere`）、PVS 遮挡剔除、其他 meshId 路径。
- **默认行为**：不传 `--lod-off` 时 `lodOff=false`，走原有 LOD 选档逻辑，逐位一致。
- **LodGroup 本体未动**（src/render/LodGroup.h 零改动）。

### openworld 球/胶囊实体勘察
- 场景总实体：**9,516**（静止 9,256 / 动态 260），meshId 0=立方体 / 3=球 / 4=胶囊。
- **球（meshId 3）**：即 260 个动态"全息投影球"（萤火虫漂移），全部为 LOD 管理对象。
- **胶囊（meshId 4）**：来自建筑"管道/空调外机"细节（每 chunk 1~4 个，50% 概率胶囊），
  按 chunk 密度估算约 **200~400** 个（Outer 层密度极低，多数 capsule 集中在 Near/Mid 层）。
- **球+胶囊合计 ~500~660 个**，占 9,516 实体的 ~5~7%——样本量偏小，LOD 收益参考性有限。

### openworld LOD 开/关基准对照（1280×720 / --no-ui / --bench-frames 120 / Radeon 780M）

| 指标 | LOD 生效（默认） | LOD 关闭（全高模） | 相对差异 |
|---|---|---|---|
| avg ms/帧 | 25.94 | 24.13 | -1.81ms（LOD off 略快） |
| min ms | 22.57 | 21.54 | -1.03ms |
| max ms | 36.46 | 32.84 | -3.62ms |
| avg FPS | 38.55 | 41.43 | +7.5%（LOD off 略高） |
| Render avg ms | 21.74 | 20.31 | -1.43ms |
| Update avg ms | 3.88 | 3.53 | -0.35ms |

- **诚实结论**：在 780M 核显上，LOD 关闭（全高模）反而**略快** ~1.8ms，方向与预期相反。
  原因分析：①球/胶囊仅占 ~5~7% 实体，顶点数差异对 GPU 光栅化影响极小；
  ②LOD 生效路径多维护 `sphereLodScratch_` / `capsuleLodScratch_` 两个低模批次，
  CPU 侧批次管理 + 每实体 `SelectLevel` 计算有额外开销；
  ③场景整体 CPU-bound（Render ~21ms 为主瓶颈），GPU 顶点数变化不是主导因素。
- **噪声判定**：~1.8ms 差异在 iGPU 运行间噪声级（±2~5ms 常见），**LOD 收益不显著**，
  不宜宣称正收益。建议在独显 GPU 上复测（顶点数差异在带宽充足的独显上可能体现为不同方向）。
- 日志存档：`build/bin/Release/out/bench_matrix/bench_lod_on.log` / `bench_lod_off.log`。

## [0.21.3] - 2026-09-23 —— AssetGuid 四情形处理收尾 + 探针跨设备复测包

> AssetGuid WIP 收尾：四情形 .meta 处理（含目录占用→空哨兵不覆写）+ 引用保序去重 + 裸指针生命周期契约，
> 修复测试夹具泄漏后 ctest 313/313 全绿。探针跨设备：本机仅 780M 核显无独显，产出可移植复测包
> （README + 一键脚本），待在独显机器上复测探针收益方向；未伪造更强设备数据，未宣称未验证收益。

### AssetGuidDatabase 四情形 .meta 处理 + 引用去重（src/core/AssetGuid.h / src/tests/test_asset_guid.cpp）
- **GuidForPath 四情形**：将 .meta 处理拆为四种情形——①不存在→生成新 GUID 写盘；②存在且合法→沿用持久化身份；
  ③存在但内容损坏（可读、无合法 guid）→生成新 GUID 覆写自愈；④存在却读不出内容（IO 异常 / 权限 / 路径被目录占用）
  →返回空哨兵 `Guid{}`，**绝不覆写、不登记、不写盘**，把异常显影给调用方。
- **目录占用检测**：`.meta` 路径被目录占用时直接返回空哨兵（目录绝不可能含合法 guid，且部分平台 ifstream 读目录会"成功返回空"，单靠 Read 成败无法区分）。
- **引用保序去重**：`SetReferences` 入库前 O(n²) 剔除重复 GUID 但保留首次出现次序，避免 FindBrokenReferences / DependentsOf 重复上报。
- **裸指针生命周期契约**：为 `Find` / `FindPath` / `ReferencesOf` 添加警告——返回 unordered_map 内部存储裸指针，任何修改操作可能触发 rehash 令指针悬空。
- **修复**：WIP 情形④测试块创建的 `ioblocked.png` 及其 `.meta` 未清理，污染后续 `ScanDirectory` 计数（预期 4 实际 5），
  补 2 行 `fs::remove` 清理后全绿。

### 探针跨设备复测包（build/bin/Release/out/bench_matrix/）
- `README_CROSS_DEVICE.md`：复测目的、前置条件、四组复测命令（参数数组）、判定标准、780M 对照表、注意事项与数据回填指引。
- `run_matrix_gpu.ps1`：openworld 四组一键脚本（基线 / PVS / 探针 / 全开），日志输出 `bench_*_gpu.log` 与核显数据区分；
  按脚本相对位置自动定位 exe，可移植到克隆仓库的任何独显机器。
- **780M 核显已有数据（对照基线）**：openworld 9,516 实体 / 1280×720 / MSAA 4x：

| 组 | avg ms/帧 | FPS | Render ms | Update ms | 相对基线 |
|---|---|---|---|---|---|
| 基线（无烘焙） | 23.06 | 43.37 | 19.88 | 3.13 | — |
| 探针（0.21.1，未优化） | 24.44 | 40.91 | 20.66 | 3.74 | -5.7% FPS |
| 探针（0.21.2，脏标记优化后） | 24.95 | 40.09 | 21.04 | 3.85 | 差异在 iGPU 噪声内 |

- **跨设备复测判定口径**：探针组 vs 基线，FPS 提升或持平→探针在该 GPU 上非负；仍下降→GPU 三线性插值开销在该设备仍占主导。
  建议每组 2-3 次取均值；复测数值（独显型号 / 驱动 / 四组数据）待回填。
- **边界声明**：本轮未修改探针源码，未在本机跑新基准；本机无独显，严禁用 780M 数据外推独显结论；
  探针 GPU 侧优化（插值简化、UBO 缩槽）因有画质 / 动态网格上限风险未实施，留待跨设备复测结果驱动。

## [0.21.2] - 2026-09-23 —— 探针 GPU 侧优化 + cybercity 小场景对照矩阵

> 针对 [0.21.1] 测出的探针净负（-5.7% FPS）落地 CPU 侧优化：脏标记延迟上传消除每帧 384 探针 SH 打包。
> 同时以同口径跑 cybercity（282 实体）四组对照，验证小场景下两模块的收益边界。
> 数据如实呈现：探针优化的 ~0.6ms CPU 节省被 iGPU 运行间噪声掩盖（代码审查可证消除）；cybercity 四组差异全在 ±0.05% 噪声级。
> 未宣称未经验证的性能收益。

### 探针 GPU 侧优化：脏标记延迟上传（src/app/Application.h / Application.cpp / Application_Record.cpp）
- **问题**：探针烘焙后静态不变，但 `UpdateUniforms` 每帧对每个 frame-in-flight 槽位调用 `PackProbeIrradianceUp`（遍历 384 探针逐点 SH 求值）+ UBO 上传，纯浪费。
- **方案**：新增 `probeDirty_` 标志，`RunPendingBakes()` 烘焙探针后置脏；`UpdateUniforms` 仅脏时打包并上传全部槽位，随后清脏。稳态帧零开销。
- **零变化语义**：脏标记只改变上传时机，不改变上传内容；未烘焙时 `probeDirty_` 恒 false，UBO 保持零初始化，渲染输出逐位一致。
- **基准复测**（openworld，`--bake-probes`，120 帧，Radeon 780M）：

| 指标 | 基线（0.21.1） | 优化后 |
|---|---|---|
| avg ms/帧 | 24.44 | 24.95 |
| FPS | 40.91 | 40.09 |
| Render ms | 20.66 | 21.04 |
| Update ms | 3.74 | 3.85 |

> iGPU 运行间噪声 ~2-5ms，掩盖了 ~0.6ms CPU 节省；但 `PackProbeIrradianceUp` 循环经代码审查确认从稳态路径消除。未做 UBO 缩槽（需处理编辑器动态网格上限风险）与插值简化（有画质损失风险）。

### cybercity 四组对比矩阵（282 实体，1280×720，MSAA 4x，Radeon 780M）
日志 `out/bench_matrix/bench_cybercity_*.log`，脚本 `run_matrix_cybercity.ps1` 可复跑。

| 组 | avg ms/帧 | min | max | FPS | Render ms | Update ms | 相对基线 |
|---|---|---|---|---|---|---|---|
| 基线（无烘焙） | 6.06 | 4.65 | 7.20 | 165.01 | 5.91 | 0.13 | — |
| PVS 烘焙（3×3 采样） | 6.06 | 5.27 | 7.54 | 164.94 | 5.90 | 0.12 | -0.0% FPS |
| 探针烘焙 | 6.06 | 1.43 | 14.25 | 164.99 | 5.77 | 0.17 | 0.0% FPS |
| 两者全开 | 6.06 | 5.22 | 6.94 | 164.92 | 5.87 | 0.16 | -0.1% FPS |

### 结论（诚实口径，与 openworld 对照）
- **小场景 GPU 已无余量压力**：cybercity 帧均 6.06ms / 165 FPS，Render 占 5.91ms（97%），Update 仅 0.13ms。瓶颈完全在核显光栅化，CPU 侧剔除 / 探针打包无可移动空间——与 openworld 23ms / 43 FPS 同属 GPU 光栅化瓶颈，但小场景帧预算太紧，±0.05% 差异均为噪声。
- **PVS 在开放展示厅无对象可剔**：cybercity 平均可见比例 83.2%（openworld 30.05%），每帧仅 PVS 剔除 20/282（7.1%），视锥剔除已先剔 138/282（48.9%）。PVS 价值随场景遮挡密度上升而上升。
- **探针腿在 cybercity 未实际测到**：cybercity 场景无探针体（probes=0），`--bake-probes` 空转完成，探针打包成本未被触发。openworld 测出的探针净负是大场景数据，不能用 cybercity 的 0% 外推为"探针无成本"。
- **总体**：cybercity 四组 FPS 差异 ±0.05%，方向性结论是"小而开阔的场景不构成剔除 / 探针优化的测试床"；两模块的收益 / 代价仍以 openworld（大而密）矩阵为准。

## [0.21.1] - 2026-09-22 —— 性能实测深化：CLI 烘焙/基准开关 + openworld 四组对比矩阵

> 为四模块收益提供**脚本化可复跑证据**：新增三个命令行参数（`--bake-probes` / `--bake-occlusion` /
> `--bench-frames N`），跑 openworld（9,516 实体，1280×720，MSAA 4x，Radeon 780M 核显）四组对比。
> 数据如实呈现：PVS 有效剔除但收益在噪声级；探针在当前实现与场景下为净负；全开 = 微正。
> 未宣称未经验证的性能收益。

### 新增 CLI（src/main.cpp / Application.h AppConfig / Application.cpp）
- `--bake-probes` / `--bake-occlusion`：启动即触发烘焙（等价编辑器面板按钮），供自动化对比。
- `--bench-frames <N>`：基准模式——跳过 warmup 30 帧后统计 N 帧，打印平均/最差帧耗时 +
  各阶段 CPU 平均耗时（FrameProfiler 跨帧聚合）到 stdout 后退出；默认 0 = 禁用，零行为变化。
- 重构：烘焙消费抽为 `Application::RunPendingBakes()`（面板按钮与 CLI 共用，Bake* 自清标志，
  `--no-ui` 下 CLI 路径也在主循环前同步兑现）；`ProjectPanel::SetCliOcclusionSampling` 供
  CLI 低采样烘焙（编辑器面板仍 9×9 全精度）。

### openworld 四组对比矩阵（120 帧统计 / 组，日志 `out/bench_matrix/bench_*.log`，脚本 `run_matrix.ps1` 可复跑）

| 组 | avg ms/帧 | min | max | FPS | Render ms | Update ms | 相对基线 |
|---|---|---|---|---|---|---|---|
| 基线（无烘焙） | 23.06 | 21.24 | 28.66 | 43.37 | 19.88 | 3.13 | — |
| PVS 烘焙（3×3 采样） | 22.85 | 21.35 | 25.88 | 43.77 | 19.18 | 3.63 | +0.9% FPS |
| 探针烘焙 | 24.44 | 22.92 | 29.16 | 40.91 | 20.66 | 3.74 | -5.7% FPS |
| 两者全开 | 22.37 | 20.48 | 25.86 | 44.70 | 19.00 | 3.33 | +3.1% FPS |

### 结论（诚实口径）
- **PVS 有效但收益噪声级**：每帧剔除 2,636/9,516（27.7%）实体，Render -0.7ms，FPS +0.9%。
  瓶颈是核显 GPU 光栅化而非 draw call 数（实例化绘制 CPU 开销小），剔除收益被 PVS 查询开销部分抵消。
- **探针为净负**：每帧 CPU 打包 2,048 探针 UBO（Update +0.6ms）+ 片元三线性插值（Render +0.8ms），
  在 1280×720 核显上 FPS -5.7%。当前实现是纯负担，需 GPU 侧优化或仅高配设备启用。
- **全开 = PVS 正 + 探针负 ≈ 微正**（+3.1%），与"两者独立符号"自洽；四组差异均在 ±1ms 级，
  受后台负载波动影响，方向性结论 > 数值精度。
- PVS 烘焙 CLI 低采样 3×3：86s（9×9 全精度在 9k 实体场景为分钟级，编辑器路径保留）。

## [0.21.0] - 2026-09-22 —— 「只建不接」清零：四模块接线投产 + 架构收敛 + 探针/NavMesh 深化

> 本轮执行 UPGRADE_PLAN §3.3 方案 A：把此前仅有头与单测的 4 个实验模块（LOD / 遮挡剔除 /
> 光照探针 / 网格导航）全部接线投产，并同步完成 ECS 渲染端收敛、RenderGraph 跨队列屏障
> 与两处深化。引擎 MSVC Release 全量构建 0 error、**ctest 304 用例 100% 通过**、
> BigHeroHeaderCheck 通过、工作区干净。

### LOD 分级接线（render/LodGroup.h → 生产）
- 新增低模网格：球 `BuildSphereVertices(8×4)`（原 20×10）、胶囊 `BuildCapsuleVertices(8×3)`（原 16×5），
  均为程序化生成、零外部资源。
- `Application::UpdateRenderables` 对 sphere/capsule（meshId 3/4）追加选档：
  `LodGroup::ScreenRelativeHeight(包围球半径, 相机距离, tan(fov/2)) → Evaluate()` 选档，
  level 0 进高模桶、level≥1 进低模桶、kCulled 跳过不绘制。
- 录制端四处（前向 / 延迟 / CSM / 立方体阴影）改为高模 + 低模两次实例化绘制；
  三角统计按实际绘制档位计。编辑器 7 个 LOD 参数暴露为 public 供逐帧同步。
- 单测 `test_lod_wiring.cpp`：近→高模、中→低模、远→剔除、ECS 分桶、Bias 影响、开关语义。

### 遮挡剔除接线（render/OcclusionCulling.h → 生产）
- 烘焙式 PVS 接入 `UpdateRenderables`：视锥剔除之后、分桶之前查 `IsVisibleAt(camPos, cullableIndex)`，
  索引与 ECS `BuildPacket` 同序同集（回调内计数器一一对应）。
- `pvsCulledCount_` 逐帧统计 + 每 120 帧日志输出剔除数；未烘焙时恒返回 true，渲染零变化。

### 光照探针接线（render/LightProbe.h → 生产）
- SH9 `LightProbeVolume` 接入光照管线：LightUBO 末尾扩展 `probeAmbient`（std140 768B static_assert），
  每帧以相机位置 + 世界 up 采样注入。
- **深化一（逐对象）**：`InstanceData` 扩展 `probeIrradiance`（112→128B），顶点着色器 location 14
  逐实例输入 → 片元 `max(逐实例, UBO)`，CPU 按实体世界矩阵平移列采样；未烘焙零变化。
- **深化二（延迟逐片元）**：新增独立 `ProbeUBO`（set1 binding10，3 个 vec4 头 + `probes[2048]`），
  CPU 端 `PackProbeIrradianceUp()` 逐探针以世界 up 预求值（与前向同口径，数学等价），
  延迟片元 8 邻域三线性插值 + 有效权重归一化，全无效回退 UBO 探针。

### 网格导航修复 + 接线（navigation/NavMesh.h → NavHost 可选后端）
- **修复**：耳切 containment 改严格内部判定（搭桥重合顶点不再挡掉合法耳）+ 接受未完全收尾的
  三角扇；StringPull 漏斗右边缘收紧符号修正。此前 2 个失败用例根因是算法缺陷，现已全绿。
- **接线**：`useNavMesh` 开关（默认 false = NavGrid，双轨并存）；`BuildNavMeshDemo()` 程序化
  演示场景；**深化** `BuildFromEcsScene` 遍历 ECS 共享立方体（meshId 0）生成 12 三角形喂给
  `NavMesh.Build()`，坡度过滤由 NavMesh 内部完成，空场景返回 true 且 PolyCount==0。

### 架构收敛（ECS 渲染端）
- `RepackScene`（每帧无条件 BuildPacket，含 vector/unordered_map 重建）改为仅运行态执行；
  编辑态 ECS 不独立漂移，结构性变更显式 Repack。
- 三角统计 / demo stats / 清空判空从 `scene_` 改直读 `EcsScene`，数值口径逐位保留。

### RenderGraph 跨队列屏障 + 队列族探测
- `RGBarrierInfo` 新增 src/dst 队列族字段（默认 IGNORED 零变化），写/读队列族显式不同时
  自动生成所有权转移 barrier；外部图像（交换链）首用/末用屏障链补单测。
- 新增 `render/QueueFamily.h/.cpp`：`SelectDedicatedTransferFamily` 纯逻辑选型
  （纯 DMA 族优先 → compute+transfer → UINT32_MAX 回退）；Context 永久探测并打印。
  **实测证据**（Radeon 780M）：暴露纯传输族 family[2]，检测已启用；逐帧跨队列上传因
  与 inFlightFence 门控冲突、收益待量化，暂不落地（已在 Context.h 标注决策）。

### 性能实测基线（1280×720，MSAA 4x，780M 核显，后台负载 ~66%）
- default（7 实体）≈ 174 FPS（5.7 ms/帧）；cybercity（282 实体）≈ 176 FPS（5.7 ms/帧）；
  openworld（9,516 实体）≈ 40 FPS（24.7 ms/帧）。日志与截图存于 `build/bin/Release/out/bench_*.log|png`。

## [0.20.1] - 2026-09-22 —— 方块世界迭代：贪心网格化 / 瞄准高亮 / 水体与游泳

> 上一轮交付可玩方块世界后，本轮按「轻量化 + 画质 + 手感」三条线继续打磨。
> 全部改动经 MSVC 真实编译、**233 用例 / 140141 断言全绿**、clang-format-19 干净、已部署 `build/bin/Debug`。

### 轻量化：贪心网格化（Greedy Meshing）
- `VoxelWorld::BuildChunkMeshWithStats` 重写为**切片掩码 + 矩形合并**：按 6 个方向切平面，
  先沿 u 扩宽、再沿 v 扩高，仅当「方块类型 + 四角 AO」完全相同时才合并成大四边形
  （AO 差异与异类型方块自动阻止合并，保证视觉与逐面实现一致）。
- **实测收益**：默认地形单区块 **466 面 vs 朴素 2408 面（削减 80.6%）**；整块实心区块
  2496 面 → 6 面。着色器侧零改动，三角形数与顶点内存同步下降。
- 顶点坐标严格保持「方块 (x,y,z) 占据 [x,x+1]³」约定，与 `CollectColliders` 完全一致
  （新增 AABB 断言锁死该约定，防止后续优化再引入半格偏移）。

### 交互
- **瞄准高亮描边**：每帧一次 DDA 射线，命中方块的 12 条棱投影到 ImGui 前景层
  （黑描边 3.5px + 白线 1.4px），Minecraft 式选中反馈；挖掘 / 放置复用同一次求交，不再重复射线。
- **放置防护**：目标格必须为空气，且不得与玩家自身 AABB 重叠（贴边 0.02m 容差），
  否则拒绝放置并 HUD 提示「放不下」—— 避免把自己封死在方块里或被挤穿地形。

### 水体与游泳
- **水下雾 + 游泳手感**：头部所在格为水时自动切换冷蓝雾（密度 0.028 → 0.34，雾色同步切换）；
  新增 `FpController::SetInWater` —— 重力降至 28%、终端下沉速度降至 3.2 m/s、移速降至 62%，
  空格由「起跳」改为「上浮推进」；出水按基准参数**精确还原**（幂等，重复设置不叠加缩放）。
- **水面下压**：水体顶面降到 0.875 格高，岸边形成自然落差，水体不再是实心蓝砖。

### 视距可调
- `[` / `]` 边沿调整流式视距（1~12 区块），立即重新流式加载（网格仍走每帧预算，不卡帧），
  HUD 实时显示当前视距；新增 `VoxelWorld::SetViewRadius` 与 `Window::kKeyLeftBracket/kKeyRightBracket`。

### 手感与性能（第二批）
- **按住连挖**：左键按住后按 0.22s 间隔连续挖掘（放置仍保持边沿，避免拖出一串方块）；
  挖掉后立即重算准星，否则连挖会一直指向已经消失的那一格。
- **近处优先重建**：待重建区块按到玩家的水平距离排序后再取每帧预算 ——
  跑动或大视距时先建脚下的区块，不再出现「眼前空、远处先冒出来」。
- **阴影投射范围收敛**：CSM 只投影玩家 2.5 区块内的方块网格（远处阴影在屏幕上贡献极小，
  却要被 4 个级联各画一遍），顶点量随视距平方下降。
- **默认视距 3 → 4 区块**（约 64m），配合上面两项优化，视野更开阔而不掉帧。

### 时段光照（[T] 循环）
- 四档时段预设（正午 / 黄昏 / 夜晚 / 清晨）：太阳方向与光色、强度、环境光、曝光、
  天空染色、雾色同步切换；**每帧向目标值平滑逼近**（约 0.4s 收敛），不做硬跳变。
- HUD 显示当前时段；夜晚档配合 IBL 与雾色形成明显不同的氛围。

### 测试新增
- `Voxel.GreedyMeshing`（坐标约定 + 整块实心合并率 > 99%）、
  `Voxel.GreedyMergeBoundaries`（异类型 / AO 差异必须切开）、
  `Voxel.GreedyTerrainGain`（真实地形收益实测并打印比例）、
  `FpController.SwimState`（入水降参 / 出水还原 / 上浮推进 / 幂等）。

## [0.20.0] - 2026-09-22 —— 方块世界（Voxel）可玩 + 渲染/物理三处 P0 修复

> 本轮交付**可实际游玩的方块世界**（`--scene voxel`）：区块化无限体素地形、噪声地貌
> （群系 / 水域 / 洞穴 / 植被）、面剔除 + 顶点 AO 网格合并、DDA 射线挖掘与放置、流式加载与
> 视锥剔除。同时修复调研中发现的三处 P0：CSM 阴影级联瓦片尺寸算错（阴影分辨率仅 1/4）、
> 物理步进补变长尾步破坏确定性、物理缺少渲染插值导致高刷抖动。
> 引擎 MSVC 真实编译通过、**229 用例 / 140104 断言全绿**、clang-format-19 干净、已部署 `build/bin/Debug`。

### 方块世界（samples/voxel/VoxelWorld）
- **世界与地形**：16×64×16 区块；确定性整数 hash + value noise（4 八度 fBm + 山脉项）生成
  高度场；温度噪声划分**草地 / 沙漠 / 雪原**群系；海平面以下填水、岸边沙地；3D 噪声挖空
  **洞穴**；草地群系按密度生成**树木**（树干 + 球形树冠，树冠内缩保证不跨区块）。
- **网格生成**：仅输出朝向空气 / 透明体的**暴露面**；**顶点级 AO**（side1/side2/corner 三分量
  推导，亮度下限 0.55），并按对角和翻转三角划分消除插值瑕疵；顶点色 = 方块基色 × AO
  （着色器 `outVertColor = inColor * inTint` 直接乘进反照率，无需贴图即有立体阴影感）。
- **交互**：Amanatides & Woo **DDA 体素射线**（命中方块 + 入射面法线 + 距离）驱动
  **左键挖掘 / 右键放置**（基岩不可破坏防挖穿）；数字键 1~6 切换手持方块；与既有
  `Game::FpController` 对接，周边实心方块实时投影为 AABB 碰撞体（走 / 跑 / 跳 / 蹲 / 台阶均可用）。
- **流式与性能**：按玩家位置圆形视距加载 / 卸载区块；**分帧网格重建**（每帧预算 2 个区块，
  首屏 64 个一次性建完）消除跨区块卡顿尖峰；渲染按区块包围球做**视锥剔除**；区块网格同时
  进入 CSM 阴影投射。
- **新增 HUD**：准星 + 操作说明 + 手持方块 + 坐标 / 区块 / 网格统计；默认开启体积雾
  （远处区块边界由雾淡出，避免看到世界被「切开」的硬边）。

### P0 修复
- **CSM 级联阴影瓦片尺寸**（`src/render/ShadowMap.h/.cpp`）：原用 `size_ / kCascadeCount`
  （2048/4 = 512），但 2×2 图集瓦片边长应为 `size_ / 2 = 1024`；着色器 `deferred_light.frag.glsl`
  按 `* 0.5` 采样，C++ 与 GLSL 约定不一致 → 75% 深度图集浪费、级联 1/2/3 采到未渲染区域、
  有效阴影分辨率仅 512²。现引入 `kAtlasDim = 2` 统一修正 —— **阴影分辨率 ×4**，
  `Application` 中 texel-snap 的 `tileTexels` 同步变正确。
- **物理固定步长累加器**（`src/physics/PhysicsTypes.h`、`PhysicsEngine.cpp`）：原实现在整数个
  固定步之后补一个 `update(remaining)` 的**变长尾步**，步长随帧率漂移 —— 既破坏确定性
  （录制回放不可复现），也让 rp3d 求解器不稳。改为纯函数 `PlanFixedSteps`（累加器 + 上限保护），
  只跑整数固定步，余量跨帧保留。
- **物理渲染插值**：新增 `SavePrevTransforms` 快照 + `GetBodyTransformInterpolated`
  （位置 lerp + 旋转 slerp）与 `InterpolationAlpha()`，消除 60 Hz 物理 + 高刷 / 波动帧率的
  阶跃抖动；静态 / 运动学体自动跳过插值。

### 测试新增
- `src/tests/test_voxel_world.cpp`（9 例）：地形确定性、方块属性、**面剔除**（孤立方块 6 面 /
  六面包围 0 面 / 十字 30 面）、**顶点 AO**（开阔 1.0、内凹 <1 且有下限）、DDA 射线
  （命中 / 法线 / 射程）、碰撞体投影、编辑（破坏 / 放置 / 越界安全）、流式加载卸载、出生点。
- 全量 `BigHeroTests`：**229 注册 / 229 运行 / 140104 断言 / 0 失败**（上一轮 220）。

## [0.19.5] - 2026-09-21 —— 第一人称沉浸式展示厅（CyberCity）· 第二轮美学

> 在 0.19.4（首轮：第一人称控制器 + 自发光材质 + 赛博城市展示厅 + 展台交互/HUD + 光标锁定 +
> 4 套离散昼夜预设）基础上，补齐「连续昼夜 + 自动昼夜循环 + 霓虹呼吸 + 画面风格画廊」，
> 让城市"活"起来且可一键换装。第二波：引擎真实 MSVC 编译通过、220 用例 / 140049 断言全绿、
> clang-format 干净、已部署到 `build/bin/Debug`。

### 功能扩展（第二轮）
- **连续昼夜过渡**：`ShowcaseHost` 改为环上连续量 `dayTime_`，`BlendTimePresets` 做 smoothstep
  插值（方向光重新归一化、数值字段逐字段混合），切 T 不再硬跳变而是 1.6 s 平滑收敛；
  按 **O** 开/关自动昼夜循环（18 s 推进一预设，整圈约 72 s）。
- **霓虹呼吸**：新增 `NeonPulse(index, time)`（确定性 hash 相位 + 慢呼吸 + 老化灯管偶发瞬暗），
  每帧调制 8 盏点光源强度；`UpdateNeonPulse` 在 `UpdateShowcase` 内调用。
- **画面风格画廊**：新增 `CyberLookPreset`（4 套：赛博霓虹 / 电影感 / 冷冽清晨 / 黑白侦探），
  复用引擎既有色调分级 + 泛光 + 暗角 + 颗粒 + 雾密度旋钮（新增 `PostProcessSync::bloomStrength/
  bloomThreshold` 两字段并接入 `SyncToPostProcessor`）；按 **G** 循环切换。
- **HUD 升级**：特性清单增 `[G]` 风格 / `[O]` 自动昼夜两行；右上角加"风格"行与自动标记；
  底部加"已体验进度条"（点亮 8 座展台）；帮助与欢迎卡补 G/O 说明。
- **新增按键常量** `Window::kKeyO=79`；`Application::ApplyTimePreset` 升级为 `ApplyAtmosphere`
  （昼夜插值 × 风格落地的统一入口，每帧调用）。

### 测试新增
- `src/tests/test_cyber_city.cpp`：`CyberCity.LookPresets` / `CyberCity.BlendTimePresets` /
  `CyberCity.NeonPulse`（确定性、区间、中点单调、方向光单位化、非同步相位）。
- `src/tests/test_showcase_host.cpp`（新增 4 例）：`Focus`（注视解算/远距失效/直达查找）、
  `DayCycle`（静止不漂移、平滑收敛、环上最短路径、自动循环整圈）、
  `LookGallery`（循环回绕/负索引）、`VisitedTracking`（位图打点/越界安全）。
- 全量 `BigHeroTests`：**220 注册 / 220 运行 / 140049 断言 / 0 失败**。

## [0.19.4] - 2026-09-21 —— 第一人称沉浸式展示厅（CyberCity）· 首轮

> 用户要求「评估后全面升级，做到美观、可第一人称体验引擎全部特性」。首轮落地：第一人称陆行
> 控制器（重力/跳跃/蹲伏/冲刺/碰撞滑动/台阶/头部摇晃）、逐实例自发光材质（location 13）、
> 程序化赛博城市展示厅（`--scene cybercity`，八座特性展台 + 八霓虹灯柱 + 环形楼群 + 天际线）、
> 展台交互/HUD、光标锁定、4 套离散昼夜预设。引擎 MSVC 真实编译通过、213 用例全绿、clang-format 干净。
> 详细改动见 git 工作区（23 文件，含 `samples/showcase/CyberCity.*`、`src/app/systems/ShowcaseHost.h`、
> `src/game/FpController.h` 等）。

## [0.19.3] - 2026-09-20 —— 开放世界场景模板（OpenWorld）

> 以「320×320 m 世界 + 10×10 m 空间分块 + 距离分层密度」的开放世界模板压测引擎规模能力：
> 约 2.5 万实体（近中心密集、远外围稀疏），98% 静止 + 2% 动态萤火虫，
> 验证 ECS 层级缓存的增量重算收益（每帧仅重算动态子树，非全量 2.5 万节点）。

### 功能扩展

- **新增 `samples/open_world/OpenWorldScene.h/.cpp`**（`BigHero::Sample::OpenWorld`，纯 CPU、
  确定性 RNG、零 Vulkan/GPU 依赖，可离线单测）：
  - **世界规格**：3220×320 m，10×10 m 分块（32×32 = 1024 块）；按 Chunk 中心距原点
    分 4 个密度层——Near ≤40 m（52 块 ×80 实体）、Mid ≤80 m（156 块 ×40）、
    Far ≤160 m（604 块 ×15）、Outer（212 块 ×3），构建后可经 `ComputeOpenWorldStats` 复核。
  - **实体构成**：岩石/矮树（2 层父子链：躯干立方体→球树冠）/草丛/灌木 4 类静态 +
    萤火虫动态小球（自转速度 30~90 deg/s，根节点）。动态配额 = 块密度 × 5% 截断取整，
    全局动态占比严格 ≤5%（低密度块预算不足 1 只时保持全静态，外层更荒凉符合叙事）。
  - **确定性**：每 Chunk 独立 `std::mt19937`（全局种子 42 + 块坐标混编），同输入同输出；
    `HashFloat` LCG 提供块内细节参数。meshId 仅用 0/3/4（立方体/球/胶囊），无外部资产依赖。
  - **统计出口** `OpenWorldStats`：totalEntities / staticCount / dynamicCount / staticRatio /
    chainCount / min·maxChainDepth / chunkCount / near·mid·far·outerChunks /
    dynamicSubtreeNodeSum（与 `EcsScene::RecomputeWorld` 增量语义逐字节对齐）。
- **接入运行**：`--scene openworld` 新场景分支（`Application::InitScene`），
  加载后自动统计日志 + 相机取景 `SetTarget(0,2,0)/SetDistance(60)`（视野覆盖近中心密度环）；
  glTF 演示物体在该场景下禁用（规模断言净化）；`main.cpp` help 与
  `BuildSettingsModel::builtinScenes` 场景清单同步为 `{"default","slice","openworld"}`。
- **新增用例 4 个**（`src/tests/test_openworld.cpp`，已登记进 `BigHeroTests`）：
  - `OpenWorld.SpecNumbers`：总规模 ≥4000 且 ≤30000 / 动静分离 ≥90% 静止 / 动态 ≤5% /
    父子链 ≥100 条且深度 2~3 / 拓扑健康（父下标 < 自身）/ meshId 白名单 / 构建确定性逐字段一致。
  - `OpenWorld.DensityLayers`：四层 Chunk 计数齐备且合计 = 总块数；各层实体估算与总计 ±30% 吻合。
  - `OpenWorld.HierarchyIncremental`：首次全量重建 = 实体数；等值 round-trip 后 0 重建；
    改链根只重算该链子树，叶节点世界平移正确传播。
  - `OpenWorld.Benchmark200Frames`：200 帧每帧 `UpdateSpins` 后仅重算动态子树
    （`nodes == dynamicSubtreeNodeSum` 逐帧成立），并与全量重建对照——增量节点数 ≤10%、
    耗时显著更短；打印 `avgUs/maxUs` 基准。

### 验证

- `BigHeroTests` 全量回归：**201 registered / 201 ran / 318785 check(s) / 0 failure(s)**。
- 构建：Debug 与 Release 双配置 `BigHeroGameEngine` 编译通过（无新增警告）。
- 冒烟：`--scene openworld --screenshot` 截图 25004 实体铺满世界、四层密度梯度视觉可辨、
  EXIT=0；实测日志 `静态 24484 / 动态 520（静止占比 0.979）、父子链 4908 条（2~2 层）、
  区块 1024（Near 52 / Mid 156 / Far 604 / Outer 212）`。
- 基准：per-frame 增量重算 **avg 520 节点 / 134.9 us（max 799.9 us）**，
  全量重建 25004 节点 / 427.6 ms —— 增量比全量少 48 倍节点、快 3171 倍；
  渲染帧率受引擎固定后处理开销主导（default 场景 ~13 fps 基线），25K 实体相对 7 实体
  每帧仅多 ~67 ms，规模增长对帧预算影响可控。

## [0.19.2] - 2026-09-20 —— 资产 GUID 数据库（U2-A2 前半：GUID + 引用追踪）

> 把「资产身份」与「文件路径」解耦的持久化标识层。补齐方案 §15.2 第二波 0.20 的
> U2-A2（资产 GUID 数据库）前半——GUID 生成/持久化 + 引用追踪 + 断链检测。
> 这是「引擎 vs 技术演示」的分界设施：资产重命名/移动后引用不再因路径变化而断。

### 功能扩展

- **新增 `src/core/AssetGuid.h`**（header-only，纯 CPU、零 GPU/窗口依赖，`BigHero::Core`）：
  - **`Core::Guid`**：128 位值类型。`Generate`（random_device 播种 mt19937_64）/
    `ToString`（32 小写 hex）/ `TryParse`（严格 32 字符、大小写均可、非法拒写）/
    `IsValid`（全零为空引用哨兵）/ `GuidHash`（unordered 容器键）。
    构造与比较均 `constexpr noexcept`（值类型契约）。
  - **`Core::AssetGuidMeta`**：`.meta` 旁车文件读写（Unity 惯例 `asset.png → asset.png.meta`，
    内容一行 `guid: <32hex>`）。
  - **`Core::AssetGuidDatabase`**：路径 ↔ GUID 双向映射（路径经 `NormalizePath` 归一化）。
    `GuidForPath` 幂等登记（已有 `.meta` 读旧值；无/损坏/撞车则生成并覆写自愈）；
    `MoveAsset` 保证 GUID 稳定且引用记录随路径换键；`RemoveAsset` 清映射与 `.meta`；
    引用追踪 `SetReferences` / `ReferencesOf` / `DependentsOf`（反向「谁在用我」）；
    `FindBrokenReferences` 断链显影（引用未登记 GUID，成对返回引用者与缺失 GUID）；
    `ScanDirectory` 递归批量导入（跳过 `.meta` 套娃）。
  - 与现有 `AssetCache` / `AssetRegistry` **正交**：不改其接口，路径键语义保持不变。
- **新增用例 `Assets.Guid` / `Assets.GuidDatabase`**（`src/tests/test_asset_guid.cpp`，
  已登记进 `CMakeLists.txt` 的 `BigHeroTests` 源列表）：覆盖 GUID 值语义 / 解析往返 /
  非法拒绝 / `.meta` 持久化复用 / 受损自愈 / 撞车防护 / GUID 稳定移动 / 引用增删改查 /
  反向依赖 / 断链检测（空引用哨兵豁免、未登记 GUID 显影、删资产显影）/ 目录扫描幂等。

### 验证

- 高保真验证：直接以 `g++ -std=c++20 -Wall -Wextra -O0` 编译**真实测试文件**
  `test_asset_guid.cpp` + 镜像 `test_main.cpp` 的入口，链接引擎真实测试注册表
  `RunAllTests`——**零警告编译通过，`2 registered, 2 ran, 88 check(s), 0 failure(s)`**。
  与 `BigHeroTests` 构建共用同一批源文件（同一编译器/标准/宏）。
- 测试规模 180 → **182 用例**。

## [0.19.1] - 2026-09-20 —— 视锥剔除 AABB 支持 + 死代码清理

### 结构清理

- **删除死代码 `src/core/Frustum.h`**（`bighero::Frustum`）：全 src 零 include、零使用，
  CMakeLists 无引用；生产与测试统一使用 `render/Frustum.h`（`Render::Frustum::FromViewProj`）。

### 功能扩展

- **`Render::Frustum` 新增 `IntersectsAABB(bmin, bmax)`**（`src/render/Frustum.h`）：
  逐平面取「正顶点」（沿该平面法线方向最远的盒角点）做保守相交测试；任一平面的
  正顶点都在外侧即整盒可剔除。对箱型物体（建筑、门窗、箱体道具）AABB 比外接球
  剔除更紧，可减少过绘制。纯数学，零 GPU 依赖。
- **新增用例 `Render.FrustumAabbCulling`**（`src/tests/test_render_logic.cpp`）：
  单位裁剪盒 + 透视相机两组场景共 8 断言（完全在内 / 与左平面相交 / 整体右外 /
  近平面后方 / 远平面外 / 视野上方外）。用例 180 → 181。

### 验证

- 沙箱同名验证驱动 `test_frustum_aabb.cpp` 以 `g++ -std=c++20 -Wall -Wextra` 编译
  （零警告）并运行通过：**16 断言全过**（8 条 `IntersectsSphere` 回归 + 8 条
  `IntersectsAABB` 新断言），与 `Render.FrustumAabbCulling` 用例断言一一对应。

## [0.19.0] - 2026-09-20 —— U1 核心工作流闭环完成（对标 Unity 的第一环）

> 本版完成方案 §10 Phase 2 的"Unity 用户的一天"闭环：Hierarchy → Inspector →
> 写 C# 脚本（热重载）→ 摆 UI → 一键构建。测试从 104 增至 **180 用例（18,247 断言）**。

### C# 脚本系统（U1-S1，含 S1d 字段进面板）

- **托管运行时库** `scriptcore/BigHero.Runtime`（net8.0 + EnableDynamicLoading，零 NuGet）：
  Behaviour 三段生命周期、Entity（32 位句柄值语义 + IsAlive 版本校验）、Transform
  （blittable Vec3）、Time/Log、ApiVersion=1；NativeApi 全 blittable C 函数指针表
  （C++/C# static_assert 逐字节对齐）。
- **引擎宿主** `src/script/CSharpHost`：hostfxr 显式路径解析（规避 PATH 上 WPT 副本
  遮蔽）；**热重载**——1s 时间戳轮询 → dotnet build 版本目录（规避 dll 锁）→
  GCHandle 全释放 → 可卸载 ALC 卸载（NoInlining 独立帧等待 WeakReference 死亡）→
  Resolving 先试默认 ALC 防 Runtime 双身份 → 按绑定重挂；.NET 缺失优雅降级。
- **字段进 Inspector（S1d）**：[Editor]/[Range] 特性 → 托管侧反射收集 → blittable
  边界（FieldValueData 20B）→ MetaRegistry getter/setter 通道 → 面板"脚本"分组 +
  撤销闭环；热重载后描述表重建（字段值回落新程序集默认，对齐 Unity 域重载）。
- **验证**：142→155→169→180 全程回归；运行冒烟 Spinner 纯脚本驱动自转
  （双截图 diff_ratio=0.095）；热重载 Speed 90→540 一秒内行为实变 + ALC 完全卸载。

### 运行时 UI 系统（U1-UI 第一增量）

- 纯逻辑模型（离线单测）：锚点矩形解算（uGUI RectTransform 简化版）、Canvas 树、
  命中测试（顶遮挡 + raycastBlock 穿透）、按钮状态机（按下锁定/原节点松开触发，
  对标 PointerClick）、文本测量。
- stb_truetype 动态字集图集（中文字体 msyh.ttc 按需光栅化）；单 draw call 批渲染
  pass（场景后、编辑器 ImGui 前）；--ui-demo 演示画布；命中 UI 时引擎拾取不穿透。
- **随做修复二则**：① 按下边沿首帧校准误吞"第一帧按下"（首帧校准只应抑制松开
  边沿）；② UTF-8 overlong 检查三字节分支边界误写 0x10000（合法三字节域
  U+0800-FFFF 全部被判 overlong），改 0x800——中文解码用例转绿。

### 动画系统（A1/A3）

- 动画事件：跨区间触发语义（半开区间防重复、loop 回绕跨 0、大 dt 每事件至多一次、
  seek 跨过不触发——对齐 Unity）；二维混合空间 BlendSpace2D（双线性插值、散点
  缺角贡献 0、单轴退化）+ 状态机参数通道集成。

### 编辑器/工作流补全

- 构建设置面板 + 一键构建（U1-B1）：清单生成纯函数 + std::filesystem 执行器 +
  自包含可分发目录（exe/shaders/assets/场景）。
- 相机 pitch 治本（P1-11/D10）：pitch 域恢复 -80°，minEyeY 地面碰撞约束
  （解析两腿 + 浮点贴齐防护），旧公式逐位回归守卫。
- 剔除球心修复：视锥剔除改用世界矩阵平移列（父挂实体此前按局部坐标误剔）。

### 工程化

- GltfLoader 拆分：1208 行 header-only → 125 行 API 头 + 823 行实现 + core/Json.h
  （JSON 解析器独立）；实现迭代 8 TU → 1 TU。
- 成像回归基线：--no-ui 旗标 + 4 张 GPU 基线（容差经 3 次运行实测）+ CI 自举式
  lavapipe 比对（无基线时上传候选产物）。
- 0.17.6 VK_LOD_CLAMP_NONE 四处遗留闭环（穷举审计 + 规范依据，零代码改动）。



### 渲染缺陷修复（P0 级）

- **前向场景通道深度清屏值悬垂栈引用**（`Renderer.cpp` DrawFrame）：`clearValues` 在
  `if(!deferredEnabled_)` 块内声明、被 scene pass lambda 按引用捕获，而渲染图在块作用域
  结束后才 `Build()/Execute()`——`vkCmdBeginRenderPass` 读到死栈垃圾。D32_SFLOAT 不钳制
  清屏值：垃圾 >1 时全部片元通过深度测试（画面正常），垃圾 ≤0 时 LESS 下**所有场景几何
  消失**（天空盒 depth=ALWAYS 与 UI 不受影响）；同一进程内垃圾槽位被一致覆写（表现稳定），
  跨进程随 ASLR 变化（实测 5 跑 3 空式间歇）。修复：pass lambda 捕获改 `[&, clearValues]`
  按值捕获（+5/-2）。
  - **验证**：Khronos 验证层诊断构建抓到 `pClearValues[1].depth = 4.3e8 / -1.3e21` 等
    VUID 违例，亮度均值与垃圾值逐跑 100% 相关；修复后 **12/12 次运行全部完整场景**
    （100.2±0.1），`--exposure 0.5/2.0/8.0` 复测 81.8/120.4/155.3。
  - 本缺陷与前两个"相机推近/拉远全黑"修复（c1433f5/1d300e6）的误诊可能相关——当时的
    pitch 收窄属于治标。
  - **诚实边界**：验证层实证另有 2 处违例本次未改（见"遗留"）。

### 验证防线（P0-1 / P0-3 / P0-6）

- **CI 真渲染**：Linux Debug 在 lavapipe + xvfb 下不带 `--headless` 运行 `--screenshot`
  两次（后处理开/关），真实录制并提交 30 帧后回读成像，经 upload-artifact 上传 PNG
  产物（`if: always()`）；`--validate-only` 保留并如实改名为"Shader File Existence
  Quick Check"。首帧崩溃类缺陷（layout 迁移缺失/renderPass 重建时序）自此进入防线。
- **Khronos 验证层启用**（本机）：SDK 自带的 `VkLayer_khronos_validation` 通过
  `VK_LAYER_PATH` 暴露——本轮 P0 级缺陷正因此获得 VUID 级证据链。
- **`--exposure <f>` 命令行参数**：异常防护解析（非数字/≤0 警告回退 1.0），经
  `AppConfig.optional` 仅显式传参时覆盖 `lightParams_.exposure`，与编辑器曝光滑条
  同一字段三链同源生效；实测曝光滑条随 CLI 值同步，亮度均值 0.5→81.7 / 8.0→155.1 单调。
- **帮助文本与实现对齐**：`--validate-only` 描述改为"仅检查 25 个 .spv 存在性"。
- **成像回归比对工具** `tools/compare_images.py`：纯标准库 PNG 解码 + 逐像素亮度容差 +
  差异像素占比阈值；已用真实截图校准（同状态重跑 diff_ratio<0.001 PASS，空/完整场景
  46%+ FAIL），对接 CI 退出码。

### 发布与文档（P0-4 / P0-5）

- 新增根目录 **MIT LICENSE**；CMake 版本 1.0.0 → **0.18.0**（与 CHANGELOG 对齐）；
  HOMEPAGE_URL 占位符 → 真实仓库地址。
- 修正三处不实/过度宣称：README"headless 首帧验证"改为与 CI 新现实一致；UPGRADE_PLAN
  两处"收益恢复"改为准确边界（消除每帧容器重建开销；增量矩阵收益待真实场景验证——
  本版的垂直切片正是该场景）。

### 垂直切片（P1-2）

- **`--scene slice`**：`samples/vertical_slice/` 确定性场景——**1200 实体 / 95% 静止 /
  50 条 3~5 层父子链**（塔群经 `CreateObject→SetParent` 生产路径挂接，`SetParent`
  首次进入生产代码）；引擎接入最小化（`AppConfig.sceneKind`，默认路径逐行不变）。
- **帧计时证据**（1200 实体 × 200 帧，生产帧形态）：每帧仅重算 **65/1200 节点（5.4%）**
  0.84µs，对照全量重建 510.9µs——以生产场景数据替换原 70× 合成基准宣称。
- 新增 3 测试用例（规格锁定 / 层级增量行为 / 帧计时基准）。

### 预研

- **C# 宿主嵌入 spike 通过**（`experiments/csharp-host/`，U1 脚本系统前置）：hostfxr
  LoadLibrary+GetProcAddress 双向调用实测（托管 Add(2,3)=5、UnmanagedCallersOnly 回调
  42）；实测坑：PATH 上 WPT 目录的 hostfxr.dll 遮蔽、类库需 `EnableDynamicLoading`
  生成 runtimeconfig.json、UTF-8 无 BOM 中文注释在代码页 936 下破坏语法；运行时体积
  实测 71MB(8.0.31)；DESIGN.md 含 Behaviour→ECS 映射与 collectible ALC 热重载方案，
  MVP 工作量估算生成 34 + 验证 26 人日。全文按【实测】/【文档】/【推断】分级标注。

### 编辑器（Unity 对标 U1-E1：Hierarchy 层级树）

- **HierarchyPanel**：树形层级窗口——TreeNode 缩进浏览、点击选中（接 Gizmo 高亮）、
  **拖拽改父**（拖到实体行=挂为其子，拖到空白=提升为根）、搜索过滤（命中节点 +
  祖先链 + 子树保持可见，大小写不敏感 ASCII 折叠）、人物组根启发式标记；
  经 DockLayout 纳入停靠体系，`SetParent` 自此进入编辑器生产路径。
- **EcsScene 数据层防御**：`SetParent` 自环/成环拒绝（沿父链上溯检测，返回 false）、
  越界归根、`[[nodiscard]]`；`DestroyAt` 子树策略 = 直接子节点提升为根；
  `LoadPacket` 第二趟挂父支持"父下标高于自身"的包逐位还原（编辑器改父/undo/读档）。
- **撤销闭环**：`parentIndex` 纳入快照差异判定，拖拽改父经 `SceneSnapshotCommand`
  入栈，Ctrl+Z 可回退。
- 新增 `test_hierarchy` 8 例 → **测试 115/115（17,181 断言）**。

### 验证层清零（承接上轮遗留）

- **Swapchain**：imageUsage 按 `supportedUsageFlags` 兜底追加 `TRANSFER_SRC`
  （截图回读布局转换需要，原先违反 VUID-01212/00186，AMD 侥幸工作）。
- **EnvironmentLighting**：irradiance/prefilter/brdf 三个 IBL 烘焙 pass 的
  `vkCmdPushConstants` stageFlags 由 VERTEX-only 对齐到布局声明的 V|F 范围
  （VUID-vkCmdPushConstants-offset-01796）。
- **验收**：x64-Debug（验证层自动启用）`--screenshot` 全程 **0 条 `[Vulkan校验]` 消息**
  （修复前同类路径每进程约 9-10 条），截图 mean_luma 99.35 为完整场景签名。

### 0.17.6 遗留核查项闭环（`VK_LOD_CLAMP_NONE` 四处采样器）——已核查，零代码改动

- 全仓 9 处采样器创建点穷举审计（`maxLod`/`minLod`/`VK_LOD_CLAMP_NONE`）完成，0.17.6 标注
  "列为后续核查项，本次不改"的 `SSAO.cpp:280` / `SSR.cpp:305` / `Renderer_PostFx.cpp:233` /
  `descriptor_set.h:411` 四处 **判定正确、无需修改**：
  - **规范依据**：现行 spec 的 VUID-VkSamplerCreateInfo-maxLod-01973 仅要求 `maxLod ≥ minLod`，
    且 `maxLod` 成员说明明确写有"为避免钳制上限，将 `maxLod` 设为常量 `VK_LOD_CLAMP_NONE`"。
    **不存在**任何把采样器 `maxLod` 与被采样图像 `mipLevels` 绑定的 VUID——mip 层级钳制发生在
    采样时的 mip 层级选择（钳到 levelCount-1），不在采样器创建或描述符写入时。0.17.6 按
    "`maxLod ≤ levelCount-1`"的更严理解标注隐患（该口径下 1000 > 0 确会越界），但该口径并非
    规范条款；按现行规范与业界惯例（仓内 Dear ImGui 官方 backend `imgui_impl_vulkan.cpp`
    同样写 `maxLod = 1000`），四处写法正确。
  - **目标图像核实**：四处不可变采样器的全部描述符绑定均为**单级 mip** 渲染目标（GBuffer
    albedo/normal/position/depth（`CreateUnbound` 默认 mipLevels=1）、SSAO 半分辨率 AO 图、
    SSR 半分辨率反射/模糊图、离屏 HDR sceneColor、dummyWhite 回退图）——动态 lod（导数决定）
    经 mip 层级选择恒钳到 0，与 `maxLod=0` 的采样结果等价，不存在"钳在错误层级"的质量风险。
  - **其余 5 处同样正确**：`EnvironmentLighting.cpp:936`（maxLod=4.0，该采样器唯一绑定的
    envCubemap_ 恰 5 级，prefilter 请求上限精确吻合）、`Texture.cpp:104`（maxLod=mipLevels-1，
    钳到自身 mip 链顶）、`ShadowMap.cpp:42` / `CubeShadowMap.cpp:44`（maxLod=0.0，单级深度
    + compare 采样）、`PostProcessor.cpp:450-458`（未显式设置即默认 maxLod=0.0，其全部后处理
    中间图经逐一核实均为 1 级——1-mip 目标下的最紧正确值）。
- **运行验证（覆盖边界如实记录）**：x64-Debug + SDK 1.4.350.0 Khronos 验证层（本机 shell 需
  `VK_LAYER_PATH` 指向 SDK `Bin` 目录，否则引擎告警"未找到校验层"并回退无校验模式），
  `--screenshot` 后处理关/开双跑：8 / 30 条 `[Vulkan校验]` 消息中 **0 条涉及
  sampler/LOD/maxLod**，截图 mean_luma 99.35（关）/ 106.94（开）为完整场景签名（与 0.18.0
  验收基线 99.35 一致）。SSAO/SSR/GBuffer/composite 四类采样器仅延迟模式生效，而延迟模式
  无 CLI 开关（`postProcessSync_.deferred` 仅编辑器 UI 勾选驱动），`--screenshot` 运行时
  无法覆盖——该四处以静态审计 + 规范依据闭环。
- **审计附带的无关既有消息（本轮不改，留待后续定位）**：SDK 1.4.350.0 层下实测：顶点属性
  未消费告警 8 条（两模式均现，创建期告警）；后处理开启另见输出链 framebuffer 格式不匹配
  （VUID-00880：`outputRenderPass_` 为 HDR 格式而 `outputFramebuffers_` 挂交换链视图）、
  描述符集在用期间被更新（未启 descriptor indexing 标志）、TRANSFER_DST 屏障/清屏缺
  usage、`vkDestroyDevice` 泄漏 1 个 VkFramebuffer。均与采样器核查无关，且与 0.18.0 验收时
  "0 条"记录的差异未逐一归因（验证层版本收紧为最可能因素）。

### 遗留（已实证，归后续版本）

- 视锥剔除球心用 `Transform.position`（父挂实体为局部坐标，切片塔的子节点剔除不准）——待办。

## [0.17.12] - 2026-09-19 —— 人物生成系统（编辑器面板 + 九部件骨骼树）

### 人物生成：PersonHost + PersonParams

- **能力**：可在运行时生成 Q 版风格人物（低模胶囊身 + 球头 + 发/眼/四肢共 9 部件），
  由 `parentIndex` 挂成一棵骨骼树，拖拽根节点即整体移动；人物按部件分桶
  （球 meshId=3、胶囊 meshId=4）走实例化渲染，前向 / 延迟 / 阴影深度四处分支接入。
- **参数系统**：`PersonParams`（身高/头比例/躯干宽/四肢粗/肤色/衣色/发色/眼色/
  姿态/动作速度）→ `ApplyParams` 重排部件布局、`Update(dt)` 持续推进动画时间并
  写回部件位姿；内置五姿态：站立（微收臂）、行走（摆腿±45°/摆臂±32°/前倾 6°）、
  挥手（右臂 88°±14°）、转身（yaw 累积）、蹲坐（前倾 30°+腿屈 65°+臂伸 60°）。
- **编辑器面板**：`DrawPersonWindow` 提供体型滑杆、外观调色、姿态下拉、生成 /
  删除按钮；勾选"鼠标点击生成"后左键点场景地面（射线求交 y=0）落点生成，否则按
  指定坐标生成；请求经 `addPersonRequested/removePersonRequested` 延迟队消费并入
  撤销栈。
- **容量修复**：`InstanceBuffer::Upload` 原静默裁剪超量实例，人物一生成即丢渲染；
  新增 `EnsureInstanceCapacities()`（`ctx_.WaitIdle` + 按 `ObjectCount()+8` 余量
  重建 cube/torus/sphere/capsule/gltf 五组缓冲），add/delete object 与
  add/remove person 四处消费点统一调用。
- **构建修复**：`CubeMesh.h` 补 `#include <glm/gtc/constants.hpp>`（GLM 1.0.3 的
  `pi`/`half_pi` 定义处）、删除未用变量（C4189）。
- **单元测试**：新增 `src/tests/test_person.cpp` 收录 6 例（骨骼树挂接 / 世界矩阵
  级联 / 姿态写回 / 参数重建 / 删除不漂移 / 外部销毁清理）→ 测试 104/104
  （2499 断言）全绿。
- **冒烟验证**：`--demo-person --post-process --screenshot out/person_demo.png`
  与 PP 关闭双跑，人物生成后实体 7→16、截图可见黄色小人在场景中央、无校验层错误。

## [0.17.11] - 2026-09-19 —— 引擎内置截图能力（P0-3 验收自足）

### P0-3 验收配套：内置截图（无需外部工具）

- **能力**：引擎内捕获最终成像并落盘 PNG——`Renderer::RequestScreenshot(path)`
  在下一帧 `vkQueueSubmit` 之后、`vkQueuePresentKHR` 之前，把交换链图像
  （PP 开/关最终画面均在此，含 UI 覆盖层）经 `PRESENT_SRC → TRANSFER_SRC →
  vkCmdCopyImageToBuffer → 恢复 PRESENT_SRC` 读回 host staging buffer，
  交换 R/B 通道（交换链 B8G8R8A8）后以 `stb_image_write` 编码 PNG。
- **触发入口**：命令行 `--screenshot <path>`（渲染 30 帧稳定后自动截图并退出，
  与 `--post-process` 组合即得 PP 开态对比图）；截图写盘前自动创建父目录；
  headless 无交换链时记录 WARN 并忽略请求、干净退出。
- **验证**：`--post-process --screenshot out/pp_on.png` 与 `--screenshot
  out/pp_off.png` 双跑均生成 1600×900 RGBA PNG（亮度均值 75.9 vs 70.2，差异符合
  双重色调映射预期）；headless 组合正常退出；BigHeroTests 98/98（2438 断言）全绿。

## [0.17.10] - 2026-09-19 —— 修复层级缓存每帧全量重建 + 双重色调映射统一收链

### P0-4：层级缓存每帧全量重建（SyncFromPacket 差异写回）

- **根因**：`EcsScene::SyncFromPacket` 无条件置 `hierarchyDirty_=true`，主循环每帧
  `SyncSceneEdits` 都触发 `TransformHierarchy::UpdateWorld` 全量重算，0.17.8 引入的
  增量层级缓存（干净时 O(1) 返回、变更仅重算受影响子树）收益被完全抵消。
- **修复**：改为逐实体差异比较——仅当 `position/rotation/scale` 任一变化时才写回
  组件并置脏；全包等值 round-trip 后缓存保持干净。增量 API（`SetObjectPosition`/
  `UpdateSpins`）路径不受影响。
- **验证**：新增回归 `EcsScene.SyncFromPacketCacheStaysClean`（预热全量 5 → 等值
  round-trip 0 重建 → 单节点移动 1 → TRS 写回全量 5 → 自转增量 5）。

### P0-3：双重色调映射（0.17.9 回退重做，三步可回滚）

- **问题（0.17.8 遗留）**：前向链片元内 ACES+×exposure 输出 LDR，`pp_composite`
  又做一次 ×exposure+ACES——暗部亮度压至 ~1/4、曝光滑条平方生效。
- **Commit 1（258fa72）**：场景主通道附件升级 HDR（R16G16B16A16_SFLOAT）存储，
  PP 开/关重建 renderPass + 交换链 framebuffers（走既有 renderPassRecreateCallback_）。
- **Commit 2（155842a）**：前向几何/天空输出线性 HDR（`ObjectPush.outputTarget` /
  `PushSky.tonemapDirect` 区分直通 vs 合成链），色调映射收敛到合成端；顺带修复
  PP 开启路径三个既有缺陷：① `Image::TransitionLayout` 缺 `UNDEFINED→SHADER_READ_ONLY`
  分支（TAA 历史图首帧初始化必崩）；② 后处理描述符池容量不足（40/1/16 → 75/15/15，
  15 集 × 5 采样器 + UBO）；③ `SetPostProcessing` 重建顺序颠倒——先建离屏 FB（挂旧
  MSAA 深度视图）后重建帧资源，首帧 scene pass `vkCmdBeginRenderPass` 解引用悬垂
  视图段错误；`destroyFrameResources` 前置同时消除挂旧通道 framebuffers_ 泄漏。
- **Commit 3（005bf56）**：deferred 链线性化——`deferred_light` 去 acesFilm 与曝光
  乘法、`deferred_composite` 补唯一 ACES（push 复用 pad 传 exposure）；`frag.glsl`
  mode3 延迟自发光补写改纯线性输出；`Renderer::SetExposure` + `PostProcessSync` 同步
  `pp->exposure`（forward+PP 开时曝光滑条恢复线性生效，与 lightUbo.exposure 同源）。
  三链统一：**离屏图保持线性 HDR，链路末端唯一一次 ×exposure+ACES**。
- **验证（三路径窗口冒烟 8s，均 EXIT=124、无校验层错误）**：PP 关直通 / PP 开
  （scene→post→ui 全 pass 正常）/ deferred（GBuffer MRT 启用）；BigHeroTests 98/98
  （2438 断言）全绿。验收依赖外部截图工具对比暗部亮度与曝光滑条线性（引擎无截图
  能力，需人工在 PP 开启状态截图）。

### 遗留缺陷修复（P0-3 验收路径清理）

- **遗留项①（dbed9d4）PP 开启时休眠直通帧缓冲消隐**：`createFrameResources` 此前无条件
  创建 `framebuffers_`（LDR MSAA 颜色 + 交换链视图），而 PP 开启时 renderPass_ 已用
  `SceneColorFormat()`（SFLOAT）重建——该组帧缓冲以 LDR 附件挂到 SFLOAT 通道下构成
  VUID 附件格式不匹配（休眠不兼容），且 PP 开启时 scene pass 实际走离屏帧缓冲（
  `toOffscreen` 分支），此组帧缓冲从不被消费。修复：直通帧缓冲与 `msaaColorImage_`
  仅在 PP 关闭时创建（深度图仍无条件创建）；`DrawFrame` per-image 闸门与
  `handleResize` 对账同步放宽 PP 开启状态。验证：PP 关 framebuffers=3 / PP 开
  framebuffers=0（日志实证），双路径冒烟 EXIT=124，98/98 回归全绿。
- **遗留项②（ddf79aa）headless 模式 EditorOverlay 段错误**：`SetupCallbacks` 无条件
  `editorOverlay_.Init`，headless（`--headless` / `--validate-only`）下 Window 无原生
  句柄，`ImGui_ImplGlfw_InitForVulkan(NULL)` 首帧段错误（exit=139），headless 渲染
  路径（CI/校验）完全不可用。修复：仅非 headless 时初始化 overlay，新增
  `EditorOverlay::IsInitialized()`，`RecordUi` 未初始化时跳帧。验证：headless 冒烟
  EXIT=124（原 139）、窗口路径 EXIT=124、98/98 回归全绿。

## [0.17.9-reverted] - 2026-09-19 —— 双重 ACES 修复尝试（已回退：启动即崩）

- **尝试内容**：4 shader 片元端线性化（去双重 ACES）+ 前向直通改经离屏缓冲 +
  `RecordBloom(tonemapOnly=true)` 仅 ACES 合成兜底 + 曝光同步 + TAA 抖动条件修复
  （原提交 07985af，补丁 4051cc6，回退提交 87390cc/72ee438）。
- **失败原因**：直通兜底的合成段只重定向了 uScene（b0），但 `pp_composite` 的 main
  **无条件采样 uBloom（b1）**、静态使用 uLinearDepth（b2）——tonemapOnly 跳过了
  bright/blur/深度线性化，这些中间图布局停留在 UNDEFINED，与描述符声明的
  SHADER_READ_ONLY 失配；无验证层（VK_LAYER_KHRONOS_validation 缺失）时 AMD 驱动
  首帧 DEVICE_LOST，双击即闪退。补丁把 b0/b1/b2 重定向到离屏解析图后**仍未恢复启动**，
  时间成本上升，整体回退到 0.17.8（e26ffaf）。
- **教训（重做时必读）**：
  - 合成着色器对描述符是**静态使用**语义——运行时分支不采样也要求描述符布局合法；
  - 凡是绕过某条渲染子链的路径，必须保证该链产出图的布局可达合法状态（或把采样点
    一并重定向到布局受保证的图）；
  - 本机无 Khronos 验证层，布局失配不会在开发期暴露，直接体现为 AMD 驱动 DEVICE_LOST；
  - 「引擎优雅退出」（VK_CHECK throw）不产生 Windows 崩溃记录，排障需控制台输出。
- 0.17.8 条目末尾的「双重 ACES 待修」问题仍然存在，待后续以更小步长重做。

## [0.17.8] - 2026-09-19 —— 修复黑天空根因：env_sunset.hdr 为全黑占位数据

- **根因（数据级，逐字节验证）**：`assets/env/env_sunset.hdr` 的全部 524,288 个像素
  RGB 分量均为 0（0.000% 非零），自 cd582eb「HDR 资源与加载基建」提交起就是一张纯黑
  占位图。后果链：天空盒采样纯黑（画面上半屏黑）→ IBL 辐照度/预滤波全黑 → 环境光项
  只剩 0.15×常数项 → 纯金属物体（kd=0 仅镜面 IBL）渲染为黑色立方体。
- **修复（程序化生成）**：用简化大气散射近似（Rayleigh 仰角渐变 + Mie 太阳晕 + 太阳
  圆盘 + 地面半球反照率）生成 1024×512 flat RGBE 替换。亮度分布（回读验证）：
  天顶 0.09 / 高仰角 0.09 / 地平线 0.67 / 太阳圆盘峰值 RGB≈(1304, 1016, 680) /
  地面半球 0.02–0.05（为 IBL 提供地面反弹）。映射约定与 `equirect_to_cube.frag.glsl`
  的 `sampleSphericalMap` 核对一致（Vulkan uv 原点在左上：文件底行=天顶、中部=地平线）。
- **验证**：RGBE 编码自洽回读（mantissa ∈ [128,256) 标准区间）；BigHeroTests 97/97
  （2432 断言）全绿；已同步 `out/build/x64-Release/bin/Release/assets/env/`。
- **已定位待修（下一轮）**：前向链双重色调映射——`frag.glsl`/`skybox.frag.glsl` 在
  片元内做 ACES+×exposure 输出 LDR，`pp_composite` 又做一次 ×exposure+ACES（设计注释
  明确其输入应为线性 HDR）。后果：暗部亮度压至 ~1/4（黑天空放大因素）、曝光滑条平方
  生效。修复方向：片元端输出线性 HDR，色调映射/曝光统一收敛到链路末端（pp_composite
  与 deferred_composite），延迟链透明叠加域随之统一。

## [0.17.7] - 2026-09-19 —— 视觉异常修复：地面边缘白条 + 全链动态视口合规（VUID-07831/07832）

- **地面扩大 20×20 → 1000×1000**（`CubeMesh.h` `BuildGroundVertices`）：运行截图右侧白色竖条
  的根因是地面网格仅 20×20，边缘之外裸露 skybox 地平线亮带，透视投影下形成生硬的竖直亮边。
  半边长扩至 500（对角 707m）后，任何视角下地面边缘都先被相机 farZ 500m 远平面裁剪，边缘
  永不可见。UV 保持 4m/格 密度（0..125，采样器 REPEAT 寻址平铺不变）。
- **物理同步**：`PhysicsHost::RebuildBodies` 静态地面盒 halfExtents (50,0.5,50) → (500,0.5,500)，
  消除渲染/碰撞尺寸不一致（原注释声明"与渲染地面对齐"但实际 20×20 vs 100×100）。
- **全链动态视口合规**：所有图形管线声明 `dynamicStates = {VIEWPORT, SCISSOR}`（pipeline.h），
  但以下通道从未显式设置视口，靠继承上一通道遗留状态"碰巧正确"（通道顺序/分辨率变化即
  未定义行为，VUID-07831/07832 违规）。补齐 `vkCmdSetViewport`/`vkCmdSetScissor`：
  - `SSAO::RecordPass`：SSAO + 水平/垂直模糊三通道（半分辨率视口）
  - `SSR::RecordPass`：ray march + 水平/垂直模糊三通道（半分辨率视口）
  - `Application::RecordLighting`：延迟光照通道（全屏视口；extent 参数此前为无名参数未使用）
  - `Renderer` 延迟合成通道（composite → 交换链，全屏视口）
  - `PostProcessor::RecordBloom`：在 `beginPass` lambda 内统一设置，一处覆盖全部 11 个
    后处理通道（亮度链 64/8/1/适应、深度线性化、DoF、MB、TAA、亮部提取、模糊×2、合成）
  - 已合规无需改动：gBuffer/前向场景（RecordScene）、RecordTransparent、阴影（ShadowMap/
    CubeShadowMap）、IBL（EnvironmentLighting）、UI（ImGui 后端自行设置）
- **已知权衡**：GBuffer position 附件为半精度 R16G16B16A16_SFLOAT，地面扩至 ±500m 后
  远处（>50m）SSAO/SSR 重建位置误差约 2.4cm（50m 处，随距离线性增长），视觉影响轻微；
  前向路径完全不受影响。
- **截图其余异常定性（非缺陷）**：上半屏黑天空 = HDR 环境贴图天顶暗部内容；左侧黑色立方体
  = 纯金属（metallic=1.0）平面在低环境光（ambientFactor 0.15）下仅镜面 IBL 的物理正确表现
  （金色圆环同为纯金属因曲面总能捕捉太阳高光而明亮）。
- **验证**：x64-Release 编译 0 新增警告；BigHeroTests 97/97 用例 2432 断言全绿；
  validate-only 无窗校验通过（EXIT=0）。

## [0.17.6] - 2026-09-18 —— 修复 GPU DEVICE_LOST（VUID-01020 view 先于内存绑定创建）

- **根因（运行时二分定位 + 静态分析）**：`Image::CreateImageAndView` 在创建 VkImage 后
  立即创建 VkImageView（`vkCreateImageView`），**此时 image 尚未绑定内存**（`vkBindImageMemory`
  在后续 `Image::Create`/`CreateBound` 中才调用）。这违反 VUID-VkImageViewCreateInfo-image-01020
  （non-sparse image 必须在创建 view 前绑定内存）。AMD 780M 驱动在 draw 命令时延迟做
  view→memory 映射查找，因 view 创建早于 bind 导致映射表缺项 → GPU 页错误 → DEVICE_LOST。
  任何 draw 命令均触发故障（阴影预通道、天空盒、不透明物体），render pass begin/end 不触发
  （CLEAR 只设置引用不做映射查找）。
- **修复**：拆分 `CreateImageAndView` 为 `CreateImageOnly`（创建 VkImage）+ `CreateView`（创建
  VkImageView）。`Image::Create` 和 `Image::CreateBound` 改为：创建 image → 绑定内存 → 创建 view。
- **修复（本版追加）**：`Image::CreateUnbound`（transient 池路径）此前仍「先建 view 后绑内存」，
  是全仓唯一不满足严格 01020 时序的点。现改为：`CreateUnbound` 仅创建图像并暂存视图参数
  （pending），视图由 `Image::FinalizePendingView` 在 `vkBindImageMemory` 成功之后创建
  （`BindExternalMemory` 内部自动调用；transient 池经 `TransientAllocator` 按原始句柄批量绑定的
  路径，由 `Renderer::bindTransientImages` 在绑定后显式调用）。相应地 GBuffer/SSR 的 framebuffer
  创建时机从 `createDeferredFramebuffers` 后移至 `bindTransientImages` 末尾（新增
  `Renderer::createDeferredFramebufferObjects`，SSR 的 `CreateFramebuffers` 改为公开并在绑定后调用），
  确保取 `.View()` 建 framebuffer 时视图已就绪；GBuffer 描述符集更新改用
  `Renderer::SetTransientBoundCallback` 在绑定完成后写入（`UpdateGBufferSets` 加
  `TransientViewsReady` 守卫），避免写入 NULL 视图。至此全仓视图创建均在绑内存之后。
- **附带修复（前轮静态定位）**：
  - VUID-07831/07832：天空盒绘制前补 `vkCmdSetViewport`/`vkCmdSetScissor`
  - 阴影渲染通道缺 0→EXTERNAL subpass dependency（`ShadowMap.cpp`/`CubeShadowMap.cpp`）
  - `GpuProfiler` query pool 首次使用未 reset 的 UB（加 `resetRecorded_` 守卫）
  - `ParallelCommandRecorder` 命令池缺 `RESET_COMMAND_BUFFER_BIT`
  - `ShadowMap` finalLayout VUID-03285（DEPTH_READ_ONLY→DEPTH_STENCIL_READ_ONLY_OPTIMAL）
  - IBL 三处 `VkRenderPassBeginInfo` 补 `clearValueCount`/`pClearValues`（辐照度/预滤波/BRDF LUT）
  - `EnvironmentLighting::createRenderPasses` 幂等保护
  - 程序化路径 envCubemap 32F→16F + CPU 半精度转换
  - envCubemap 生成 kPrefilterMips 级 mip 链 + irradiance/prefilter 显式 textureLod
- **静态审计（已确认，非运行时验证）**：
  - 全仓 9 处 `vkCreateImageView` 逐点核对：`Image::Create`/`CreateBound` 及手动站点
    （`CubeShadowMap`/`EnvironmentLighting`）均在绑定内存后创建视图；`CreateUnbound` 经本版
    重构后亦满足。
  - 三处遗留雷已在 `872fb9b` 落地：① `createRenderPasses` 幂等守卫；② `GpuProfiler` 用
    `TOP_OF_PIPE_BIT` + `resetRecorded_` 守卫，规避可选时间戳特性依赖；③ 采样器 `maxLod` 修正
    （见下方「运行期 DEVICE_LOST 二次定位」——原「与 mip 级数对齐」的写法实为越界 1 级）。
- **待验证**：本版代码改动（`CreateUnbound` 视图延迟 + framebuffer 时机后移）**尚未经过真实
  GPU 运行时验证**。需在 AMD 780M 机器上：编译 0 新增警告 → headless 运行确认走完
  HDR/立方图/辐照度/预滤波/BRDF/IBL 全流程且无 DEVICE_LOST → 小窗 960×540 存活 ≥30s →
  核查系统事件无新增 141/144。通过前不得视为已解决。
- **诊断方法（前轮）**：6 轮二分探针（PRE_SKIP/SCENE_SKIP/PAR_SKIP/IBL_SKIP_DRAW/IBL_PROBE_NODRAW/SKIP_SKYBOX）
  用于定位最小存活集；全部探针已移除。
- **运行期 DEVICE_LOST 二次定位（依 2026-09-18 用户复现日志，纯静态修复，未启动进程）**：
  - **现象**：启动后约 8 秒异常退出，stderr 为
    `[ERROR] 提交一次性命令 | VkResult: VK_ERROR_DEVICE_LOST` → `引擎异常退出`；stdout 最后一条为
    `[DBG] 立方图转换后设备状态检查`（诊断残留）。崩溃点在 `EnvironmentLighting::setupIBL` 的
    首个 `SubmitOneTime`（envCubemap mip 链生成）。
  - **根因（VUID-VkSamplerCreateInfo-maxLod-01973）**：采样器 `maxLod` 必须
    **≤ 被采样图像 levelCount − 1**。`EnvironmentLighting::createSampler` 原写
    `maxLod = IblMipLevels()`（= 5.0），而 `envCubemap_` 恰有 5 级 mip（合法上限 4.0）——**越界 1 级**。
    该采样器经 `envSet_` 采样 `envCubemap_`，被 `irradiance.frag`/`prefilter.frag` 密集 lod 采样
    （`prefilter.frag` 请求 `roughness*4.0` ∈ [0,4]）。AMD 780M 驱动对越界 `maxLod` 做未定义的
    lod 钳制/映射 → GPU 页错误 → DEVICE_LOST。**注意**：`maxLod` 是 `VkSamplerCreateInfo` 的静态字段，
    与运行时请求的 lod 无关——即便 shader 从不请求 lod>4，越界的 `maxLod` 本身即构成规范违规，
    且驱动可能在 `vkCreateSampler` 时即建立越界的 mip 描述表。
  - **修复（4 个文件）**：
    - `EnvironmentLighting.cpp`：`maxLod = IblMipLevels() - 1`（= 4.0，与 prefilter 请求上限精确吻合）
    - `Texture.cpp`：`maxLod = mipLevels - 1`（`mipLevels` 由上文保证 ≥ 1，无下溢）
    - `ShadowMap.cpp` / `CubeShadowMap.cpp`：单级 mip 图像，`maxLod` 由 1.0 改 0.0
  - **未改动但已标注（同类潜在隐患，超出本次崩溃路径范围）**：`SSAO.cpp` / `SSR.cpp` /
    `Renderer_PostFx.cpp` / `descriptor_set.h` 使用 `VK_LOD_CLAMP_NONE`，规范意义上同样要求
    `≤ levelCount-1`；这四处为动态采样（lod 由导数决定）的标准写法，且被采样图像为单级 mip 的
    GBuffer/离屏图，实际 lod 恒被钳到 0。**列为后续核查项，本次不改**（避免扩大改动面）。
  - **诚实的边界**：本节为**静态定位**，未在 780M 上运行验证。推理依据 = 用户日志的崩溃位置
    （`setupIBL` 首次提交）+ 规范条款 + 全仓 `maxLod` 穷举审计。**须真机复跑确认。**
- **防御性加固（本版追加，纯静态修改，未启动任何 GPU 进程）**：
  - **析构时序**：`~Renderer` 新增 `ssr_.Destroy()` 并置于 `destroyDeferredResources()` 之前。
    `ssr_` 帧缓冲引用 transient 池显存，若晚于池释放被销毁则构成悬垂引用（池绑定图像此时已失效）。
    `Renderer.h` 中原「声明序确保池显存晚于图像销毁」的注释实为反向，已更正为说明成员析构逆序
    （`transientAlloc_` 声明在图像成员之后 → 析构先于图像，故不能依赖 RAII，须显式 teardown）。
    已复核四条生命周期路径（`~Renderer`／`handleResize`／`SetSSR`／`SetDeferred(false)`）销毁顺序均正确。
  - **异常安全（资源泄漏）**：`Image::Create` 在 `FindMemoryType` 失败、以及
    `vkAllocateMemory`/`vkBindImageMemory` 经 `VK_CHECK` 失败的路径上，此前会直接抛异常而
    泄漏已创建的 VkImage。现统一先 `Destroy()` 再抛（后者用 try/catch 包裹以保证任意异常路径回收）。
  - **越界/空视图防御**：`Renderer::createDeferredFramebufferObjects` 循环上界改用
    `deferredFramebuffers_.size()`（原用 `swapchain_.ImageCount()`，交换链重建后计数短暂不一致时
    可能越界），并校验各帧缓冲/图像向量尺寸一致、`offscreenColorImage_` 非空；几何与透明通道的
    视图为空时直接 `throw`（而非把 `VK_NULL_HANDLE` 传给 `vkCreateFramebuffer`）。
    `SSR::CreateFramebuffers` 同样补空视图 `throw` 与幂等守卫。
  - **异常传播**：上述 `throw` 经 `DrawFrame` 上抛至 `Application::run` 顶层 `catch`，走
    `EXIT_FAILURE` 干净退出——时序错误不可恢复，fail-fast 优于静默提交空视图。
- **待验证（同上）**：本节全部改动仅为静态防御，**未启动任何进程**；仍需真机 GPU 运行时验证。

## [0.17.5] - 2026-09-16 —— 修复 IBL 预滤波挂死（GPU LiveKernelEvent 141 根因）

- **根因（静态审查 + 事件日志交叉验证）**：`setupIBL()` 的 GGX 预滤波阶段此前**未绑定**
  `envSet_` 描述符集。辐照度阶段有 `vkCmdBindDescriptorSets`，预滤波阶段没有；
  而各阶段分别运行在独立的一次性命令缓冲里，描述符绑定不跨缓冲继承。
  `prefilter.frag.glsl` 声明 `layout(set=0, binding=0) uniform samplerCube envMap`
  并密集采样它 —— 采样一个从未绑定的描述符集在 AMD 驱动上是确定性设备挂死，
  与现象（每次启动必现 LiveKernelEvent 141 看门狗复位、三次尝试零差异）完全吻合。
- **排除超时假设**：IBL 预计算的合法算力（128³ 级立方图重采样、BRDF 1024 采样×512²）
  仅数十毫秒量级，远低于约 2 秒的 TDR 窗口；TDR 复位必然来自挂死而非超时。
- **修复**：预滤波阶段补 `vkCmdBindDescriptorSets(envSet_)`，与辐照度阶段一致。
- **诊断残留清理**：撤销工作区未提交的诊断代码（TEMP 纯红纹理替换 HDR、
  立方图转换后空提交探针、transferPool→commandPool 实验），恢复真实 HDR 路径。
- 运行时验证需真实 GPU，已在 AMD 780M 机器上留给本机验证步骤。

## [0.17.4] - 2026-09-16 —— 描述符池扩容 + 分代重置（阶段二 · 2.1 技术债）

- **修复真实缺陷**：`DescriptorManager` 的描述符池此前创建时**未设
  `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`**，却已在 `Release()`
  中调用 `vkFreeDescriptorSets` —— 该调用因此实为无效；更严重的是
  `AllocateGBufferSets()` 在重建（窗口 resize）时只 `clear()` 向量、**不释放旧集合**，
  会把固定容量的池逐步耗尽，最终 `vkAllocateDescriptorSets` 溢出。
- 池容量不再写死：由“每帧组消耗 × 帧在飞组数 + 交换链图像上限”**派生**
  （`kUniformBuffersPerFrameGroup`/`kSamplersPerFrameGroup`/`kPoolFrameGroups`/
  `kPoolSwapchainImages`），并以 `MaxU32` 保留**不低于历史值**（maxSets 400 /
  UBO 200 / sampler 256）的下限，杜绝容量回归。
- 池创建加入 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`，使逐集合释放生效。
- **分代重置**：新增 `ResetPool()`（`vkResetDescriptorPool` + 清空全部集合 + 代号自增）
  与 `PoolGeneration()`；`AllocateGBufferSets()`/`AllocateAOSet()` 在重建前先回收上一代
  集合；`Swap()` 同步 `poolGeneration_`。
- 依据：`Renderer::MaxFramesInFlight()==2`、`kDescriptorSetsPerFrame==3`、
  `kObjectTextureSlots==16`；本改动仅涉资源生命周期，不改变布局或绑定契约。

## [0.17.3] - 2026-09-16 —— 着色器 binding 集中化（阶段一 · 1.3 技术债）

- **新增中央契约头** `shaders/include/bindings.glsl`：把此前散落在 23 个着色器中的
  **62 处** `layout(set = N, binding = M)` 魔法数字收敛为具名宏（`BH_SET_MATERIAL`、
  `BH_MATERIAL_LIGHT_UBO`、`BH_GBUFFER_POSITION` 等）。经核实，`set=1`（材质/光照，
  binding 0..9）在 `frag.glsl` / `deferred_light.frag.glsl` / `gbuffer.frag.glsl` /
  `skybox.frag.glsl` 中**完全一致**，是稳定的全局绑定契约；`set=0/2/3` 按管线复用，
  故以用途分名（如 `BH_SET_POST` 与 `BH_SET_CAMERA` 同值）。
- **新增 C++ 镜像** `src/render/shader_bindings.h`（`BigHero::Render::ShaderBindings`）：
  与 GLSL 宏逐一对应的 `constexpr` 常量 + 静态断言（纹理池 16 槽、材质集 0..9），
  使描述符布局代码与 GLSL 共享同一份数字来源。
- `src/render/descriptor_set.h`：改为包含 `shader_bindings.h`，`kObjectTextureSlots`
  由字面量 `16` 改为引用 `ShaderBindings::kMaterialObjectTextureSlots`（单一来源，
  消除与着色器 `uObjectTex[16]` 的重复定义）。
- `CMakeLists.txt`：新增 `BIGHERO_SHADER_HEADERS` glob，并给 `glslc` 加
  `-I${shaders}` 供 `#include "include/bindings.glsl"` 解析，且把共享头列入
  `DEPENDS`——任一绑定头改动都会触发全部着色器重编译。共享头置于 `shaders/include/`
  子目录以避免被 `shaders/*.glsl` 的顶层 glob 误当作独立着色器编译。
- **安全保证**：改写由规则化脚本执行，逐文件解析回整数序列并与改写前逐一比对，
  **23/23 文件、62/62 处全部一致（FAILURES=0）**，语义零变化。
- 依据：`src/main.cpp` 的 `--headless`/`--validate-only`（见 0.17.2）依赖 24 个
  `shaders/*.spv`；本改动仅重命名 GLSL 中的常量，不改变 SPIR-V 行为。

## [0.17.2] - 2026-09-16 —— 放开 CI lavapipe headless 校验（阶段一 · 1.2 外部验证）

- **止损**：`.github/workflows/ci.yml` 的「Headless Vulkan Validation (lavapipe)」步骤此前整段被注释；
  即便解除注释，原草稿也因**工作目录错误**（从仓库根运行，相对路径 `shaders/*.spv` 指向源码
  `.glsl` 目录、找不到已编译的 `.spv`）并追加 `|| true` 而形同虚设，无法提供任何外部验证。
- 现启用该步骤并修正执行方式（Linux Debug 作业）：用 `find` 定位构建出的 `BigHeroGameEngine`
  二进制，切入其所在目录后运行 `--headless --validate-only`，确保 `Application::ValidateOnly()`
  的相对路径检查命中二进制旁的 `shaders/*.spv`；并**移除 `|| true`**，使其成为真实门禁。
- 依据：`src/main.cpp` 解析 `--headless`/`--validate-only`（后者调用 `Application::ValidateOnly()`）；
  headless 模式经 `Window::CreateHeadless()` 与 `Context(true)` 跳过窗口与交换链，仅校验 24 个必需
  SPIR-V 的存在性，因而无需窗口系统；步骤前已安装 `mesa-vulkan-drivers` 并导出 lvp ICD。
- 已用 `yaml.safe_load` 校验工作流语法（7 个 job 完好）。

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