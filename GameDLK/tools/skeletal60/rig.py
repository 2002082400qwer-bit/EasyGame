"""三视图固定拆件骨架、动作采样和透明 PNG 合成。"""

from __future__ import annotations

import math

from PIL import Image


PART_ORDER = (  # PART_ORDER：固定的十六个拆件名称及输入顺序。
    "head", "torso", "pelvis", "hair",
    "upper_arm_l", "upper_arm_r", "forearm_l", "forearm_r",
    "thigh_l", "thigh_r", "shin_l", "shin_r",
    "foot_l", "foot_r", "coat_l", "coat_r",
)
VIEWS = ("front", "side", "back")  # VIEWS：允许的绑定视角。
ACTIONS = ("idle", "slow_walk", "run", "jump", "dash", "crouch")  # ACTIONS：允许的动作名称。
LEG_LENGTH = 100.0  # LEG_LENGTH：大腿和小腿的关节中心距，单位为512画布像素。
HIP_Y = 254.0  # HIP_Y：站姿髋关节的画布纵坐标。
ANKLE_Y = 451.0  # ANKLE_Y：站姿脚踝的画布纵坐标。


def build_rig(view: str, sizes: dict[str, tuple[int, int]]) -> dict:
    """按视角与各部件原像素宽高建立512画布骨架；view为方向，sizes为原图尺寸。"""
    if view not in VIEWS:
        raise ValueError(f"未知视角: {view}")
    missing = set(PART_ORDER) - set(sizes)  # missing：输入中缺少的部件名称。
    if missing:
        raise ValueError(f"缺少部件尺寸: {sorted(missing)}")

    side_view = view == "side"  # side_view：侧面绑定需压窄肩、胯与衣摆。
    shoulder_offset = 5 if side_view else 36  # shoulder_offset：肩关节到身体中心的水平距离。
    hip_offset = 3 if side_view else 18  # hip_offset：髋关节到身体中心的水平距离。
    coat_left_x = 10 if side_view else -32  # coat_left_x：左衣摆贴腰的局部横坐标。
    coat_right_x = 14 if side_view else 32  # coat_right_x：右衣摆贴腰的局部横坐标。
    boot_size = (54, 44) if side_view else (30, 52) if view == "back" else (48, 44)  # boot_size：本视角短靴显示尺寸。
    boot_left_x = 0.72 if side_view else 0.50 if view == "back" else 0.65  # boot_left_x：左靴原图枢轴水平比例。
    boot_right_x = 0.72 if side_view else 0.50 if view == "back" else 0.35  # boot_right_x：右靴原图枢轴水平比例。
    boot_y = 1.0 - 20.0 / boot_size[1]  # boot_y：使脚踝下方保留约20像素鞋底的枢轴比例。
    # layouts：名称到父节点、局部关节位置、显示尺寸及原图归一化枢轴的映射。
    layouts = {
        "pelvis": (None, 256, 247, (48 if side_view else 78, 44), (0.50, 0.45)),
        "torso": ("pelvis", 0, -52, (58 if side_view else 88, 120), (0.50, 0.69)),
        "head": ("torso", 0, -98, (65 if side_view else 70, 90), (0.50, 0.74)),
        "hair": ("head", 26 if side_view else 0, -36,
                 (86 if view == "side" else 98 if view == "back" else 80, 190), (0.50, 0.15)),
        "upper_arm_l": ("torso", -shoulder_offset, -48, (32 if side_view else 40, 72), (0.50, 0.10)),
        "upper_arm_r": ("torso", shoulder_offset, -48, (32 if side_view else 40, 72), (0.50, 0.10)),
        "forearm_l": ("upper_arm_l", 0, 56, (30 if side_view else 36, 76), (0.50, 0.10)),
        "forearm_r": ("upper_arm_r", 0, 56, (30 if side_view else 36, 76), (0.50, 0.10)),
        "thigh_l": ("pelvis", -hip_offset, 7, (34 if side_view else 44, 115), (0.50, 0.07)),
        "thigh_r": ("pelvis", hip_offset, 7, (34 if side_view else 44, 115), (0.50, 0.07)),
        "shin_l": ("thigh_l", 0, LEG_LENGTH, (31 if side_view else 40, 115), (0.50, 0.07)),
        "shin_r": ("thigh_r", 0, LEG_LENGTH, (31 if side_view else 40, 115), (0.50, 0.07)),
        "foot_l": ("shin_l", 0, LEG_LENGTH, boot_size, (boot_left_x, boot_y)),
        "foot_r": ("shin_r", 0, LEG_LENGTH, boot_size, (boot_right_x, boot_y)),
        "coat_l": ("pelvis", coat_left_x, -20, (60 if side_view else 76, 176), (0.50, 0.09)),
        "coat_r": ("pelvis", coat_right_x, -20, (60 if side_view else 76, 176), (0.50, 0.09)),
    }
    # layers：方向对应的遮挡层级；数值越大，越靠近观众。
    if view == "side":
        layers = {"foot_l": 0, "shin_l": 1, "thigh_l": 2, "forearm_l": 3,
                  "upper_arm_l": 4, "coat_l": 5, "pelvis": 6, "torso": 7,
                  "thigh_r": 8, "shin_r": 9, "foot_r": 10, "coat_r": 11,
                  "upper_arm_r": 12, "forearm_r": 13, "head": 14, "hair": 7.5}
    elif view == "back":
        layers = {"foot_r": 0, "shin_r": 1, "thigh_r": 2, "pelvis": 3,
                  "torso": 4, "head": 5, "foot_l": 6, "shin_l": 7,
                  "thigh_l": 8, "coat_r": 9, "coat_l": 10, "upper_arm_r": 11,
                  "forearm_r": 12, "upper_arm_l": 13, "forearm_l": 14, "hair": 15}
    else:
        layers = {"foot_l": 0, "shin_l": 1, "thigh_l": 2, "foot_r": 3,
                  "shin_r": 4, "thigh_r": 5, "coat_l": 6, "coat_r": 7,
                  "pelvis": 8, "torso": 9, "upper_arm_l": 10, "upper_arm_r": 11,
                  "forearm_l": 12, "forearm_r": 13, "hair": 5.5, "head": 15}

    bones = []  # bones：返回的骨骼记录列表。
    for name in PART_ORDER:  # name：当前部件和关节名称。
        parent, x, y, display_size, fractions = layouts[name]  # parent等：本部件的绑定数据。
        source_size = sizes[name]  # source_size：本部件原始宽高。
        if len(source_size) != 2 or min(source_size) <= 0:
            raise ValueError(f"无效部件尺寸: {name}={source_size}")
        pivot = [source_size[0] * fractions[0], source_size[1] * fractions[1]]  # pivot：原图像素坐标内的旋转枢轴。
        length = LEG_LENGTH if name.startswith(("thigh_", "shin_")) else 0.0  # length：到下一主要关节的参考长度。
        bones.append({"name": name, "parent": parent, "x": x, "y": y,
                      "length": length, "rest_angle": 0.0,
                      "draw_order": layers[name], "part": name,
                      "pivot": pivot, "display_size": list(display_size)})
    return {"view": view, "canvas_size": 512, "root": [256, 247],
            "render_scale": 0.8, "render_anchor": [256, 471], "bones": bones}


