# 用实时骨骼接管游戏动画

用户已明确弃用逐帧重绘方案，要求检查其已有功能并改用现有骨骼拆件。旧35组所承接的物理、输入、朝向和衔接功能继续保留；旧美术不删除，但游戏运行不再读取该目录，也不读取骨骼导出的2160张参考帧或36张图集。

运行只加载现有三方向各16件透明PNG，共48件。Python从已验收的骨架和动作公式导出紧凑C姿势数据，不生成新图片。C按连续时间插值姿势并实时计算父子关节变换，以固定图层合成。三方向使用各自固定站姿标尺，所有动作共用尺度，右侧镜像；转向以相邻视角缩窄与短交叉过渡表达，竖直尺度不变，不声称有缺少的三维视角原画。

沿用现有35个逻辑动作编号和控制器，保留Ctrl跑步、四向试演、八相邻转身/180度串接、蹲/起身、起跳/落地、站冲/蹲冲/空冲/恢复、冷却及方向锁定。适配层从逻辑状态输出至多两套骨骼姿势。跳跃世界升降只由物理负责，去掉参考动画中额外的整身上移；起身/蹲下用连续深度并重算接地腿IK。无新增战斗等功能。

接口约定：纯数学骨骼模块提供16部件、3视角、6基础动作；SkeletonPose含root[2]、angle[16]、scale[16]、chest[2]；SkeletonBone含parent、x/y、width/height、pivot_x/pivot_y（归一比例）、source_width/source_height；SkeletonJoint含x/y/angle。SkeletonRig、SkeletonPartName、SkeletonDrawOrder、SkeletonReferenceScale提供绑定；SkeletonSample(view,action,progress,loop,out)、SkeletonBlend(first,last,weight,out)、SkeletonCrouch(view,depth,out)、SkeletonWorld(view,pose,joints)提供数学。0/1/2对应front/side/back；动作顺序idle/walk/run/jump/dash/crouch与Python一致。参考尺度将原512骨架单位映射为208像素站姿。渲染适配层与资源模块由主任务实现。

验收：既有32项物理及3173项动画断言继续通过；新测试先复现无实时骨骼接口，再验证采样、父链、循环、接地和衔接。实际GPU检查全部35逻辑状态、四向13动作场景和8转向共95项，另外对旧图集不存在但48部件齐全的隔离工程启动，确认不依赖旧方案。缺失/损坏部件报具体路径。通过真实run.cmd验证中文代码页与异工作目录。检查截图中转身高度、脚底、跳跃及冲刺层级。记录体积、资源与代码位置，保存context并提交和按既有授权同步远程。
