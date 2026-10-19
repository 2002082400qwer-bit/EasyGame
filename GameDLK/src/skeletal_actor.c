#include "skeletal_actor.h"
#include <math.h>

#define ACTOR_PI 3.14159265358979323846f // 动作包络使用的圆周率

/* 夹紧动作比例：value为任意进度，输出零到一。 */
static float Clamp(float value)
{
    return fmaxf(0, fminf(1, value));
}

/* 平滑动作混合：value为零到一进度，输出两端速度为零的权重。 */
static float Smooth(float value)
{
    value = Clamp(value);
    return value * value * (3 - 2 * value);
}

/* 初始化绘制层：view为四向逻辑朝向，layer接收固定三视图绑定和镜像信息。 */
static void Layer(ActorView view, SkeletalLayer *layer)
{
    *layer = (SkeletalLayer){.view = view == VIEW_FRONT ? 0 : view == VIEW_BACK ? 2 : 1,
        .mirror = view == VIEW_RIGHT, .opacity = 1, .width_scale = 1};
}

/* 将混合后的支撑脚回贴地面：view为绑定索引，pose为待校正的全身姿势。 */
static void GroundSupport(int view, SkeletonPose *pose)
{
    SkeletonJoint joints[SKELETON_PART_COUNT]; // 混合姿势的实际脚踝位置
    SkeletonWorld(view, pose, joints);
    pose->root[1] += 451 - joints[12].y;
}

/* 适配原35逻辑状态：player提供真实物理，animation提供衔接和连续时间，layers输出至多两视角。 */
int SkeletalEvaluate(const Player *player, const ActorAnimation *animation, SkeletalLayer layers[2])
{
    const int clip = animation->clip; // 原状态机的逻辑动作编号，不再用作图片文件编号
    const float frame_phase = Clamp(animation->frame / 59.0f); // 检查/转向场景需要的离散阶段提示
    if (clip >= 27 && clip < ANIMATION_CLIP_COUNT) {
        static const ActorView from[] = {VIEW_FRONT, VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_BACK, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT}; // 八条转身的起始方向
        static const ActorView to[] = {VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_FRONT, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT, VIEW_BACK}; // 八条转身的目标方向
        const float phase = animation->turn_clip >= 27 ? Clamp(animation->turn_time / ANIMATION_TURN_DURATION) : frame_phase; // 运行按连续转向时间，直接姿态检查使用指定阶段
        const float blend = Smooth((phase - .30f) / .40f); // 仅在中段短暂交叉过渡两个固定视角
        for (int index = 0; index < 2; ++index) { // 转向来源和目标视角
            Layer(index == 0 ? from[clip - 27] : to[clip - 27], &layers[index]);
            SkeletonSample(layers[index].view, SKELETON_IDLE, 0, true, &layers[index].pose);
            layers[index].opacity = index == 0 ? 1 - blend : blend;
            layers[index].width_scale = 1; // 固定原图宽度，禁止用压扁整个人模拟转身
        }
        return 2;
    }
    const int safe_clip = clip >= 0 && clip < 27 ? clip : 9; // 无效内部状态回退到左侧待机
    const ActorMotion motion = (ActorMotion)(safe_clip % 9); // 保留的九类动作/衔接状态
    const ActorView logical_view = safe_clip < 9 ? VIEW_FRONT : safe_clip >= 18 ? VIEW_BACK : animation->mirror ? VIEW_RIGHT : VIEW_LEFT; // 三方向绑定与右侧镜像
    Layer(logical_view, &layers[0]);
    SkeletalLayer *layer = &layers[0]; // 普通动作只需一套固定部件
    if (motion == MOTION_IDLE || motion == MOTION_WALK || motion == MOTION_RUN) {
        const SkeletonAction action = motion == MOTION_RUN ? SKELETON_RUN : motion == MOTION_WALK ? SKELETON_WALK : SKELETON_IDLE; // 基础循环动作
        const float duration = motion == MOTION_IDLE ? 2 : motion == MOTION_RUN ? .65f : 1; // 保留原运行节奏
        SkeletonSample(layer->view, action, animation->action_time / duration, true, &layer->pose);
    } else if (motion == MOTION_RISE) {
        SkeletonCrouch(layer->view, animation->crouch_pose, &layer->pose);
    } else if (motion == MOTION_LAUNCH || motion == MOTION_JUMP) {
        if (player->grounded && player->action == ACTION_LAND) {
            const float phase = Clamp(1 - player->landing_remaining / PLAYER_LAND_DURATION); // 真实碰撞之后的落地恢复进度
            SkeletonCrouch(layer->view, .30f * sinf(ACTOR_PI * phase), &layer->pose);
        } else {
            const float flight = 2 * PLAYER_JUMP_SPEED / PLAYER_GRAVITY; // 原物理同高度起落的总飞行时间
            const float phase = player->action == ACTION_JUMP ? Clamp(player->jump_time / flight) : frame_phase; // 起跳和空中共用同一时间轴
            SkeletonSample(layer->view, SKELETON_JUMP, phase, false, &layer->pose);
            layer->pose.root[1] = -3; // 世界升降已经由player.y处理，只保留收腿与上身动作
        }
    } else {
        if (motion == MOTION_CROUCH_DASH && player->action != ACTION_DASH && animation->recovery > 0) {
            SkeletonSample(layer->view, SKELETON_IDLE, animation->action_time / 2, true, &layer->pose);
        } else {
            const float phase = player->action == ACTION_DASH ? Clamp(1 - player->dash_remaining / PLAYER_DASH_DURATION) : frame_phase; // 站冲开始/主体和蹲冲共享物理计时
            SkeletonSample(layer->view, SKELETON_DASH, phase, false, &layer->pose);
            if (animation->dash_crouch_start > 0 && phase < .20f) {
                SkeletonPose crouching; // 起步前保留下蹲姿势
                SkeletonCrouch(layer->view, animation->dash_crouch_start, &crouching);
                SkeletonBlend(&crouching, &layer->pose, Smooth(phase / .20f), &layer->pose);
                GroundSupport(layer->view, &layer->pose);
            }
        }
    }
    return 1;
}
