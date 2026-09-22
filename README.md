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
- **级联阴影贴图 CSM**：方向光 4 级联 2x2 深度图集——实用分割法轴向视深分带、逐级联视锥切片拟合
  光视正交矩阵（纹素对齐防闪烁）、单渲染通道逐级联 viewport/scissor 图集绘制；着色器按视深选级 +
  级间 20% 渐变混合 + PCF 核子块收拢防跨块混叠，前向/延迟/glTF 透明路径统一接入，
  近处阴影清晰、远处无锯齿闪烁
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
- **glTF PBR 纹理映射与材质系统**：baseColor/normal/metallicRoughness 贴图入纹理池
  （set 1 binding 9 纹理数组）+ 逐 primitive 材质批次绘制，前向/延迟/阴影统一接入
  （演示资产 `assets/models/model.gltf`）；透明/自发光材质——alphaMode MASK 裁剪 /
  BLEND 深度只读排序混合 / emissive 加性叠加，GBuffer 深度 STORE + 透明叠加 Pass
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
- 集成 ReactPhysics3D v0.10.2 物理引擎（FetchContent 自动拉取，缓存至构建目录），
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
- **动画编辑器整合（升级 24）**：编辑器动画面板与 `AnimationStateMachine` 双向绑定——播放/暂停、
  时间轴滑条 seek、速度/循环调节、状态属性写回与强制过渡；根节点动画姿态增量驱动 glTF 实例矩阵

**后处理 / SSAO / SSR / TAA**
- **后处理**：Bloom（亮部提取 + 高斯模糊）+ ACES 色调映射（仅前向模式，编辑器开关）
- **色调分级 Color Grading（升级 21）**：纯逻辑核心 `render/ColorGrading.h`（`GradeColor`：gain/lift → 伽马 →
  对比度 → 饱和度，ASC CDL 风格，可离线单测）与 GPU 合成阶段分级（ACES 之后）同源公式；编辑器"后处理 Bloom"下展开
  "色调分级 (Color Grading)"节点，实时调饱和度 / 对比度 / 暗部提升 / 增益 / 伽马。仅前向模式（后处理关闭时不影响画面）
- **景深 Depth of Field（升级 22）**：独立景深 Pass。先将 MSAA 深度图还原为线性深度（R32F 单采样图），再用黄金角圆盘采集
  （gather）按弥散圆（CoC）虚化场景颜色；前景/背景均产生合理虚化。仅 MSAA 路径生效（引擎默认 4x/2x MSAA）。编辑器"后处理 Bloom"
  下展开"景深 (Depth of Field)"节点：开关 / 对焦距离 / 光圈强度 / 最大模糊，实时调参。默认关闭，开启后处理不影响画面。
- **相机运动模糊 Motion Blur（升级 23）**：独立运动模糊 Pass（紧随景深之后）。用当前帧 MSAA 深度重建 NDC 坐标，
  以重投影矩阵 `prevVP × inverse(currVP)` 把像素映射到上一帧屏幕 UV，得速度向量后沿轨迹方向多次累积采样形成拖尾。
  重投影矩阵单 `mat4`（64B）含 `inverse(currVP)`，与 3 个 float 参数共存（push constant 共 80B，远低于 128B 上限）。
  仅 MSAA 路径生效；关闭时着色器直通景深输出（始终运行以稳定产出供 bloom 读取）。编辑器"后处理 Bloom"下展开
  "运动模糊 (Motion Blur)"节点：开关 / 拖尾强度 / 最大模糊 / 采样数，实时调参。默认关闭，开启后处理不影响画面。
- **体积雾 Volumetric Fog（升级 25）**：合成 Pass 16 步光线步进高度雾——指数高度密度分布 +
  Henyey-Greenstein 相函数前向散射 + 像素抖动去条带，线性深度图确定射线终点；编辑器"后处理 Bloom"下展开
  "体积雾 (Volumetric Fog)"节点：开关 / 密度 / 高度衰减 / 基准高度 / 阳光散射 / 雾染色，实时调参
- **自动曝光 + 电影化后处理（升级 26）**：Eye Adaptation——场景 → 64² 对数亮度 → 8² → 1x1 盒式收敛 +
  1x1 ping-pong 指数趋近亮度适应，合成 Pass 按键值/均值反推曝光并钳制；暗角（径向平滑）与胶片颗粒
  （时变加性噪声）作用于显示参考空间；编辑器实时开关 + 键值 / 适应速度 / 暗角强度 / 半径 / 颗粒参数面板
