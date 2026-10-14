"""真实解码成品并验证透明PNG、图集、骨架和离线预览的数据一致性。"""
from __future__ import annotations
import hashlib  # 文件与像素校验摘要
import json  # 索引与验证报告
import math  # 实际前臂末端的画布投影
from pathlib import Path  # 中文工程路径
import numpy as np  # 图集像素逐项比较
from PIL import Image  # 正式PNG解码
from build import OUTPUT, write_json, digest  # 成品路径和报告保存
from rig import PART_ORDER, VIEWS, ACTIONS, render_pose  # 骨骼重放接口


def gait_metrics(animation: dict) -> dict:
    """从已导出的animation世界坐标计算手脚配合、跨度和腾空，不调用动作采样公式。"""
    feet = []  # 左脚相对髋的实际前后位置
    hands = []  # 左手相对肩的实际前后位置
    faults = 0  # 同侧手脚在同一前后方向的有效样本数
    flight = 0  # 双脚均明显离地的帧数
    visible_flight = 0  # 双脚离地超过8骨架像素的可读腾空帧
    knee_flexions = []  # 一侧小腿相对大腿的屈曲幅度
    running = animation['id'].endswith('_run')  # 跑步收腿时不能用身后的脚踝判断摆臂相位
    for frame in animation['frames']:  # 已保存的一帧正式数据
        joints = frame['world']  # 本帧世界关节坐标
        flight += min(451-joints['foot_l'][1], 451-joints['foot_r'][1]) > 2
        visible_flight += min(451-joints['foot_l'][1], 451-joints['foot_r'][1]) > 8
        knee_flexions.append(abs(frame['pose']['angles']['shin_l']))
        for side in ('l', 'r'):  # 左右身体侧
            forearm = joints['forearm_' + side]  # 前臂的根和世界角
            foot = joints['foot_' + side][0] - joints['thigh_' + side][0]  # 脚相对髋的位置
            hand = forearm[0] - math.sin(forearm[2])*68.4 - joints['upper_arm_' + side][0]  # 可见手端相对肩的位置
            other = 'r' if side == 'l' else 'l'  # 对侧腿
            coordination = (joints['shin_' + side][0] - joints['thigh_' + side][0]
                            - joints['shin_' + other][0] + joints['thigh_' + other][0]) if running else foot  # 跑步比较双大腿相位，走和冲刺比较伸展的同侧脚
            faults += abs(coordination) > (12 if running else 6) and coordination * hand > 0
            if side == 'l':
                feet.append(foot)
                hands.append(hand)
    return {'same_side_faults': faults, 'foot_span': max(feet)-min(feet),
            'hand_span': max(hands)-min(hands), 'flight_frames': flight,
            'visible_flight_frames': visible_flight, 'max_knee_flexion_degrees': math.degrees(max(knee_flexions)),
            'coordination_basis': 'relative_thigh' if running else 'ankle'}


def posture_metrics(animation: dict) -> dict:
    """从正式动画关节测量上身前倾；animation为含world关节的侧面动作，角度不读取采样公式。"""
    inclines = []  # 各帧髋到胸的实际前倾角，面朝左时为正。
    for frame in animation['frames']:  # 正式输出的每个动作帧
        hip, chest = frame['world']['pelvis'], frame['world']['torso']  # 真实髋与胸部关节
        inclines.append(math.degrees(math.atan2(hip[0] - chest[0], hip[1] - chest[1])))
        assert abs(math.hypot(chest[0] - hip[0], chest[1] - hip[1]) - 52.0) < .001, ('胸髋距离改变', frame['frame'])
    return {'minimum_forward_degrees': min(inclines), 'maximum_forward_degrees': max(inclines),
            'frames_0_15_30_45': [inclines[index] for index in (0, 15, 30, 45)]}


