"""从现有公式导出运行时关节数据；只在内存量取待机标尺，不输出图片。"""

from pathlib import Path  # 中文资源路径与工程根目录定位。
import argparse  # 命令行输出位置。
import math  # 有限浮点数据校验。
from PIL import Image  # 读取现有拆件及内存站姿合成。
from rig import PART_ORDER, VIEWS, ACTIONS, build_rig, sample_pose, render_pose  # 既有骨骼与动作定义。

ROOT = Path(__file__).resolve().parents[2]  # 当前工程根目录。
PARTS = ROOT / "图片/游戏素材/骨骼拆分方式/parts"  # 现有48件透明PNG目录。
FRAME_COUNT = 60  # 每动作只保存关节采样数量。
VALUE_COUNT = 36  # 两根位移、十六角度、十六缩放及两胸偏移。


def cfloat(value: float) -> str:
    """生成有效C单精度字面量；value为待序列化数值。"""
    if not math.isfinite(value):
        raise ValueError("姿势存在非有限数值")
    text = f"{value:.8g}"  # 保留单精度足够的有效数字。
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def flatten(pose: dict) -> list[float]:
    """把pose转换为紧凑连续浮点行，仅保存实际使用的胸偏移。"""
    return [*pose["root"], *(pose["angles"][name] for name in PART_ORDER),
            *(pose["scales"].get(name, 1.0) for name in PART_ORDER),
            *pose["offsets"].get("torso", (0.0, 0.0))]


def export(output: Path) -> None:
    """输出独立C数据头；output为目标路径，原始图片始终只读。"""
    rigs = []  # 三方向绑定定义。
    sizes_by_view = []  # 三方向原始拆件尺寸。
    reference_scales = []  # 原始512坐标到208站高的固定倍率。
    for view in VIEWS:  # 当前方向名称。
        parts = {name: Image.open(PARTS / view / f"{name}.png").convert("RGBA")
                 for name in PART_ORDER}  # 只读现有美术。
        sizes = {name: image.size for name, image in parts.items()}  # 当前拆件原图尺寸。
        rig = build_rig(view, sizes)  # 与参考导出相同的绑定。
        standing = render_pose(parts, rig, sample_pose("idle", 0, view))  # 只在内存计算0.8倍率站姿。
        mask = standing.getchannel("A").point(lambda alpha: 255 if alpha >= 128 else 0)  # 核心不透明轮廓。
        bounds = mask.getbbox()  # 核心轮廓包围框。
        if bounds is None:
            raise ValueError(f"{view}待机无核心像素")
        height = bounds[3] - bounds[1]  # 含首尾行的核心站姿高度。
        reference_scales.append(0.8 * 208.0 / height)
        rigs.append(rig)
        sizes_by_view.append(sizes)
        print(f"{view}: standing core={height}, scale={reference_scales[-1]:.8f}")
        for image in parts.values():  # 当前内存拆件图。
            image.close()
    lines = ["/* 由tools/skeletal60/export_runtime.py自动生成；修改骨骼公式后重新导出。 */",
             "#ifndef MX002_SKELETON_DATA_H", "#define MX002_SKELETON_DATA_H /* 紧凑数据保护。 */",
             "#define SKELETON_SAMPLE_COUNT 60 /* 每动作关节采样数。 */",
             "#define SKELETON_VALUE_COUNT 36 /* 每采样的连续浮点数量。 */",
             "static const char *const skeleton_names[16] = { /* 固定资源名称。 */",
             "    " + ", ".join(f'\"{name}\"' for name in PART_ORDER) + "};",
             "static const float skeleton_reference_scales[3] = { /* 固定站姿标尺。 */",
             "    " + ", ".join(map(cfloat, reference_scales)) + "};",
             "static const SkeletonBone skeleton_rigs[3][16] = { /* 三方向绑定。 */"]  # 生成文件行缓存。
    for rig, sizes in zip(rigs, sizes_by_view):  # 当前绑定和真实原图尺寸。
        lines.append("    {")
        for bone in rig["bones"]:  # 当前待写入部件绑定。
            name = bone["name"]  # 当前部件名称。
            parent = PART_ORDER.index(bone["parent"]) if bone["parent"] else -1  # 索引父节点。
            width, height = sizes[name]  # 原图像素尺寸。
            values = [bone["x"], bone["y"], *bone["display_size"],
                      bone["pivot"][0] / width, bone["pivot"][1] / height]  # 紧凑绑定浮点字段。
            lines.append("        {" + str(parent) + ", " + ", ".join(map(cfloat, values)) +
                         f", {width}, {height}}}, /* {name}绑定。 */")
        lines.append("    },")
    lines.append("};")
    lines.append("static const unsigned char skeleton_orders[3][16] = { /* 从后到前的部件索引。 */")
    for rig in rigs:  # 当前方向绑定。
        ordered = sorted(rig["bones"], key=lambda bone: bone["draw_order"])  # 与Python合成一致的图层。
        lines.append("    {" + ", ".join(str(PART_ORDER.index(bone["name"])) for bone in ordered) + "},")
    lines.extend(["};", "static const float skeleton_samples[3][6][60][36] = { /* 仅关节数据，无逐部件冗余偏移。 */"])
    for view in VIEWS:  # 当前方向。
        lines.append("    { /* " + view + "视角。 */")
        for action in ACTIONS:  # 当前基础动作。
            lines.append("        { /* " + action + "动作。 */")
            for frame in range(FRAME_COUNT):  # 当前关节采样帧号。
                values = flatten(sample_pose(action, frame, view))  # 当前动作紧凑采样。
                assert len(values) == VALUE_COUNT
                lines.append("            {" + ",".join(map(cfloat, values)) + "},")
            lines.append("        },")
        lines.append("    },")
    lines.extend(["};", "#endif", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {output}: {output.stat().st_size} source bytes, {3 * 6 * FRAME_COUNT * VALUE_COUNT * 4} pose bytes")


def main() -> None:
    """解析命令行并导出数据；无参数。"""
    parser = argparse.ArgumentParser(description=__doc__)  # 命令行解析器。
    parser.add_argument("--output", type=Path, default=ROOT / "src/skeleton_data.h")
    args = parser.parse_args()  # 用户提供的目标文件路径。
    export(args.output)


if __name__ == "__main__":
    main()
