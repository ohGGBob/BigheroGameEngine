# BigHeroGameEngine

基于 **Vulkan 1.3 + GLFW + GLM + C++20** 的从零实现的 3D 游戏引擎，目前处于早期开发阶段，
已完成一条完整可用的基础渲染管线。

## 当前特性

**渲染（Vulkan）**
- 实例 / 校验层（Khronos validation + debug messenger）/ 窗口表面 完整初始化流程
- 物理设备打分选择（独显优先），图形/呈现队列族自动匹配
- 交换链封装：信箱优先呈现模式、`oldSwapchain` 加速重建、窗口尺寸变化自动重建、最小化挂起等待
- **MSAA 4x 抗锯齿**：颜色/深度多重采样附件 + 解析附件，自动探测采样数支持
- 颜色 + 深度双附件渲染通道
- 图形管线可配置（顶点输入 / 推送常量 / 剔除与深度状态 / 采样数）
- 双帧并行（frames in flight）：每帧独立命令缓冲、信号量/栅栏、独立 UBO 与描述符集
- 索引化绘制，staging 缓冲上传到设备本地内存
- **stb_image 纹理资源加载**（assets/ 下 PNG/JPG/BMP，SRGB 采样）+ 程序化棋盘格回退
- **纹理 mipmap 链**：GPU blit 自动生成完整 mip 级 + 三线性采样，远处地面不再闪烁
- 图像布局迁移、合并图像采样器、各向异性过滤
- Blinn-Phong 已升级为 **PBR（Cook-Torrance 金属度/粗糙度工作流）**：
  GGX法线分布 + Smith几何项 + Schlick菲涅尔，ACES 色调映射，逐物体金属度/粗糙度
- **多光源 PBR**：方向光 + 最多8盏点光源（平方衰减+半径窗口），编辑器实时增删调节
- **IBL 环境光照**：CPU程序化HDR天空（渐变+太阳+地面反弹）作为环境源，
  GPU预计算辐照度立方图（漫反射卷积）+ GGX预滤波立方图（镜面mip链）+ BRDF LUT
  （分裂求和），PBR环境光按IBL强度与常数环境光混合，强度编辑器可调
- **天空盒背景**：环境立方图渲染为场景背景，与场景共用同一色调映射
- **阴影贴图**：2048深度预通道（仅深度管线、前向剔除）+ 光照视空间矩阵 + 3x3 PCF 软阴影，
  浓度/偏移可调
- **点光源立方体阴影**：1024 立方体贴图深度附件（`CUBE_COMPATIBLE`，6 面独立预通道），
  6 个 90° 视锥的视投影矩阵经 set 2 专用 UBO 传入，`samplerCube` 采样 + 3x3x3 PCF 软阴影，
  跨面平滑过渡；每盏点光源可独立开关（编辑器"点光源"面板"投影阴影"勾选）
- **法线贴图**：顶点切线（解析/通用Lengyel计算）构建 TBN，切线空间法线扰动，
  配套程序化生成的法线图资源（与反照率贴图同一高度场）
- 推送常量：逐物体模型矩阵 + 材质参数（tint/metallic/roughness），一份网格驱动多实例
- **Mesh 网格资源类**（VBO+IBO RAII 封装，子范围索引化绘制，自动计算局部包围球供剔除）
- **视锥剔除（前向剔除）**：每帧从相机视图投影矩阵提取 6 平面视锥（Gribb-Hartmann，
  适配 Vulkan NDC z∈[0,1]），对物体包围球做相交测试，视锥外物体直接跳过绘制，
  编辑器"渲染统计"面板实时显示剔除数。仅剔除主场景通道，阴影预通道渲染全部投射体以保证阴影正确
- **实例化渲染（instancing）**：立方体/圆环/地面各用一次 `vkCmdDrawIndexedInstanced` 批量绘制，
  逐实例的模型矩阵 + PBR 材质经绑定1（`VK_VERTEX_INPUT_RATE_INSTANCE`）下传，
  主场景绘制批次从 N 次下降到恒定 3 次；`InstanceBuffer` 按容量分配设备本地缓冲、每帧经 staging 上传可见实例
