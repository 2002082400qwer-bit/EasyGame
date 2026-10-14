# 三视图骨骼素材交付进度

Plan: docs/superpowers/plans/2026-10-13-skeletal60.md

|任务|依赖/接口|核对结果|
|---|---|---|
|1 骨骼渲染|parts→build_rig/sample_pose/render_pose|原稿无原始像素比例保证，渲染器按绑定显示尺寸统一；公开16部件名与任务2匹配|
|2 拆件补画|三视图→48部件|image_gen负责所有新绘美术；程序仅裁出部件|
|3 导出对照|任务1+2→18组60帧|512和256分别输出1080帧；旧8帧对照保持原始姿势播放|
|1↔2|PART_ORDER与4×4原稿|一致，侧面l为后侧，r为前侧|
|1↔3|骨骼/姿势与合成接口|JSON保存角度单位为弧度，导出器显式标记|
|2↔3|parts/view/name.png|同一命名，不覆盖旧资源|

Ruling: 用户已授权上一轮建议的骨骼方案并明确执行目录与60帧，直接按该方案实施，不追加重复设计批准。
Ruling: 现有RequirementDocument.md修改与tools/redraw60、diluc-redrawn-60属于其他工作，保持不改不提交。
Ruling: 指定MX002路径内新增独立素材流水线，沿用现有功能分支，不另建工作树。

Task 1: complete — agent /root/skeletal_renderer，开始基准HEAD 2d2129e；父代理重新运行21骨骼测试与2导出测试，共23项通过。
Task 2: complete — 三方向拆件主表、侧面布局修正版及长发/背面前臂补图保存，48部件已裁出并目视检查。
Task 3: complete — 已导出18组动作、2160张透明PNG、36张图集、18份姿势数据；2026-10-14重新运行23项测试及完整成品验证通过。独立最终审查无Critical/Important问题，两处说明措辞已修正。浏览器已检查暂停、逐帧、进度、动作切换和骨骼叠加，截图保存在previews。

Ruling: 真实抽样发现正面长发盖胸、侧身肩胯过宽、膝IK横向符号与父链不一致、下蹲过浅；补失败回归测试后修正。
Ruling: 保持所有动作的同一人物大小，使用render_scale=0.8围绕(256,471)统一缩放，为90单位跳跃保留顶部空间；骨骼JSON与HTML明确应用此映射。
Ruling: 只复制已存在完整60帧索引与图集的重绘组作为比较快照；不修改或等待其他流水线的未完成动作。所有18组均有原8帧对照。

Remote delivery: 用户指定 https://github.com/2002082400qwer-bit/EasyGame/blob/main/GameDLK。远程main已有GameDLK占位文件，内容只有换行；发布时将它替换为同名工程目录，保持远程README和历史。从本地已提交快照同步工程与本任务素材，不纳入其他未完成重绘工作或其已暂存改动。