- **雾效阴影采样 + 体积光 God Rays（升级 27）**：雾光线步进点复用场景 CSM 级联 UBO 与 2x2 深度图集，
  逐点级联投影 + 2x2 PCF 遮挡判定，阴影处散射削减——遮挡体在雾中投出丁达尔光柱；步进数运行时
  三档（16/32/64）编码进 push constant 小数分量；编辑器实时开关 + 步进质量档
- **TAA 时间抗锯齿（升级 28）**：全分辨率 ping-pong 历史链 + Halton(2,3) 8 相位亚像素抖动投影
  （OrbitCamera z→x/y 系数注入，与着色器 "+jitter 还原未抖动 NDC" 互逆）+ MSAA 深度重投影
  （prevVP × inverse(currVP)）+ 3x3 邻域 YCoCg AABB 钳制抗 ghosting + 自适应历史混合（上限 0.95）；
  平滑几何锯齿、SSAO 噪点与雾抖动颗粒；仅前向 MSAA 路径，bright/composite 场景源每帧按开关重定向，
  开关切换/窗口重建自动重置历史；编辑器"TAA 时间抗锯齿"节点：开关 / 历史权重滑杆
- **SSAO**：半分辨率环境光遮蔽（仅延迟模式，编辑器开关）
- **SSR**：半分辨率屏幕空间反射（ray march + 高斯模糊，仅延迟模式，编辑器开关）

**场景序列化**
- `SceneSerializer`：场景（物体 / 点光源 / 方向光 / 相机 FOV）**MsgPack 二进制序列化**
  （`SerializeSceneToMsgPack` 紧凑字节流，体积与读写性能优于 JSON；JSON 分支保留）
- **F5 保存 / F9 加载**（边沿检测防重复触发），文件 `scene.json`
  （历史文件名保留，内容已是二进制格式）

**应用架构（Application）**
- `app/Application` 类：资源装配（窗口 / 上下文 / 渲染器 / 音频 / 物理 / 编辑器）
  + 主循环（`Run()`：逐系统 `UpdateXxx` → `DrawFrame` 录制回调）+ 输入 / UI / 录制回调
- **跨平台窗口层（platform/）**：`Window` 抽象基类 + 双后端——GlfwWindow（桌面 Win/Linux/macOS）
  与 AndroidWindow（NativeActivity native_app_glue：触摸→键鼠映射，单指=指针+左键、双指竖滑=滚轮、
  外接键盘 AKEYCODE 映射；APK 资产落地内部存储；Vulkan Android surface）；
  Win/Linux/macOS/Android 四平台 CMake 预设 + CI android 编译校验
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

> 说明：玩法系统的纯逻辑核心（A\* / 粒子模拟 / 命令栈）均有单测覆盖（`src/tests/test_gameplay.cpp`），
> 沙箱无 GPU 时仍可验证；GPU 公告板渲染与编辑器可视化需在带显示的设备上运行。

**场景**
- `SceneObject` 实例化场景列表（位置/缩放/色调/自转速度/网格引用），共用立方体网格
- **变换层级（Transform Hierarchy）**：`Transform.h` 组件化 TRS（平移/四元数旋转/非均匀缩放 + 父节点索引），
  局部→世界矩阵级联（`LocalToWorldMatrix`）、世界位置查询（`WorldPosition`）、
  世界空间 AABB 计算（`WorldAabb`，8角点变换保守包含），为 glTF/ECS 骨架与场景树打基础；
  另提供批量接口 `ComputeAllWorldMatrices`：单趟 O(n) 求整层世界矩阵（父世界×子局部逐级联、
  记忆化复用），支持任意存储顺序并对悬空父索引安全回退，替代逐节点 O(n·深度) 递归
- **ECS 组件系统**：`core/ecs.h` 轻量 EnTT 风格 ECS（纯CPU、仅标准库、可离线单测）。
  `Entity` 为 32 位打包句柄（20 位 index + 12 位 version，index 0 保留为空实体哨兵），
  实体销毁后重建自动复用 index 并递增 version 使旧句柄失效；`Registry` 管理生命周期，
  `SparseSet` 组件池（dense+sparse 稀疏集，O(1) 增删查，swap-pop 保持紧凑）；
  `View<T...>::Each(fn)` 一次迭代同时拥有全部指定组件的实体，供未来场景实体化与数据驱动更新使用。
  注册表增强：`TryGet`（缺失返回 nullptr 的安全读取）、`Count<T>`/`EntityCount` 统计、
  `Clear<T>`/`ClearAllComponents` 组件清空（实体保持存活）、`SparseSet::Reserve` 预分配；
  `Has`/`Remove` 等只读/防御操作不再对未注册组件类型产生副作用（不创建空池）