- **HDR 环境贴图加载（RGBE）**：`HdrImage` 纯 CPU 解析 Radiance `.hdr`（头 + 扫描线 RLE + RGBE→线性 float，
  含 EXPOSURE 增益），不依赖 stb_image；附等距柱状投影→立方图（`EquirectToCube`）与方向采样（`SampleEquirect`），
  采样约定与 GPU IBL 卷积自洽，可直接喂给环境光照管线
- **极简 OBJ 模型加载**（v/vt/vn / 多边形扇形三角化 / 负索引 / 角点去重）
- **Wavefront .mtl 材质解析**：`MtlMaterial` 解析 `newmtl/Ka/Kd/Ks/Ns/d/Tr/illum/map_*`，
  OBJ 加载器支持 `mtllib` + `usemtl` 按材质把面聚合为子网格（`SubMesh`），缺失材质库时优雅降级
- **glTF 2.0 加载器**：`GltfLoader.h` 纯 CPU 解析 glTF 2.0 静态网格（JSON + base64 内嵌缓冲，
  自带精简 JSON 解析器，不依赖外部库）。支持 `buffers/bufferViews/accessors/meshes.primitives`
  的 POSITION/NORMAL/TEXCOORD_0/COLOR_0/TANGENT 属性与 UINT8/16/32 索引，`mode=4` 三角网格，
  多 primitive 聚合为子网格，缺失法线/UV/顶点色自动回退（与 OBJ 加载器一致），
  每个 primitive 可按 `material` 引用材质（抽取 PBR baseColorFactor）。
  并解析 **骨骼蒙皮数据**：`nodes[]` 层级（TRS + 反向 children→parent）、`skins[]` 的关节节点
  与逆绑定矩阵（MAT4）、逐顶点 `JOINTS_0`/`WEIGHTS_0`（至多 4 关节）
- **骨骼蒙皮（Skeleton）**：`Skeleton.h` 基于 glTF 骨骼数据的 CPU 姿态计算器（纯 CPU、可离线单测）。
  `ComputeGlobalNodeMatrices` 沿父链递归级联（记忆化，不受节点顺序影响）求全局矩阵，
  `ComputeGlobalJointMatrices` 提取关节全局矩阵，`ComputeSkinMatrices = 全局关节 * 逆绑定矩阵`，
  `SkinVertices` 对顶点/法线做 4 关节加权蒙皮（权重归一化），用于骨骼动画的 CPU 预览/校验
- **glTF 动画系统（AnimationPlayer）**：`Animation.h` 纯 CPU 动画播放器（可离线单测）。
  `GltfLoader` 解析 `animations[]`（通道 `target.node/path` + 采样器 `input/output/interpolation`），
  `AnimationPlayer` 在给定时刻按通道求值各节点局部 TRS（平移/缩放 lerp、旋转 slerp、STEP 取前值，
  loop 回绕）；未命中通道保持模型默认 TRS。
  另有 `AnimationState`（播放时间/速度倍率/循环/暂停）与 `AnimationBlender`（多动画加权混合，
  权重归一化 + 四元数短弧累加，用于 crossfade 过渡）
- **骨骼动画端到端管线（SkinnedMesh）**：`SkinnedMesh.h` 把动画与蒙皮串成完整 CPU 链路（可离线单测）：
  `AnimationPlayer` 采样节点局部 TRS → `Skeleton` 沿父链级联求全局矩阵并乘逆绑定得皮肤矩阵
  → 逐顶点 4 关节加权蒙皮输出位置/法线。`Evaluate(animIndex, time, loop)` 按动画求值、
  `EvaluatePose` 接外部混合姿态、`EvaluateBind` 输出绑定姿态；无骨骼/无动画时自动回退为静态网格。
  骨骼沿层级级联，故驱动父节点会自然带动全部子节点