def _smoothstep(value: float) -> float:
    """将0至1的进度平滑到静止端点；value为原始进度。"""
    value = max(0.0, min(1.0, value))  # value：夹紧到合法区间的进度。
    return value * value * (3.0 - 2.0 * value)


def _leg_angles(dx: float, dy: float, bend: float) -> tuple[float, float, float]:
    """求两节等长腿到脚踝目标的局部角；dx/dy为脚踝相对髋的偏移，bend为膝弯方向。"""
    distance = max(1e-6, min(2 * LEG_LENGTH - 1e-4, math.hypot(dx, dy)))  # distance：可到达的目标距离。
    heading = math.atan2(-dx, dy)  # heading：髋到脚踝相对向下轴的方向，正角朝画面左。
    opening = math.acos(distance / (2 * LEG_LENGTH))  # opening：大腿对目标线的夹角。
    thigh = heading + bend * opening  # thigh：大腿相对竖直向下的角度。
    knee_x = -math.sin(thigh) * LEG_LENGTH  # knee_x：膝相对髋的横坐标。
    knee_y = math.cos(thigh) * LEG_LENGTH  # knee_y：膝相对髋的纵坐标。
    shin_global = math.atan2(-(dx - knee_x), dy - knee_y)  # shin_global：小腿世界角度。
    return thigh, shin_global - thigh, -shin_global