- **ECS 场景实体化（EcsScene）**：场景物体迁移为 Registry 实体 + 组件（Transform/Renderable/Spin/
  PhysicsBody/PhysicsRef），组件化更新系统（自转 Spin.angle 积分）与稳定序销毁；
  LoadPacket/BuildPacket/SyncFromPacket 与 SceneObject 包双向投影——渲染/编辑器/Gizmo/序列化/物理
  既有路径零改动消费包投影，编辑器修改经 SyncSceneEdits 写回；
  6 个单测覆盖生命周期/投影往返/自转/销毁/读档/物理映射
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

> 说明：PBR 环境光来自程序化天空的 IBL（HDR 环境贴图加载已支持，见"渲染"特性）；纯金属在完全无光源角度仍偏暗属预期。

**引擎架构**
```
src/
├── core/       双命名空间基础库：
│                ① BigHero::Core 基础设施——分级日志、VK_CHECK 异常校验、VkResult/内存类型/格式工具、
│                ECS（ecs.h：Entity/SparseSet/Registry/View 组件系统）、
│                AssetCache/AssetManager（引用计数 LRU 资源缓存）、JobSystem 线程池、FrameProfiler
│                ② bighero:: 基础积木块（engine foundation toolkit，2026-09 两轮最小清理后 72 个自包含头文件，
│                全部经 BigHeroHeaderCheck 单 TU 自包含编译检查）——
│                数学（Vector2/3/4、Matrix4、Quaternion、Mathf）、几何（AABB、Plane3、Sphere3、Triangle、
│                视锥、BVH/KDTree）、曲线动画（CurveKey、FloatCurve、ColorCurve、EasingCurve、Bezier/Spline）、
│                噪声（Perlin/Simplex/Value/FBM/Ridged/DomainWarp）、序列化 IO（BinaryReader/Writer、
│                CRC32、Base64、BitStream/BitVector）、容器与并发（RingBuffer、ObjectPool、BinaryHeap、
│                EventBus、Delegate）、渲染描述符（ShaderModule/ShaderStageFlag/Material 等纯数据结构）
├── platform/   Window 抽象窗口层（Vulkan surface/实例扩展/输入统一接口）：
│                GlfwWindow（桌面 Win/Linux/macOS 后端：键盘/鼠标/滚轮、光标增量、尺寸变化标记）、
│                android/AndroidWindow（NativeActivity 后端：触摸→键鼠映射、双指滚轮、APK 资源落地）
├── render/     Vulkan 封装层：
│                Context（实例/设备/队列 + MemoryPools 双池显存子分配门面）→ Swapchain → RenderPass →
│                Renderer（帧循环/渲染图/MSAA/深度附件/重建，按职责拆分 4 个翻译单元：
│                Renderer.cpp 主帧循环+渲染图、Renderer_FrameResources.cpp 帧资源与交换链重建、
│                Renderer_Deferred.cpp 延迟几何/光照/透明三通道、Renderer_PostFx.cpp 后处理/SSAO/SSR/合成）、
│                GpuAllocator（块内子分配 + 相邻合并）、Buffer、Image、Texture、Mesh、
│                GraphicsPipeline、DescriptorManager、UboBuffer（全部 RAII）
├── scene/      OrbitCamera（轨道相机）、CubeMesh（内置网格）、ObjModel（OBJ加载）、
│                GltfLoader（glTF2.0加载+骨骼数据）、Skeleton（CPU骨骼蒙皮）、
│                Animation（CPU动画播放器+播放状态+混合）、
│                SkinnedMesh（骨骼动画端到端管线）、Scene（场景物体定义）、
│                AnimationStateMachine（状态机）、SceneSerializer（JSON序列化）、Picking（射线拾取）、
│                EcsScene（ECS 场景实体化）、Transform（变换层级）、Camera/FirstPersonCamera、
│                PersonHost（程序化人物）、AvatarRetarget（骨骼动画重定向）
├── editor/     EditorOverlay（ImGui覆盖层与UI渲染通道）、EditorPanel（编辑器面板）、Gizmo（变换手柄）、
│                Hierarchy/Inspector/Project/BuildSettings 面板与模型、ScriptFieldUndo
├── ui/         运行时 UI 系统（0.19）：UiRuntime/UiModel（数据模型）、UiRenderer（渲染）、
│                UiFontAtlas/UiFontCore（字体图集）
├── audio/      AudioEngine（miniaudio 封装）、Sound（音效/音乐）
├── physics/    PhysicsEngine（ReactPhysics3D 封装）、PhysicsTypes（刚体/关节/形状类型）
├── game/       NavGrid（A* 导航网格）、ParticleSystem（粒子模拟）、CommandStack（撤销重做）、
│                FpController（第一人称陆行控制器：重力/跳跃/蹲伏/冲刺/碰撞滑动/台阶/头部摇晃，
│                纯 CPU 逻辑，头文件实现，可离线单测）
├── script/     C# 托管脚本宿主（0.19）：CSharpHost（dotnet 热重载宿主）、ScriptFields（字段绑定）
├── navigation/ NavMesh（网格导航，⚠️ 实验性·尚未接入生产管线）
├── app/        Application（装配编排 + Run 主循环 + 六个 Record* 录制回调 + 编辑器编排）+
│                systems/ 六个构造注入子系统：PostProcessSync（后处理参数同步 + 相机抖动）、
│                SceneIoHost（场景序列化）、AnimationHost（动画状态机）、NavHost（导航）、
│                ParticleHost（粒子，持 GPU 资源）、PhysicsHost（物理/角色控制器）
└── main.cpp    入口：创建 Application 并运行
```
所有 Vulkan 资源 RAII 管理，失败路径通过异常统一回收；`VK_CHECK` 宏记录 VkResult 后抛出。