- **GPU 蒙皮（骨骼矩阵调色板）**：`render/Skinning.h` + `shaders/skinned.vert.glsl` 把蒙皮从 CPU 移到 GPU。
  CPU 每帧只求值骨骼矩阵并打包进 `SkinningUBO`（std140 `mat4[128]`，数组步长 64 字节，
  可整体 memcpy 上传），顶点着色器按逐顶点关节索引采样调色板做 4 关节线性混合蒙皮（含权重归一化防护）。
  `SkinnedVertex` 前 5 个属性与 `Scene::Vertex` 一致（复用同一片段着色器），
  权重/关节占用 location 11/12（5~10 为逐实例属性）；`SkinningPalette` 提供带越界保护的填充接口，
  并可直接从 `SkinnedMesh` 的动画姿态一键填充
- **VMA 显存分配器（GpuAllocator）**：`render/GpuAllocator.h` 自研轻量 VMA 替代（纯策略、可离线单测）。
  在少量大块 `VkDeviceMemory` 之上做块内子分配（sub-allocation），避免每次资源都调用昂贵的 `vkAllocateMemory`：
  `GpuBlockAllocator`（free-list + 相邻合并）管理单块字节区间，支持对齐分配、释放与碎片合并；
  `GpuAllocator` 门面持有多块并按需扩容（`maxBlocks` 上限），真实 `vkAllocateMemory` 通过注入的
  `CreateBlockFn` 回调解耦，便于离线单测与将来平滑切换到开源 VMA。分配句柄含块号+偏移+大小，
  供 `vkBindBufferMemory(device, buf, MemoryOf(block), a.offset)` 直接绑定
- **延迟渲染通道（Deferred Rendering）**：GBuffer 多渲染目标（MRT）几何子通道 + 输入附件
  （input attachment）延迟光照子通道的双子通道架构。`GpuAllocator`/`GBuffer.h` 定义三张 GBuffer 颜色附件
  （RGBA8 反照率+金属度 / RGBA16F 法线+粗糙度 / RGBA16F 世界坐标，alpha 作几何标记）；几何阶段经 `gbuffer.frag`
  把材质/世界法线/世界坐标写入 MRT，子通道间通过 `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT` 以 `subpassLoad` 读回；
  延迟光照阶段以全屏三角形（`deferred_light.vert/frag`）采样 GBuffer，复用与前向一致的多光源 PBR/阴影/IBL 模型
  输出最终颜色，背景像素由 `gPosition.a<=0` 几何标记走天空分支。编辑器面板"渲染统计"可实时切换前向/延迟模式，
  GBuffer 图像与帧缓冲随开关惰性创建/释放，渲染通道始终保留并与交换链格式同步（供 GBuffer/光照管线持续引用）

**物理（ReactPhysics3D）**
- 集成 ReactPhysics3D v0.10.0 物理引擎（FetchContent 自动拉取，缓存至构建目录），
  支持静态 / 动态 / 运动学三类刚体，盒 / 球 / 胶囊三种碰撞形状
- **第三人称角色控制器**：胶囊体动态刚体 + WASD 移动 + 空格跳跃 + 地面检测 + 相机自动跟随
- **物理射线检测**：鼠标拾取优先走物理射线命中（返回物体索引），未命中回退到 AABB；
  右键在命中点生成动态立方体（物理交互 demo）
- **关节系统**：固定 / 铰链 / 球窝 / 滑块四类关节（编辑器"场景"面板连接两物体），
  可视化调试线绘制连接、锚点与轴
- 重力 / 摩擦 / 弹性等参数编辑器实时可调，刚体随场景增删与属性变更自动重建

**音频（miniaudio）**
- 集成 miniaudio 跨平台音频后端，主音量实时可调（编辑器"渲染统计"面板）
- BGM 自动加载（`assets/audio/bgm.wav`），文件缺失时静默就绪、放入即播

**动画状态机**
- `AnimationStateMachine`：Idle / Walk / Jump 状态 + 基于 `Speed` / `Grounded` / `Jump`
  参数与条件阈值的状态过渡（含退出时间 crossfade）；与角色控制器联动：
  水平速度驱动 Idle↔Walk，跳跃触发 Jump，着地回到 Idle

