"""固定拆件骨骼的可观察行为测试。"""

import math
import hashlib
import unittest

from PIL import Image

from rig import PART_ORDER, _leg_angles, build_rig, render_pose, sample_pose, world_transforms


class RigTests(unittest.TestCase):
    """验证骨架结构、动作采样和 RGBA 合成。"""

    def test_parent_rotation_moves_child_to_hand_computed_position(self):
        """父节点旋转 90 度时，检查传入的子节点偏移得到手算坐标。"""
        # self：当前测试实例。
        rig = {"bones": [  # rig：两节简单测试骨架。
            {"name": "root", "parent": None, "x": 5, "y": 7, "rest_angle": 0},
            {"name": "child", "parent": "root", "x": 0, "y": 10, "rest_angle": 0},
        ]}
        pose = {"root": [0, 0], "angles": {"root": math.pi / 2}}  # pose：根关节转角。
        joints = world_transforms(rig, pose)  # joints：计算出的世界关节。
        self.assertAlmostEqual(joints["child"][0], -5, places=6)
        self.assertAlmostEqual(joints["child"][1], 7, places=6)

    def test_all_views_have_sixteen_connected_named_parts(self):
        """三视图均保留固定部件、完整父链与画布内站姿。"""
        # self：当前测试实例。
        self.assertEqual(len(PART_ORDER), 16)
        for view in ("front", "side", "back"):  # view：当前视角。
            sizes = {name: (64, 64) for name in PART_ORDER}  # sizes：测试部件像素尺寸。
            rig = build_rig(view, sizes)  # rig：当前视角骨架。
            bones = rig["bones"]  # bones：骨骼记录。
            names = {bone["name"] for bone in bones}  # names：骨骼名称集合。
            self.assertEqual(names, set(PART_ORDER))
            self.assertEqual(len(bones), 16)
            for bone in bones:  # bone：待检查的骨骼。
                self.assertIn(bone["parent"], names | {None})
                self.assertEqual(bone["part"], bone["name"])
                self.assertEqual(len(bone["pivot"]), 2)
                self.assertEqual(len(bone["display_size"]), 2)
            standing = world_transforms(rig, sample_pose("idle", 0, view))  # standing：待机关节位置。
            self.assertAlmostEqual(standing["pelvis"][0], 256, delta=2)
            self.assertLess(standing["head"][1], standing["pelvis"][1])
            self.assertGreater(standing["foot_l"][1], standing["pelvis"][1])

    def test_adult_proportions_and_ground_baseline(self):
        """头、躯干、双腿接近成年比例，脚踝落在固定地面附近。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面站姿骨架。
        by_name = {bone["name"]: bone for bone in rig["bones"]}  # by_name：按名称索引部件。
        joints = world_transforms(rig, {"root": [0, 0], "angles": {}})  # joints：站姿世界关节。
        self.assertGreaterEqual(joints["foot_l"][1] - joints["thigh_l"][1], 195)
        self.assertLessEqual(joints["foot_l"][1] - joints["thigh_l"][1], 220)
        self.assertGreaterEqual(by_name["head"]["display_size"][1], 80)
        self.assertLessEqual(by_name["head"]["display_size"][1], 90)
        self.assertGreaterEqual(by_name["torso"]["display_size"][1], 115)
        self.assertLessEqual(by_name["torso"]["display_size"][1], 125)
        self.assertAlmostEqual(joints["foot_l"][1], 451, delta=5)

    def test_long_hair_layers_behind_side_face_and_over_back(self):
        """侧面长发藏于脸后，背面长发覆盖躯干。"""
        # self：当前测试实例。
        sizes = {name: (64, 64) for name in PART_ORDER}  # sizes：统一测试原图尺寸。
        side = {bone["name"]: bone for bone in build_rig("side", sizes)["bones"]}  # side：侧面骨骼索引。
        back = {bone["name"]: bone for bone in build_rig("back", sizes)["bones"]}  # back：背面骨骼索引。
        front = {bone["name"]: bone for bone in build_rig("front", sizes)["bones"]}  # front：正面骨骼索引。
        self.assertGreater(side["hair"]["display_size"][1], 170)
        self.assertLess(side["hair"]["draw_order"], side["head"]["draw_order"])
        self.assertGreater(back["hair"]["draw_order"], back["torso"]["draw_order"])
        self.assertLess(front["hair"]["draw_order"], front["torso"]["draw_order"])
        self.assertLess(front["hair"]["draw_order"], front["upper_arm_l"]["draw_order"])

    def test_side_binding_has_narrow_depth_and_backward_ankle_pivot(self):
        """侧面肩胯收拢、衣摆贴腰，朝左的脚以右后踝为枢轴。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面骨架。
        bones = {bone["name"]: bone for bone in rig["bones"]}  # bones：骨骼名称索引。
        self.assertLessEqual(abs(bones["upper_arm_l"]["x"] - bones["upper_arm_r"]["x"]), 12)
        self.assertLessEqual(abs(bones["thigh_l"]["x"] - bones["thigh_r"]["x"]), 8)
        self.assertLessEqual(bones["torso"]["display_size"][0], 62)
        self.assertLessEqual(bones["pelvis"]["display_size"][0], 52)
        self.assertLessEqual(bones["upper_arm_l"]["display_size"][0], 34)
        self.assertGreater(bones["foot_l"]["pivot"][0], 0.65 * 64)
        self.assertGreaterEqual(bones["hair"]["x"], 24)
        self.assertLessEqual(abs(bones["coat_l"]["x"] - bones["coat_r"]["x"]), 12)
        self.assertGreaterEqual(bones["coat_l"]["display_size"][0], 58)
        self.assertGreaterEqual(bones["coat_l"]["x"], 8)

    def test_front_back_coat_keeps_source_width(self):
        """正背面两片衣摆保留向外张开的宽度。"""
        # self：当前测试实例。
        for view in ("front", "back"):  # view：当前视角。
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # rig：当前视角骨架。
            bones = {bone["name"]: bone for bone in rig["bones"]}  # bones：骨骼名称索引。
            for side in ("l", "r"):  # side：左或右衣摆。
                self.assertGreaterEqual(bones[f"coat_{side}"]["display_size"][0], 72)
                self.assertLessEqual(bones[f"coat_{side}"]["display_size"][0], 78)
            self.assertLessEqual(bones["coat_l"]["x"], -30)
            self.assertGreaterEqual(bones["coat_r"]["x"], 30)

    def test_boots_keep_shaft_height_and_view_facing(self):
        """三方向短靴保留踝筒高度，并绕面向正确的踝点旋转。"""
        # self：当前测试实例。
        sizes = {name: (100, 100) for name in PART_ORDER}  # sizes：易于读取百分比枢轴的原图尺寸。
        front = {bone["name"]: bone for bone in build_rig("front", sizes)["bones"]}  # front：正面绑定。
        side = {bone["name"]: bone for bone in build_rig("side", sizes)["bones"]}  # side：侧面绑定。
        back = {bone["name"]: bone for bone in build_rig("back", sizes)["bones"]}  # back：背面绑定。
        self.assertGreaterEqual(front["foot_l"]["display_size"][1], 40)
        self.assertGreaterEqual(side["foot_l"]["display_size"][1], 40)
        self.assertGreaterEqual(back["foot_l"]["display_size"][1], 48)
        self.assertGreater(front["foot_l"]["pivot"][0], 60)
        self.assertLess(front["foot_r"]["pivot"][0], 40)
        self.assertGreater(side["foot_l"]["pivot"][0], 65)
        self.assertAlmostEqual(back["foot_l"]["pivot"][0], 50, delta=5)
        self.assertAlmostEqual(side["foot_l"]["display_size"][1] *
                               (1 - side["foot_l"]["pivot"][1] / 100), 20, delta=2)

    def test_every_action_starts_and_ends_on_same_ankle_baseline(self):
        """非循环首尾双脚回地，跑步从接地起、下次接地前允许仍在下降但不穿地。"""
        # self：当前测试实例。
        for view in ("front", "side", "back"):  # view：当前视角。
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # rig：当前视角骨架。
            for action in ("idle", "slow_walk", "run", "jump", "dash", "crouch"):  # action：当前动作。
                for frame in (0, 59):  # frame：动作首帧或末帧。
                    joints = world_transforms(rig, sample_pose(action, frame, view))  # joints：当前关节位置。
                    if action in ("slow_walk", "run"):
                        if action == 'run' and frame == 59:
                            self.assertLessEqual(max(joints['foot_l'][1], joints['foot_r'][1]), 451.01)
                            continue
                        self.assertLess(min(abs(joints["foot_l"][1] - 451), abs(joints["foot_r"][1] - 451)), 1)
                        continue
                    for side in ("l", "r"):  # side：待检查的脚。
                        self.assertAlmostEqual(joints[f"foot_{side}"][1], 451, delta=1,
                                               msg=(view, action, frame, side))

    def test_jump_and_dash_return_to_ground(self):
        """跳跃与冲刺首尾恢复基线，中段确实发生位移。"""
        # self：当前测试实例。
        for action in ("jump", "dash"):  # action：非循环动作。
            first = sample_pose(action, 0, "side")  # first：首帧姿势。
            middle = sample_pose(action, 30, "side")  # middle：中段姿势。
            last = sample_pose(action, 59, "side")  # last：末帧姿势。
            self.assertAlmostEqual(first["root"][1], last["root"][1], delta=0.1)
            self.assertLess(sample_pose("jump", 30, "side")["root"][1], first["root"][1] - 15)
            if action == "dash":
                self.assertNotAlmostEqual(middle["angles"]["torso"], first["angles"]["torso"], delta=0.05)

    def test_scaled_render_makes_room_for_visible_jump(self):
        """统一缩放后站姿约350高、跳跃抬升超50且60帧不裁切。"""
        # self：当前测试实例。
        parts = {name: Image.new("RGBA", (64, 64), "red") for name in PART_ORDER}  # parts：实色全套拆件。
        for view in ("front", "side", "back"):  # view：当前视角。
            rig = build_rig(view, {name: part.size for name, part in parts.items()})  # rig：当前骨架。
            standing = render_pose(parts, rig, sample_pose("idle", 0, view), 512)  # standing：基准站姿。
            apex = render_pose(parts, rig, sample_pose("jump", 30, view), 512)  # apex：跳跃最高点。
            base_box = standing.getbbox()  # base_box：站姿可见区域。
            apex_box = apex.getbbox()  # apex_box：跳跃可见区域。
            self.assertGreaterEqual(base_box[3] - base_box[1], 325)
            self.assertLessEqual(base_box[3] - base_box[1], 380)
            self.assertGreater(base_box[1] - apex_box[1], 50)
            for frame in range(60):  # frame：待验收跳跃帧。
                box = render_pose(parts, rig, sample_pose("jump", frame, view), 512).getbbox()  # box：当前可见边界。
                self.assertGreater(box[0], 0, (view, frame))
                self.assertGreater(box[1], 0, (view, frame))
                self.assertLess(box[2], 512, (view, frame))
                self.assertLess(box[3], 512, (view, frame))

    def test_side_jump_bends_both_knees_forward(self):
        """侧面腾空时两大腿与双膝均朝画面左侧。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面骨架。
        pose = sample_pose("jump", 30, "side")  # pose：跳跃中段姿势。
        joints = world_transforms(rig, pose)  # joints：跳跃关节位置。
        self.assertGreater(pose["angles"]["thigh_l"], 0.30)
        self.assertGreater(pose["angles"]["thigh_r"], 0.16)
        self.assertLess(pose["angles"]["shin_l"], -0.85)
        self.assertLess(pose["angles"]["shin_r"], -0.65)
        for side in ("l", "r"):  # side：当前腿。
            self.assertLess(joints[f"shin_{side}"][0], joints[f"thigh_{side}"][0])

    def test_crouch_lowers_head_then_recovers(self):
        """下蹲中段头关节明显降低，首尾回到站姿。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面骨架。
        first = world_transforms(rig, sample_pose("crouch", 0, "side"))["head"][1]  # first：首帧头部高度。
        middle = world_transforms(rig, sample_pose("crouch", 30, "side"))["head"][1]  # middle：下蹲头部高度。
        last = world_transforms(rig, sample_pose("crouch", 59, "side"))["head"][1]  # last：末帧头部高度。
        self.assertGreater(middle, first + 70)
        self.assertAlmostEqual(last, first, delta=2)
        self.assertGreaterEqual(sample_pose("crouch", 30, "side")["root"][1], 80)

    def test_dash_keeps_a_supporting_foot_near_ground(self):
        """冲刺中段至少一只脚踝接近站姿地面，避免双脚漂浮。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面骨架。
        joints = world_transforms(rig, sample_pose("dash", 30, "side"))  # joints：冲刺中段关节。
        grounded = min(abs(joints["foot_l"][1] - 451), abs(joints["foot_r"][1] - 451))  # grounded：最近脚踝的地面误差。
        self.assertLess(grounded, 3)

    def test_walk_and_run_use_opposed_legs_and_run_is_broader(self):
        """走跑左右腿异相，跑步步幅大于慢走。"""
        # self：当前测试实例。
        for action in ("slow_walk", "run"):  # action：当前循环步态。
            p0 = sample_pose(action, 7, "side")  # p0：左腿前伸时姿势。
            p1 = sample_pose(action, 37, "side")  # p1：半周期后的姿势。
            self.assertGreater(abs(p0["angles"]["thigh_l"] - p0["angles"]["thigh_r"]), 0.08)
            self.assertAlmostEqual(p0["angles"]["thigh_l"], p1["angles"]["thigh_r"], delta=0.11)
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：用于比较脚踝距离的侧面骨架。
        walk_feet = [world_transforms(rig, sample_pose('slow_walk', frame, 'side'))['foot_l'][0] for frame in range(60)]  # 慢走完整足迹
        run_feet = [world_transforms(rig, sample_pose('run', frame, 'side'))['foot_l'][0] for frame in range(60)]  # 跑步完整足迹，落脚靠髋不代表整周期幅度小
        walk_span = max(walk_feet) - min(walk_feet)  # 慢走全周期脚部跨度
        run_span = max(run_feet) - min(run_feet)  # 跑步全周期脚部跨度
        self.assertGreater(run_span, walk_span + 10)

    def test_ik_reaches_hand_computed_left_foot_target(self):
        """逆解(-40,170)后再经真实父链正解，脚踝必须到目标且膝向左。"""
        # self：当前测试实例。
        rig = build_rig("side", {name: (64, 64) for name in PART_ORDER})  # rig：侧面测试骨架。
        thigh, shin, foot = _leg_angles(-40.0, 170.0, 1.0)  # thigh等：由给定脚踝目标求出的关节角。
        pose = {"root": [0, 0], "angles": {"thigh_l": thigh, "shin_l": shin, "foot_l": foot}}  # pose：单腿逆解姿势。
        joints = world_transforms(rig, pose)  # joints：同一姿势的正向关节位置。
        hip = joints["thigh_l"]  # hip：髋世界位置。
        ankle = joints["foot_l"]  # ankle：脚踝世界位置。
        knee = joints["shin_l"]  # knee：膝世界位置。
        self.assertAlmostEqual(ankle[0] - hip[0], -40.0, delta=0.1)
        self.assertAlmostEqual(ankle[1] - hip[1], 170.0, delta=0.1)
        self.assertLess(knee[0], hip[0])

    def test_front_back_gait_lifts_feet_without_wide_side_steps(self):
        """正背面走跑的脚保持在髋下方附近，抬脚仍可观察。"""
        # self：当前测试实例。
        for view in ("front", "back"):  # view：正面或背面。
            rig = build_rig(view, {name: (64, 64) for name in PART_ORDER})  # rig：当前视角骨架。
            for action in ("slow_walk", "run"):  # action：当前步态。
                lifted = []  # lifted：各抽样帧的抬脚高度。
                for frame in (0, 15, 30, 45):  # frame：分布于周期中的抽样帧。
                    joints = world_transforms(rig, sample_pose(action, frame, view))  # joints：当前关节位置。
                    for side in ("l", "r"):  # side：当前脚。
                        self.assertLess(abs(joints[f"foot_{side}"][0] - joints[f"thigh_{side}"][0]), 12)
                        lifted.append(451 - joints[f"foot_{side}"][1])
                self.assertGreater(max(lifted), 7)

    def test_vertical_limb_scale_moves_child_and_image_together(self):
        """压缩父肢体后，孩子关节和可见父部件末端同步移动。"""
        # self：当前测试实例。
        rig = {"canvas_size": 64, "bones": [  # rig：带可缩短父段的简单骨架。
            {"name": "root", "parent": None, "x": 20, "y": 20, "rest_angle": 0,
             "draw_order": 0, "part": "root", "pivot": [2, 0], "display_size": [4, 12]},
            {"name": "child", "parent": "root", "x": 0, "y": 10, "rest_angle": 0,
             "draw_order": 1, "part": "child", "pivot": [2, 2], "display_size": [4, 4]},
        ]}
        pose = {"root": [0, 0], "angles": {}, "scales": {"root": 0.5}}  # pose：父肢体纵向缩半。
        joints = world_transforms(rig, pose)  # joints：缩放后的世界关节。
        self.assertAlmostEqual(joints["child"][1], 25)
        parts = {"root": Image.new("RGBA", (4, 12), "red"),
                 "child": Image.new("RGBA", (4, 4), "blue")}  # parts：可辨认的父子部件。
        result = render_pose(parts, rig, pose, 64)  # result：缩放后的透明合成图。
        self.assertEqual(result.getpixel((20, 25))[:3], (0, 0, 255))
        self.assertEqual(result.getpixel((20, 30))[3], 0)

    def test_all_eighteen_groups_have_sixty_unique_poses(self):
        """每视角每动作 60 帧均含不同关节或根位移。"""
        # self：当前测试实例。
        for view in ("front", "side", "back"):  # view：当前视角。
            for action in ("idle", "slow_walk", "run", "jump", "dash", "crouch"):  # action：当前动作。
                keys = set()  # keys：姿势数值签名。
                for frame in range(60):  # frame：待采样帧号。
                    pose = sample_pose(action, frame, view)  # pose：当前帧姿势。
                    self.assertEqual(len(pose["root"]), 2)
                    self.assertIn("phase", pose)
                    keys.add((tuple(round(value, 7) for value in pose["root"]),
                              tuple((name, round(value, 7)) for name, value in sorted(pose["angles"].items()))))
                self.assertEqual(len(keys), 60, (view, action))

    def test_loop_end_is_close_to_loop_start(self):
        """循环动作最后一帧与首帧无明显关节跳变。"""
        # self：当前测试实例。
        for action in ("idle", "slow_walk", "run"):  # action：当前循环动作。
            first = sample_pose(action, 0, "side")  # first：首帧姿势。
            last = sample_pose(action, 59, "side")  # last：末帧姿势。
            self.assertLess(abs(first["root"][1] - last["root"][1]), 2)
            self.assertLess(max(abs(first["angles"][name] - last["angles"][name])
                                for name in first["angles"]), 0.2)

    def test_render_follows_rotated_parent_and_keeps_background_clear(self):
        """小彩块绕父关节旋转后移到预期位置，未覆盖区域透明。"""
        # self：当前测试实例。
        rig = {"canvas_size": 64, "bones": [  # rig：64画布上的可见父子部件小骨架。
            {"name": "root", "parent": None, "x": 20, "y": 20, "rest_angle": 0,
             "draw_order": 0, "part": "root", "pivot": [2, 2], "display_size": [4, 4]},
            {"name": "child", "parent": "root", "x": 0, "y": 10, "rest_angle": 0,
             "draw_order": 1, "part": "child", "pivot": [2, 2], "display_size": [4, 4]},
        ]}
        parts = {"root": Image.new("RGBA", (4, 4), "red"),
                 "child": Image.new("RGBA", (4, 4), "blue")}  # parts：红蓝测试部件。
        pose = {"root": [0, 0], "angles": {"root": math.pi / 2}}  # pose：父关节旋转姿势。
        result = render_pose(parts, rig, pose, 64)  # result：合成图。
        self.assertEqual(result.size, (64, 64))
        self.assertEqual(result.getpixel((10, 20))[:3], (0, 0, 255))
        self.assertEqual(result.getpixel((20, 20))[:3], (255, 0, 0))
        self.assertEqual(result.getpixel((40, 40))[3], 0)

    def test_jump_fits_canvas_and_dash_frames_change_at_half_size(self):
        """跳跃不裁切长发，半尺寸冲刺逐帧产生实际像素变化。"""
        # self：当前测试实例。
        parts = {name: Image.new("RGBA", (64, 64), (100, 80, 160, 255))
                 for name in PART_ORDER}  # parts：覆盖所有部件的实色测试图。
        for view in ("front", "side", "back"):  # view：当前视角。
            rig = build_rig(view, {name: part.size for name, part in parts.items()})  # rig：当前视角骨架。
            hashes = set()  # hashes：冲刺合成图像摘要。
            for frame in range(60):  # frame：当前输出帧。
                jumping = render_pose(parts, rig, sample_pose("jump", frame, view), 256)  # jumping：半尺寸跳跃图。
                box = jumping.getbbox()  # box：实际可见区域边界。
                self.assertGreater(box[0], 0, (view, frame))
                self.assertGreater(box[1], 0, (view, frame))
                self.assertLess(box[2], 256, (view, frame))
                self.assertLess(box[3], 256, (view, frame))
                dashing = render_pose(parts, rig, sample_pose("dash", frame, view), 256)  # dashing：半尺寸冲刺图。
                hashes.add(hashlib.sha256(dashing.tobytes()).digest())
            self.assertEqual(len(hashes), 60, view)


if __name__ == "__main__":
    unittest.main()
