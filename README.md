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
- **法线贴图**：顶点切线（解析/通用Lengyel计算）构建 TBN，切线空间法线扰动，
  配套程序化生成的法线图资源（与反照率贴图同一高度场）
- 推送常量：逐物体模型矩阵 + 材质参数（tint/metallic/roughness），一份网格驱动多实例
- **Mesh 网格资源类**（VBO+IBO RAII 封装，子范围索引化绘制）
- **极简 OBJ 模型加载**（v/vt/vn / 多边形扇形三角化 / 负索引 / 角点去重）

**场景**
- `SceneObject` 实例化场景列表（位置/缩放/色调/自转速度/网格引用），共用立方体网格
- 圆环体模型（`assets/models/torus.obj`）演示外部网格加载，文件缺失时自动剔除
- 轨道相机：左键拖拽旋转、滚轮缩放、**WASD + QE 平移**
- 标题栏实时 FPS 与 MSAA 状态显示

**编辑器（Dear ImGui）**
- `src/editor`：EditorOverlay（UI渲染通道/后端管理）+ EditorPanel（界面逻辑）
- 中文界面（自动加载系统微软雅黑字体）
- 面板：渲染统计（FPS/帧耗时/GPU/MSAA/三角形数）、光照参数（方向/颜色/强度/环境光）、
  相机FOV、场景物体属性（位置/缩放/色调/**金属度/粗糙度**/自转速度）直接编辑运行时数据
- UI渲染通道：场景通道之后 LOAD 叠加绘制，覆盖层独立重建随窗口变化

> 说明：当前 PBR 为单方向光 + 常数环境光，无 IBL 环境探针，因此纯金属物体的背光面
> 会偏暗（仅镜面反射项），属预期表现；IBL 已列入 Roadmap。

**引擎架构**
```
src/
├── core/       基础设施：分级日志、VK_CHECK 异常校验、VkResult/内存类型/格式工具
├── platform/   Window：GLFW RAII 封装（键盘/鼠标/滚轮、光标增量、尺寸变化标记）
├── render/     Vulkan 封装层：
│                Context（实例/设备/队列）→ Swapchain → RenderPass → Renderer
│                （帧循环/MSAA/深度附件/重建）、Buffer、Image、Texture、Mesh、
│                GraphicsPipeline、DescriptorManager、UboBuffer（全部 RAII）
├── scene/      OrbitCamera（轨道相机）、CubeMesh（内置网格）、ObjModel（OBJ加载）、
│                Scene（场景物体定义）
├── editor/     EditorOverlay（ImGui覆盖层与UI渲染通道）、EditorPanel（编辑器面板）
└── main.cpp    薄编排层：装配资源 + 主循环
```
所有 Vulkan 资源 RAII 管理，失败路径通过异常统一回收；`VK_CHECK` 宏记录 VkResult 后抛出。

**交互**
- 鼠标左键拖拽：环绕旋转视角
- 滚轮：缩放距离
- WASD / QE：平移相机目标点
- 标题栏显示实时 FPS 与 MSAA 采样数
- 场景内立方体以各自速度自转（验证推送常量与逐帧 UBO 更新）

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

当前描述符布局：
- set 0 binding 0：CameraUBO（视图/投影，顶点阶段）
- set 1 binding 0：LightUBO（片段阶段）
- set 1 binding 1：漫反射纹理合并采样器（片段阶段）
- 推送常量：模型矩阵 + 顶点色乘数（顶点阶段，逐物体变换）

## Roadmap

- [x] ~~stb_image 贴图资源加载（assets/）~~
- [x] ~~MSAA 抗锯齿~~
- [x] ~~纹理 mipmap 链~~
- [x] ~~OBJ 模型加载与 Mesh 网格资源~~
- [x] ~~`src/editor` 编辑器面板（Dear ImGui）~~
- [x] ~~法线贴图与 PBR 材质（Cook-Torrance）~~
- [ ] IBL 环境光照（预滤波环境贴图）
- [ ] glTF 加载（骨骼动画）
- [ ] 延迟渲染通道 / 阴影贴图
- [ ] 场景系统深化（变换层级 / 组件化）与 ECS
- [ ] 编辑器深化：Gizmo、物体拾取、停靠布局