**后处理 / SSAO / SSR**
- **后处理**：Bloom（亮部提取 + 高斯模糊）+ ACES 色调映射（仅前向模式，编辑器开关）
- **色调分级 Color Grading（升级 21）**：纯逻辑核心 `render/ColorGrading.h`（`GradeColor`：gain/lift → 伽马 →
  对比度 → 饱和度，ASC CDL 风格，可离线单测）与 GPU 合成阶段分级（ACES 之后）同源公式；编辑器"后处理 Bloom"下展开
  "色调分级 (Color Grading)"节点，实时调饱和度 / 对比度 / 暗部提升 / 增益 / 伽马。仅前向模式（后处理关闭时不影响画面）
- **SSAO**：半分辨率环境光遮蔽（仅延迟模式，编辑器开关）
- **SSR**：半分辨率屏幕空间反射（ray march + 高斯模糊，仅延迟模式，编辑器开关）

**场景序列化**
- `SceneSerializer`：场景（物体 / 点光源 / 方向光 / 相机 FOV）JSON 序列化
- **F5 保存 / F9 加载**（边沿检测防重复触发），文件 `scene.json`

**应用架构（Application）**
- `app/Application` 类：资源装配（窗口 / 上下文 / 渲染器 / 音频 / 物理 / 编辑器）
  + 主循环（`Run()`：逐系统 `UpdateXxx` → `DrawFrame` 录制回调）+ 输入 / UI / 录制回调
- 各玩法 / 工具系统以独立方法挂载，录制回调 `RecordScene / RecordUi / RecordPrePass / RecordLighting`
  注入 `Renderer::DrawFrame`
- 成员声明顺序即初始化顺序、析构逆序释放，保证 Vulkan 资源在 `Context` 销毁前全部释放

**玩法系统（升级 17–20：导航 / AI 巡逻 / 粒子 / 撤销重做 / 粒子编辑器 / 属性编辑撤销）**
- **导航网格 A\* 寻路**（`game/NavGrid.h`，纯逻辑、可离线单测）：规则二维网格 + 曼哈顿 / 欧氏 /
  Octile 启发式 + 4/8 邻接 + 对角切角防护（两侧正交均阻挡时禁止斜穿）；
  编辑器勾选"导航网格 (A\*)"可视化网格线 / 障碍叉线 / 路径线（绿=起点 红=终点 蓝=网格 红叉=障碍）
- **AI 导航代理 NavAgent**（`game/NavAgent.h`，纯逻辑、可离线单测，升级 18）：在 `NavGrid` 的 A\*
  路径上以恒定世界速度线性插值移动（单帧可跨多格），支持环形巡逻点队列（抵达一站自动规划下一站，
  形成 `points[0]→points[1]→…→points[n-1]→points[0]` 闭环）；编辑器勾选"AI 导航代理 (NavAgent)"可视化
  代理位置（金黄圆点）与当前朝向 / 路径（黄线）。默认四角巡逻，可由 `Application::InitGameSystems` 配置。
- **粒子系统**（`game/ParticleSystem.h` CPU 模拟 + `render/ParticleBuffer.h` + `shaders/particle.*.glsl`
  GPU 实例化公告板渲染）：固定容量对象池 + 显式欧拉积分（重力 + 阻尼）+ 速率发射 / 手动爆发；
  GPU 端以 `gl_VertexIndex` 生成单位四边形、用相机 `camRight` / `camUp` 世界轴展开 billboard、
  Alpha 混合（pipeline 新增 `blendEnable` 开关）；按 **P** 在相机注视点触发粒子爆发
- **粒子编辑器（升级 19）**（`game/EmitterPresets.h` 纯逻辑预设 + 编辑器实时调参）：4 套预设配方
  （喷泉 / 爆发 / 烟雾 / 火花，`EmitterPreset` = 发射器配置 + 重力 + 阻尼）；编辑器"渲染统计"面板展开
  "粒子编辑器"节点，可下拉切换预设并实时拖动发射速率 / 初速度 / 寿命 / 尺寸 / 颜色 / 重力 / 阻尼等参数，
  每帧写入 `ParticleSystem`，所见即所得
