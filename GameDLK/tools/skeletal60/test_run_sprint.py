"""跑步必须呈现腾空与折叠回收，冲刺必须有明显的爆发轮廓。"""
import math  # 世界关节角和手端投影
import unittest  # 动作回归框架
from rig import PART_ORDER, build_rig, sample_pose, world_transforms  # 正式动作求解接口


class RunSprintTests(unittest.TestCase):
    """验证跑步与快走的可见差别；self为当前测试实例。"""

    def test_running_has_sustained_visible_flight_in_all_views(self):
        """三视图每周期至少14帧双脚离地8像素，不能只擦过两帧腾空。"""
        for view in ('front', 'side', 'back'):  # 当前视角
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # 正式绑定
            count = 0  # 双脚明显腾空的帧数
            for frame in range(60):  # 一周期全部帧
                joints = world_transforms(rig, sample_pose('run', frame, view))  # 实际双脚坐标
                count += min(451 - joints['foot_l'][1], 451 - joints['foot_r'][1]) > 8
            self.assertGreaterEqual(count, 14, (view, count))

    def test_run_recovers_heel_before_driving_knee_forward(self):
        """跑步离地后先折叠小腿回收，再抬膝送腿，脚不能一直画扁椭圆。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 侧面骨架
        recovered = []  # 后脚已经折叠回收的帧
        flexions = []  # 膝关节屈曲角
        knee_heights = []  # 膝相对髋的高度，用于核对先回收再抬膝的时序
        for frame in range(60):  # 当前周期帧
            pose = sample_pose('run', frame, 'side')  # 当前骨骼姿势
            joints = world_transforms(rig, pose)  # 实际世界关节
            hip, knee, ankle = joints['thigh_l'], joints['shin_l'], joints['foot_l']  # 左髋膝踝
            flexions.append(abs(pose['angles']['shin_l']))
            knee_heights.append(knee[1] - hip[1])
            if ankle[0] > hip[0] + 25 and ankle[1] < hip[1] + 100:
                recovered.append(frame)
        self.assertGreaterEqual(len(recovered), 5)
        self.assertGreater(max(flexions), math.radians(125))
        self.assertLess(min(recovered), knee_heights.index(min(knee_heights)))

    def test_landing_is_near_hips_rather_than_reaching_far_forward(self):
        """落脚靠近髋部下方，不能以大跨步伸脚制造跑步假象。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 侧面骨架
        for frame, side in ((0, 'l'), (30, 'r')):  # 左右脚接地时点
            joints = world_transforms(rig, sample_pose('run', frame, 'side'))  # 实际关节
            self.assertLess(abs(joints['foot_' + side][0] - joints['thigh_' + side][0]), 35)
            self.assertAlmostEqual(joints['foot_' + side][1], 451, delta=.01)

    def test_running_arms_oppose_thighs_even_while_heel_is_folded(self):
        """摆臂对应大腿，折叠回收时脚踝还在身后不能强迫手臂跟错方向。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 侧面真实绑定
        for frame in range(60):  # 全周期
            joints = world_transforms(rig, sample_pose('run', frame, 'side'))  # 当前世界关节
            for side in ('l', 'r'):  # 左右身体侧
                other = 'r' if side == 'l' else 'l'  # 对侧腿
                knee = (joints['shin_' + side][0] - joints['thigh_' + side][0]
                        - joints['shin_' + other][0] + joints['thigh_' + other][0])  # 当前大腿相对另一大腿的前后量
                arm = joints['forearm_' + side]  # 前臂世界关节
                hand = arm[0] - math.sin(arm[2]) * 68.4 - joints['upper_arm_' + side][0]  # 可见手端相对肩的前后量
                if abs(knee) > 12:
                    self.assertLess(knee * hand, 0, (frame, side))

    def test_dash_has_stronger_lean_and_wide_push_off(self):
        """冲刺中段躯干与前后腿构成更夸张的爆发轮廓，同时保持腿可达。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 侧面绑定
        for frame in (15, 30, 45):  # 冲刺爆发段
            pose = sample_pose('dash', frame, 'side')  # 发力姿势
            joints = world_transforms(rig, pose)  # 实际关节
            self.assertGreater(-pose['angles']['torso'], math.radians(48))
            self.assertGreater(joints['foot_r'][0] - joints['foot_l'][0], 220)
            for side in ('l', 'r'):  # 两条腿都不得依赖不可达目标
                hip, ankle = joints['thigh_' + side], joints['foot_' + side]  # 髋踝世界位置
                self.assertLess(math.hypot(ankle[0] - hip[0], ankle[1] - hip[1]), 199.99)


if __name__ == '__main__':
    unittest.main()
