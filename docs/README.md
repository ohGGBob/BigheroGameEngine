# docs/ — 文档归档

本目录存放引擎的规划、评估与设计类文档，与根目录的 `README.md`（面向使用者）、
`CHANGELOG.md`（版本记录）、`UPGRADE_PLAN.md`（进行中的升级计划）区分开。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `planning/` | 阶段性的开发计划、全面评估报告、商业化方案等一次性文档 |

## 归档规则

- **根目录只保留**：`README.md`、`CHANGELOG.md`、`UPGRADE_PLAN.md`、`LICENSE` 及构建/配置文件。
- 其余方案、评估、复盘类文档一律放 `docs/planning/`，文件名保持 `主题_YYYY-MM-DD.md` 格式。
- 纯本地使用的临时提示词模板（如 `全面评估提示词_*.md`）不入库，见 `.gitignore`。