- **撤销 / 重做命令栈**（`game/CommandStack.h`，纯逻辑、可离线单测）：抽象 `Command` 的 `Do/Undo`
  + 双栈（撤销 / 重做）；场景增删（编辑器添加 / 删除、右键生成物理立方体）纳入可撤销命令，
  **Ctrl+Z** 撤销 / **Ctrl+Y** 重做（并提供编辑器按钮）
- **属性编辑撤销（升级 20）**（`game/SceneCommand.h` 纯逻辑快照 + 命令，可离线单测）：场景快照
  `SceneSnapshot`（物体列表 / 自转角 / 可见性）经抽象接口 `SceneSnapshotTarget`（由 `Application` 实现）读写；
  物体属性连续编辑——编辑器滑块 / 调色板（基于 `ImGui::IsAnyItemActive` 边沿手势）与 Gizmo 变换拖拽
  （屏幕手柄位移到松手）——在"手势起始快照 vs 松手快照"对象数不变且确有差异时，各作为一个撤销步压入命令栈，
  与增删 / 生成共用同一 `SceneSnapshotCommand`；显式命令帧设 `suppressEditGesture_` 抑制手势重复记录，避免空命令与重复撤销

> 说明：玩法系统的纯逻辑核心（A\* / 粒子模拟 / 命令栈）均有单测覆盖（`src/tests/test_main.cpp`），
> 沙箱无 GPU 时仍可验证；GPU 公告板渲染与编辑器可视化需在带显示的设备上运行。

**场景**
- `SceneObject` 实例化场景列表（位置/缩放/色调/自转速度/网格引用），共用立方体网格
- **变换层级（Transform Hierarchy）**：`Transform.h` 组件化 TRS（平移/四元数旋转/非均匀缩放 + 父节点索引），
  局部→世界矩阵级联（`LocalToWorldMatrix`）、世界位置查询（`WorldPosition`）、
  世界空间 AABB 计算（`WorldAabb`，8角点变换保守包含），为 glTF/ECS 骨架与场景树打基础
- **ECS 组件系统**：`core/ecs.h` 轻量 EnTT 风格 ECS（纯CPU、仅标准库、可离线单测）。
  `Entity` 为 32 位打包句柄（20 位 index + 12 位 version，index 0 保留为空实体哨兵），
  实体销毁后重建自动复用 index 并递增 version 使旧句柄失效；`Registry` 管理生命周期，
  `SparseSet` 组件池（dense+sparse 稀疏集，O(1) 增删查，swap-pop 保持紧凑）；
  `View<T...>::Each(fn)` 一次迭代同时拥有全部指定组件的实体，供未来场景实体化与数据驱动更新使用
- **资源缓存（AssetManager / AssetCache）**：`core/AssetCache.h` 引用计数的 LRU 资源缓存
  （纯CPU、仅标准库、可离线单测）。`AssetCache<T>` 以路径为键、工厂按需加载，
  命中刷新 MRU 端（list 前端）；超软容量时从 LRU 端（list 后端）只淘汰**未被外部引用**
  的条目（`use_count()==1`），调用方持有句柄期间条目不被淘汰；`Get` 为不刷新的查询，
  工厂返回 nullptr 视为加载失败不缓存。`AssetManager` 按 type_index 统一托管多类型缓存
  （`Load<T>/Get<T>/Remove<T>`），为纹理/网格/着色器等资源去重复用打基础
- 圆环体模型（`assets/models/torus.obj`）演示外部网格加载，文件缺失时自动剔除
- 轨道相机：左键拖拽旋转、滚轮缩放、**WASD + QE 平移**
- 标题栏实时 FPS 与 MSAA 状态显示

**编辑器（Dear ImGui）**
- `src/editor`：EditorOverlay（UI渲染通道/后端管理）+ EditorPanel（界面逻辑）
- 中文界面（自动加载系统微软雅黑字体）
- 面板：渲染统计（FPS/帧耗时/GPU/MSAA/三角形数）、光照参数（方向/颜色/强度/环境光/IBL）、
  相机FOV、点光源管理（位置/颜色/强度/半径，增删至多8盏）、场景物体属性
  （位置/缩放/色调/金属度/粗糙度/自转速度）直接编辑运行时数据
