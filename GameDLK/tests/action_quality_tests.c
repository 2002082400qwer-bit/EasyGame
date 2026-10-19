#include "motion.h"
#include <math.h>
#include <stdio.h>

static int failures = 0, checks = 0; // 本轮真实动作轮廓回归计数
static const PlayerWorld world = {-10000, 10000, 600, 640}; // 不受边界干扰的输入场地

/* 记录真实行为：condition为验证结果，name为失败提示。 */
static void Check(bool condition, const char *name)
{
    ++checks;
    if (!condition) { ++failures; fprintf(stderr, "FAIL: %s\n", name); }
}

/* 推进真实输入：player/animation/motion为三层实际状态，input为单物理步按键。 */
static void Tick(Player *player, ActorAnimation *animation, MotionState *motion, PlayerInput input)
{
    PlayerUpdate(player, input, &world, 1.0f / 120);
    AnimationUpdate(animation, player, 1.0f / 120);
    MotionUpdate(motion, player, animation, 1.0f / 120);
}

/* 移动反向应保持侧面跑姿，不再一边滑行一边播放正面站立转向。 */
static void TestMovingReversal(void)
{
    Player player, visual; // 物理与显示角色
    ActorAnimation animation; // 方向与动作状态
    MotionState motion; // 持久动作
    SkeletalLayer layers[2]; // 最终显示姿态
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    for (int step = 0; step < 240; ++step) { // 前后连续跑，并反复换向
        const int direction = (step / 40) % 2 ? 1 : -1; // 当前真实输入方向
        Tick(&player, &animation, &motion, (PlayerInput){.move = direction, .run = true});
        const int count = MotionSample(&motion, .5f, &visual, layers); // 与正式绘制一致的采样
        Check(animation.clip < 27 && count == 1 && layers[0].view == 1, "running reversal keeps a single side running silhouette");
        Check(layers[0].width_scale == 1, "running reversal never squeezes the body into a sheet");
        Check(layers[0].mirror == (direction > 0), "reversal responds to the physical direction immediately");
    }
}

/* 站立数字键转向也应保持原图完整横向尺度，不能用压扁模拟体积。 */
static void TestStandingWidth(void)
{
    Player player; // 原地角色
    ActorAnimation animation; // 可指定视角切换进度
    SkeletalLayer layers[2]; // 原逻辑层
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    float minimum = 1; // 全八条转向的最低横向尺度
    for (int clip = 27; clip < 35; ++clip) { // 八条站立视图切换
        animation.clip = clip;
        for (int frame = 0; frame < 60; ++frame) { // 每个阶段都检查，不能只看首尾
            animation.frame = frame;
            const int count = SkeletalEvaluate(&player, &animation, layers); // 真实转换接口
            for (int layer = 0; layer < count; ++layer) minimum = fminf(minimum, layers[layer].width_scale); // 最小显示宽度
        }
    }
    printf("standing turn minimum width scale: %.3f\n", minimum);
    Check(minimum == 1, "standing view changes preserve full width at every phase");
}

/* 跑步需有明显前倾和回收，不只在原走路上加速。 */
static void TestRunSilhouette(void)
{
    Player player; // 跑步角色
    ActorAnimation animation; // 实际状态机
    MotionState motion; // 新动作控制器
    float minimum_lean = 90; // 完整稳定跑周期中的最小前倾
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    for (int step = 0; step < 240; ++step) { // 先进入稳定跑，再记录两个周期
        Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true});
        if (step > 120) minimum_lean = fminf(minimum_lean, -motion.current[0].pose.angle[1] * 57.29578f);
    }
    printf("steady run minimum lean: %.3f deg\n", minimum_lean);
    Check(minimum_lean >= 20, "run has a distinct forward driven torso silhouette");
}

/* 反向必须检查膝肘世界位置，保住脚踝不能掩盖整条腿镜像硬切。 */
static void TestReversalJoints(void)
{
    float maximum = 0; // 不同跑步时机反向的最大膝肘单步位移
    for (int delay = 60; delay < 180; ++delay) { // 覆盖多个腿部接触与腾空阶段
        Player player; // 实际角色
        ActorAnimation animation; // 实际方向状态机
        MotionState motion; // 持久姿态
        SkeletonJoint before[16], after[16]; // 反向前后关节
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        MotionInit(&motion, &player, &animation);
        for (int step = 0; step < delay; ++step) Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true}); // 进入目标跑步相位
        SkeletonWorld(1, &motion.current[0].pose, before);
        const float old_x = player.x; // 反向前的角色世界位置
        Tick(&player, &animation, &motion, (PlayerInput){.move = 1, .run = true});
        SkeletonWorld(1, &motion.current[0].pose, after);
        for (int part = 6; part <= 11; ++part) { // 肘与膝，跳过大腿髋关节
            if (part == 8 || part == 9) continue;
            const float dx = player.x - (after[part].x - 256) * SkeletonReferenceScale(1) - old_x - (before[part].x - 256) * SkeletonReferenceScale(1); // 镜像前后真实世界横向差
            const float dy = (after[part].y - before[part].y) * SkeletonReferenceScale(1); // 世界纵向差
            maximum = fmaxf(maximum, hypotf(dx, dy));
        }
    }
    printf("reversal knee/elbow maximum: %.3f px/tick\n", maximum);
    Check(maximum < 12, "reversal blends the actual knees and elbows rather than only pinning feet");
}

