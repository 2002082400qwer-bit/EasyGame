"""验证拆件导出时的真实透明裁剪和图集排布行为。"""
import unittest  # 标准测试运行框架
import numpy as np  # 检查透明通道像素
from PIL import Image  # 实际RGBA像素图像
from build import isolate_part, make_atlas  # 待实现的裁件与图集导出接口


class BuildTests(unittest.TestCase):
    """检查会污染素材或错排帧序的边界情况。"""

    def test_crop_drops_isolated_pixel_but_preserves_connected_edge(self):
        """输入透明图，主件8像素与孤立噪点；主件透明边缘不得被丢失。"""
        source = Image.new('RGBA', (12, 12))  # 合成的透明原稿
        for y in range(4, 8):  # 主体纵坐标
            for x in range(4, 6):  # 主体横坐标
                source.putpixel((x, y), (255, 0, 0, 255))
        source.putpixel((6, 6), (255, 0, 0, 50))
        source.putpixel((0, 0), (255, 0, 0, 255))
        result = isolate_part(source, minimum_area=2)  # 裁出的主体
        self.assertEqual(result.size, (3, 4))
        self.assertEqual(result.getpixel((2, 2))[3], 50)
        self.assertEqual(np.count_nonzero(np.asarray(result)[:, :, 3]), 9)

    def test_atlas_is_row_major_without_alpha_multiplication(self):
        """三张2×2半透明色块应准确落在两列图集，不重复乘透明度。"""
        frames = [Image.new('RGBA', (2, 2), color) for color in  # 手工指定色块
                  [(255, 0, 0, 128), (0, 255, 0, 200), (0, 0, 255, 255)]]
        atlas = make_atlas(frames, columns=2)  # 实际生成的图集
        self.assertEqual(atlas.size, (4, 4))
        self.assertEqual(atlas.getpixel((0, 0)), (255, 0, 0, 128))
        self.assertEqual(atlas.getpixel((3, 1)), (0, 255, 0, 200))
        self.assertEqual(atlas.getpixel((1, 3)), (0, 0, 255, 255))
        self.assertEqual(atlas.getpixel((3, 3)), (0, 0, 0, 0))


if __name__ == '__main__':
    unittest.main()
