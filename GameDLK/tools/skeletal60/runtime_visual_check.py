"""从真实GPU截图检查站立及转向的角色轮廓高度，不用报告常量冒充像素测量。"""
import json  # 输出可复核的截图测量记录
from pathlib import Path  # 从工具定位当前工程
from PIL import Image  # 仅读实际GPU截图像素


def check() -> dict:
    """检查四向待机及八转身中点截图；无参数，文件缺失或轮廓异常时抛错。"""
    root = Path(__file__).resolve().parents[2]  # 当前MX002工程根目录
    directory = root / 'verification'  # 本次真实窗口截图目录
    names = [view + '-idle' for view in ('FRONT', 'LEFT', 'BACK', 'RIGHT')]  # 四向站立
    names += ['turn_' + pair for pair in ('front_to_left', 'left_to_front', 'front_to_right', 'right_to_front',
                                         'back_to_left', 'left_to_back', 'back_to_right', 'right_to_back')]  # 八条转向中点
    records = []  # 按固定像素条件测得的结果
    for name in names:  # 当前待检查截图
        path = directory / (name + '.png')  # 实际GPU截图路径
        with Image.open(path) as source:  # 截图只读输入
            picture = source.convert('RGB')  # 明确读取最终显示颜色
            assert picture.size == (1280, 720), ('窗口截图尺寸异常', name)
            rows = []  # 排除HUD/地面标记后，角色红色核心轮廓占据的行
            for y in range(330, 600):  # 角色所在区域，截掉地面及HUD
                if any(red > 65 and red > green * 1.5 and red > blue * 1.15
                       for red, green, blue in (picture.getpixel((x, y)) for x in range(500, 780))):
                    rows.append(y)
            assert rows, ('截图角色没有真实前景', name)
            height = max(rows) - min(rows) + 1  # 真实红色轮廓高度，不是报告中的站姿参考常量
            assert abs(height - 208) <= 2, ('待机或转身发生尺寸变化', name, height)
            records.append({'name': name, 'first_row': min(rows), 'last_row': max(rows), 'measured_red_height': height})
    result = {'passed': True, 'screenshots': len(records), 'region': [500, 330, 780, 600],
              'criterion': 'R>65 and R>1.5G and R>1.15B', 'records': records,
              'scope': '四向待机与八转向中点的真实截图红色轮廓；不冒称所有动作的美术验收'}  # 像素测量条件和范围
    (directory / 'skeletal-visual-report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    report = check()  # 本次实际截图测量
    print(f"GPU screenshot height: {report['screenshots']} scenes passed")
