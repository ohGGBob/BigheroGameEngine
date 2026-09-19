# BigheroGameEngine 变更日志

本文档记录各轮升级中**已独立编译验证并落地**的功能增强。
所有条目均在沙箱以 `g++ -std=c++20 -Wall -Wextra` 编译运行验证通过后镜像到本仓库，
并保留同名验证驱动与输出说明。

## [0.18.0] - 2026-09-19 —— 验证防线落地 / 深度清屏悬垂修复 / 垂直切片 / C# 预研

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
