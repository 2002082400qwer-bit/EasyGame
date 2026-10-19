#include "skeletal_renderer.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define PART_PATH_CAPACITY 32768 // 支持中文工程的宽字符文件路径容量
#define PART_MAX_BYTES 8000000 // 固定拆件压缩文件的最大允许字节数

/* 解码固定PNG：path为宽字符完整路径，返回尚未上传的图像，失败返回零结构。 */
static Image ReadPart(const wchar_t *path)
{
    FILE *file = _wfopen(path, L"rb"); // 中文路径拆件输入文件
    if (file == NULL) return (Image){0};
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return (Image){0}; }
    const long size = ftell(file); // PNG实际压缩字节数
    rewind(file);
    if (size <= 0 || size > PART_MAX_BYTES) { fclose(file); return (Image){0}; }
    unsigned char *bytes = malloc((size_t)size); // 仅解码期间持有的PNG压缩数据
    if (bytes == NULL) { fclose(file); return (Image){0}; }
    const size_t read = fread(bytes, 1, (size_t)size, file); // 实际读入字节数
    fclose(file);
    Image image = {0}; // 返回的解码图像
    if (read == (size_t)size) image = LoadImageFromMemory(".png", bytes, (int)size);
    free(bytes);
    return image;
}

/* 一次加载48件：renderer为输出纹理集合，root为工程根目录，不读取任何逐帧图片或图集。 */
bool SkeletalRendererLoad(SkeletalRenderer *renderer, const wchar_t *root)
{
    static const char *views[] = {"front", "side", "back"}; // 既有三方向拆件文件夹
    for (int view = 0; view < SKELETON_VIEW_COUNT; ++view) { // 当前绑定方向
        const SkeletonBone *bones = SkeletonRig(view); // 同时用来验证原部件尺寸的绑定
        for (int part = 0; part < SKELETON_PART_COUNT; ++part) { // 当前固定部件
            wchar_t path[PART_PATH_CAPACITY]; // 待加载PNG的完整中文路径
            swprintf(path, PART_PATH_CAPACITY, L"%ls\\图片\\游戏素材\\骨骼拆分方式\\parts\\%hs\\%hs.png", root, views[view], SkeletonPartName(part));
            Image image = ReadPart(path); // 解码后的真实部件像素
            if (image.data == NULL || image.width != bones[part].source_width || image.height != bones[part].source_height) {
                if (image.data != NULL) UnloadImage(image);
                fprintf(stderr, "Missing or invalid skeletal part: %s/%s.png\n", views[view], SkeletonPartName(part));
                renderer->failed = true;
                return false;
            }
            ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            renderer->textures[view][part] = LoadTextureFromImage(image);
            renderer->texture_bytes += (unsigned long long)image.width * (unsigned int)image.height * 4;
            UnloadImage(image);
            if (renderer->textures[view][part].id == 0) {
                fprintf(stderr, "Could not upload skeletal part: %s/%s.png\n", views[view], SkeletonPartName(part));
                renderer->failed = true;
                return false;
            }
            SetTextureFilter(renderer->textures[view][part], TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(renderer->textures[view][part], TEXTURE_WRAP_CLAMP);
            ++renderer->texture_count;
        }
    }
    return true;
}

/* 释放全部或部分加载的部件：renderer为纹理集合，每个纹理最多释放一次。 */
void SkeletalRendererUnload(SkeletalRenderer *renderer)
{
    for (int view = 0; view < SKELETON_VIEW_COUNT; ++view) { // 当前绑定方向
        for (int part = 0; part < SKELETON_PART_COUNT; ++part) { // 当前固定部件
            if (renderer->textures[view][part].id != 0) UnloadTexture(renderer->textures[view][part]);
            renderer->textures[view][part] = (Texture2D){0};
        }
    }
    renderer->texture_count = 0;
}

/* 绘制柔性拆件：texture为原图，target/origin/angle为绑定变换，bend为尾部弯曲，mirror为反射，color为透明度。 */
static void DrawFlexible(Texture2D texture, Rectangle target, Vector2 origin, float angle, float bend, bool mirror, Color color)
{
    const float cosine = cosf(angle * DEG2RAD), sine = sinf(angle * DEG2RAD); // 关节旋转的正余弦
    const float corners[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}}; // 与raylib纹理四边形一致的绕序
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(0, 0, 1);
    for (int row = 0; row < 10; ++row) { // 十段纵向网格保持纹理连续，根部不变形
        for (int corner = 0; corner < 4; ++corner) { // 当前条带四个顶点
            const float u = corners[corner][0], v = (row + corners[corner][1]) / 10.0f; // 原图连续UV
            float point[2]; // 变形后相对关节的局部坐标
            MotionRibbon(u, v, target.width, target.height, origin.x / target.width, origin.y / target.height, mirror ? -bend : bend, point);
            rlTexCoord2f(mirror ? 1 - u : u, v);
            rlVertex2f(target.x + cosine * point[0] - sine * point[1], target.y + sine * point[0] + cosine * point[1]);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

/* 按实时骨骼绘制：renderer为纹理集合，player/animation为世界与动作，opacity/offset为残影参数。 */
void SkeletalRendererDraw(SkeletalRenderer *renderer, const Player *player, const ActorAnimation *animation, float opacity, float offset)
{
    SkeletalLayer layers[2]; // 普通动作一层，短转向最多两个视角层
    const int count = renderer->animated_count > 0 ? renderer->animated_count : SkeletalEvaluate(player, animation, layers); // 正式运行使用持久连续姿态
    if (renderer->animated_count > 0) for (int index = 0; index < count; ++index) layers[index] = renderer->animated_layers[index]; // 残影反复读取，不推进状态
    for (int index = 0; index < count; ++index) { // 当前视角层
        const SkeletalLayer *layer = &layers[index]; // 待合成的姿态及透明度
        if (layer->opacity <= 0) continue;
        const SkeletonBone *bones = SkeletonRig(layer->view); // 本方向固定绑定
        SkeletonJoint joints[SKELETON_PART_COUNT]; // 当前连续时间下的世界关节
        SkeletonWorld(layer->view, &layer->pose, joints);
        const float scale = SkeletonReferenceScale(layer->view); // 原骨架坐标到固定208像素站姿的唯一标尺
        const float sign = layer->mirror ? -1 : 1; // 朝右时全身关节的横向镜像符号
        for (int depth = 0; depth < SKELETON_PART_COUNT; ++depth) { // 从远到近的固定拆件顺序
            const int part = SkeletonDrawOrder(layer->view, depth); // 当前深度对应的部件索引
            const SkeletonBone *bone = &bones[part]; // 绑定尺寸及归一枢轴
            const Texture2D texture = renderer->textures[layer->view][part]; // 仅引用已上传纹理，逐帧不加载文件
            if (texture.id == 0) { renderer->failed = true; return; }
            const float width = bone->width * scale * layer->width_scale; // 横向转向压窄后的显示宽度
            const float height = bone->height * scale * layer->pose.scale[part]; // 纵向透视由骨骼姿势控制，不按姿态重新归一站高
            const int slot = part == 3 ? 0 : part == 14 ? 1 : part == 15 ? 2 : -1; // 有反向投影根部偏移的柔性附件
            const float shift_x = slot >= 0 ? layer->shift[slot][0] : 0, shift_y = slot >= 0 ? layer->shift[slot][1] : 0; // 已插值的根部偏移
            const Rectangle source = {0, 0, layer->mirror ? -(float)texture.width : (float)texture.width, (float)texture.height}; // 右侧普通动作只镜像一次
            const Rectangle target = {player->x + offset + sign * (joints[part].x + shift_x - 256) * scale * layer->width_scale,
                player->y + (joints[part].y + shift_y - 471) * scale, width, height}; // 以同一世界脚底定位关节
            const Vector2 origin = {(layer->mirror ? 1 - bone->pivot_x : bone->pivot_x) * width, bone->pivot_y * height}; // 纹理镜像后枢轴也必须反射
            if (part == 3 || part == 14 || part == 15) {
                DrawFlexible(texture, target, origin, sign * joints[part].angle * RAD2DEG,
                    layer->bend[part == 3 ? 0 : part - 13], layer->mirror, Fade(WHITE, opacity * layer->opacity));
            } else DrawTexturePro(texture, source, target, origin, sign * joints[part].angle * RAD2DEG, Fade(WHITE, opacity * layer->opacity));
        }
    }
}