- **物体拾取**：左键点击场景物体选中（射线-AABB），右键取消，选中项在面板高亮
- **Gizmo 变换手柄**：选中物体后显示 X/Y/Z 三轴屏幕手柄（纯逻辑数学，可离线单测），
  平移/旋转两种模式（编辑器"场景"面板切换），左键拖拽手柄直接改物体的世界位置/欧拉旋转，
  与自转叠加；手柄绘制于 ImGui 前景层，拖拽中的轴加粗高亮
- **响应式停靠布局**：内置 ImGui 为 master 分支（无 DockSpace API），以"边缘吸附 + 响应式重排"
  模拟停靠观感，"渲染统计"面板可切换 经典（四角分散）/ 紧凑（左侧单列）两预设，随窗口尺寸自适应
- UI渲染通道：场景通道之后 LOAD 叠加绘制，覆盖层独立重建随窗口变化

> 说明：PBR 环境光来自程序化天空的 IBL；纯金属在完全无光源角度仍偏暗属预期，
> HDR 环境贴图资源加载已列入 Roadmap。

**引擎架构**
```
src/
├── core/       基础设施：分级日志、VK_CHECK 异常校验、VkResult/内存类型/格式工具、
│                ECS（ecs.h：Entity/SparseSet/Registry/View 组件系统）、
│                AssetCache/AssetManager（引用计数 LRU 资源缓存）
├── platform/   Window：GLFW RAII 封装（键盘/鼠标/滚轮、光标增量、尺寸变化标记）
├── render/     Vulkan 封装层：
│                Context（实例/设备/队列）→ Swapchain → RenderPass → Renderer
│                （帧循环/MSAA/深度附件/重建）、Buffer、Image、Texture、Mesh、
│                GraphicsPipeline、DescriptorManager、UboBuffer（全部 RAII）
├── scene/      OrbitCamera（轨道相机）、CubeMesh（内置网格）、ObjModel（OBJ加载）、
│                GltfLoader（glTF2.0加载+骨骼数据）、Skeleton（CPU骨骼蒙皮）、
│                Animation（CPU动画播放器+播放状态+混合）、
│                SkinnedMesh（骨骼动画端到端管线）、Scene（场景物体定义）、
│                AnimationStateMachine（状态机）、SceneSerializer（JSON序列化）、Picking（射线拾取）
├── editor/     EditorOverlay（ImGui覆盖层与UI渲染通道）、EditorPanel（编辑器面板）、Gizmo（变换手柄）
├── audio/      AudioEngine（miniaudio 封装）、Sound（音效/音乐）
├── physics/    PhysicsEngine（ReactPhysics3D 封装）、PhysicsTypes（刚体/关节/形状类型）
├── game/       NavGrid（A* 导航网格）、ParticleSystem（粒子模拟）、CommandStack（撤销重做）
├── app/        Application（资源装配 + 主循环 + 输入/UI/录制回调，原 main.cpp 过程式代码重构为类）
└── main.cpp    入口：创建 Application 并运行
```
所有 Vulkan 资源 RAII 管理，失败路径通过异常统一回收；`VK_CHECK` 宏记录 VkResult 后抛出。

**交互**
- 鼠标左键拖拽：环绕旋转视角；滚轮：缩放距离；WASD / QE：平移相机目标点
- 左键单击：拾取场景物体（编辑器高亮），右键取消选择
- 右键点击物理命中物体：在命中点生成动态立方体（可撤销）
- 标题栏显示实时 FPS 与 MSAA 采样数
- 场景内立方体以各自速度自转（验证推送常量与逐帧 UBO 更新）
- **P**：在相机注视点触发粒子爆发
- **Ctrl+Z** / **Ctrl+Y**：撤销 / 重做场景编辑（添加 / 删除物体、生成物理立方体）
- **F5** / **F9**：保存 / 加载场景（JSON）
- 编辑器面板"渲染统计"中勾选：导航网格 (A\*) 可视化、AI 导航代理 (NavAgent) 开关、粒子系统开关、撤销 / 重做按钮；
  "渲染统计"中还可切换延迟渲染 / 后处理 Bloom / SSAO / SSR、物理模拟与调试线框、角色控制器等