**近期新增子系统（2026-09 下旬）**：`ui/` 运行时 UI（UiRuntime/UiRenderer/UiFontAtlas/UiModel）、
`script/` C# 托管脚本宿主（CSharpHost + dotnet 热重载）、`navigation/` NavMesh 网格导航、
`scene/` 程序化人物（PersonHost）与动画重定向（AvatarRetarget）、第一人称相机（FirstPersonCamera）、
`editor/` Hierarchy/Inspector/Project/BuildSettings 面板、资产数据库（core/AssetGuid + AssetDatabase）。
⚠️ 其中 `render/LightProbe`、`render/LodGroup`、`render/OcclusionCulling`、`navigation/NavMesh`
为**已建待接线**的实验模块（仅有头与单测、尚未接入渲染/玩法管线），详见 `UPGRADE_PLAN.md`。

**命名空间策略**：新基础模块统一使用 `bighero::` 命名空间；引擎既有模块保持
`BigHero::`（含 `BigHero::Core`/`BigHero::Scene` 等子命名空间）；`BigHero::Core`
内以 `using` 别名桥接（如 `FastRng = bighero::Random`）供渐进迁移。新代码请优先
使用 `bighero::` 基础积木块与既有引擎模块，避免在 `core/` 中新增与 GLM /
ReactPhysics3D / miniaudio 职责重复的实现。

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

## 第一人称沉浸式展示厅（CyberCity，0.19.4/0.19.5）

一个可第一人称漫游的"引擎能力展示厅"——把引擎全部特性做成可交互展台，既是演示也是回归样本。

```bash
# 加载赛博城市展示厅（第一人称漫游 + 八座引擎特性展台）
BigHeroGameEngine --scene cybercity
# 也可配合 --camera fp 直接进入第一人称
BigHeroGameEngine --scene cybercity --camera fp
```

**场景构成**（程序化生成，确定性可复现）
- 环形楼群 + 道路网格 + 天际线，逐实例自发光材质（instance 属性 `location 13`）
- 八座特性展台（各对应一项引擎能力）+ 八根霓虹灯柱（每根一盏点光源）
- 无外部资源依赖，`--screenshot` 可离线出图用于基线比对

**第一人称控制器**（`src/game/FpController.h`，纯 CPU 逻辑、可离线单测）
重力 / 跳跃 / 蹲伏 / 冲刺、碰撞滑动、台阶自动抬升、头部摇晃（head-bob）；
按 **V** 切飞行俯瞰模式（关闭重力与碰撞）。

**连续昼夜 + 氛围画廊**
- 4 套离散昼夜预设（`T` 键切换），切换时做 smoothstep 插值，1.6 s 平滑收敛而非硬跳变
- 自动昼夜循环（`O` 键开/关，18 s 推进一预设，整圈约 72 s）
- 霓虹呼吸（`NeonPulse`：确定性 hash 相位 + 慢呼吸 + 老化灯管偶发瞬暗）
- 4 套画面风格（`G` 键循环）：赛博霓虹 / 电影感 / 冷冽清晨 / 黑白侦探，
  复用色调分级 + 泛光 + 暗角 + 颗粒 + 雾密度，落地为统一的 `ApplyAtmosphere` 入口

