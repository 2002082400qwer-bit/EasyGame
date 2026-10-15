#include "animation.h"
#include <stdio.h>
#include <string.h>

static int checks = 0; // 动画断言总数
static int failures = 0; // 动画断言失败数
static const PlayerWorld world = {48, 1232, 600, 640}; // 独立测试场地
static const float step = 1.0f / 120.0f; // 与游戏一致的固定时间步

/* 记录断言：condition为实际条件，name为失败说明。 */
static void Check(bool condition, const char *name)
{
    ++checks;
    if (!condition) { ++failures; printf("FAIL: %s\n", name); }
}

/* 推进真实控制器和动画：player为角色，animation为动画，input为输入，count为步数。 */
static void Tick(Player *player, ActorAnimation *animation, PlayerInput input, int count)
{
    for (int index = 0; index < count; ++index) { // 当前固定更新序号
        PlayerUpdate(player, input, &world, step);
        AnimationUpdate(animation, player, step);
        Check(animation->frame >= 0 && animation->frame < 60, "all action frames stay inside atlas");
        input.jump = false;
        input.dash = false;
    }
}

/* 检查三方向完整动作：view为待验证朝向，所有输入通过正式物理和动画接口执行。 */
static void TestActions(ActorView view)
{
    Player player; // 当前方向的独立角色
    ActorAnimation animation; // 当前方向的独立动画
    PlayerInit(&player, &world);
    AnimationInit(&animation, view);
    Check(animation.clip == AnimationClip(view, MOTION_IDLE), "dedicated idle selected");
    Tick(&player, &animation, (PlayerInput){0}, 240);
    Check(animation.frame <= 1, "idle loops after two seconds");
    Tick(&player, &animation, (PlayerInput){.move = -1}, 20);
    Check(animation.clip == AnimationClip(view, MOTION_WALK), "walk uses 60 frame clip");
    Tick(&player, &animation, (PlayerInput){.move = -1, .run = true}, 20);
    Check(animation.clip == AnimationClip(view, MOTION_RUN) && player.vx == -PLAYER_RUN_SPEED, "run clip agrees with run physics");
    Tick(&player, &animation, (PlayerInput){.crouch = true}, 30);
    Check(animation.clip == AnimationClip(view, MOTION_RISE) && animation.frame == 0, "crouch holds deep crouching pose");
    Tick(&player, &animation, (PlayerInput){0}, 8);
    Check(animation.clip == AnimationClip(view, MOTION_RISE) && animation.frame > 0 && animation.frame < 59, "release crouch plays rise transition");
    Tick(&player, &animation, (PlayerInput){0}, 30);
    Tick(&player, &animation, (PlayerInput){.jump = true}, 1);
    Check(animation.clip == AnimationClip(view, MOTION_LAUNCH), "jump starts with standing to jump transition");
    Tick(&player, &animation, (PlayerInput){0}, 28);
    Check(animation.clip == AnimationClip(view, MOTION_JUMP) && animation.frame >= 20 && animation.frame < 40, "airborne jump skips grounded anticipation");
    Tick(&player, &animation, (PlayerInput){0}, 65);
    Check(player.grounded && animation.clip == AnimationClip(view, MOTION_JUMP) && animation.frame >= 40, "landing shows compression and recovery");
    Tick(&player, &animation, (PlayerInput){0}, 50);
    Tick(&player, &animation, (PlayerInput){.dash = true}, 1);
    Check(animation.clip == AnimationClip(view, MOTION_DASH_START), "standing dash starts with transition");
    Tick(&player, &animation, (PlayerInput){0}, 10);
    Check(animation.clip == AnimationClip(view, MOTION_DASH), "standing dash enters dash body");
    Tick(&player, &animation, (PlayerInput){.crouch = true}, 95);
    Tick(&player, &animation, (PlayerInput){.crouch = true, .dash = true}, 1);
    Check(animation.clip == AnimationClip(view, MOTION_CROUCH_DASH), "crouching dash selects dedicated full transition");
    Tick(&player, &animation, (PlayerInput){.crouch = true}, 24);
    Check(animation.clip == AnimationClip(view, MOTION_CROUCH_DASH) && animation.frame >= 40, "crouching dash plays standing recovery");
    Tick(&player, &animation, (PlayerInput){.crouch = true}, 30);
    Check(animation.clip == AnimationClip(view, MOTION_IDLE) && !player.crouching, "crouching dash finishes standing idle");
}

