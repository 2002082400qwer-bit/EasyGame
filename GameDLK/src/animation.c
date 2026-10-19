#include "animation.h"
#include <stddef.h>
#include <math.h>

static const char *names[ANIMATION_CLIP_COUNT] = { // 与图集目录一致的三方向九动作和八组转向
    "front_idle", "front_slow_walk", "front_run", "front_jump", "front_dash", "front_crouch_to_stand", "front_stand_to_jump", "front_stand_to_dash", "front_crouch_to_dash_to_stand",
    "side_idle", "side_slow_walk", "side_run", "side_jump", "side_dash", "side_crouch_to_stand", "side_stand_to_jump", "side_stand_to_dash", "side_crouch_to_dash_to_stand",
    "back_idle", "back_slow_walk", "back_run", "back_jump", "back_dash", "back_crouch_to_stand", "back_stand_to_jump", "back_stand_to_dash", "back_crouch_to_dash_to_stand",
    "turn_front_to_left", "turn_left_to_front", "turn_front_to_right", "turn_right_to_front", "turn_back_to_left", "turn_left_to_back", "turn_back_to_right", "turn_right_to_back"
};

/* 查询资源编号：view为朝向，motion为三方向通用动作。 */
int AnimationClip(ActorView view, ActorMotion motion)
{
    return (view == VIEW_FRONT ? 0 : (view == VIEW_BACK ? 18 : 9)) + motion;
}

/* 查询资源名称：clip为编号；越界时返回空指针。 */
const char *AnimationClipName(int clip)
{
    return clip >= 0 && clip < ANIMATION_CLIP_COUNT ? names[clip] : NULL;
}

/* 查询朝向文字：view为朝向，输出面板英文标签。 */
const char *AnimationViewName(ActorView view)
{
    const char *views[] = {"FRONT", "LEFT", "BACK", "RIGHT"}; // 朝向对应标签
    return views[view];
}

/* 量取帧轮廓：rgba为原像素，width/height为图集尺寸，frame为六十帧编号。 */
float AnimationContentHeight(const unsigned char *rgba, int width, int height, int frame)
{
    if (rgba == NULL || frame < 0 || frame >= ANIMATION_FRAME_COUNT || width <= 0 || height <= 0) return 0.0f;
    const int left = frame % 12 * 256; // 当前帧在十二列图集中的起始横坐标
    const int top = frame / 12 * 256; // 当前帧在图集中的起始纵坐标
    if (left + 256 > width || top + 256 > height) return 0.0f;
    int first = 256; // 核心不透明轮廓的最高行
    int last = -1; // 核心不透明轮廓的最低行
    for (int y = 0; y < 256; ++y) { // 当前帧内部的纵坐标
        for (int x = 0; x < 256; ++x) { // 当前帧内部的横坐标
            const size_t offset = ((size_t)(top + y) * width + left + x) * 4 + 3; // 当前RGBA像素的透明度偏移
            if (rgba[offset] < 128) continue;
            if (y < first) first = y;
            if (y > last) last = y;
        }
    }
    return last >= first ? (float)(last - first + 1) : 0.0f;
}

/* 查询站姿绘制倍率：clip为动作编号，content_height为实测核心高度；蹲跳等姿态不能按站高放大。 */
float AnimationRenderScale(int clip, float content_height)
{
    const bool turn = clip >= 27 && clip < ANIMATION_CLIP_COUNT; // 八组站立转身动作
    const bool standing = clip >= 0 && clip < 27 && (clip % 9 == MOTION_IDLE || clip % 9 == MOTION_WALK); // 三方向待机与慢走，与转身保持共同站姿尺度
    if ((turn || standing) && content_height > 0.0f) return ANIMATION_STANDING_PIXELS / content_height;
    return 1.0f;
}

/* 初始化动画：animation为输出状态，view为初始朝向。 */
void AnimationInit(ActorAnimation *animation, ActorView view)
{
    *animation = (ActorAnimation){.view = view, .requested = view, .turn_clip = -1, .clip = AnimationClip(view, MOTION_IDLE), .mirror = view == VIEW_RIGHT};
}

/* 记录朝向请求：animation为状态，view为目标朝向。 */
void AnimationFace(ActorAnimation *animation, ActorView view)
{
    animation->requested = view;
}

/* 将进度映射到闭区间帧：progress为零到一的播放比例，first/last为起止帧。 */
static int Phase(float progress, int first, int last)
{
    progress = fminf(1.0f, fmaxf(0.0f, progress));
    return first + (int)(progress * (last - first) + 0.00001f);
}

/* 写入普通动作绘制信息：animation为状态，motion为动作，frame为帧号。 */
static void Pose(ActorAnimation *animation, ActorMotion motion, int frame)
{
    animation->clip = AnimationClip(animation->view, motion);
    animation->frame = frame;
    animation->mirror = animation->view == VIEW_RIGHT;
}

/* 查询转向图集：from/to为相邻朝向，只有九十度转向可以直接播放。 */
static int TurnClip(ActorView from, ActorView to)
{
    if (from == VIEW_FRONT) return to == VIEW_LEFT ? 27 : 29;
    if (from == VIEW_BACK) return to == VIEW_LEFT ? 31 : 33;
    if (from == VIEW_LEFT) return to == VIEW_FRONT ? 28 : 32;
    return to == VIEW_FRONT ? 30 : 34;
}