**交互按键**

| 按键 | 功能 |
| --- | --- |
| 鼠标 | 锁定光标环视；`Esc` 释放光标 |
| `W A S D` | 移动（`Shift` 冲刺，`Ctrl` 蹲伏） |
| `Space` | 跳跃 |
| `V` | 切换飞行俯瞰模式 |
| `E` | 与展台交互（注视 + 按键） |
| `1`–`8` | 直接切换展台的引擎特性 |
| `T` | 切换昼夜预设（平滑插值） |
| `O` | 开/关自动昼夜循环 |
| `G` | 循环切换画面风格 |
| `R` | 回到出生点 |
| `H` | 显示/隐藏按键帮助 |
| `P` | 在注视点触发粒子爆发 |

HUD 含准星、交互提示、特性清单、风格/自动标记与"已体验进度条"（点亮 8 座展台）。

## 方块世界（Voxel World，0.20.0 / 0.20.1 / 0.20.3）

Minecraft 式可交互体素地形，第一人称实机可玩。**默认进入「纯游戏模式」**：编辑器面板
（人物 / 光照 / 相机 / 点光源 / 资源 / 物理）整体收起，只留准星与方块世界 HUD。

```bash
# 加载方块世界（自动以第一人称出生在地表；默认纯游戏模式）
BigHeroGameEngine.exe --scene voxel

# 需要现场调参时才展开编辑器面板（等价于运行期按 F1）
BigHeroGameEngine.exe --scene voxel --editor-ui
```

操作：**[WASD]** 移动、**[Shift]** 冲刺、**[空格]** 跳跃、**[Ctrl]** 蹲下、
**[左键]** 挖掘、**[右键]** 放置、**[1-6]** 切换手持方块（岩石 / 草地 / 泥土 / 沙 / 木头 / 树叶）、
**[** / **]** 调整流式视距（1~12 区块）、**[T]** 切换时段（正午 / 黄昏 / 夜晚 / 清晨）、
**[F]** 切换光标锁定、**[F1]** 切换编辑器面板显隐（纯游戏模式 ↔ 调参模式）、
**[V]** 飞行俯瞰、**滚轮** 调整移动速度。准星指向的方块会显示白色描边框；按住左键可连续挖掘；
入水自动切换**游泳手感**（重力降至 28%、空格上浮）与**冷蓝水下雾**。

- **地形**：16×64×16 区块；确定性整数 hash + value noise（4 八度 fBm + 山脉项）生成高度场；
  温度噪声划分草地 / 沙漠 / 雪原群系；含海平面水域、3D 噪声洞穴、程序化树木（树干 + 球形树冠）。
- **网格**：只生成朝向空气 / 透明体的暴露面；**顶点级 AO**（side1/side2/corner 三分量推导，
  亮度下限 0.55），顶点色 = 方块基色 × AO 直接乘进反照率；按对角和翻转三角划分消除 AO 插值瑕疵。
- **交互**：Amanatides & Woo **DDA 体素射线**（命中方块 + 入射面法线 + 距离）驱动挖掘 / 放置
  （基岩不可破坏，防挖穿世界）；周边实心方块实时投影为 AABB，交给既有 `FpController` 做陆行碰撞。
- **贪心网格化（0.20.1）**：网格生成改为「切片掩码 + 矩形合并」——沿 6 个方向切平面，
  先沿 u 扩宽、再沿 v 扩高，仅当方块类型与四角 AO 完全相同时才合并成大四边形。
  默认地形单区块实测 **466 面 vs 朴素 2408 面（削减 80.6%）**，整块实心区块 2496 → 6 面；
  AO 差异与异类型方块会自动阻止合并，视觉与逐面实现一致。
- **性能**：圆形视距流式加载 / 卸载；**分帧网格重建**（每帧预算 2 个区块，首屏 64 个一次建完）
  消除跨区块卡顿尖峰；按区块包围球做视锥剔除；区块网格参与 CSM 阴影投射；默认开启体积雾淡出世界边界。
- **时段光照（0.20.1）**：四档时段预设切换太阳方向与光色 / 强度 / 环境光 / 曝光、天空染色与雾色，
  每帧向目标值平滑逼近（约 0.4s 收敛），不做硬跳变。
- **性能补充（0.20.1）**：待重建区块按到玩家距离排序（近处优先）；CSM 只投影玩家 2.5 区块内的方块网格。

