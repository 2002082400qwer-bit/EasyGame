# 无武器迪卢克逐帧动画资源

基于已确认的角色三视图制作。正面 `front`、侧面 `side`、背面 `back`，每个方向包含四种动作，共12组；每组8帧。侧面角色朝左，朝右可在游戏中水平翻转。

| 动作标识 | 动作 | 帧率 | 循环 |
| --- | --- | --- | --- |
| slow_walk | 慢走 | 8 fps | 是 |
| run | 跑步 | 12 fps | 是 |
| jump | 跳跃 | 12 fps | 否 |
| dash | 冲刺 | 20 fps | 是 |

## 文件

- `frames/256`、`frames/512`：真实透明RGBA PNG独立帧，每个尺寸96帧，帧序从00至07。
- `atlases/256`、`atlases/512`：4列2行图集，从左到右、从上到下。图集分别为1024×512和2048×1024。
- 图集旁同名 `.json`：TexturePacker JSON Hash格式；帧名为 `<动作组>_00` 至 `<动作组>_07`，无旋转和裁边。
- `metadata`、`manifest.json`：准确帧率、循环标志、图集矩形、锚点及来源记录。
- `previews`：各组GIF和侧面四动作合成GIF。GIF是观看用，游戏使用PNG；GIF透明度及颜色受格式限制。
- `preview.html`：离线预览，包含图像数据，无需联网，可选择方向、动作和播放速度。
- `godot/diluc_256.tres`、`godot/diluc_512.tres`：Godot 4的SpriteFrames资源。
- `verification.json`：真实文件解码、透明度、帧数、图集像素一致性及播放参数检查结果。
- `godot_import_verification.json`：Godot 4.5.1临时工程实际导入结果，包含两个尺寸共24个动画、192张纹理及被验证文件的SHA-256摘要。

## 游戏播放

PNG使用完整方形帧，统一根部锚点为归一化 `(0.5, 0.92)`，对应256尺寸的 `(128,235.52)` 和512尺寸的 `(256,471.04)`。循环动作按脚底基线注册，12组采用共同缩放和水平原点。跳跃整组以恢复帧对齐地面，保留腾空高度变化。

慢走、跑步、冲刺是原地循环：世界坐标移动、碰撞、速度、冲刺距离由游戏控制。帧率控制动作播放节奏，不等于人物的移动速度。

跳跃00为蓄力、01为起跳、02至04为腾空、05至06为落地缓冲、07为恢复。游戏物理决定落地时刻；需要交互控制时，拆成准备、空中、落地阶段，空中阶段可等待真实落地事件再进入05，而不要让整个固定时长动画独立决定碰撞。

图集帧之间没有额外挤边，使用Clamp纹理寻址；缩放时有邻格串色可选独立PNG，或在引擎打图集时启用边缘扩展。二维游戏可直接按清单播放，不包含3D模型、骨骼或物理控制器。

## Godot 4

把整个 `diluc` 文件夹复制到游戏的 `res://diluc/`，将对应尺寸的SpriteFrames资源赋给AnimatedSprite2D。动画名称如 `side_run`，按需调用播放；跳跃资源已设为不循环。

AnimatedSprite2D保持默认 `centered=true` 时，把节点 `offset.y` 设为 `(0.5-0.92)×帧尺寸`：256使用 `-107.52`，512使用 `-215.04`；`offset.x=0`，从而使节点坐标位于声明的根部锚点。侧面朝右可设置 `flip_h=true`。

已通过Godot 4.5.1临时工程的headless导入与资源加载检查，核对两种尺寸的24个动画、192张纹理、坐标、帧率和循环标志。验证脚本保存在工程 `tools/diluc-sprites/validate_godot.gd`。参考：[AnimatedSprite2D](https://docs.godotengine.org/en/stable/classes/class_animatedsprite2d.html)、[SpriteFrames](https://docs.godotengine.org/en/stable/classes/class_spriteframes.html)。用户未指定实际游戏工程，目标项目中的角色状态、碰撞和动画切换仍需接入。

## 来源与重建

插画和三视图由用户确认，动作原稿由图像生成工具制作。头发、衣摆、装饰存在逐帧绘制差异，动作质量可通过GIF和离线播放器查看。原稿及提示词保存在本地仓库的 `raw` 中，运行资源ZIP不包含原稿和淘汰版本。

包装源码在工程 `tools/diluc-sprites/`。使用Node.js 20或更新版本，并安装该目录package.json声明的sharp依赖。

```powershell
node tools/diluc-sprites/pack.cjs --root output/game-sprites/diluc --layout=components --strict-edges=false
node tools/diluc-sprites/export.cjs --root output/game-sprites/diluc
node --test tools/diluc-sprites/pack.test.cjs tools/diluc-sprites/export.test.cjs
```

原稿实际人物轮廓越过理想网格，但人物之间彼此分离，使用连通轮廓提取八帧；保留透明发梢，再按共同坐标系和尺度注册，避免按理想格子裁断角色。仅清理alpha不大于3且距离alpha不小于32的主体超过12源像素的孤立透明噪点，清理数量和规则记录在清单中；原稿文件保留不变。主体附近半透明像素完整保留。

`export.cjs`重新运行会重建文件验证报告，并将`engineImportTested`恢复为false；需要重跑Godot验证脚本后再记录新的引擎证据，旧报告的SHA-256应与当前图集一致。
