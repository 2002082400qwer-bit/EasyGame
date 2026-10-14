"""用实际关节轨迹检查顺拐、步幅和抬腿，避免只检查图片数量。"""
import math  # 将前臂末端投影到画布
import unittest  # 动作质量回归测试
from rig import PART_ORDER, build_rig, sample_pose, world_transforms  # 正式采样和关节合成


def limb_excursions(action: str, frame: int, view: str = 'side') -> dict:
    """取得实际同侧手脚前后偏移；action为动作，frame为帧号，view为视角。"""
    rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # 当前视角的真实绑定
    joints = world_transforms(rig, sample_pose(action, frame, view))  # 当前帧世界关节
    result = {}  # 左右手脚相对各自近端关节的位移
    for side in ('l', 'r'):  # 当前身体侧
        elbow = joints['forearm_' + side]  # 前臂根部的世界坐标及角度
        hand_x = elbow[0] - math.sin(elbow[2]) * 68.4  # 前臂含手的可见末端，76高扣除10%枢轴
        result[side] = (joints['foot_' + side][0] - joints['thigh_' + side][0],
                        hand_x - joints['upper_arm_' + side][0])
    return result


class GaitQualityTests(unittest.TestCase):
    """检验可见的动作关系；self为当前测试实例。"""

    def test_same_side_hand_and_foot_oppose_over_the_whole_cycle(self):
        """走路同侧脚在前时手应在后；跑步折叠回收的配合另按双大腿相位检查。"""
        for action in ('slow_walk',):  # 走路脚踝轨迹可直接表示同侧前后相位
            for frame in range(60):  # 当前采样帧
                for side, (foot, hand) in limb_excursions(action, frame).items():  # 当前同侧手脚位移
                    if abs(foot) > 6:
                        self.assertLess(foot * hand, -10, (action, frame, side, foot, hand))

    def test_dash_also_uses_opposite_arm_and_leg(self):
        """冲刺中段保持前脚配后手，不能只是走跑修好而冲刺仍顺拐。"""
        for frame in (15, 30, 45):  # 冲刺发力段
            for side, (foot, hand) in limb_excursions('dash', frame).items():  # 同侧前后关系
                self.assertLess(foot * hand, -200, (frame, side, foot, hand))

    def test_side_walk_and_run_have_readable_steps_and_arm_swing(self):
        """人物大小不变时，走跑脚和手必须有可见跨度，跑步应更大。"""
        for action, foot_min, hand_min in (('slow_walk', 80, 90), ('run', 132, 130)):  # 独立设定的最小可见跨度
            values = [limb_excursions(action, frame)['l'] for frame in range(60)]  # 实际一侧关节轨迹
            self.assertGreater(max(v[0] for v in values) - min(v[0] for v in values), foot_min)
            self.assertGreater(max(v[1] for v in values) - min(v[1] for v in values), hand_min)

    def test_front_back_steps_are_visible_below_the_coat(self):
        """正背面增加抬腿，脚踝高度变化不能只剩几像素。"""
        for view in ('front', 'back'):  # 正背两个视角
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # 当前视角骨架
            for action, height_min in (('slow_walk', 24), ('run', 44)):  # 两种步态最低抬脚高度
                heights = [world_transforms(rig, sample_pose(action, frame, view))['foot_l'][1] for frame in range(60)]  # 实际脚踝轨迹
                self.assertGreater(max(heights) - min(heights), height_min, (view, action))

    def test_running_has_flight_while_walking_keeps_support(self):
        """跑步应有真实双脚离地，走路则始终留一只支撑脚。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 侧面实际绑定
        airborne = 0  # 跑步双脚同时离地的采样数
        for frame in range(60):  # 当前周期帧
            walk = world_transforms(rig, sample_pose('slow_walk', frame, 'side'))  # 走路当前姿势
            run = world_transforms(rig, sample_pose('run', frame, 'side'))  # 跑步当前姿势
            self.assertLess(min(abs(walk['foot_l'][1]-451), abs(walk['foot_r'][1]-451)), 0.1)
            airborne += min(451-run['foot_l'][1], 451-run['foot_r'][1]) > 2
        self.assertGreaterEqual(airborne, 2)

    def test_dash_support_foot_stays_on_ground_during_entry_and_exit(self):
        """冲刺目标已经平滑变化，进入与退出时支撑脚也不能穿地。"""
        for view in ('front', 'side', 'back'):  # 当前视角
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # 当前真实绑定
            for frame in range(60):  # 包括起步和收势的全部帧
                joints = world_transforms(rig, sample_pose('dash', frame, view))  # 实际求解的脚踝位置
                self.assertAlmostEqual(joints['foot_l'][1], 451, delta=.01, msg=(view, frame))


if __name__ == '__main__':
    unittest.main()
