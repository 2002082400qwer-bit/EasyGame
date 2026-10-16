#include "skeletal_actor.h"
#include <math.h>
#include <stdio.h>

static int checks = 0; // 断言总数
static int failures = 0; // 失败总数

/* 记录断言：condition为通过条件，name为可定位的行为名称。 */
static void Check(bool condition, const char *name)
{
    ++checks;
    if (!condition) { ++failures; fprintf(stderr, "FAIL: %s\n", name); }
}

/* 检查每个旧逻辑动作均能用有效的实时部件姿势表达，不依赖对应图集。 */
static void TestLogicalCoverage(void)
{
    for (int clip = 0; clip < ANIMATION_CLIP_COUNT; ++clip) { // 原35个状态编号
        Player player = {.grounded = true}; // 固定地面角色
        ActorAnimation animation = {.clip = clip, .frame = 29, .turn_to = VIEW_LEFT}; // 待试演逻辑状态
        SkeletalLayer layers[2]; // 实时姿势输出
        const int count = SkeletalEvaluate(&player, &animation, layers); // 本动作所需的姿势层数
        Check(count >= 1 && count <= 2, "all 35 logical states have a live pose");
        for (int layer = 0; layer < count; ++layer) { // 当前转向姿势层
            Check(layers[layer].view >= 0 && layers[layer].view < 3, "valid fixed rig view");
            Check(layers[layer].width_scale > 0 && layers[layer].opacity >= 0, "valid turn shape and opacity");
            for (int bone = 0; bone < SKELETON_PART_COUNT; ++bone) { // 当前固定部件
                Check(isfinite(layers[layer].pose.angle[bone]) && layers[layer].pose.scale[bone] > 0, "finite pose and positive part size");
            }
        }
    }
}

/* 检查下蹲全过程双脚接地，跳跃不叠加离线预览的第二份整身升降。 */
static void TestGroundAndJump(void)
{
    for (int view = 0; view < 3; ++view) { // 正、左、背三个绑定
        for (int index = 0; index <= 20; ++index) { // 从站立到完全下蹲的连续深度
            Player player = {.grounded = true, .crouching = true}; // 真实下蹲物理
            ActorAnimation animation = {.clip = view * 9 + MOTION_RISE, .crouch_pose = index / 20.0f}; // 起身/蹲下共用的连续程度
            SkeletalLayer layers[2]; // 接收骨骼姿势
            SkeletonJoint joints[SKELETON_PART_COUNT]; // 真实求解后的关节
            SkeletalEvaluate(&player, &animation, layers);
            SkeletonWorld(view, &layers[0].pose, joints);
            Check(fabsf(joints[12].y - 451) < .02f && fabsf(joints[13].y - 451) < .02f, "crouch interpolation plants both feet");
        }
        Player player = {.action = ACTION_JUMP, .jump_time = .35f, .grounded = false}; // 世界位置已经由物理升起的角色
        ActorAnimation animation = {.clip = view * 9 + MOTION_JUMP, .frame = 30}; // 空中收腿状态
        SkeletalLayer layers[2]; // 接收骨骼姿势
        SkeletalEvaluate(&player, &animation, layers);
        Check(fabsf(layers[0].pose.root[1] + 3.0f) < .01f, "jump removes duplicate preview root translation");
    }
}

/* 检查站冲切换逻辑状态时不会重新站直，蹲冲开头接住原蹲姿。 */
static void TestDashContinuity(void)
{
    const PlayerWorld world = {48, 1232, 600, 640}; // 与实际游戏一致的地面场景
    Player partial; // 只进入一点蹲姿就冲刺的角色
    ActorAnimation partial_animation; // 保留真实状态机下蹲深度的动画
    SkeletalLayer before[2], after[2]; // 起冲前后的实际骨盆位置
    PlayerInit(&partial, &world);
    AnimationInit(&partial_animation, VIEW_LEFT);
    PlayerUpdate(&partial, (PlayerInput){.crouch = true}, &world, 1.0f / 120);
    AnimationUpdate(&partial_animation, &partial, 1.0f / 120);
    SkeletalEvaluate(&partial, &partial_animation, before);
    PlayerUpdate(&partial, (PlayerInput){.crouch = true, .dash = true}, &world, 1.0f / 120);
    AnimationUpdate(&partial_animation, &partial, 1.0f / 120);
    SkeletalEvaluate(&partial, &partial_animation, after);
    Check(fabsf(before[0].pose.root[1] - after[0].pose.root[1]) * SkeletonReferenceScale(1) < 8, "partial crouch dash must not jump into full crouch");
    Player player = {.action = ACTION_DASH, .dash_remaining = PLAYER_DASH_DURATION * .72f, .grounded = true}; // 站冲主体切换点
    ActorAnimation animation = {.view = VIEW_LEFT, .clip = AnimationClip(VIEW_LEFT, MOTION_DASH_START), .frame = 59}; // 切换前逻辑状态
    SkeletalLayer first[2], next[2]; // 切换前后的两份实时姿势
    SkeletalEvaluate(&player, &animation, first);
    animation.clip = AnimationClip(VIEW_LEFT, MOTION_DASH);
    animation.frame = 0;
    SkeletalEvaluate(&player, &animation, next);
    Check(fabsf(first[0].pose.angle[1] - next[0].pose.angle[1]) < .00001f, "dash start and dash body share physical progress");
    player.dash_from_crouch = true;
    player.dash_remaining = PLAYER_DASH_DURATION;
    animation.clip = AnimationClip(VIEW_LEFT, MOTION_CROUCH_DASH);
    animation.dash_crouch_start = 1;
    SkeletalEvaluate(&player, &animation, next);
    Check(next[0].pose.root[1] > 60, "crouch dash begins from crouching instead of popping upright");
}

/* 检查跑步时间连续采样以及转身固定高度、正确端点和权重。 */
static void TestContinuousTimeAndTurns(void)
{
    Player player = {.grounded = true, .action = ACTION_RUN}; // 跑步角色
    ActorAnimation animation = {.clip = AnimationClip(VIEW_LEFT, MOTION_RUN), .action_time = .12345f}; // 两个极近的连续时间
    SkeletalLayer first[2], next[2]; // 连续采样输出
    SkeletalEvaluate(&player, &animation, first);
    animation.action_time += .0001f;
    SkeletalEvaluate(&player, &animation, next);
    Check(fabsf(first[0].pose.angle[8] - next[0].pose.angle[8]) > .000001f, "live pose is not quantized to animation frame number");
    for (int clip = 27; clip < 35; ++clip) { // 八条相邻方向转身
        animation = (ActorAnimation){.clip = clip, .frame = 29};
        const int count = SkeletalEvaluate(&player, &animation, next); // 转向时两个固定视角的过渡
        float opacity = 0; // 两个视角权重之和
        for (int layer = 0; layer < count; ++layer) opacity += next[layer].opacity; // 累加过渡权重
        Check(fabsf(opacity - 1) < .0001f, "turn opacity is normalized");
        Check(next[0].width_scale >= .75f, "turn only narrows modestly without shrinking height");
    }
}

/* 测试入口：不创建窗口、不读取任何图片，验证实时骨骼动作适配；无参数。 */
int main(void)
{
    TestLogicalCoverage();
    TestGroundAndJump();
    TestDashContinuity();
    TestContinuousTimeAndTurns();
    printf("Skeletal actor: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
