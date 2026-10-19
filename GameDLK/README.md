# MX002：C + raylib 横版动作原型

双击根目录 **`run.cmd`** 即可构建最新代码并游玩。已构建程序在 `build/MX002.exe`；它从自身位置寻找资源，可以从其他工作目录启动，请保留 `build` 和 `图片` 的目录关系。首次克隆请先执行 `tools/setup.ps1` 安装工程内的官方构建依赖。

启动器固定CRLF换行并先切换UTF-8代码页，兼容中文Windows；使用系统PowerShell完整路径。入口回归检查可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\launcher_tests.ps1`，它从传统中文代码页936和临时工作目录调用同一个`run.cmd --verify`，实际构建并启动游戏完成95项GPU检查。

游戏已切换为实时骨骼：一次加载三方向各16件，共48张固定PNG，实时计算关节、枢轴、图层和镜像。原35组动作所承担的输入、方向和衔接逻辑继续保留。旧逐帧重绘及骨骼导出参考图仅作历史对照，游戏不再读取。

2026-10-19动作更新：走跑保留相位，按真实位移求支撑脚；停止、蹲起和跳冲使用可中断短过渡，衣发加入阻尼滞后和网格弯曲。物理仍固定120Hz，绘制位置与骨骼使用余量插值，输入规则保持。对照《艾希》官方演示改进现有移动动作；没有新增战斗，也不把当前固定拆件称为商业成品美术。动态验收与修改行号见[动作流畅度说明](docs/fluid-motion.md)。

继续修正：移动左右反向现在保持侧面跑姿，膝、肘、手与衣发根部完整过渡；数字键视图切换不再压窄人物。稳定跑步改为21～29度前倾并按实际腿位交叉摆臂，每只脚独立完成摆腿，快速反向不会把抬腿挤到周期末两帧。最新效果和修改行号见[转向与跑姿修正](docs/action-reversal.md)。

| 按键 | 功能 |
| --- | --- |
| A / D，← / → | 左右走；同时按左右则停止 |
| Ctrl + 移动 | 跑步，松开Ctrl回到慢走 |
| 空格 | 地面跳跃；无二段跳 |
| S / ↓ | 按住下蹲，可缓慢移动；松开站起 |
| 左右 Shift | 按下冲刺，朝移动方向或当前面向；按住不会连续触发 |
| S + Shift | 蹲姿冲刺，结束站立；原来的蹲键松开并重新按下后才能再次下蹲 |
| 1 / 2 / 3 / 4 | 正面 / 左侧 / 背面 / 右侧，播放相应转身衔接 |
| R | 重置位置和动作 |
| F1 | 切换碰撞框显示 |
| Esc | 退出 |

冲刺持续0.18秒，开始后冷却0.6秒；空中允许一次，落地恢复。下蹲时不能直接跳跃，松开蹲键后可以起跳。地面和左右边缘有碰撞；当前为单屏动作测试场。

数字键切换站立朝向：180度转身串接两段90度转身。选择正面或背面后，可用该方向的全部动作在横版场地中试演；选择侧面后，移动自动朝左或朝右。跳跃、冲刺和蹲行优先响应实际侧面移动方向，冲刺期间锁定方向；空中提出的正背转向在回到可转身状态后播放。

## 构建与测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
```

`test.ps1`保留32项物理和3173项动画断言，并执行骨骼数学/接地IK及895项逻辑姿势适配断言。`verify.ps1`检查95个真实GPU场景，再在仅复制程序和48拆件、无任何旧图集/参考帧的隔离目录完成95场景，并验证部件缺失和损坏的错误分支。截图和报告位于`verification`。运行`tools/skeletal60/runtime_visual_check.py`可检查四向待机和八转向中点的真实截图轮廓高度。

`test.ps1`还执行10961项连续动作及726项反向/轮廓检查，包括走跑切换、脚部离地、快速重启、蹲起/落地穿地、衣发衰减及30/60/144Hz显示一致性。构建后执行`build/MX002.exe --motion-preview`导出7.2秒、432张真实GPU截图和JSONL数值记录，再用`tools/skeletal60/motion_video.py --ffmpeg <程序路径>`编码60FPS视频；编码工具是可选验证依赖，不进入游戏运行包。当前对照左侧为基础采样姿势，右侧为重做的完整动作，两边共用修复后的方向状态机、物理及48个PNG；左侧不冒称旧转向Bug的完整复现。`reversal_visual_check.py`可量取视图切换和高频左右反向阶段132张真实GPU轮廓。

## 实时骨骼与最小运行包

48个PNG合计约3.20 MiB，姿势浮点表约152 KiB，纹理RGBA约10.14 MiB（不含窗口、驱动额外开销）。每次绘制由关节变换组合固定部件，运行不需要Python、JSON解析器或数千张动作图片。三方向使用同一208像素站姿标尺，蹲跳等姿态自然变矮，不逐帧拉回站高；跳跃升降来自物理，骨骼只负责收腿与落地缓冲。

转向用现有三视图短交叉过渡表达，不新增三分之二角度原画。右侧使用左侧拆件的完整镜像。旧35个逻辑状态依旧处理四向动作、起身、站冲和蹲冲恢复，姿势在运行时按实际时间采样。

构建后执行`powershell -NoProfile -ExecutionPolicy Bypass -File .\package.ps1`生成`build/MX002-skeletal-runtime.zip`。解压后双击包内`build/MX002.exe`，无需带上旧逐帧资源或编译器；保留包内目录结构。

修改动作时使用`tools/skeletal60/rig.py`，随后运行`tools/skeletal60/export_runtime.py`重建`src/skeleton_data.h`，再构建游戏；此过程不生成PNG。当前数据来自已有三方向六基础动作，C在35个逻辑状态中组合、插值和接地校正。文件/行号、旧功能迁移对照和验收证据见`docs/live-skeleton-migration.md`。

官方参考：[raylib头文件/API](https://github.com/raysan5/raylib/blob/6.0/src/raylib.h)、[raylib 6.0发布](https://github.com/raysan5/raylib/releases/tag/6.0)、[w64devkit](https://github.com/skeeto/w64devkit)。第三方代码保留原许可证；原角色资源来源说明位于资源目录README。