def _gait_target(phase: float, stride: float, lift: float, direction: float, contact: float = 0.60) -> tuple[float, float]:
    """生成脚目标；phase为相位，stride为半步幅，lift为抬脚量，direction为朝向，contact为接地比例。"""
    if phase < contact:
        progress = phase / contact  # progress：支撑期内的进度。
        return direction * stride * (1.0 - 2.0 * progress), 0.0
    progress = (phase - contact) / (1.0 - contact)  # progress：摆腿期内的进度。
    across = _smoothstep(progress)  # across：平滑前摆量。
    return direction * stride * (-1.0 + 2.0 * across), -lift * math.sin(math.pi * progress) ** 2


def _opposed_arm(forward: float, amplitude: float, elbow: float, torso: float) -> tuple[float, float]:
    """侧面交叉摆臂；forward为同侧脚前后量，amplitude为摆幅，elbow为屈肘角，torso为胸部世界角。"""
    upper_length = 56.0  # 肩到肘的实际绑定距离
    lower_length = 68.4  # 肘到前臂含手末端的实际绑定距离
    center = -math.atan2(lower_length * math.sin(elbow), upper_length + lower_length * math.cos(elbow))  # 抵消屈肘本身的向前偏移
    return center - forward * amplitude - torso, elbow


def sample_pose(action: str, frame: int, view: str) -> dict:
    """采样60帧动作；action为动作名，frame为0至59帧号，view为三视图方向。"""
    if action not in ACTIONS:
        raise ValueError(f"未知动作: {action}")
    if view not in VIEWS:
        raise ValueError(f"未知视角: {view}")
    if not isinstance(frame, int) or not 0 <= frame < 60:
        raise ValueError(f"帧号应在0..59: {frame}")

    phase = frame / 60.0  # phase：循环动画的归一化相位。
    cycle = 2.0 * math.pi * phase  # cycle：完整周期的弧度。
    angles = {name: 0.0 for name in PART_ORDER}  # angles：每根骨骼的局部角度偏移。
    scales = {}  # scales：各腿段的纵向缩短比例，正背面抬脚时使用。
    root_x = 0.0  # root_x：骨盆相对站姿的横向位移。
    root_y = 0.0  # root_y：骨盆相对站姿的纵向位移。
    direction = -1.0 if view == "side" else 1.0  # direction：侧面向左时步幅的符号。
    tiny = math.sin(cycle)  # tiny：保证连续呼吸和独立帧的细小变化。

    if action == "idle":
        root_y = -3.0 + 0.7 * (1.0 - math.cos(cycle))
        angles["torso"] = 0.008 * tiny
        angles["head"] = -0.004 * tiny
        angles["upper_arm_l"] = 0.025 + 0.012 * math.sin(cycle - 0.5)
        angles["upper_arm_r"] = -0.025 - 0.012 * math.sin(cycle - 0.5)
        angles["forearm_l"] = 0.035
        angles["forearm_r"] = -0.035
    elif action in ("slow_walk", "run"):
        stride = 52.0 if action == "slow_walk" else 82.0  # stride：放大后仍在腿可达范围内的半步幅。
        lift = 36.0 if action == "slow_walk" else 74.0  # lift：走路抬脚和跑步收腿的可见高度。
        contact = 0.60 if action == "slow_walk" else 0.42  # contact：跑步缩短接地期，包含双脚腾空阶段。
        targets = {}  # targets：以同一脚步相位同时驱动手臂和腿的目标。
        for side, offset in (("l", 0.0), ("r", 0.5)):  # side/offset：左右腿及半周相差。
            targets[side] = _gait_target((phase + offset) % 1.0, stride, lift, direction, contact)
        if view == "side":
            reach = 2.0 * LEG_LENGTH - 1.0  # reach：保留关节余量的最大腿长。
            required_drop = max(ANKLE_Y + target_y - HIP_Y - math.sqrt(reach * reach - target_x * target_x)
                                for target_x, target_y in targets.values())  # required_drop：大步幅下仍能让支撑脚接地的骨盆高度。
            root_y = required_drop + 2.0 + (2.0 if action == "slow_walk" else 6.0) * (1.0 - math.cos(2.0 * cycle)) / 2.0
        else:
            root_y = (3.0 if action == "slow_walk" else 6.0) * (1.0 - math.cos(2.0 * cycle)) / 2.0
        angles["torso"] = direction * ((0.075 if action == "slow_walk" else 0.20) if view == "side" else 0.035) + 0.012 * tiny
        angles["head"] = -angles["torso"] * 0.45
        for side, offset in (("l", 0.0), ("r", 0.5)):  # side/offset：左右腿及半周相位差。
            foot_phase = (phase + offset) % 1.0  # foot_phase：当前脚的周期相位。
            target_x, target_y = targets[side]  # target_x/y：该脚本帧的统一目标。
            forward = direction * target_x / stride  # forward：同侧脚向前为正、向后为负的归一化相位。
            if view == "side":
                thigh, shin, foot = _leg_angles(target_x, ANKLE_Y + target_y - HIP_Y - root_y, 1.0)  # thigh等：侧面腿角。
                angles[f"thigh_{side}"] = thigh
                angles[f"shin_{side}"] = shin
                angles[f"foot_{side}"] = foot
                upper, forearm = _opposed_arm(forward, 0.52 if action == "slow_walk" else 0.92,
                                              0.45 if action == "slow_walk" else 1.35, angles["torso"])  # upper/forearm：同侧脚前时手后，两侧肘均朝前弯。
                angles[f"upper_arm_{side}"] = upper
                angles[f"forearm_{side}"] = forearm
            else:
                swing = max(0.0, (foot_phase - contact) / (1.0 - contact))  # swing：正背面摆腿阶段的进度。
                raised = (30.0 if action == "slow_walk" else 56.0) * math.sin(math.pi * swing) ** 2  # raised：正背面不靠横向劈叉来增加抬脚可见度。
                segment_scale = (ANKLE_Y - HIP_Y - root_y - raised) / (2.0 * LEG_LENGTH)  # segment_scale：使脚踝到达目标地面的腿段纵向比例。
                scales[f"thigh_{side}"] = segment_scale
                scales[f"shin_{side}"] = segment_scale
                sway = 0.025 * math.sin(2.0 * math.pi * foot_phase)  # sway：控制轻微腿摆，保持正背面朝向。
                angles[f"thigh_{side}"] = sway
                angles[f"shin_{side}"] = -sway
                arm_depth = -forward  # arm_depth：前摆的手对应后摆的同侧腿。
                outward = 1.0 if side == "l" else -1.0  # outward：左右手少量外展的画面方向。
                angles[f"upper_arm_{side}"] = outward * (0.06 + (0.14 if action == "slow_walk" else 0.24) * abs(arm_depth))
                angles[f"forearm_{side}"] = -outward * (0.10 + 0.12 * max(0.0, -arm_depth))
                scales[f"upper_arm_{side}"] = 1.0 - 0.10 * max(0.0, arm_depth)
                scales[f"forearm_{side}"] = 1.0 - (0.40 if action == "slow_walk" else 0.58) * max(0.0, arm_depth)
    else:
        progress = frame / 59.0  # progress：非循环动作的首尾封闭进度。
        arch = math.sin(math.pi * progress)  # arch：首尾为零的动作包络。
        if action == "jump":
            root_y = -3.0 - 90.0 * arch * arch
            tuck = 0.35 * arch  # tuck：空中收腿幅度。
            if view == "side":
                angles["thigh_l"] = tuck
                angles["thigh_r"] = 0.20 * arch
                angles["shin_l"] = -0.95 * arch
                angles["shin_r"] = -0.75 * arch
            else:
                angles["thigh_l"] = -tuck
                angles["thigh_r"] = tuck * 0.76
                angles["shin_l"] = 0.80 * arch
                angles["shin_r"] = -0.64 * arch
            angles["foot_l"] = -angles["thigh_l"] - angles["shin_l"]
            angles["foot_r"] = -angles["thigh_r"] - angles["shin_r"]
            angles["torso"] = direction * 0.08 * arch
            angles["upper_arm_l"] = -0.32 * arch
            angles["upper_arm_r"] = 0.34 * arch
        elif action == "dash":
            drive = _smoothstep(min(progress * 10.0, (1.0 - progress) * 10.0))  # drive：快速进入和退出冲刺的强度。
            root_x = direction * 3.0 * drive * math.sin(cycle)
            root_y = -3.0 + (12.0 if view == "side" else 18.0) * drive
            angles["torso"] = direction * (0.58 if view == "side" else 0.10) * drive
            angles["head"] = -angles["torso"] * 0.45
            for side in ("l", "r"):  # side：冲刺支撑腿或摆动腿。
                bend = 1.0 if view == "side" else (1.0 if side == "l" else -1.0)  # bend：膝盖弯曲方向。
                target_x = (direction * (62.0 if side == "l" else -78.0) if view == "side" else (-8.0 if side == "l" else 8.0)) * drive  # target_x：侧面前后大幅伸腿，正背面只少量外展。
                target_y = ANKLE_Y - (0.0 if side == "l" else 60.0 * drive)  # target_y：前脚贴地、后腿明显收起。
                thigh, shin, foot = _leg_angles(target_x, target_y - HIP_Y - root_y, bend)  # thigh等：按目标脚踝求出的腿角。
                angles[f"thigh_{side}"] = thigh
                angles[f"shin_{side}"] = shin
                angles[f"foot_{side}"] = foot
                if view == "side":
                    upper, forearm = _opposed_arm(1.0 if side == "l" else -1.0,
                                                  (0.90 + 0.06 * math.sin(cycle)) * drive,
                                                  1.40 * drive, angles["torso"])  # upper/forearm：前脚配后手、后脚配前手的冲刺发力姿势。
                    angles[f"upper_arm_{side}"] = upper
                    angles[f"forearm_{side}"] = forearm
                else:
                    outward = 1.0 if side == "l" else -1.0  # outward：正背面手臂少量外展方向。
                    angles[f"upper_arm_{side}"] = outward * 0.30 * drive
                    angles[f"forearm_{side}"] = -outward * 0.18 * drive
                    scales[f"forearm_{side}"] = 1.0 - (0.15 if side == "l" else 0.60) * drive
        else:
            depth = _smoothstep(min(progress * 4.0, (1.0 - progress) * 4.0))  # depth：下蹲压低与起身强度。
            root_y = 85.0 * depth + 0.55 * depth * (1.0 - math.cos(cycle))
            angles["torso"] = direction * 0.16 * depth
            angles["head"] = -angles["torso"] * 0.44
            if view == "side":
                angles["upper_arm_l"] = 0.52 * depth
                angles["upper_arm_r"] = 0.62 * depth
                angles["forearm_l"] = 0.19 * depth
                angles["forearm_r"] = 0.14 * depth
            else:
                angles["upper_arm_l"] = 0.27 * depth
                angles["upper_arm_r"] = -0.27 * depth
                angles["forearm_l"] = -0.12 * depth
                angles["forearm_r"] = 0.12 * depth
            for side in ("l", "r"):  # side：待求解的腿。
                bend = 1.0 if view == "side" else (1.0 if side == "l" else -1.0)  # bend：膝盖朝向。
                target_x = (-2.0 if side == "l" else 2.0) * depth  # target_x：下蹲时脚踝的轻微左右偏移。
                thigh, shin, foot = _leg_angles(target_x, ANKLE_Y - HIP_Y - root_y, bend)  # thigh等：脚接地的腿角。
                angles[f"thigh_{side}"] = thigh
                angles[f"shin_{side}"] = shin
                angles[f"foot_{side}"] = foot

    # 裙摆与发梢相对身体迟滞，微幅周期项也保证非循环动作逐帧独立。
    angles["hair"] = -0.38 * angles["head"] + 0.025 * math.sin(cycle - 0.45)
    angles["coat_l"] = -0.22 * angles["torso"] + 0.035 * math.sin(cycle - 0.65)
    angles["coat_r"] = -0.22 * angles["torso"] - 0.035 * math.sin(cycle - 0.65)
    if action in ("slow_walk", "run", "dash"):
        strength = (0.55 if action == "slow_walk" else 1.0) if action != "dash" else drive  # strength：动作发力程度，冲刺首尾平滑恢复。
        wind = (-0.18 if view == "side" else 0.0) * strength  # wind：侧面前进时头发与衣摆向后拖动。
        angles["hair"] += wind + 0.07 * strength * math.sin(cycle - 0.55)
        angles["coat_l"] += wind + 0.15 * strength * math.sin(cycle - 0.50)
        angles["coat_r"] += wind - 0.15 * strength * math.sin(cycle - 0.50)
    return {"root": [root_x, root_y], "angles": angles, "scales": scales, "phase": phase}


