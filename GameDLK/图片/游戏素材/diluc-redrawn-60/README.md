# 60帧角色重绘素材

本目录对应35组、每组60张透明PNG的需求。实际已通过检查的组、数量和待补项目以`verification-report.json`为准。双击`preview.html`可以离线播放、拖动逐帧查看及检查循环接缝。

2026-10-14交付：35组、2100张正式PNG的文件检查通过，播放器35组加载与末帧停止检查通过。关键帧与循环接缝的视觉检查图在`qa/`；`visualContinuityVerified`保留为false，表示没有声称逐帧检查所有美术细节。

## 文件

- `frames/<动作id>/00.png`到`59.png`：正式256×256 RGBA独立帧。
- `atlases/<动作id>.png`：12列×5行，3072×1280，每格256×256。
- `atlases/<动作id>.json`：矩形坐标、尺寸与脚底锚点，兼容常见TexturePacker哈希结构。
- `metadata/<动作id>.json`：动作状态、循环方式、播放建议、独立帧与来源摘要、真实来源选帧索引。
- `previews/<动作id>.gif`：60帧播放预览。循环组另有`-seam.gif`检查56—59—00—03。
- `raw/`：内置图像生成工具新绘源图、实际提示词、参考图路径与选帧清单；`part0`等旧尝试为草稿，正式使用的来源见每组metadata。

## 动作范围

`front/side/back`分别表示正面、左侧面、背面。四种原动作是`slow_walk/run/jump/dash`；新增`idle/crouch_to_stand/stand_to_jump/stand_to_dash/crouch_to_dash_to_stand`。最后一种播放完成后进入站立状态。转向使用`turn_<起始>_to_<目标>`，覆盖正面、背面与左右侧面之间的双向过渡。完整180度转身可接续两段90度转向。

## 使用约定

循环动作重复播放00—59；非循环动作播放到59后转入metadata所列`endState`，不要从59重启到00。下蹲冲刺的完整末状态为站立。统一脚底锚点为帧宽的50%、帧高的92%；实际游戏位移与碰撞由控制器处理。

跳跃PNG提供蓄力、蹬地、收腿和落地的姿势；人物在世界中的升降由游戏跳跃物理负责。所有PNG脚底根部保持一致，避免切换来源图时人物突然掉落。补绘段使用共同交接姿态做等比尺度注册；不生成中间姿势、不压扁图片。

60张PNG表示动作包含60个不同图像，并不强制动作必须持续一秒。metadata提供`durationSeconds/suggestedFps`，接入游戏时可以调整时长。GIF使用10毫秒量化延迟，网页预览按浏览器动画时钟播放。

## 制作与验证

美术由内置`image_gen`参考原人物三视图重新绘制。脚本仅裁取、等比缩放、定位、选取多画的实绘帧和编码图集，不进行光流插值、骨骼变形、复制帧补数。源图不足60帧时继续新绘衔接段；超过60帧时选60张不同帧并记录来源。生成器的实际行列和帧数可能与请求不同，metadata记录真实计数。

执行`node tools/redraw60/audit.cjs`检查PNG数量、格式、透明边缘、文件/像素摘要、来源真实性、图集像素一致性及GIF帧数/时长。循环像素差异只是异常提示，不能代替动作与首尾姿态的视觉检查；`visualContinuityVerified`明确记录视觉验收状态。

打包工具使用Node.js与`sharp`。接口依据[sharp官方输出文档](https://sharp.pixelplumbing.com/api-output/#gif)；提示词全集位于工程`tools/redraw60/full-generation-plan.json`。旧8帧资源与另一个骨骼素材目录均独立保留。

离线看PNG、GIF和网页无需安装工具。需要重新打包时，在工程`tools/redraw60`执行`npm install`安装锁定版本；当前环境也支持Codex捆绑依赖。`npm test`检查13项行为，`npm run audit`执行全量文件验收，`npm run preview-check`需要已安装Microsoft Edge。

## 2026-10-15 连贯性修正

- 三方向`jump`按蓄力、蹬地、顶点、下降、落地、起身六段各新绘10张，修正中途提前站直又重新起跳。
- `back_crouch_to_stand`四段各新绘15张；按实绘姿态复核播放次序，将放下双臂的姿态集中在末段，修正起身中途重新回蹲。排序只改变真实帧的播放位置，不插值或复制图片。
- `front_dash`和`turn_front_to_left`重绘末15张；前者衔接循环初始步态，后者全段维持左侧面，避免转回正面。
- 同规格阶段使用固定等比倍率，避免按屈膝或头发高度累计缩小人体。旧版打包默认行为保留。
- `qa/continuity-comparison.gif`为三方向跳跃正常速度的修正前后对照，左旧右新。六组旧图集、GIF和元数据在`qa/continuity-before`保留。
- 修改工具的行为检查包含来源清单与素材一起事务提交；发布失败不会留下新清单对应旧帧的混合状态。

重新组装三方向跳跃或背面起身：`node tools/redraw60/continuity-build.cjs <动作id>`默认只生成候选；检查后加`--publish`替换正式组。其余两组的实际来源与选择以`raw/<动作id>-source-layout.json`为准，仍用原`pack.cjs pack`入口重现。新提示词保存于`raw/*-v3-*.json`，代码位置在工程`tools/redraw60/CHANGELOG.md`。

视觉检查覆盖35张完整图集以及修正组的全部姿态、阶段交接和关键接缝。复杂衣纹与头发仍存在少量帧间绘制差异，数值通过不表示每个美术细节都已逐像素一致。
