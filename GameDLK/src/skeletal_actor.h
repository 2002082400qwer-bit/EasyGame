#ifndef MX002_SKELETAL_ACTOR_H // 防止动作适配接口重复包含
#define MX002_SKELETAL_ACTOR_H // 动作适配接口保护标记

#include "animation.h"
#include "skeleton.h"

typedef struct SkeletalLayer {
    SkeletonPose pose; // 当前视角的连续关节姿势
    int view; // 绑定索引：正面0、左侧1、背面2
    bool mirror; // 是否将左侧绑定镜像为右侧
    float opacity; // 转向交叉过渡的不透明度
    float width_scale; // 转向仅压窄横向，竖直尺寸保持不变
} SkeletalLayer;

/* 求实时姿势：player提供物理计时，animation提供原35逻辑动作，layers接收至多两层。 */
int SkeletalEvaluate(const Player *player, const ActorAnimation *animation, SkeletalLayer layers[2]);

#endif
