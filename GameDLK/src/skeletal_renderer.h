#ifndef MX002_SKELETAL_RENDERER_H // 防止拆件绘制接口重复包含
#define MX002_SKELETAL_RENDERER_H // 拆件绘制接口保护标记

#include "raylib.h"
#include "skeletal_actor.h"
#include <wchar.h>

typedef struct SkeletalRenderer {
    Texture2D textures[SKELETON_VIEW_COUNT][SKELETON_PART_COUNT]; // 一次加载的48张固定部件纹理
    unsigned int texture_count; // 已成功上传的纹理数量
    unsigned long long texture_bytes; // 纹理RGBA像素占用字节数，不含驱动额外开销
    bool failed; // 部件加载或绘制失败标记
} SkeletalRenderer;

/* 加载固定拆件：renderer接收纹理和状态，root为程序定位的工程根目录。 */
bool SkeletalRendererLoad(SkeletalRenderer *renderer, const wchar_t *root);
/* 释放拆件：renderer为全部或部分加载的纹理，允许失败路径调用。 */
void SkeletalRendererUnload(SkeletalRenderer *renderer);
/* 实时绘制：renderer为部件，player为世界位置，animation为逻辑状态，opacity/offset为残影参数。 */
void SkeletalRendererDraw(SkeletalRenderer *renderer, const Player *player, const ActorAnimation *animation, float opacity, float offset);

#endif
