# MX002：C + raylib 横版动作原型

双击根目录 **`run.cmd`** 即可游玩。已构建程序在 `build/MX002.exe`；它从自身位置寻找资源，可以从其他工作目录启动，请保留 `build` 和 `图片` 的目录关系。

| 按键 | 功能 |
| --- | --- |
| A / D，← / → | 左右走；同时按左右则停止 |
| 空格 | 地面跳跃；无二段跳 |
| S / ↓ | 按住下蹲，可缓慢移动；松开站起 |
| 左右 Shift | 按下冲刺，朝移动方向或当前面向；按住不会连续触发 |
| R | 重置位置和动作 |
| F1 | 切换碰撞框显示 |
| Esc | 退出 |

冲刺持续0.18秒，开始后冷却0.6秒；空中允许一次，落地恢复。下蹲时不能直接跳跃和冲刺，先松开下蹲键。地面和左右边缘有碰撞；当前仅为单屏动作测试场。

## 构建与测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\test.ps1
.\build\MX002.exe --verify
```

最后一条命令创建隐藏的真实OpenGL窗口，用相同控制器和绘制路径运行七种场景，在 `verification` 输出截图和 `report.json`，有任意状态/截图失败时返回非零退出码。它不代替真人按键手感测试。

使用C99、raylib 6.0、w64devkit GCC 16.2.0，编译启用 `-Wall -Wextra -Werror`。编译器与库在工程内，不修改全局环境；`tools` 内下载和工具链、`third_party`、构建与验证产物不纳入Git。新克隆工程先执行 `tools/setup.ps1` 下载官方依赖，再构建。

## 美术与物理

使用 `图片/游戏素材/diluc-game-sprites/diluc/atlases/256` 的侧面图集。人物原图朝左，向右通过水平镜像。原始资源不改动，脚底锚点 `(0.5, 0.92)`；物理每秒更新120次，动画依据动作状态与时间选择，落地以实际物理结果为准。

**当前资源没有独立待机和下蹲动画。** 待机临时使用跳跃第07帧，下蹲使用第00帧的蓄力姿态，下蹲真实碰撞高由156降至98像素，保持脚底位置、不压扁图片。之后可以只替换动作资源，无需重写移动控制器。

`src/player.c` 包含可调的动作规则，速度、重力、碰撞尺寸和冲刺参数集中在 `src/player.h`。`src/main.c` 负责窗口、键盘、图集加载和测试场显示。Windows下通过宽字符文件API读取中文路径，再交给raylib内存解码。

官方参考：[raylib头文件/API](https://github.com/raysan5/raylib/blob/6.0/src/raylib.h)、[raylib 6.0发布](https://github.com/raysan5/raylib/releases/tag/6.0)、[w64devkit](https://github.com/skeeto/w64devkit)。第三方代码保留原许可证；原角色资源来源说明位于资源目录README。
