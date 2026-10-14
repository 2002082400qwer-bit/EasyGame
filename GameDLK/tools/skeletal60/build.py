"""裁出生成的拆件、绑定骨架，并导出三方向六动作的60帧素材。"""
from __future__ import annotations
import argparse  # 命令行参数
import hashlib  # 原稿与输出的摘要
import json  # 元数据序列化
import math  # 图集行数计算
import shutil  # 原素材对照的无损复制
from collections import deque  # 连通透明区域的扫描队列
from pathlib import Path  # 中文路径与工程定位
import numpy as np  # RGBA像素数组
from PIL import Image, ImageDraw, ImageFont, ImageOps  # PNG合成与预览标注
from rig import PART_ORDER, ACTIONS, VIEWS, build_rig, sample_pose, render_pose, world_transforms  # 骨骼公开接口

ROOT = Path(__file__).resolve().parents[2]  # 当前MX002工程根目录
OUTPUT = ROOT / '图片/游戏素材/骨骼拆分方式'  # 用户指定的独立交付目录
OLD = ROOT / '图片/游戏素材/diluc-game-sprites/diluc'  # 原8帧动作对照目录
REDRAWN = ROOT / '图片/游戏素材/diluc-redrawn-60'  # 其他流水线已完成的60帧重绘素材，只读取快照
NAMES = {'front': '正面', 'side': '侧面', 'back': '背面', 'idle': '待机',
         'slow_walk': '慢走', 'run': '跑步', 'jump': '跳跃', 'dash': '冲刺', 'crouch': '下蹲'}  # 界面中文名称


