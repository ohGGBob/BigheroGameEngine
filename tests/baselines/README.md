# 成像回归基线（P1-3）

引擎截图回归的基线存放目录。比对工具：`tools/compare_images.py`（纯标准库，
逐像素亮度容差 `--tolerance` + 差异像素占比上限 `--max-ratio`，退出码 0=PASS / 1=超差 / 2=输入错误）。

```text
tests/baselines/
├── gpu/       本地 GPU 回归基线（AMD 780M，1600x900，--no-ui 纯场景）
└── lavapipe/  CI 软件光栅基线（自举式，见下文；采纳前为空）
```

## 核心原则

- **基线全部由 `--no-ui` 生成（纯场景）**：编辑器覆盖层不进入画面，
  因此**编辑器 UI 改动不影响这些基线**。只有场景内容 / 渲染 / 后处理的
  **有意改动**才需要重新生成基线（命令见下）。
- **GPU 基线与 lavapipe 基线不可混用**：软件光栅与硬件成像不同，各自成体系。
- 基线工作流要求每次渲染命令完全一致（分辨率 / 场景 / 后处理开关 / 帧数）。

## GPU 基线（本地回归）

### 生成命令

从构建输出 bin 目录（含 `assets/`、`shaders/`）执行，例如
`out/build/x64-Release/bin/Release/`：

```sh
ENGINE=./BigHeroGameEngine.exe   # Linux 下换成 ./BigHeroGameEngine
$ENGINE --no-ui                          --screenshot <repo>/tests/baselines/gpu/default-nopp.png
$ENGINE --no-ui --post-process           --screenshot <repo>/tests/baselines/gpu/default-pp.png
$ENGINE --no-ui --scene slice            --screenshot <repo>/tests/baselines/gpu/slice-nopp.png
$ENGINE --no-ui --scene slice --post-process --screenshot <repo>/tests/baselines/gpu/slice-pp.png
```

当前清单（2026-09-19 生成，1600x900）：

| 文件 | 命令组合 | mean_luma |
|---|---|---|
| `default-nopp.png` | `--no-ui` | 103.47 |
| `default-pp.png` | `--no-ui --post-process` | 116.24 |
| `slice-nopp.png` | `--no-ui --scene slice` | 129.57 |
| `slice-pp.png` | `--no-ui --scene slice --post-process` | 143.39 |

### 回归检查

```sh
python tools/compare_images.py tests/baselines/gpu/default-nopp.png <新截图>.png --tolerance 24 --max-ratio 0.12
```

### 容差选定依据（tolerance=24，max-ratio=0.12）

同命令多次运行的 run-to-run `diff_ratio` 实测（2026-09-19，AMD 780M，
相对基线的两次独立重跑，1440000 像素/张）：

| 配置 | tol=8（run B / run C） | tol=24（run B / run C） |
|---|---|---|
| default-nopp | 0.13% / 0.73% | 0.033% / 0.18% |
| default-pp | 7.82% / 7.89% | 4.75% / 4.81% |
| slice-nopp | 0.001% / 0.003% | 0.001% / 0.001% |
| slice-pp | 0.049% / 0.037% | 0.023% / 0.018% |

噪声来源：默认场景 6 个立方体 + glTF 演示体按 `dt` 积分自转（无 clamp，
`Application::UpdateTime`），两次运行的帧节拍抖动造成旋转相位差；PP 路径
叠加 TAA 收敛与自适应曝光，差异最大（tol=24 时约 4.8%）。垂直切片场景
95% 实体静止，噪声极低。

选定 `tolerance=24`、`max-ratio=0.12`：对最差配置（default-pp，4.8%）
留 **约 2.5 倍余量**，同时保持对大面积亮度/材质/色调映射改动的敏感性。

## lavapipe CI 基线（自举式，一次性行政动作）

软件光栅与 GPU 成像不同，**不能拿 GPU 基线在 CI 上比对**。lavapipe 基线
按以下自举流程建立（`.github/workflows/ci.yml` 已接好线）：

1. **首次 CI 运行**：`tests/baselines/lavapipe/` 无基线 → "Real Render
   Screenshot Validation" 步骤照常用 `--no-ui` 渲染 4 张
   （default/slice × pp/nopp），比对步骤自动跳过，截图作为
   **`baseline-candidates` artifact** 上传。
2. **人工审核（一次性）**：下载 artifact，本地用 lavapipe 复验
   （`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` +
   `tools/compare_images.py` 交叉检查）或直接采信 CI 产物；再取第二份
   CI artifact 与第一份两两比对，实测 lavapipe 的 run-to-run `diff_ratio`
   （软件渲染慢、`dt` 无 clamp → 自转相位噪声远大于 GPU，**必须实测
   重校准**，勿沿用 GPU 数值）。
3. **提交基线**：把 4 张 PNG 按 `default-pp.png` / `default-nopp.png` /
   `slice-pp.png` / `slice-nopp.png` 命名放入 `tests/baselines/lavapipe/`，
   并把 ci.yml 中 `IMG_TOLERANCE` / `IMG_MAX_RATIO` 更新为
   实测最差值 × 2~3 倍余量。
4. **防线生效**：此后每个 PR 的 Real Render 步骤自动与基线比对，
   超差即 CI 红；有意的渲染改动按上面 GPU 基线同款命令（把输出路径换成
   `tests/baselines/lavapipe/<name>.png`，CI 环境下运行）重新生成并提交。

### 场景渲染改动的基线再生成（命令）

- GPU：上文"生成命令"原样重跑后提交 `tests/baselines/gpu/`。
- lavapipe：在装有 lavapipe 的 Linux 环境按 ci.yml "Real Render" 步骤
  的 4 条命令把输出改名落到 `tests/baselines/lavapipe/` 后提交
  （或采纳新的 `baseline-candidates` artifact）。
