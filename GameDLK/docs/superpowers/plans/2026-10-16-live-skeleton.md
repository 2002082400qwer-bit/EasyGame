# 实时骨骼迁移 Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development; root integrates rendering and an isolated implementer builds the pure mathematical skeleton module.

**Goal:** 48件现有美术在游戏中实时组合，完整承接旧35组动作逻辑。

**Architecture:** 保留player/animation状态机；新增骨骼数学/紧凑数据、逻辑姿势适配、GPU拆件加载绘制三个边界。旧PNG仅作历史对照。

**Tech Stack:** C99、raylib 6.0、现有Python/Pillow工具。

**Spec:** docs/superpowers/specs/2026-10-16-live-skeleton-design.md

## Global Constraints

中文函数/参数/变量注释；交付标文件行号。不生成新美术、不删旧资源。保持物理速度、冷却及快捷键，跳跃只作一次世界升降；三方向站高固定208。现有codex/movement-prototype分支工作，任务开始时Git干净，用户指定MX002游戏入口，原位协作以便直接运行。

### Task 1: 紧凑姿势与数学

- [x] 新增tests/skeleton_tests.c，先验证缺少接口失败；所有接口按设计文档。
- [x] 新增src/skeleton.h、src/skeleton.c及自动生成src/skeleton_data.h；新增tools/skeletal60/export_runtime.py，从现有公式生成姿势数据和固定参考尺度。
- [x] 用便携gcc编译数学测试，检查3视角采样、父链、插值、关节有限值、接地IK、循环及跑步腾空。核对Python基准值。

### Task 2: 动作适配及GPU替换

- [x] 新增src/skeletal_actor.h/c及tests/skeletal_actor_tests.c，先验证旧35状态映射、蹲姿接地和跳跃去除重复高度的失败。
- [x] 适配输出至多两套视角姿势，涵盖35逻辑状态、连续循环、蹲冲衔接和真实落地；转向只缩窄横向和淡入淡出，保持高度。
- [x] 新增src/skeletal_renderer.h/c，加载48PNG并以父链、枢轴、层序和镜像DrawTexturePro绘制。main.c换渲染入口，移除旧图集缓存及高度扫描运行依赖；保留状态与调试操作。
- [x] 更新build.ps1/test.ps1，编译-Wall -Wextra -Werror；既有测试和新增测试共同通过。

### Task 3: 真运行和交付

- [x] 将95项GPU验证和verify.ps1迁移为骨骼报告，检查48纹理、35逻辑姿态、四向动作及8转身。
- [x] 隔离工程仅复制48部件与程序，测试成功启动及单部件缺失/损坏报错；旧2100/2160帧均不复制。
- [x] 真实run.cmd --verify、CP936、异工作目录构建启动；查看当前GPU截图并独立代码复审，修复重要问题。
- [x] 更新需求、功能对照/资源说明/代码行号、context.jsonl；提交本地并同步EasyGame/main/GameDLK，核对远程提交。正常启动新游戏供用户体验。