## 性能剖析与工程化（2026 升级）

- **GPU 时间戳性能剖析**：基于 Vulkan 核心时间戳查询（`VK_QUERY_TYPE_TIMESTAMP`），
  在阴影预通道 / 场景通道 / UI 通道边界写入时间戳并回读，编辑器"渲染统计"面板实时显示
  整帧与各阶段 GPU 耗时（毫秒）。设备不支持时自动禁用。
- **色调映射曝光控制**：`LightUBO` 新增 `exposure` 字段，编辑器"光照"面板可实时调节 HDR→LDR 前的整体曝光。
- **单元测试**：`src/tests` 下 `BigHeroTests` 目标覆盖场景/网格/UBO 布局等纯逻辑，
  以及升级 17–20 的玩法核心——A\* 导航（`NavGrid`）、AI 导航代理（`NavAgent`）、粒子模拟（`ParticleSystem`）、
  发射器预设（`EmitterPresets`）、撤销重做命令栈（`CommandStack`）、场景快照命令（`SceneCommand`）、
  色调分级（`ColorGrading`），CI 自动构建运行。
- **CI**：`.github/workflows/ci.yml` 在 Windows + VS2022 + Vulkan SDK 环境下自动编译引擎与测试。
- **代码规范**：`.clang-format`（Microsoft 4 空格、K&R 花括号）/ `.clang-tidy`（bugprone/modernize/performance）/ `.editorconfig`。
- **健壮性修复**：
  - 修复标题栏帧耗时显示偏差（漏乘 1000，原值偏小约 10 倍）。
  - 交换链格式变化触发渲染通道重建后，通过 `SetRenderPassRecreateCallback` 自动重建依赖主渲染通道
    的场景/天空盒管线，消除潜在的失效管线崩溃。

## 构建要求