## 性能剖析与工程化（2026 升级）

- **GPU 时间戳性能剖析**：基于 Vulkan 核心时间戳查询（`VK_QUERY_TYPE_TIMESTAMP`），
  在阴影预通道 / 场景通道 / UI 通道边界写入时间戳并回读，编辑器"渲染统计"面板实时显示
  整帧与各阶段 GPU 耗时（毫秒）。设备不支持时自动禁用。
- **色调映射曝光控制**：`LightUBO` 新增 `exposure` 字段，编辑器"光照"面板可实时调节 HDR→LDR 前的整体曝光。
- **单元测试**：`src/tests` 下 `BigHeroTests` 目标采用自研轻量测试框架
  （`framework/test_assert.h`：`TEST_CASE` 静态注册 + `CHECK` 断言 + 逐用例汇报，
  零依赖、跨平台、与 CTest/CI 退出码约定一致）。原单体 `test_main.cpp`（2664 行）
  已拆分为按模块组织的 14 个文件——`test_core`（ECS/资源缓存/剖析器/线程池）、
  `test_foundation`（bighero:: 基础积木块：向量/矩阵/Mathf/曲线/几何/二进制序列化/容器/随机/噪声）、
  `test_scene`（场景/变换层级/Gizmo/序列化）、`test_ecs_scene`（实体化投影/自转/缓存置脏语义）、
  `test_parent_hierarchy`（层级级联/悬空父）、`test_transform_cache`（脏标记增量基准）、
  `test_assets`（MTL/glTF）、`test_animation`（动画插值/蒙皮管线/状态机）、
  `test_gameplay`（A\*/导航/粒子/命令栈）、`test_render_logic`（UBO/视锥/实例化/HDR/分配器/渲染图）
  及 core/ 运行时回归（`test_containers`/`test_utilities`/`test_memory_math`）——共 **98 个用例 / 2438 处断言**，
  CI 自动构建运行。另有 `BigHeroHeaderCheck` 目标将全部 `src/core/*.h` 编译进单一翻译单元，
  强制头文件自包含（CI Debug 构建执行）。
- **CI**：`.github/workflows/ci.yml` 共 7 个 job、覆盖 Windows（VS2022）×2 配置、Linux ×2 配置、
  macOS ×2 配置、Android（NDK 编译校验）、Linux Sanitizers（ASan+UBSan）、clang-format lint，
  发布 tag 时自动打包 Release；Linux Debug 额外在 lavapipe（软件 Vulkan）+ xvfb 下以
  `--screenshot` 真实渲染首帧并上传截图产物，同时保留 `--headless --validate-only`
  作为快速文件存在性检查。
- **代码规范**：`.clang-format`（Microsoft 4 空格、K&R 花括号）/ `.clang-tidy`（bugprone/modernize/performance）/ `.editorconfig`。
- **健壮性修复**：
  - 修复标题栏帧耗时显示偏差（漏乘 1000，原值偏小约 10 倍）。
  - 交换链格式变化触发渲染通道重建后，通过 `SetRenderPassRecreateCallback` 自动重建依赖主渲染通道
    的场景/天空盒管线，消除潜在的失效管线崩溃。

## 架构重构（2026-09）

针对"上帝对象 / 重构半成品 / 多套并行 / core/ 膨胀"四项审计问题完成的六阶段重构，
每阶段独立可构建、ctest 全绿，纯重构不改行为：

**1. 子系统拆分（原 Application.cpp 2458 行）**：删除 src/app/systems/ 15 个从未接入构建的骨架头文件，
   按职责提取 6 个真实子系统（构造注入依赖引用，不引入 ISubSystem/SystemManager 框架）：

   | 子系统 | 职责 |
   |---|---|
   | PostProcessSync | 后处理参数同步、相机 Halton 抖动、视锥矩阵交换 |
   | SceneIoHost | 场景保存 / 加载（JSON） |
   | AnimationHost | 动画状态机初始化与推进、glTF 姿态采样 |
   | NavHost | 导航网格与 AI 代理逐帧推进 |
   | ParticleHost | 粒子模拟、发射器配置、GPU 实例缓冲 |
   | PhysicsHost | 物理世界推进、刚体/关节同步、角色控制器 |

   Application 保留装配、Run 主循环、六个 Record* 录制方法与编辑器编排。