def write_json(path: Path, value: object) -> None:
    """将value写成UTF8格式JSON；path为目标文件路径。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def digest(path: Path) -> str:
    """计算path指定文件的SHA256，供来源与成品校验。"""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def isolate_part(source: Image.Image, minimum_area: int = 60) -> Image.Image:
    """保留source的连通美术与透明边缘；minimum_area为要保留的最小像素区域。"""
    pixels = np.array(source.convert('RGBA'))  # 裁格后的RGBA像素
    mask = pixels[:, :, 3] > 8  # 忽略生成器背景中的极低透明噪点
    visited = np.zeros(mask.shape, dtype=bool)  # 已扫描的区域
    keep = np.zeros(mask.shape, dtype=bool)  # 保留的连通区域
    height, width = mask.shape  # 当前裁格尺寸
    for sy, sx in zip(*np.nonzero(mask)):  # 未扫描的前景像素起点
        if visited[sy, sx]:
            continue
        queue = deque([(int(sy), int(sx))])  # 本区域待扫描的像素
        component = []  # 本区域包含的所有像素
        visited[sy, sx] = True
        while queue:
            y, x = queue.popleft()  # 当前扫描坐标
            component.append((y, x))
            for dy, dx in ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)):  # 八邻域偏移
                ny, nx = y + dy, x + dx  # 相邻像素坐标
                if 0 <= ny < height and 0 <= nx < width and mask[ny, nx] and not visited[ny, nx]:
                    visited[ny, nx] = True
                    queue.append((ny, nx))
        if len(component) >= minimum_area:
            ys, xs = zip(*component)  # 连通主体的数组索引
            keep[ys, xs] = True
    pixels[~keep] = 0
    cleaned = Image.fromarray(pixels)  # 保留真实半透明边缘的图像
    bounds = cleaned.getchannel('A').getbbox()  # 透明裁边范围
    if bounds is None:
        raise ValueError('裁格中没有足够大的美术主体')
    return cleaned.crop(bounds)


def make_atlas(frames: list[Image.Image], columns: int = 12) -> Image.Image:
    """将frames按行优先放进columns列图集；不重复乘源图透明度。"""
    width, height = frames[0].size  # 单帧宽高
    atlas = Image.new('RGBA', (width * columns, height * math.ceil(len(frames) / columns)))  # 图集画布
    for index, frame in enumerate(frames):  # 当前帧序号和源图
        atlas.paste(frame, ((index % columns) * width, (index // columns) * height))
    return atlas


def font(size: int = 20) -> ImageFont.ImageFont:
    """加载中文预览字体；size为显示字号。"""
    return ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', size)


def extract_parts() -> dict:
    """裁出三个主表及补图，保存48部件和精确裁剪来源；无参数。"""
    sources = {'reference': '三视图.png', 'reference_sha256': digest(OLD.parent.parent / '三视图.png'),
               'method': 'image_gen补画，确定性裁格，固定拆件骨骼驱动', 'parts': [], 'raw': []}  # 来源记录
    supplement = Image.open(OUTPUT / 'raw/supplement_parts.png').convert('RGBA')  # 独立长发与背面前臂补图
    supp_boxes = {'front': (0, 0, 627, 634), 'side': (627, 0, 1254, 634), 'back': (0, 634, 627, 1254)}  # 补图中长发的范围
    row_edges = {'front': ((0, 322, 636, 939, 1254), (0, 322, 636, 939, 1254),
                           (0, 324, 635, 917, 1254), (0, 328, 634, 917, 1254)),
                 'side': ((0, 323, 625, 934, 1254), (0, 323, 625, 934, 1254),
                          (0, 329, 627, 913, 1254), (0, 329, 627, 913, 1254)),
                 'back': ((0, 341, 644, 934, 1254), (0, 341, 644, 934, 1254),
                          (0, 355, 646, 925, 1254), (0, 355, 646, 925, 1254))}  # 目视核对的列内分界，避免原稿非严格等格的切断
    for raw in sorted((OUTPUT / 'raw').glob('*.png')):  # 每张原稿文件
        sources['raw'].append({'path': raw.relative_to(OUTPUT).as_posix(), 'sha256': digest(raw)})
    for view in VIEWS:  # 当前绑定视角
        sheet = Image.open(OUTPUT / f'raw/{view}_parts.png').convert('RGBA')  # 当前主拆件表
        parts = {}  # 本视角裁出的原始部件
        for index, name in enumerate(PART_ORDER):  # 格序号与部件名称
            column, row = index % 4, index // 4  # 原表的列行序号
            edges = row_edges[view][column]  # 当前列的纵向边界
            box = (round(column * 1254 / 4), edges[row], round((column + 1) * 1254 / 4), edges[row + 1])  # 精确裁格矩形
            source_name = f'raw/{view}_parts.png'  # 当前部件使用的原稿
            if name == 'hair':
                box = supp_boxes[view]
                cropped = supplement.crop(box)  # 使用完整长发補图
                source_name = 'raw/supplement_parts.png'
            elif view == 'back' and name == 'forearm_r':
                box = (627, 634, 1254, 1254)
                cropped = supplement.crop(box)  # 使用未被头发污染的前臂補图
                source_name = 'raw/supplement_parts.png'
            else:
                cropped = sheet.crop(box)  # 主表当前格
            if view == 'front' and name == 'forearm_r':
                part = ImageOps.mirror(parts['forearm_l'])  # 对称复用干净左前臂，避免主表长发污染
                source_name = 'parts/front/forearm_l.png（左右镜像复用）'
            else:
                part = isolate_part(cropped)
            parts[name] = part
            target = OUTPUT / f'parts/{view}/{name}.png'  # 独立透明部件目标路径
            target.parent.mkdir(parents=True, exist_ok=True)
            part.save(target)
            sources['parts'].append({'view': view, 'name': name, 'path': target.relative_to(OUTPUT).as_posix(),
                                     'size': list(part.size), 'source': source_name, 'box': list(box), 'sha256': digest(target)})
        sizes = {name: part.size for name, part in parts.items()}  # 绑定器消费的部件像素尺寸
        write_json(OUTPUT / f'rigs/{view}.json', build_rig(view, sizes))
    shutil.copy2(OLD.parent.parent / '三视图.png', OUTPUT / '三视图参考.png')
    write_json(OUTPUT / 'sources.json', sources)
    return sources


def refresh_rigs() -> None:
    """按现有拆件实际尺寸重新生成三视图绑定，复用美术时也更新层序与枢轴；无参数。"""
    for view in VIEWS:  # 当前视角
        sizes = {name: Image.open(OUTPUT / f'parts/{view}/{name}.png').size for name in PART_ORDER}  # 已交付原图的真实尺寸
        write_json(OUTPUT / f'rigs/{view}.json', build_rig(view, sizes))


def load_view(view: str) -> tuple[dict, dict]:
    """读取view的透明部件和已保存骨架，供采样与导出。"""
    parts = {name: Image.open(OUTPUT / f'parts/{view}/{name}.png').convert('RGBA') for name in PART_ORDER}  # 本视角所有部件
    rig = json.loads((OUTPUT / f'rigs/{view}.json').read_text(encoding='utf-8'))  # 本视角骨架
    return parts, rig


def contact_sheet(frames: list[Image.Image], title: str, columns: int = 6) -> Image.Image:
    """把frames放在深色背景拼图中；title为标题，columns为列数。"""
    cell = 210  # 每格含标题与人物的展示宽度
    rows = math.ceil(len(frames) / columns)  # 展示行数
    result = Image.new('RGB', (cell * columns, (cell + 28) * rows + 48), '#132330')  # 深色拼图
    draw = ImageDraw.Draw(result)  # 文本和地面辅助绘制器
    draw.text((16, 12), title, fill='#f3f5f8', font=font(21))
    for index, frame in enumerate(frames):  # 样图序号及图像
        x, y = (index % columns) * cell, (index // columns) * (cell + 28) + 48  # 展示格左上角
        preview = frame.resize((cell, cell), Image.Resampling.LANCZOS)  # 展示用缩小图
        result.paste(preview, (x, y), preview)
        draw.line((x + 8, y + cell * .92, x + cell - 8, y + cell * .92), fill='#507173')
        draw.text((x + 8, y + cell), str(index), font=font(15), fill='#9fbdc4')
    return result


def sample_previews() -> None:
    """先导出三方向六动作抽样，检查比例与关节接缝；无参数。"""
    for view in VIEWS:  # 当前预览视角
        parts, rig = load_view(view)  # 部件及骨架
        samples = []  # 六动作四个阶段的样图
        for action in ACTIONS:  # 当前动作名称
            for frame in (0, 15, 30, 45):  # 四个动作检查时刻
                samples.append(render_pose(parts, rig, sample_pose(action, frame, view)))
        target = OUTPUT / f'previews/{view}_samples.png'  # 当前方向拼图
        target.parent.mkdir(parents=True, exist_ok=True)
        contact_sheet(samples, NAMES[view] + '：待机 / 慢走 / 跑步 / 跳跃 / 冲刺 / 下蹲', columns=4).save(target)
        print('sample', view, flush=True)


def save_previous_version(snapshot_name: str = 'before-gait-fix') -> None:
    """保留修复前图集、骨骼与姿势；snapshot_name为本轮对照目录名，已有快照不再覆盖。"""
    previous = OUTPUT / 'comparison' / snapshot_name  # 本轮修复前的固定对照目录
    for view in VIEWS:  # 当前视角
        for relative in [f'rigs/{view}.json'] + [f'{kind}/{view}_{action}.{suffix}' for action in ACTIONS
                                                for kind, suffix in (('atlases/256', 'png'), ('animations', 'json'))]:  # 当前视角的图集与可重放源数据
            source = OUTPUT / relative  # 尚未被重新导出的旧版文件
            target = previous / relative  # 保持相对路径的固定快照
            if source.exists() and not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)


def comparison_snapshot() -> str:
    """选择最近一轮已固定保存的修改前素材；无参数，旧版快照始终保留。"""
    for name in ('before-run-fix', 'before-posture-fix', 'before-gait-fix'):  # 从最新到最初的修正轮次
        if (OUTPUT / 'comparison' / name).exists():
            return name
    return 'before-gait-fix'


def export_all(rebuild_actions: tuple[str, ...] = ACTIONS) -> None:
    """输出60帧成品；rebuild_actions为要重新导出的动作，其他动作保留已经验证的索引。"""
    clips = []  # 所有正式动作的元数据
    manifest_path = OUTPUT / 'manifest.json'  # 已存在的正式动作索引
    existing = {clip['id']: clip for clip in json.loads(manifest_path.read_text(encoding='utf-8'))['clips']} if manifest_path.exists() else {}  # 增量导出时保留未修改的动作
    for view in VIEWS:  # 当前方向
        parts, rig = load_view(view)  # 本方向部件与骨架
        for action in ACTIONS:  # 当前动作
            clip_id = f'{view}_{action}'  # 独立动作标识
            if action not in rebuild_actions:
                clips.append(existing[clip_id])
                continue
            poses = [sample_pose(action, index, view) for index in range(60)]  # 本动作60个独立骨骼姿势
            frames = [render_pose(parts, rig, pose) for pose in poses]  # 当前动作512透明帧
            for size in (512, 256):  # 正式输出的两种分辨率
                resized = frames if size == 512 else [frame.resize((size, size), Image.Resampling.LANCZOS) for frame in frames]  # 当前尺寸帧
                folder = OUTPUT / f'frames/{size}/{clip_id}'  # 当前动作独立帧文件夹
                folder.mkdir(parents=True, exist_ok=True)
                for index, frame in enumerate(resized):  # 实际图片帧与序号
                    frame.save(folder / f'{index:02}.png')
                atlas_path = OUTPUT / f'atlases/{size}/{clip_id}.png'  # 当前尺寸图集
                atlas_path.parent.mkdir(parents=True, exist_ok=True)
                make_atlas(resized).save(atlas_path)
            tracks = []  # 每帧骨骼与世界坐标快照
            for index, pose in enumerate(poses):  # 当前姿势及帧序号
                tracks.append({'frame': index, 'time': index / 60, 'pose': pose, 'world': world_transforms(rig, pose)})
            write_json(OUTPUT / f'animations/{clip_id}.json', {'id': clip_id, 'angle_unit': 'radian', 'frames': tracks})
            old_id = clip_id if action in ('slow_walk', 'run', 'jump', 'dash') else f'{view}_jump'  # 原游戏临时姿势对照
            old_clip = next(item for item in json.loads((OLD / 'manifest.json').read_text(encoding='utf-8'))['clips'] if item['id'] == old_id)  # 原素材动作元数据
            old_source = OLD / f'atlases/256/{old_id}.png'  # 默认原8帧图集位置
            if not old_source.exists():
                old_source = OLD / old_clip['outputs']['256']['image']
            old_target = OUTPUT / f'comparison/{old_id}.png'  # 预览独立副本
            old_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(old_source, old_target)
            clips.append({'id': clip_id, 'view': view, 'action': action, 'frame_count': 60, 'fps': 60,
                          'duration': 1.0, 'loop': action in ('idle', 'slow_walk', 'run'), 'columns': 12, 'rows': 5,
                          'pivot': {'x': .5, 'y': .92}, 'atlas': f'atlases/256/{clip_id}.png',
                          'atlas512': f'atlases/512/{clip_id}.png', 'animation': f'animations/{clip_id}.json',
                          'old': f'comparison/{old_id}.png', 'old_frame': 7 if action == 'idle' else 0 if action == 'crouch' else None,
                          'old_sha256': digest(old_source)})
            candidate = REDRAWN / f'atlases/{clip_id}.png'  # 已完成重绘组的候选图集
            candidate_meta = candidate.with_suffix('.json')  # 对应的完整帧索引
            if candidate.exists() and candidate_meta.exists():
                redrawn_meta = json.loads(candidate_meta.read_text(encoding='utf-8'))  # 重绘帧序元数据
                snapshot = Image.open(candidate).convert('RGBA')  # 与外部后续写入分离的解码快照
                if len(redrawn_meta.get('frames', {})) == 60 and snapshot.size == (3072, 1280):
                    target = OUTPUT / f'comparison/redrawn60/{clip_id}.png'  # 60帧对照的独立快照
                    target.parent.mkdir(parents=True, exist_ok=True)
                    snapshot.save(target)
                    reference = REDRAWN / f'frames/{view}_idle/00.png'  # 用站立帧校准高度
                    reference_image = Image.open(reference) if reference.exists() else snapshot.crop((0, 0, 256, 256))  # 已完成站立参考
                    reference_box = reference_image.getchannel('A').getbbox()  # 对照站姿透明边界
                    fresh_reference = render_pose(parts, rig, sample_pose('idle', 0, view), 256)  # 新素材站姿高度
                    fresh_box = fresh_reference.getchannel('A').getbbox()  # 新素材站姿透明边界
                    clips[-1]['redrawn'] = {'path': target.relative_to(OUTPUT).as_posix(), 'frame_count': 60,
                                            'columns': 12, 'sha256': digest(target),
                                            'scale': (fresh_box[3]-fresh_box[1]) / (reference_box[3]-reference_box[1]),
                                            'source': candidate.relative_to(ROOT).as_posix()}
            contact_sheet([frames[index] for index in (0, 10, 20, 30, 40, 50)], NAMES[view] + ' · ' + NAMES[action] + ' · 60帧骨骼拆件').save(OUTPUT / f'previews/{clip_id}.png')
            print('export', clip_id, flush=True)
    for clip in clips:  # 增量导出时，未重画的动作也必须指向同一轮修改前快照
        previous = OUTPUT / f'comparison/{comparison_snapshot()}/atlases/256/{clip["id"]}.png'  # 当前动作的同尺寸60帧对照
        if previous.exists():
            clip['previous'] = {'path': previous.relative_to(OUTPUT).as_posix(), 'frame_count': 60,
                                'columns': 12, 'scale': 1.0, 'sha256': digest(previous)}
    write_json(OUTPUT / 'manifest.json', {'version': 1, 'method': 'fixed-cutout-skeletal', 'clips': clips,
                                        'part_count': 48, 'frame_count_per_resolution': 1080, 'resolutions': [256, 512],
                                        'reference': '三视图参考.png', 'rigs': {view: f'rigs/{view}.json' for view in VIEWS}})
    shutil.copy2(Path(__file__).with_name('preview-template.html'), OUTPUT / 'preview.html')
    tracks = {clip['id']: [item['world'] for item in json.loads((OUTPUT / clip['animation']).read_text(encoding='utf-8'))['frames']] for clip in clips}  # 离线预览直接使用的骨骼坐标
    rigs = {view: json.loads((OUTPUT / f'rigs/{view}.json').read_text(encoding='utf-8')) for view in VIEWS}  # 三方向绑定数据
    comparison_scale = {}  # 原素材与新素材的站姿高度对齐比例
    for view in VIEWS:  # 当前站姿对照方向
        original = Image.open(OLD / f'frames/256/{view}_jump/07.png')  # 原游戏采用的站姿帧
        fresh = Image.open(OUTPUT / f'frames/256/{view}_idle/00.png')  # 新骨骼站姿帧
        old_bounds = original.getchannel('A').getbbox()  # 原站姿透明边界
        new_bounds = fresh.getchannel('A').getbbox()  # 新站姿透明边界
        comparison_scale[view] = (new_bounds[3] - new_bounds[1]) / (old_bounds[3] - old_bounds[1])
    payload = {'manifest': json.loads((OUTPUT / 'manifest.json').read_text(encoding='utf-8')), 'tracks': tracks, 'rigs': rigs, 'comparisonScale': comparison_scale}  # 无网络请求的完整预览数据
    (OUTPUT / 'data.js').write_text('window.SKELETAL_DATA = ' + json.dumps(payload, ensure_ascii=False) + ';\n', encoding='utf-8')


def export_auxiliary_gif() -> None:
    """把侧面六动作60帧合成半速GIF，补充HTML以外的直接预览；无参数。"""
    frames = []  # 六动作同时展示的60张GIF画面
    cell = 192  # 每个人物预览的显示宽度
    for index in range(60):  # 当前60帧中的序号
        canvas = Image.new('RGB', (cell * 3, (cell + 32) * 2 + 38), '#142733')  # 六动作预览背景
        draw = ImageDraw.Draw(canvas)  # 标题和基线绘制器
        draw.text((12, 6), f'侧面六动作 · 半速预览 · 帧{index:02}', font=font(17), fill='#f4f8fa')
        for group, action in enumerate(ACTIONS):  # 当前动作格序号及名称
            x, y = (group % 3) * cell, (group // 3) * (cell + 32) + 38  # 当前格左上角
            image = Image.open(OUTPUT / f'frames/256/side_{action}/{index:02}.png').resize((cell, cell), Image.Resampling.LANCZOS)  # 当前动作帧
            canvas.paste(image, (x, y), image)
            draw.line((x + 4, y + cell * .92, x + cell - 4, y + cell * .92), fill='#658b82')
            draw.text((x + 10, y + cell), NAMES[action], font=font(16), fill='#adc3cf')
        frames.append(canvas)
    frames[0].save(OUTPUT / 'previews/side_actions_half_speed.gif', save_all=True, append_images=frames[1:],
                   duration=[30, 30, 40] * 20, loop=0, disposal=2, optimize=False)


def export_gait_comparison() -> None:
    """导出正常速度的修正前后走跑冲刺GIF，辅助预览采用30FPS，正式PNG仍各60帧。"""
    frames = []  # 30张正常速度对照画面
    actions = ('run', 'dash')  # 本轮专门比较跑步与爆发冲刺，留出更大人物显示面积
    snapshot_name = comparison_snapshot()  # 本轮对照来源与HTML一致
    if not all((OUTPUT / f'comparison/{snapshot_name}/atlases/256/side_{action}.png').exists() for action in actions):
        return
    cell = 288  # 对照中每个人物的显示宽度
    for index in range(0, 60, 2):  # 从正式60帧每隔一帧取样，保持一秒周期
        canvas = Image.new('RGB', (cell * len(actions), (cell + 32) * 2 + 42), '#142733')  # 两行对照画布
        draw = ImageDraw.Draw(canvas)  # 中文标签绘制器
        draw.text((10, 7), '同尺寸 · 正常速度 · 上排修正前 / 下排修正后', font=font(16), fill='#eef5f7')
        for row in (0, 1):  # 上行为旧版，下行为新版
            for column, action in enumerate(actions):  # 当前动作列
                source = OUTPUT / (f'comparison/{snapshot_name}/atlases/256/side_{action}.png' if row == 0 else f'atlases/256/side_{action}.png')  # 当前对照图集
                atlas = Image.open(source)  # 当前动作的透明图集
                sx, sy = (index % 12) * 256, (index // 12) * 256  # 当前帧在图集中的位置
                image = atlas.crop((sx, sy, sx + 256, sy + 256)).resize((cell, cell), Image.Resampling.LANCZOS)  # 保持相同尺度的预览帧
                x, y = column * cell, row * (cell + 32) + 42  # 当前展示格坐标
                canvas.paste(image, (x, y), image)
                draw.line((x + 4, y + cell * .92, x + cell - 4, y + cell * .92), fill='#648d84')
                draw.text((x + 8, y + cell), ('修正前 ' if row == 0 else '修正后 ') + NAMES[action], font=font(16), fill='#e4a09b' if row == 0 else '#8ce2c9')
        frames.append(canvas)
    frames[0].save(OUTPUT / 'previews/gait_comparison_normal_speed.gif', save_all=True, append_images=frames[1:],
                   duration=[30, 30, 40] * 10, loop=0, disposal=2, optimize=False)


def main() -> None:
    """解析导出模式并执行；--all正式导出全部动作，默认只导出抽样。"""
    parser = argparse.ArgumentParser(description=__doc__)  # 命令行参数解析器
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--reuse-parts', action='store_true')
    args = parser.parse_args()  # 用户选择的导出模式
    if args.all:
        save_previous_version('before-run-fix')
    if not args.reuse_parts:
        extract_parts()
    else:
        refresh_rigs()
    sample_previews()
    if args.all:
        export_all()
        export_auxiliary_gif()
        export_gait_comparison()


if __name__ == '__main__':
    main()