def world_transforms(rig: dict, pose: dict) -> dict[str, tuple[float, float, float]]:
    """沿父链合成关节世界坐标；rig为骨架记录，pose为局部转角和根位移。"""
    bones = {bone["name"]: bone for bone in rig["bones"]}  # bones：通过名称查找骨骼。
    angles = pose.get("angles", {})  # angles：骨骼局部角度偏移。
    scales = pose.get("scales", {})  # scales：骨骼纵向尺寸比例。
    root = pose.get("root", (0.0, 0.0))  # root：根关节的画布位移。
    offsets = pose.get("offsets", {})  # offsets：可选的关节局部位移。
    result = {}  # result：已经求得的世界关节变换。
    visiting = set()  # visiting：检测意外循环父链的名称集合。

    def resolve(name: str) -> tuple[float, float, float]:
        """递归求单根关节；name为要解析的骨骼名称。"""
        if name in result:
            return result[name]
        if name in visiting:
            raise ValueError(f"骨骼父链存在环: {name}")
        visiting.add(name)
        bone = bones[name]  # bone：当前关节的绑定记录。
        shift = offsets.get(name, (0.0, 0.0))  # shift：当前关节可选局部位移。
        local_x = float(bone["x"]) + float(shift[0])  # local_x：局部横坐标。
        local_y = float(bone["y"]) + float(shift[1])  # local_y：局部纵坐标。
        local_angle = float(bone.get("rest_angle", 0.0)) + float(angles.get(name, 0.0))  # local_angle：局部总角度。
        parent = bone.get("parent")  # parent：父骨骼名称。
        if parent is None:
            transform = (local_x + float(root[0]), local_y + float(root[1]), local_angle)  # transform：根世界变换。
        else:
            if parent not in bones:
                raise ValueError(f"未知父骨骼: {parent}")
            px, py, pa = resolve(parent)  # px/py/pa：父关节世界变换。
            parent_scale = float(scales.get(parent, 1.0))  # parent_scale：父段纵向缩短比例。
            cosine = math.cos(pa)  # cosine：父关节角余弦。
            sine = math.sin(pa)  # sine：父关节角正弦。
            transform = (px + cosine * local_x - sine * local_y * parent_scale,
                         py + sine * local_x + cosine * local_y * parent_scale,
                         pa + local_angle)
        result[name] = transform
        visiting.remove(name)
        return transform

    for name in bones:  # name：待计算的骨骼名称。
        resolve(name)
    return result