**2. Renderer 拆分（原 Renderer.cpp 1399 行 → 4 个翻译单元，类结构不变）**：
   `Renderer.cpp`（构造/析构、DrawFrame 渲染图编排、命令资源）
   / `Renderer_FrameResources.cpp`（帧资源、同步对象、交换链重建）
   / `Renderer_Deferred.cpp`（延迟几何/光照/透明三通道与 GBuffer 资源）
   / `Renderer_PostFx.cpp`（后处理 / SSAO / SSR / 合成资源）。

**3. GpuAllocator 收敛（消除裸 vkAllocateMemory）**：新增 `Render::MemoryPools` 双池门面——
   deviceLocal（256MB×8 块上限 2GB）与 hostVisible（64MB×8 块上限 512MB，块级持久映射），
   挂在 Context 上供 Buffer/Image/UboBuffer 统一子分配；`minAlign = max(bufferImageGranularity, 256)`
   使 buffer/image 安全同块；超块 / memoryTypeBits 不含池类型时自动回退独占分配路径。
   收益：交换链重建时 GBuffer/MSAA/后处理十几张全屏图反复 Create/Destroy 均在块内复用。

**4. core/ 最小清理（679 → 432 → 70 个头文件，两轮共删 609 个错位/零引用文件）**：
   Round A 错位 UI/2D（UICanvas/Sprite/Tilemap/Tween/Audio 系列 47 个）→
   Round B 错位渲染空壳（SkyboxRenderer/TemporalAA/SSAO/ShadowMap/SwapChain/Texture2D 等 72 个）→
   Round C 错位玩法物理（Terrain/物理碰撞/动画剪辑/曲线编辑器等 71 个）→
   Round D 零引用 _v2（57 个）。保留：引擎 + 测试外部引用闭包（37 个）∪ bighero:: 基础积木块；
   每轮独立构建三目标 + ctest 全绿。

**ECS 收敛铺垫**：EcsScene 已是场景权威存储，子系统均经包投影消费数据；
   渲染端逐组件抽离（Renderable 批次化）作为下一轮收敛方向。

## 构建要求

