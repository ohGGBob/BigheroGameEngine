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
│                Scene（场景物体定义）
├── editor/     EditorOverlay（ImGui覆盖层与UI渲染通道）、EditorPanel（编辑器面板）
└── main.cpp    薄编排层：装配资源 + 主循环
```
所有 Vulkan 资源 RAII 管理，失败路径通过异常统一回收；`VK_CHECK` 宏记录 VkResult 后抛出。

**交互**
- 鼠标左键拖拽：环绕旋转视角；滚轮：缩放距离；WASD / QE：平移相机目标点
- 左键单击：拾取场景物体（编辑器高亮），右键取消选择
- 标题栏显示实时 FPS 与 MSAA 采样数
- 场景内立方体以各自速度自转（验证推送常量与逐帧 UBO 更新）

## 性能剖析与工程化（2026 升级）

- **GPU 时间戳性能剖析**：基于 Vulkan 核心时间戳查询（`VK_QUERY_TYPE_TIMESTAMP`），
  在阴影预通道 / 场景通道 / UI 通道边界写入时间戳并回读，编辑器"渲染统计"面板实时显示
  整帧与各阶段 GPU 耗时（毫秒）。设备不支持时自动禁用。
- **色调映射曝光控制**：`LightUBO` 新增 `exposure` 字段，编辑器"光照"面板可实时调节 HDR→LDR 前的整体曝光。
- **单元测试**：`src/tests` 下 `BigHeroTests` 目标覆盖场景/网格/UBO 布局等纯逻辑，CI 自动构建运行。
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
- [ ] glTF 动画通道插值（animations/samplers，骨骼动画播放）
- [ ] 延迟渲染通道
- [x] ~~变换层级（Transform Hierarchy：TRS + 父级级联 + 世界AABB）~~
- [x] ~~ECS 组件系统（Entity/SparseSet/Registry/View）~~
- [ ] 编辑器深化：Gizmo、停靠布局
- [x] ~~资源缓存（AssetManager / AssetCache：引用计数 LRU）~~
- [ ] VMA 显存分配器（显存 GPU 侧资源管理）