def render_pose(parts: dict[str, Image.Image], rig: dict, pose: dict, size: int = 512) -> Image.Image:
    """把固定RGBA拆件按关节变换合成透明图；parts为原图，rig为骨架，pose为动作姿势，size为输出边长。"""
    if size <= 0:
        raise ValueError(f"无效画布尺寸: {size}")
    base_size = float(rig.get("canvas_size", 512))  # base_size：骨架基准画布边长。
    scale = size / base_size  # scale：输出画布与基准画布的缩放比。
    render_scale = float(rig.get("render_scale", 1.0))  # render_scale：骨架相对脚底锚点的统一美术缩放。
    anchor = rig.get("render_anchor", (0.0, 0.0))  # anchor：统一缩放时保持不动的骨架坐标。
    if render_scale <= 0:
        raise ValueError(f"无效绘制缩放: {render_scale}")
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))  # canvas：最终透明合成画布。
    transforms = world_transforms(rig, pose)  # transforms：每根骨骼的世界关节变换。
    ordered = sorted(rig["bones"], key=lambda bone: bone.get("draw_order", 0))  # ordered：按遮挡层级排序的骨骼。
    for bone in ordered:  # bone：当前待绘制的骨骼。
        name = bone["name"]  # name：部件名称。
        part = parts.get(bone.get("part", name))  # part：当前原始拆件图像。
        if part is None:
            raise KeyError(f"缺少部件图像: {name}")
        vertical_scale = float(pose.get("scales", {}).get(name, 1.0))  # vertical_scale：当前部件纵向缩短比例。
        if vertical_scale <= 0:
            raise ValueError(f"无效骨骼缩放: {name}={vertical_scale}")
        width = max(1, round(float(bone["display_size"][0]) * scale * render_scale))  # width：输出尺寸的部件宽度。
        height = max(1, round(float(bone["display_size"][1]) * scale * render_scale * vertical_scale))  # height：输出尺寸的部件高度。
        image = part.convert("RGBA").resize((width, height), Image.Resampling.LANCZOS)  # image：缩放后的RGBA拆件。
        pivot = bone["pivot"]  # pivot：原始部件像素枢轴。
        pivot_x = float(pivot[0]) * width / part.width  # pivot_x：缩放后的横向枢轴。
        pivot_y = float(pivot[1]) * height / part.height  # pivot_y：缩放后的纵向枢轴。
        joint_x, joint_y, angle = transforms[name]  # joint_x/y/angle：当前关节世界变换。
        joint_x = (float(anchor[0]) + (joint_x - float(anchor[0])) * render_scale) * scale
        joint_y = (float(anchor[1]) + (joint_y - float(anchor[1])) * render_scale) * scale
        cosine = math.cos(angle)  # cosine：世界角余弦。
        sine = math.sin(angle)  # sine：世界角正弦。
        corners = [  # corners：四个源图角经枢轴旋转后的世界位置。
            (joint_x + cosine * (x - pivot_x) - sine * (y - pivot_y),
             joint_y + sine * (x - pivot_x) + cosine * (y - pivot_y))
            for x, y in ((0, 0), (width, 0), (0, height), (width, height))
        ]
        left = math.floor(min(point[0] for point in corners)) - 1  # left：旋转图包围框左界。
        top = math.floor(min(point[1] for point in corners)) - 1  # top：旋转图包围框上界。
        right = math.ceil(max(point[0] for point in corners)) + 1  # right：旋转图包围框右界。
        bottom = math.ceil(max(point[1] for point in corners)) + 1  # bottom：旋转图包围框下界。
        if right <= 0 or bottom <= 0 or left >= size or top >= size:
            continue
        out_width = right - left  # out_width：局部旋转补丁宽度。
        out_height = bottom - top  # out_height：局部旋转补丁高度。
        affine = (  # affine：目标像素到源图坐标的仿射映射。
            cosine, sine, pivot_x + cosine * (left - joint_x) + sine * (top - joint_y),
            -sine, cosine, pivot_y - sine * (left - joint_x) + cosine * (top - joint_y),
        )
        patch = image.transform((out_width, out_height), Image.Transform.AFFINE,
                                affine, Image.Resampling.BICUBIC)  # patch：已绕枢轴旋转的局部图。
        canvas.alpha_composite(patch, (left, top))
    return canvas