- Windows 10/11（其他平台参考下方"跨平台预设"）
- CMake ≥ 3.25
- Visual Studio 2022（含 MSVC v143）
- [Vulkan SDK](https://vulkan.lunarg.com/)（含 glslc；SDK 目录自动探测，
  也可用 `-DVULKAN_SDK_PATH=<路径>` 显式指定）

**跨平台预设**：Linux/macOS 需 CMake + Vulkan SDK + C++20 编译器（GCC 11+/Clang 14+/Xcode 15+）；
Android 需 NDK r27+ 与 Android Studio（Gradle 打包工程，NativeActivity）。

```bash
# Linux / macOS
cmake --preset linux-x64-debug    # 或 macos-arm64-debug
cmake --build --preset linux-x64-debug

# Android（libmain.so 由 Gradle 工程打包进 APK）
cmake --preset android-arm64-debug
cmake --build --preset android-arm64-debug
```

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
- [x] ~~后处理扩展：景深（DoF，深度线性化 + 黄金角圆盘采集，MSAA 路径）~~
- [x] ~~后处理扩展：运动模糊（Motion Blur，重投影矩阵 + MSAA 深度重建速度，MSAA 路径）~~
- [x] ~~后处理扩展：色调分级（Color Grading，纯逻辑 GradeColor + GPU 合成接入）~~
- [x] ~~glTF PBR 纹理映射（baseColor/normal/metallicRoughness 贴图入纹理池 set1 binding9 数组 + 逐 primitive 材质批次绘制，前向/延迟/阴影统一接入；演示资产 assets/models/model.gltf）~~
- [x] ~~glTF 透明/自发光材质（alphaMode MASK 裁剪 / BLEND 深度只读排序混合 / emissiveFactor+emissiveTexture 加性叠加，GBuffer 深度 STORE + 透明叠加 Pass，前向/延迟统一接入）~~
- [x] ~~ECS 场景实体化（`EcsScene` 权威存储：场景物体 = Registry 实体 + 组件 Transform/Renderable/Spin/PhysicsBody/PhysicsRef；
  组件化更新系统（自转 Spin.angle 积分）、稳定序销毁、LoadPacket/BuildPacket/SyncFromPacket 与 SceneObject 包双向投影——
  渲染/编辑器/Gizmo/序列化/物理既有路径零改动消费包投影，编辑器修改经 SyncSceneEdits 写回，physicsBodyIds_ 并行数组由 PhysicsRef 组件替代；6 个 ECS 单测用例覆盖生命周期/投影往返/自转/销毁/读档/物理映射）~~
- [x] ~~移动端 / Linux 跨平台支持（`platform/Window` 抽象窗口层：桌面 GLFW 后端 + Android native_app_glue 后端
  （NativeActivity libmain.so、触摸→键鼠映射、双指滚轮、APK 资源落地、Vulkan Android surface、Gradle 打包工程）；
  Win/Linux/macOS/Android CMake 预设 + CI android 编译校验 job）~~
- [x] ~~级联阴影贴图 CSM（方向光 4 级联 2x2 深度图集：实用分割法轴向视深分带 + 逐级联视锥切片拟合光视正交
  （纹素对齐防闪烁）、单渲染通道逐级联 viewport/scissor 图集绘制、着色器按视深选级 + 级间 20% 渐变混合 +
  PCF 核子块收拢防跨块混叠，前向/延迟/gTF 透明路径统一接入）~~
- [x] ~~glTF 动画编辑器整合（AnimationStateMachine 编辑器控制 API：播放/暂停、时间轴滑条、状态属性写回、强制过渡；
  编辑器动画面板与状态机双向绑定，根节点动画姿态增量驱动 glTF 实例矩阵）~~
- [x] ~~体积雾后处理（Volumetric Fog：合成 Pass 16 步光线步进高度雾——指数高度密度分布 +
  Henyey-Greenstein 相函数前向散射 + 像素抖动去条带，线性深度图定射线终点；
  编辑器实时开关 + 密度/高度衰减/基准高度/散射强度/雾染色参数面板）~~
- [x] ~~自动曝光 + 电影化后处理（Eye Adaptation：场景 → 64² 对数亮度 → 8² → 1x1 盒式收敛 + 1x1 ping-pong
  指数趋近亮度适应（1-e^(-dt·speed)），合成 Pass 按键值/均值反推曝光并钳制；暗角（径向平滑）与胶片颗粒
  （时变加性噪声）作用于显示参考空间；编辑器实时开关 + 键值/适应速度/暗角强度/半径/颗粒参数面板）~~
- [x] ~~雾效阴影采样 + 体积光 God Rays（雾光线步进点复用场景 CSM 级联 UBO 与 2x2 深度图集，逐点级联投影 +
  2x2 PCF 遮挡判定，阴影处散射降至 6% 环境项——遮挡体在雾中投出丁达尔光柱；步进数运行时三档（16/32/64）
  编码进 push constant 小数分量；编辑器实时开关 + 步进质量档）~~
- [x] ~~TAA 时间抗锯齿（全分辨率 ping-pong 历史链 + Halton(2,3) 8 相位亚像素抖动投影（OrbitCamera z→x/y 系数注入，
  与着色器 "+jitter 还原未抖动 NDC" 互逆）+ MSAA 深度重投影（prevVP × inverse(currVP)）+ 3x3 邻域 YCoCg AABB
  钳制抗 ghosting + 自适应历史混合（上限 0.95）；bright/composite 场景源每帧按开关重定向，开关切换/窗口重建
  自动重置历史直通重建；仅前向 MSAA 路径，编辑器实时开关 + 历史权重滑杆）~~
- [x] ~~架构重构 2026-09（删除 systems/ 骨架 → MemoryPools/GpuAllocator 收敛消除裸 vkAllocateMemory →
  Application 拆分 6 子系统 → Renderer 拆分 4 TU → core/ 最小清理 679→432→70 → README 同步；
  ECS 收敛——渲染端 Renderable 批次化抽离——铺垫已就绪）~~
- [x] ~~开放世界场景模板 OpenWorld（`--scene openworld`：320×320 m 世界 + 10×10 m 空间分块 +
  距离分层密度，压测引擎规模能力）~~
- [x] ~~第一人称沉浸式展示厅 CyberCity（`--scene cybercity`：程序化赛博城市 + 八座引擎特性展台 +
  第一人称陆行控制器 FpController（重力/跳跃/蹲伏/冲刺/碰撞滑动/台阶/头部摇晃/飞行俯瞰）+
  逐实例自发光材质 + 展台交互/HUD + 光标锁定；连续昼夜插值与自动循环（T/O）+
  霓虹呼吸脉冲 + 画面风格画廊（G：赛博霓虹/电影感/冷冽清晨/黑白侦探））~~
- [x] ~~方块世界 Voxel World（`--scene voxel`：区块化体素地形 + 噪声群系/水域/洞穴/植被 +
  面剔除与顶点 AO 网格合并 + DDA 射线挖掘/放置 + 圆形视距流式加载 + 分帧重建与视锥剔除）~~