/* 播放转向：animation为状态，dt为更新秒数；返回是否占用本步绘制。 */
static bool Turn(ActorAnimation *animation, float dt)
{
    if (animation->turn_clip >= 0 && animation->turn_time >= ANIMATION_TURN_DURATION) {
        animation->view = animation->turn_to;
        animation->turn_clip = -1;
    }
    if (animation->turn_clip < 0) {
        if (animation->view == animation->requested) return false;
        animation->turn_to = animation->requested;
        if ((animation->view == VIEW_LEFT && animation->requested == VIEW_RIGHT) ||
            (animation->view == VIEW_RIGHT && animation->requested == VIEW_LEFT)) animation->turn_to = VIEW_FRONT;
        if ((animation->view == VIEW_FRONT && animation->requested == VIEW_BACK) ||
            (animation->view == VIEW_BACK && animation->requested == VIEW_FRONT)) animation->turn_to = VIEW_LEFT;
        animation->turn_clip = TurnClip(animation->view, animation->turn_to);
        animation->turn_time = 0.0f;
    } else animation->turn_time = fminf(ANIMATION_TURN_DURATION, animation->turn_time + dt);
    animation->clip = animation->turn_clip;
    animation->frame = Phase(animation->turn_time / ANIMATION_TURN_DURATION, 0, 59);
    animation->mirror = false;
    return true;
}

/* 更新六十帧动画：animation为状态，player为更新后的物理，dt为固定时间步秒数。 */
void AnimationUpdate(ActorAnimation *animation, const Player *player, float dt)
{
    if (dt <= 0.0f) return;
    if (player->action == ACTION_DASH && animation->previous != ACTION_DASH) {
        animation->dash_crouch_start = animation->crouch_pose;
    }
    animation->action_time = animation->previous == player->action ? animation->action_time + dt : 0.0f;
    animation->previous = player->action;
    if ((animation->requested == VIEW_LEFT || animation->requested == VIEW_RIGHT) &&
        (player->move_intent != 0 || player->action == ACTION_DASH)) {
        const int facing = player->action == ACTION_DASH ? player->dash_direction : player->facing; // 冲刺固定朝向，其他动作跟随实际水平朝向
        animation->requested = facing < 0 ? VIEW_LEFT : VIEW_RIGHT;
    }
    const bool immediate = player->move_intent != 0 || player->action == ACTION_DASH || player->action == ACTION_JUMP ||
        player->crouching || animation->crouch_pose > 0.0f || animation->recovery > 0.0f; // 需要立即响应、不能等待站姿转身的动作
    if (immediate && (animation->requested == VIEW_LEFT || animation->requested == VIEW_RIGHT)) {
        animation->view = animation->requested;
        animation->turn_clip = -1;
    }
    if (player->action == ACTION_DASH || player->action == ACTION_JUMP) {
        if (animation->turn_clip >= 0) {
            animation->view = animation->turn_to;
            animation->turn_clip = -1;
        }
        animation->crouch_pose = 0.0f;
        animation->recovery = 0.0f;
        if (player->action == ACTION_DASH) {
            const float progress = 1.0f - player->dash_remaining / PLAYER_DASH_DURATION; // 冲刺物理阶段完成比例
            if (player->dash_from_crouch) {
                Pose(animation, MOTION_CROUCH_DASH, Phase(progress, 0, 39));
                animation->recovery = ANIMATION_RECOVERY_DURATION;
            } else if (player->grounded && progress < 0.28f) {
                Pose(animation, MOTION_DASH_START, Phase(progress / 0.28f, 0, 59));
            } else {
                Pose(animation, MOTION_DASH, Phase(player->grounded ? (progress - 0.28f) / 0.72f : progress, 0, 59));
            }
        } else if (player->jump_time < 0.10f) {
            Pose(animation, MOTION_LAUNCH, Phase(player->jump_time / 0.10f, 0, 59));
        } else {
            const float flight = 2.0f * PLAYER_JUMP_SPEED / PLAYER_GRAVITY; // 同高度起落的预计飞行时间
            Pose(animation, MOTION_JUMP, Phase((player->jump_time - 0.10f) / (flight - 0.10f), 20, 39));
        }
        return;
    }
    if (animation->recovery > 0.0f) {
        animation->recovery = fmaxf(0.0f, animation->recovery - dt);
        Pose(animation, MOTION_CROUCH_DASH, Phase(1.0f - animation->recovery / ANIMATION_RECOVERY_DURATION, 40, 59));
        return;
    }
    const float old_pose = animation->crouch_pose; // 更新前下蹲程度，用于确保起身末帧可以显示
    animation->crouch_pose = fminf(1.0f, fmaxf(0.0f, old_pose + (player->crouching ? dt : -dt) / ANIMATION_CROUCH_DURATION));
    if (player->crouching || old_pose > 0.0f) {
        Pose(animation, MOTION_RISE, Phase(1.0f - animation->crouch_pose, 0, 59));
        return;
    }
    if (player->action == ACTION_LAND) {
        Pose(animation, MOTION_JUMP, Phase(1.0f - player->landing_remaining / PLAYER_LAND_DURATION, 40, 59));
        return;
    }
    if (Turn(animation, dt)) return;
    const ActorMotion motion = player->action == ACTION_RUN ? MOTION_RUN : (player->action == ACTION_WALK ? MOTION_WALK : MOTION_IDLE); // 地面循环动作
    const float duration = motion == MOTION_IDLE ? 2.0f : (motion == MOTION_RUN ? 0.65f : 1.0f); // 循环完整播放时间
    const int frame = (int)((animation->action_time + 0.00001f) * 60.0f / duration) % 60; // 与渲染帧率独立的循环帧
    Pose(animation, motion, frame);
}
