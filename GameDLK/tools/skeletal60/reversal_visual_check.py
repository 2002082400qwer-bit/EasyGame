"""检查真实GPU视图切换和连续反向的完整轮廓，数值检查不能替代播放复看。"""
import json  # 保存逐帧结果
from pathlib import Path  # 定位验证产物
from PIL import Image  # 仅读GPU像素


def check() -> dict:
    """检查300至431帧的数字键视图切换和反向跑；无参数，失败抛出异常。"""
    root = Path(__file__).resolve().parents[2]  # 当前工程
    directory = root / 'verification'  # 最终GPU截图目录
    records = []  # 每个真实帧的轮廓宽高
    for index in range(300, 432):  # 覆盖所有视角和高频反向阶段
        with Image.open(directory / f'motion-{index:04d}.png') as source:  # 仅读当前GPU截图
            picture = source.convert('RGB')  # 最终颜色像素
            points = [(x, y) for y in range(300, 600) for x in range(820, 1100)
                      if (lambda pixel: pixel[0] > 65 and pixel[0] > 1.5 * pixel[1] and pixel[0] > 1.15 * pixel[2])(picture.getpixel((x, y)))]  # 排除背景和文字，量取红色核心轮廓
        assert points, ('角色消失', index)
        width = max(point[0] for point in points) - min(point[0] for point in points) + 1  # 真实核心宽度
        height = max(point[1] for point in points) - min(point[1] for point in points) + 1  # 真实核心高度
        assert width >= 24 and height >= 145 and len(points) >= 550, ('角色轮廓塌缩', index, width, height, len(points))
        records.append({'frame': index, 'width': width, 'height': height, 'core_pixels': len(points)})
    result = {'passed': True, 'frames': len(records), 'minimum_width': min(record['width'] for record in records),
              'minimum_height': min(record['height'] for record in records), 'records': records,
              'scope': '真实GPU数字键视图切换及连续左右反向132帧，固定区域红色核心；不代表全帧美术质量'}  # 明确测量条件与范围
    (directory / 'reversal-visual-report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"Reversal GPU silhouettes: {len(records)} frames, min width {result['minimum_width']}px, min height {result['minimum_height']}px")
    return result


if __name__ == '__main__':
    check()