/* 扫描整个反向过渡和再次中断，防止重复抬腿把脚抬过髋部造成IK膝翻分支。 */
static void TestRepeatedReversal(void)
{
    float maximum = 0, highest_foot = 451; // 全部过渡内部最大关节位移与最高脚位置
    for (int cadence = 0; cadence <= 12; cadence += 12) { // 单次反向，以及每十二步再次反向
        Player player; // 真实物理状态
        ActorAnimation animation; // 真实方向状态
        MotionState motion; // 持久动作
        SkeletonJoint previous[16]; // 上一步的真实关节
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        MotionInit(&motion, &player, &animation);
        for (int step = 0; step < 89; ++step) Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true}); // 进入独立复审复现的步态阶段
        float old_x = player.x, old_sign = 1; // 上一步世界位置和镜像
        SkeletonWorld(1, &motion.current[0].pose, previous);
        for (int step = 0; step < 120; ++step) { // 覆盖完整反向过程和十次再次中断
            Tick(&player, &animation, &motion, (PlayerInput){.move = cadence == 0 || (step / cadence) % 2 == 0 ? 1 : -1, .run = true});
            SkeletonJoint current[16]; // 本步实际关节
            SkeletonWorld(1, &motion.current[0].pose, current);
            const float sign = motion.current[0].mirror ? -1 : 1; // 本步世界投影方向
            for (int part = 6; part <= 11; ++part) { // 肘与膝
                if (part == 8 || part == 9) continue;
                const float dx = player.x + sign * (current[part].x - 256) * SkeletonReferenceScale(1) - old_x - old_sign * (previous[part].x - 256) * SkeletonReferenceScale(1); // 世界横向差
                maximum = fmaxf(maximum, hypotf(dx, (current[part].y - previous[part].y) * SkeletonReferenceScale(1)));
            }
            highest_foot = fminf(highest_foot, fminf(current[12].y, current[13].y));
            for (int part = 0; part < 16; ++part) previous[part] = current[part]; // 保存下一步基准
            old_x = player.x;
            old_sign = sign;
        }
    }
    printf("full repeated reversal joint maximum: %.3f px/tick; highest ankle %.3f\n", maximum, highest_foot);
    Check(maximum < 18, "the entire reversal remains continuous through repeated interruptions");
    Check(highest_foot >= 330, "reversal never adds a second full lift to an already raised foot");
}

/* 显示中点也检查真实脚位，防止角度与段长插值让已锁住的脚画出一条弧线。 */
static void TestInterpolatedFeet(void)
{
    Player player, visual; // 物理和显示位置
    ActorAnimation animation; // 方向状态机
    MotionState motion; // 前后两物理步
    SkeletalLayer layers[2]; // 实际显示采样
    float maximum = 0; // 中间显示姿态偏离两端脚位直线的最大值
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    for (int step = 0; step < 360; ++step) { // 跑步与数次反向
        Tick(&player, &animation, &motion, (PlayerInput){.move = step < 120 || step >= 240 ? -1 : 1, .run = true});
        MotionSample(&motion, .5f, &visual, layers);
        if (motion.previous[0].mirror != motion.current[0].mirror) continue;
        SkeletonJoint before[16], after[16], middle[16]; // 前、后、显示中点关节
        SkeletonWorld(1, &motion.previous[0].pose, before);
        SkeletonWorld(1, &motion.current[0].pose, after);
        SkeletonWorld(1, &layers[0].pose, middle);
        for (int side = 0; side < 2; ++side) { // 两只脚
            const float dx = middle[12 + side].x - (before[12 + side].x + after[12 + side].x) * .5f; // 画布横坐标差，世界位置已经线性插值
            const float dy = middle[12 + side].y - (before[12 + side].y + after[12 + side].y) * .5f; // 画布纵坐标差
            maximum = fmaxf(maximum, hypotf(dx, dy) * SkeletonReferenceScale(1));
        }
    }
    printf("interpolated ankle projection maximum error: %.3f px\n", maximum);
    Check(maximum < .1f, "render interpolation keeps the intended world ankle path");
}

/* 执行反向、完整转向尺度和跑姿回归；无参数，失败返回1。 */
int main(void)
{
    TestMovingReversal();
    TestStandingWidth();
    TestRunSilhouette();
    TestReversalJoints();
    TestRepeatedReversal();
    TestInterpolatedFeet();
    printf("action quality: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
