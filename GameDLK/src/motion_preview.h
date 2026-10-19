#ifndef MX002_MOTION_PREVIEW_H // 动态GPU对照接口保护
#define MX002_MOTION_PREVIEW_H // 动态对照接口标记
#include "skeletal_renderer.h"
/* 真实GPU回放：renderer为已加载的48拆件，root为验证产物输出工程根目录，返回退出码。 */
int MotionPreview(SkeletalRenderer *renderer, const wchar_t *root);
#endif
