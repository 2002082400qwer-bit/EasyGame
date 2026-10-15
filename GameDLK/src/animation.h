#ifndef MX002_ANIMATION_H // 防止动画接口重复包含
#define MX002_ANIMATION_H // 动画模块头文件保护标记

#include "player.h"

#define ANIMATION_CLIP_COUNT 35 // 三方向动作及八组转向图集总数
#define ANIMATION_FRAME_COUNT 60 // 每个图集的独立帧数
#define ANIMATION_TURN_DURATION 0.16f // 每次九十度转身播放秒数
#define ANIMATION_CROUCH_DURATION 0.18f // 完整下蹲或起身播放秒数
#define ANIMATION_RECOVERY_DURATION 0.12f // 蹲姿冲刺结束后的站立恢复秒数

typedef enum ActorView {
    VIEW_FRONT, // 正面
    VIEW_LEFT, // 左侧面，侧面原图朝向
    VIEW_BACK, // 背面
    VIEW_RIGHT // 右侧面，普通侧面动作需要镜像
} ActorView;

typedef enum ActorMotion {
    MOTION_IDLE, // 呼吸待机
    MOTION_WALK, // 慢走循环
    MOTION_RUN, // 跑步循环
    MOTION_JUMP, // 跳跃及落地阶段
    MOTION_DASH, // 冲刺主体
    MOTION_RISE, // 起身，倒放时为蹲下
    MOTION_LAUNCH, // 站立到跳跃
    MOTION_DASH_START, // 站立到冲刺
    MOTION_CROUCH_DASH // 下蹲到冲刺再站立
} ActorMotion;

typedef struct ActorAnimation {
    ActorView view; // 已完成转向的朝向
    ActorView requested; // 输入请求最终到达的朝向
    ActorView turn_to; // 当前九十度转向的目标朝向
    int turn_clip; // 当前转向图集编号，负值表示未转向
    float turn_time; // 当前转向已播放秒数
    float action_time; // 当前物理动作的累计动画时间
    float crouch_pose; // 下蹲程度，零为站立，一为完全下蹲
    float recovery; // 蹲姿冲刺的剩余站立恢复时间
    PlayerAction previous; // 上次更新时的物理动作
    int clip; // 当前绘制图集编号
    int frame; // 当前绘制帧，范围零到五十九
    bool mirror; // 当前图集是否需要水平镜像
} ActorAnimation;

/* 初始化动画：animation为输出状态，view为初始朝向。 */
void AnimationInit(ActorAnimation *animation, ActorView view);
/* 请求转向：animation为状态，view为最终朝向；对面朝向经两次九十度转向到达。 */
void AnimationFace(ActorAnimation *animation, ActorView view);
/* 更新动画：animation为输出状态，player为更新后的物理状态，dt为固定步秒数。 */
void AnimationUpdate(ActorAnimation *animation, const Player *player, float dt);
/* 查询图集编号：view为角色朝向，motion为动作；右侧普通动作复用左侧资源。 */
int AnimationClip(ActorView view, ActorMotion motion);
/* 查询资源名称：clip为图集编号，返回不含扩展名的文件名，非法编号返回空指针。 */
const char *AnimationClipName(int clip);
/* 查询朝向标签：view为朝向，返回窗口使用的英文名称。 */
const char *AnimationViewName(ActorView view);

#endif
