"""把真实GPU连续输入截图编码成60FPS视频；不写任何角色运行素材。"""
import argparse  # 可显式指定官方ffmpeg程序
import json  # 核对实际GPU记录
from pathlib import Path  # 工程和验证文件路径
import subprocess  # 调用编码器，不经过shell
import sys  # 查找工程内可选验证依赖


def encode(executable: str | None) -> Path:
    """校验432帧记录并编码；executable为可选ffmpeg路径，返回60FPS对照视频。"""
    root = Path(__file__).resolve().parents[2]  # 当前工程根目录
    output = root / 'verification'  # 只写忽略提交的验证目录
    records = [json.loads(line) for line in (output / 'motion-trace.jsonl').read_text().splitlines()]  # 真实GPU逐帧记录
    assert len(records) == 432 and all(record['frame'] == index and record['passed'] for index, record in enumerate(records)), 'GPU回放未完整通过'
    assert all((output / f'motion-{index:04d}.png').is_file() for index in range(432)), '缺少实际GPU截图'
    if executable is None:
        sys.path.insert(0, str(root / 'tools/downloads/media-runtime'))
        try:
            import imageio_ffmpeg  # 可选验证工具，仅编码时加载
        except ImportError as error:
            raise SystemExit('请提供--ffmpeg完整路径，或在tools/downloads/media-runtime安装imageio-ffmpeg。') from error
        executable = imageio_ffmpeg.get_ffmpeg_exe()
    video = output / 'motion-before-after-60fps.mp4'  # 实际游戏GPU旧新对照视频
    subprocess.run([executable, '-y', '-loglevel', 'error', '-framerate', '60', '-i', str(output / 'motion-%04d.png'),
                    '-frames:v', '432', '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)], check=True)
    preview = output / 'motion-before-after.gif'  # 便于离线快速观看的缩小预览，正式比较以60FPS视频为准
    subprocess.run([executable, '-y', '-loglevel', 'error', '-i', str(video), '-filter_complex',
                    'fps=30,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse', '-loop', '0', str(preview)], check=True)
    print(f'60 FPS actual GPU comparison: {video}')
    print(f'Video bytes: {video.stat().st_size}; GIF preview bytes: {preview.stat().st_size}')
    return video


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)  # 验证视频编码命令行
    parser.add_argument('--ffmpeg', help='可选ffmpeg可执行文件完整路径')
    options = parser.parse_args()  # 当前命令行选项
    encode(options.ffmpeg)