- Windows 10/11
- CMake ≥ 3.20
- Visual Studio 2022（含 MSVC v143）
- [Vulkan SDK](https://vulkan.lunarg.com/)（含 glslc；SDK 目录自动探测，
  也可用 `-DVULKAN_SDK_PATH=<路径>` 显式指定）

## 构建与运行

```bash
# 使用预设（推荐）
cmake --preset win-x64-debug
cmake --build --preset win-x64-debug

# 或手动
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

产物输出到 `build/bin/<配置>/`，构建时自动用 glslc 编译 `shaders/*.glsl` 并连同
`assets/` 拷贝到可执行文件旁，直接运行即可：

```bash
./build/bin/Debug/BigHeroGameEngine.exe
```

在 Visual Studio 中打开 `build/BigHeroGameEngine.sln` 调试时，调试工作目录已配置为输出目录。

## 着色器约定

`shaders/` 下每个 `.glsl` 文件在构建期自动编译为同名 `.spv`，阶段由文件名推断：
`vert` / `frag` / `comp` / `geom` / `tesc` / `tese`。

当前描述符布局（片段阶段为主）：
- set 0 binding 0：CameraUBO（视图/投影，顶点阶段）
- set 1 binding 0：LightUBO（方向光+点光源数组+光照视空间矩阵+阴影/IBL参数）
- set 1 binding 1~3：反照率纹理 / 法线贴图 / 阴影贴图
- set 1 binding 4~7：环境立方图 / 辐照度立方图 / 预滤波立方图 / BRDF LUT
- set 1 binding 8：点光源立方体阴影贴图（`samplerCube`，片段阶段）
- set 2 binding 0：PointShadowUBO（6 个面视投影矩阵，顶点阶段）
- 推送常量：模型矩阵 + 材质参数（tint/metallic/roughness，顶点+片段阶段）

## Roadmap

- [x] ~~stb_image 贴图资源加载（assets/）~~
- [x] ~~MSAA 抗锯齿~~
- [x] ~~纹理 mipmap 链~~
- [x] ~~OBJ 模型加载与 Mesh 网格资源~~
- [x] ~~`src/editor` 编辑器面板（Dear ImGui）~~
- [x] ~~法线贴图与 PBR 材质（Cook-Torrance）~~
- [x] ~~多光源 PBR + 方向光阴影贴图（PCF）~~
- [x] ~~IBL 环境光照（辐照度/预滤波/BRDF LUT + 天空盒）~~
- [x] ~~编辑器物体拾取（射线-AABB点击选择）~~
- [x] ~~GPU 时间戳性能剖析（阴影/场景/UI 阶段耗时）~~
- [x] ~~色调映射曝光实时控制~~
- [x] ~~单元测试 + CI 自动构建~~
- [x] ~~点光源阴影（立方体阴影贴图）~~
- [x] ~~视锥剔除（前向剔除优化）~~
- [x] ~~实例化渲染（instancing）~~
- [x] ~~HDR 环境贴图资源加载（.hdr RGBE + 等距柱状转立方图）~~
- [x] ~~glTF 2.0 静态网格加载（JSON + base64 内嵌缓冲，属性/索引/多 primitive）~~
- [x] ~~glTF 骨骼蒙皮（nodes 层级 + skins 逆绑定 + JOINTS/WEIGHTS + CPU SkinVertices）~~
- [x] ~~glTF 动画系统（animations 解析 + AnimationPlayer 插值采样）~~
- [x] ~~骨骼动画端到端管线（SkinnedMesh + AnimationState + AnimationBlender）~~
- [x] ~~GPU 蒙皮（顶点着色器骨骼矩阵调色板，替代 CPU 蒙皮以支持大规模角色）~~
- [x] ~~延迟渲染通道（GBuffer MRT + 输入附件延迟光照，编辑器实时切换）~~
- [x] ~~变换层级（Transform Hierarchy：TRS + 父级级联 + 世界AABB）~~
- [x] ~~ECS 组件系统（Entity/SparseSet/Registry/View）~~
- [x] ~~编辑器深化：Gizmo（三轴屏幕手柄 + 平移/旋转）、响应式停靠布局~~
- [x] ~~资源缓存（AssetManager / AssetCache：引用计数 LRU）~~
- [x] ~~VMA 显存分配器（GpuAllocator 块内子分配 + 相邻合并）~~
- [x] ~~物理引擎（ReactPhysics3D：静态/动态/运动学刚体、角色控制器、射线检测、关节系统）~~
- [x] ~~音频系统（miniaudio：BGM 自动加载、主音量调节）~~
- [x] ~~动画状态机（Idle/Walk/Jump 参数化过渡，与角色控制器联动）~~
- [x] ~~后处理（Bloom + ACES 色调映射）、SSAO、SSR（编辑器实时开关）~~
- [x] ~~场景序列化（SceneSerializer：JSON + F5 保存 / F9 加载）~~
- [x] ~~应用架构重构（Application 类：资源装配 + 主循环 + 录制回调）~~
- [x] ~~玩法系统·导航（NavGrid A* 寻路：启发式/8邻接/切角防护 + 编辑器可视化）~~
- [x] ~~玩法系统·粒子（ParticleSystem CPU 模拟 + ParticleBuffer GPU 实例化公告板 + Alpha 混合管线）~~
- [x] ~~玩法系统·撤销重做（CommandStack 命令栈 + 场景增删可撤销）~~
- [x] ~~玩法系统·AI 巡逻（NavAgent：基于 NavGrid A* 路径移动 + 环形巡逻队列）~~
- [x] ~~玩法系统·粒子编辑器（EmitterPresets 预设 + 编辑器实时调参）~~
- [x] ~~玩法系统·属性编辑撤销（物体位置/材质等连续编辑纳入命令栈，SceneCommand 纯逻辑 + 编辑器手势）~~
- [ ] 后处理扩展：景深（DoF）
- [ ] 后处理扩展：运动模糊（Motion Blur）
- [x] ~~后处理扩展：色调分级（Color Grading，纯逻辑 GradeColor + GPU 合成接入）~~
- [ ] HDR 环境贴图资源加载（真实 .hdr 资源替换程序化天空）
- [ ] ECS 场景实体化（以 ECS 驱动场景物体与组件化更新）
- [ ] 移动端 / Linux 跨平台支持
