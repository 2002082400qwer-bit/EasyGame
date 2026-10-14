"""检查落脚时的躯干姿态以及衣摆、腿和衣领的真实合成遮挡。"""
import math  # 将实际躯干向量转换成角度
import unittest  # 姿态和遮挡回归框架
from PIL import Image  # 构造有明确颜色的遮挡样本
from rig import PART_ORDER, build_rig, render_pose, sample_pose, world_transforms  # 正式骨架和渲染接口


class PostureLayerTests(unittest.TestCase):
    """对支撑相位和像素遮挡作独立验收；self为当前测试实例。"""

    def test_walk_contact_has_more_forward_lean_than_mid_support(self):
        """左右脚刚接地时均略前倾，半步中段回收，不得只维持固定倾角。"""
        for contact, middle in ((0, 15), (30, 45)):  # 左右脚接地和半步中段的帧号
            landing = -sample_pose('slow_walk', contact, 'side')['angles']['torso']  # 接地前倾角
            recovery = -sample_pose('slow_walk', middle, 'side')['angles']['torso']  # 中段前倾角
            self.assertGreater(landing - recovery, math.radians(3))
            self.assertAlmostEqual(landing, -sample_pose('slow_walk', (contact + 30) % 60, 'side')['angles']['torso'])

    def test_run_has_forward_mean_and_distinct_support_timing(self):
        """跑步保持前倾，俯仰每步一次且接地时不会机械套用走路的峰值。"""
        lean = [-sample_pose('run', frame, 'side')['angles']['torso'] for frame in range(60)]  # 一周期前倾角
        self.assertGreater(min(lean), math.radians(8))
        self.assertGreater(max(lean) - min(lean), math.radians(4))
        self.assertLess(lean[0], lean[15])
        self.assertAlmostEqual(lean[0], lean[30])

    def test_upper_body_tilts_from_hips_instead_of_chest_center(self):
        """胸部关节也沿髋部转动，不能只有贴图在胸口原地旋转。"""
        rig = build_rig('side', {name: (64, 64) for name in PART_ORDER})  # 实际侧面绑定
        for action in ('slow_walk', 'run', 'dash', 'crouch'):  # 有明显上身姿态的动作
            for frame in (0, 15, 30, 45):  # 代表性支撑和过渡帧
                pose = sample_pose(action, frame, 'side')  # 当前姿势
                joints = world_transforms(rig, pose)  # 实际关节世界位置
                hip, chest = joints['pelvis'], joints['torso']  # 髋部及胸部关节
                incline = math.atan2(chest[0] - hip[0], hip[1] - chest[1])  # 髋到胸实际前后倾角
                self.assertAlmostEqual(incline, pose['angles']['torso'], places=6)

    def test_actual_compositing_exposes_inner_leg_and_covers_neck_with_collar(self):
        """将有遮挡关系的拆件叠在同一像素，确认最终可见的是腿和胸部衣领。"""
        for view, below, above in (('side', 'coat_l', 'shin_l'),
                                  ('side', 'head', 'torso'),
                                  ('front', 'head', 'torso'),
                                  ('back', 'head', 'torso')):  # 视角、后层与前层部件
            rig = build_rig(view, {name: (8, 8) for name in PART_ORDER})  # 使用正式层序的真实绑定
            rig.update(canvas_size=32, render_scale=1, render_anchor=[0, 0])
            for bone in rig['bones']:  # 将所有拆件对齐以触发稳定重叠
                bone.update(parent=None, x=16, y=16, pivot=[4, 4], display_size=[8, 8])
            parts = {name: Image.new('RGBA', (8, 8)) for name in PART_ORDER}  # 其余部件透明
            parts[below] = Image.new('RGBA', (8, 8), (255, 0, 0, 255))  # 后层红色
            parts[above] = Image.new('RGBA', (8, 8), (0, 255, 0, 255))  # 前层绿色
            self.assertEqual(render_pose(parts, rig, {}, 32).getpixel((16, 16)), (0, 255, 0, 255), (view, below, above))


if __name__ == '__main__':
    unittest.main()