/* 验证八条九十度转向及一百八十度串接，右转图集不能二次镜像。 */
static void TestTurns(void)
{
    const ActorView origins[] = {VIEW_FRONT, VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_BACK, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT}; // 八组转向起点
    const ActorView targets[] = {VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_FRONT, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT, VIEW_BACK}; // 八组转向终点
    const char *names[] = {"turn_front_to_left", "turn_left_to_front", "turn_front_to_right", "turn_right_to_front", "turn_back_to_left", "turn_left_to_back", "turn_back_to_right", "turn_right_to_back"}; // 对应资源名称
    for (int index = 0; index < 8; ++index) { // 当前转向测试编号
        Player player; // 原地转向的角色
        ActorAnimation animation; // 转向状态
        PlayerInit(&player, &world);
        AnimationInit(&animation, origins[index]);
        AnimationFace(&animation, targets[index]);
        Tick(&player, &animation, (PlayerInput){0}, 10);
        Check(strcmp(AnimationClipName(animation.clip), names[index]) == 0 && !animation.mirror, "correct authored turn without extra mirroring");
        Tick(&player, &animation, (PlayerInput){0}, 40);
        Check(animation.view == targets[index] && animation.clip == AnimationClip(targets[index], MOTION_IDLE), "turn completes into destination idle");
        Check(animation.mirror == (targets[index] == VIEW_RIGHT), "only ordinary right side clips are mirrored");
    }
    Player player; // 一百八十度转身角色
    ActorAnimation animation; // 串接转向状态
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    AnimationFace(&animation, VIEW_RIGHT);
    Tick(&player, &animation, (PlayerInput){0}, 60);
    Check(animation.view == VIEW_RIGHT, "opposite sides connect through two authored turns");
    AnimationFace(&animation, VIEW_BACK);
    Tick(&player, &animation, (PlayerInput){.jump = true}, 1);
    Check(animation.clip == AnimationClip(VIEW_RIGHT, MOTION_LAUNCH), "jump input interrupts pending turn immediately");
}

/* 验证组合输入朝向：左右两侧的移动加跳跃、冲刺、蹲行及转身中断都应采用实际移动朝向。 */
static void TestDirectionalActions(void)
{
    for (int direction = -1; direction <= 1; direction += 2) { // 分别验证向左和向右的对称情形
        const ActorView origin = direction > 0 ? VIEW_LEFT : VIEW_RIGHT; // 与即将移动方向相反的初始朝向
        const ActorView target = direction > 0 ? VIEW_RIGHT : VIEW_LEFT; // 输入对应的期望朝向
        const PlayerInput inputs[] = {{.move = direction, .jump = true}, {.move = direction, .dash = true}, {.move = direction, .crouch = true}}; // 三组需要即时响应的组合输入
        for (int index = 0; index < 3; ++index) { // 当前组合输入编号
            Player player; // 接受组合输入的角色
            ActorAnimation animation; // 与角色同步的动画
            PlayerInit(&player, &world);
            player.facing = -direction;
            AnimationInit(&animation, origin);
            Tick(&player, &animation, inputs[index], 1);
            Check(animation.view == target && animation.mirror == (direction > 0), "direction plus action immediately matches physical facing");
        }
        Player player; // 用于验证转身中断的角色
        ActorAnimation animation; // 一百八十度转身中间状态
        PlayerInit(&player, &world);
        player.facing = -direction;
        AnimationInit(&animation, origin);
        Tick(&player, &animation, (PlayerInput){.move = direction}, 1);
        Tick(&player, &animation, (PlayerInput){.move = direction, .dash = true}, 1);
        Check(animation.view == target && animation.clip == AnimationClip(target, MOTION_DASH_START), "dash interrupts turn into final side rather than intermediate front");
        Tick(&player, &animation, (PlayerInput){.move = -direction}, 8);
        Check(animation.view == target, "opposite movement cannot flip a locked dash");
        PlayerInit(&player, &world);
        player.facing = -direction;
        player.x = direction > 0 ? world.right : world.left;
        AnimationInit(&animation, origin);
        Tick(&player, &animation, (PlayerInput){.move = direction, .jump = true}, 1);
        Check(player.vx == 0.0f && animation.view == target && animation.mirror == (direction > 0), "blocked horizontal movement still updates airborne facing");
    }
}

/* 测试入口：验证资源名唯一、三方向全部动作、转向与镜像规则，无命令行参数。 */
int main(void)
{
    for (int clip = 0; clip < ANIMATION_CLIP_COUNT; ++clip) { // 当前图集索引
        Check(AnimationClipName(clip) != NULL, "every animation has an atlas name");
        for (int previous = 0; previous < clip; ++previous) { // 当前图集之前的资源索引
            Check(strcmp(AnimationClipName(clip), AnimationClipName(previous)) != 0, "atlas names are unique");
        }
    }
    TestActions(VIEW_FRONT);
    TestActions(VIEW_LEFT);
    TestActions(VIEW_BACK);
    TestTurns();
    TestDirectionalActions();
    printf("Animation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