def verify() -> dict:
    """检查全部独立帧和图集，并保存验证报告；错误直接抛出，不标记通过。"""
    manifest = json.loads((OUTPUT / 'manifest.json').read_text(encoding='utf-8'))  # 正式动作索引
    expected = {f'{view}_{action}' for view in VIEWS for action in ACTIONS}  # 用户范围内18组动作
    assert {clip['id'] for clip in manifest['clips']} == expected
    records = []  # 各动作的验证详情
    file_count = 0  # 实际解码的独立帧总数
    atlas_count = 0  # 实际解码的图集总数
    part_records = []  # 48张透明部件的检查结果
    gait_records = []  # 修复前后实际导出姿势的步态指标
    posture_records = []  # 实际髋胸连线测得的落脚前倾变化
    layer_records = []  # 正式绑定中的遮挡关系
    for view in VIEWS:  # 当前视角
        rig = json.loads((OUTPUT / f'rigs/{view}.json').read_text(encoding='utf-8'))  # 绑定数据
        assert {bone['name'] for bone in rig['bones']} == set(PART_ORDER)
        layers = {bone['name']: bone['draw_order'] for bone in rig['bones']}  # 读取正式交付绑定的遮挡顺序
        assert layers['head'] < layers['torso'], ('脖根覆盖胸部衣领', view)
        if view == 'side':
            assert layers['coat_l'] < min(layers[name] for name in ('thigh_l', 'shin_l', 'foot_l')), '后衣摆遮挡内侧腿'
            assert layers['hair'] < layers['head'], '后发覆盖脸部'
            assert layers['coat_r'] > layers['thigh_r'], '外侧衣摆的前后关系被反转'
        layer_records.append({'view': view, 'neck_behind_collar': True, 'draw_order': layers})
        for part in PART_ORDER:  # 当前透明部件名
            path = OUTPUT / f'parts/{view}/{part}.png'  # 当前部件文件
            image = Image.open(path)  # 已解码部件
            assert image.mode == 'RGBA' and image.getchannel('A').getextrema() == (0, 255), path
            part_records.append({'path': path.relative_to(OUTPUT).as_posix(), 'sha256': digest(path), 'size': list(image.size)})
    for clip in manifest['clips']:  # 当前正式动作
        clip_id = clip['id']  # 当前动作标识
        animation = json.loads((OUTPUT / clip['animation']).read_text(encoding='utf-8'))  # 保存的60帧骨骼数据
        assert clip['frame_count'] == 60 and len(animation['frames']) == 60
        assert animation['angle_unit'] == 'radian'
        assert [item['frame'] for item in animation['frames']] == list(range(60))
        sizes = []  # 当前动作两种分辨率验证详情
        for size in (256, 512):  # 正式帧尺寸
            folder = OUTPUT / f'frames/{size}/{clip_id}'  # 当前动作独立帧目录
            paths = sorted(folder.glob('*.png'))  # 实际独立帧文件列表
            assert [path.name for path in paths] == [f'{index:02}.png' for index in range(60)]
            atlas_path = OUTPUT / f'atlases/{size}/{clip_id}.png'  # 图集文件
            atlas = np.asarray(Image.open(atlas_path))  # 实际图集RGBA数组
            assert atlas.shape == (size * 5, size * 12, 4)
            hashes = []  # 60个独立帧的像素摘要
            frame_bounds = []  # 人物前景包围盒
            for index, path in enumerate(paths):  # 独立帧序号和文件
                image = Image.open(path)  # 真正解码的PNG
                assert image.mode == 'RGBA' and image.size == (size, size), path
                pixels = np.asarray(image)  # 当前帧RGBA像素
                alpha = pixels[:, :, 3]  # 透明通道
                assert alpha.min() == 0 and alpha.max() == 255, path
                assert not np.any(alpha[0]) and not np.any(alpha[-1]) and not np.any(alpha[:, 0]) and not np.any(alpha[:, -1]), ('裁切', str(path))
                x, y = (index % 12) * size, (index // 12) * size  # 图集内的当前帧矩形
                assert np.array_equal(pixels, atlas[y:y+size, x:x+size]), ('图集像素不一致', str(path))
                hashes.append(hashlib.sha256(pixels.tobytes()).hexdigest())
                frame_bounds.append(image.getchannel('A').getbbox())
                file_count += 1
            assert len(set(hashes)) == 60, ('实际帧重复', clip_id, size, len(set(hashes)))
            sizes.append({'size': size, 'unique_pixel_frames': len(set(hashes)), 'atlas_matches_every_frame': True,
                          'atlas_sha256': digest(atlas_path), 'pixel_sha256': hashes,
                          'minimum_margin': min(min(box[0], box[1], size-box[2], size-box[3]) for box in frame_bounds)})
            atlas_count += 1
        assert digest(OUTPUT / clip['old']) == clip['old_sha256'], ('原对照副本被修改', clip_id)
        if clip.get('redrawn'):
            snapshot_path = OUTPUT / clip['redrawn']['path']  # 已完成重绘组的独立快照
            assert digest(snapshot_path) == clip['redrawn']['sha256']
            assert Image.open(snapshot_path).size == (3072, 1280)
        if clip.get('previous'):
            assert digest(OUTPUT / clip['previous']['path']) == clip['previous']['sha256']
        if clip['view'] == 'side' and clip['action'] in ('slow_walk', 'run', 'dash'):
            metrics = gait_metrics(animation)  # 新版交付姿势的真实轨迹指标
            assert metrics['same_side_faults'] == 0, ('同侧手脚一起前后摆动', clip_id, metrics)
            if clip['action'] != 'dash':
                assert metrics['foot_span'] > (80 if clip['action'] == 'slow_walk' else 132)
                assert metrics['hand_span'] > (90 if clip['action'] == 'slow_walk' else 130)
            if clip['action'] == 'run':
                assert metrics['visible_flight_frames'] >= 14, ('跑步仍像快走，缺乏清楚腾空', clip_id)
                assert metrics['max_knee_flexion_degrees'] > 125, ('缺乏折叠回收', clip_id)
                for index, side in ((0, 'l'), (30, 'r')):  # 左右脚触地时应靠髋下，不能前伸过度
                    joints = animation['frames'][index]['world']  # 正式落脚帧关节
                    assert abs(joints['foot_' + side][0] - joints['thigh_' + side][0]) < 35, ('落脚太远', index)
            if clip['action'] == 'dash':
                middle = animation['frames'][30]  # 冲刺爆发段正式姿势
                assert -middle['pose']['angles']['torso'] > math.radians(48)
                assert middle['world']['foot_r'][0] - middle['world']['foot_l'][0] > 220
            previous_path = (OUTPUT / clip['previous']['path']).parents[2] / f'animations/{clip_id}.json'  # 和预览一致的本轮修正前正式姿势
            before = gait_metrics(json.loads(previous_path.read_text(encoding='utf-8'))) if previous_path.exists() else None  # 相同算法度量旧版，避免只给新版结论
            gait_records.append({'id': clip_id, 'before': before, 'after': metrics})
            posture = posture_metrics(animation)  # 由已保存髋胸关节计算，避免把贴图旋转误当作整体前倾
            if clip['action'] in ('slow_walk', 'run'):
                angles = posture['frames_0_15_30_45']  # 左接地、中段、右接地、中段的实际倾角
                assert abs(angles[0] - angles[2]) < .001 and abs(angles[1] - angles[3]) < .001, ('左右支撑不对称', clip_id)
                assert (angles[0] - angles[1] > 3.0) if clip['action'] == 'slow_walk' else (angles[1] - angles[0] > 4.0), ('支撑前倾时序错误', clip_id)
            posture_records.append({'id': clip_id, **posture})
        parts = {name: Image.open(OUTPUT / f'parts/{clip["view"]}/{name}.png').convert('RGBA') for name in PART_ORDER}  # 元数据重放部件
        rig = json.loads((OUTPUT / f'rigs/{clip["view"]}.json').read_text(encoding='utf-8'))  # 元数据重放绑定
        for index in (0, 17, 37, 59):  # 首尾与中段的重放校验样本
            replay = render_pose(parts, rig, animation['frames'][index]['pose'])  # 从交付元数据重新渲染
            saved = Image.open(OUTPUT / f'frames/512/{clip_id}/{index:02}.png')  # 已交付的独立帧
            assert np.array_equal(np.asarray(replay), np.asarray(saved)), ('元数据无法重放', clip_id, index)
        records.append({'id': clip_id, 'frame_count': 60, 'resolutions': sizes, 'replay_samples': [0, 17, 37, 59]})
        print('verified', clip_id, flush=True)
    assert file_count == 2160 and atlas_count == 36 and len(part_records) == 48
    report = {'passed': True, 'decoded_frames': file_count, 'decoded_atlases': atlas_count, 'parts': part_records,
              'clips': records, 'gait_quality': gait_records, 'posture_quality': posture_records, 'layer_quality': layer_records,
              'scope': '三方向×六动作×60帧×两尺寸',
              'limitations': ['本验证不自动判断美术自然程度，需要查看动作拼图和浏览器对照',
                              '原图遮挡补画由image_gen完成，与参考设计存在少量细节差异',
                              '自定义骨骼JSON不是Spine工程文件，C项目可直接使用导出的PNG图集']}
    write_json(OUTPUT / 'verification.json', report)
    return report


if __name__ == '__main__':
    result = verify()  # 本次完整验证结果
    print(json.dumps({'passed': result['passed'], 'frames': result['decoded_frames'], 'atlases': result['decoded_atlases']}))
