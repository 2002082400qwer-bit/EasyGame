#include "motion.h"
#include <math.h>
#include <stdio.h>

static int checks = 0; // 本轮动作连续性断言数量
static int failures = 0; // 本轮失败断言数量
static const PlayerWorld world = {-10000, 10000, 600, 640}; // 不触碰边界的输入回归场地

/* 记录行为：condition为真实结果，name为失败原因。 */
static void Check(bool condition, const char *name)
{
    ++checks;
    if (!condition) { ++failures; fprintf(stderr, "FAIL: %s\n", name); }
}

/* 推进真实输入：player/animation为被测状态，input为单步输入。 */
static void Tick(Player *player, ActorAnimation *animation, MotionState *motion, PlayerInput input)
{
    PlayerUpdate(player, input, &world, 1.0f / 120);
    AnimationUpdate(animation, player, 1.0f / 120);
    MotionUpdate(motion, player, animation, 1.0f / 120);
}

/* 复现行走途中按Ctrl，要求摆臂不能突然跳回起始相位。 */
static void TestWalkRunCut(void)
{
    Player player; // 测试角色
    ActorAnimation animation; // 真实状态机
    SkeletalLayer before[2], after[2]; // 切换前后实际姿态
    MotionState motion; // 实际连续动作控制器
    Player visual; // 渲染采样位置
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    float maximum = 0; // 不同中断时机的最大左上臂单步角差
    for (int delay = 1; delay < 120; ++delay) { // 覆盖整个行走周期中的Ctrl按下时机
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        MotionInit(&motion, &player, &animation);
        for (int step = 0; step < delay; ++step) Tick(&player, &animation, &motion, (PlayerInput){.move = -1}); // 中断前真实行走
        MotionSample(&motion, 1, &visual, before);
        Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true});
        MotionSample(&motion, 1, &visual, after);
        const float degrees = fabsf(remainderf(after[0].pose.angle[4] - before[0].pose.angle[4], 6.2831853f)) * 57.29578f; // 切换单步角差
        maximum = fmaxf(maximum, degrees);
    }
    printf("walk->run upper arm maximum jump: %.3f deg\n", maximum);
    Check(maximum < 12, "walk to run must preserve the current arm pose instead of restarting the cycle");
}

/* 验证走跑相位连续、真实支撑锚点、被墙挡住时不继续踏步。 */
static void TestPhaseAndFeet(void)
{
    for (int direction = -1; direction <= 1; direction += 2) { // 左右镜像共用同一物理行为
        Player player; // 真实角色
        ActorAnimation animation; // 真实动作状态
        MotionState motion; // 被测持久动作
        Player visual; // 插值位置
        SkeletalLayer layers[2]; // 当前实际骨骼
        PlayerInit(&player, &world);
        AnimationInit(&animation, direction < 0 ? VIEW_LEFT : VIEW_RIGHT);
        MotionInit(&motion, &player, &animation);
        float old_x[2] = {0}, old_root = 0, maximum_foot = 0, maximum_root = 0; // 检查支撑和摆腿之间的连续性，覆盖刚好离地的单步
        bool old_contact[2] = {false, false}; // 判断真正离地的边界，避免把正常摆腿速度当硬切
        for (int step = 0; step < 360; ++step) { // 连续走、跑、走三个阶段
            Tick(&player, &animation, &motion, (PlayerInput){.move = direction, .run = step >= 120 && step < 240});
            MotionSample(&motion, 1, &visual, layers);
            SkeletonJoint joints[16]; // 真实变换后的关节
            SkeletonWorld(1, &layers[0].pose, joints);
            if (step > 0) maximum_root = fmaxf(maximum_root, fabsf(layers[0].pose.root[1] - old_root) * SkeletonReferenceScale(1));
            old_root = layers[0].pose.root[1];
            for (int side = 0; side < 2; ++side) { // 两只脚分别验证
                const float world_x = visual.x + (direction < 0 ? 1 : -1) * (joints[12 + side].x - 256) * SkeletonReferenceScale(1); // 实际脚踝世界位置
                if (step > 0) maximum_foot = fmaxf(maximum_foot, fabsf(world_x - old_x[side]));
                if (step > 0 && old_contact[side] && !motion.contact[side]) Check(fabsf(world_x - old_x[side]) < .1f, "toe off begins at the actual last planted position");
                old_x[side] = world_x;
                old_contact[side] = motion.contact[side];
                if (motion.contact[side]) {
                    const float x = visual.x + (direction < 0 ? 1 : -1) * (joints[12 + side].x - 256) * SkeletonReferenceScale(1); // 实际绘制脚踝横坐标
                    if (fabsf(x - motion.foot_world[side]) >= .08f) printf("foot diagnostic step=%d side=%d phase=%f root=%f x=%f expected=%f y=%f\n", step, side, motion.phase, layers[0].pose.root[1], x, motion.foot_world[side], joints[12 + side].y);
                    Check(fabsf(x - motion.foot_world[side]) < .08f, "support foot stays at its world contact instead of skating");
                    Check(fabsf(joints[12 + side].y - 451) < .08f, "support foot stays on the floor");
                }
            }
            Check(layers[0].mirror == (direction > 0), "movement facing matches the physical direction");
        }
        printf("direction %d: foot max %.3f px/tick, root max %.3f px/tick\n", direction, maximum_foot, maximum_root);
        Check(maximum_foot < 12, "walk run walk never causes the previous 28 pixel foot discontinuity");
        Check(maximum_root < 6, "body height cannot jump at the support to swing boundary");
        const float phase = motion.phase; // 停止实际位移后的步态相位
        player.x = motion.player.x;
        MotionUpdate(&motion, &player, &animation, 1.0f / 120);
        Check(fabsf(phase - motion.phase) < .00001f, "zero actual displacement does not advance gait");
    }
}

/* 验证行走按蹲与落地的最终混合结果，端点和渲染中点均不能穿地。 */
static void TestGroundedTransitions(void)
{
    Player player, visual; // 真实和插值位置
    ActorAnimation animation; // 真实动作状态
    MotionState motion; // 持久动作状态
    SkeletalLayer layers[2]; // 最终绘制姿态
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    for (int step = 0; step < 420; ++step) { // 行走后按蹲、站起、跳跃并落地
        Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .crouch = step >= 120 && step < 180, .jump = step == 240});
        if (!player.grounded || motion.previous_player.y < world.ground) continue;
        for (int sample = 1; sample <= 2; ++sample) { // 检查半物理步及端点
            MotionSample(&motion, sample * .5f, &visual, layers);
            SkeletonJoint feet[16]; // 混合后的实际关节，不能只检查源动作
            SkeletonWorld(layers[0].view, &layers[0].pose, feet);
            Check(fmaxf(feet[12].y, feet[13].y) <= 451.08f, "grounded crouch and landing blends never push a foot below the floor");
        }
    }
}

/* 验证渲染中间状态、零时间更新与连续中断，不能因为多绘制残影而加速弹簧。 */
static void TestSamplingAndInterrupts(void)
{
    Player player, first, middle, last; // 物理位置和三个绘制位置
    ActorAnimation animation; // 实际状态机
    MotionState motion; // 连续控制器
    SkeletalLayer layers[2]; // 绘制采样
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    Tick(&player, &animation, &motion, (PlayerInput){.move = -1});
    MotionSample(&motion, 0, &first, layers);
    MotionSample(&motion, .5f, &middle, layers);
    MotionSample(&motion, 1, &last, layers);
    Check(fabsf(middle.x - (first.x + last.x) * .5f) < .001f && first.x != last.x, "rendering uses fractional physical position");
    const float phase = motion.phase; // 只读采样前的动作时钟
    for (int sample = 0; sample < 100; ++sample) MotionSample(&motion, sample / 99.0f, &middle, layers); // 多次绘制不推进动作
    Check(motion.phase == phase, "render sampling never advances the animation");
    MotionUpdate(&motion, &player, &animation, 0);
    Check(motion.phase == phase, "zero dt is harmless");
    for (int step = 0; step < 400; ++step) { // 混合期间继续中断，覆盖蹲冲、跳冲、落地及换向
        PlayerInput input = {.move = step < 200 ? -1 : 1, .run = step % 40 < 20,
            .jump = step == 8 || step == 240, .dash = step == 18 || step == 300, .crouch = step > 150 && step < 180}; // 当前确定性输入
        Tick(&player, &animation, &motion, input);
        const int count = MotionSample(&motion, .5f, &middle, layers); // 真实中间姿态
        Check(count >= 1 && count <= 2, "interrupts keep a valid layer count");
        for (int part = 0; part < 16; ++part) Check(isfinite(layers[0].pose.angle[part]) && layers[0].pose.scale[part] > 0, "interrupts keep finite positive bone transforms"); // 不允许中断产生无效骨骼
        if (layers[0].view == 1) Check(fabsf(layers[0].pose.chest[0] - 52 * sinf(layers[0].pose.angle[1])) < .01f, "chest pivot follows the actual blended torso angle during interruptions");
    }
}

/* 验证柔性根部固定、零弯曲等价于原图和末端真实变形。 */
static void TestRibbon(void)
{
    float root[2], straight[2], curved[2]; // 根部及两个尾部顶点
    MotionRibbon(.5f, .15f, 80, 190, .5f, .15f, .5f, root);
    MotionRibbon(.5f, 1, 80, 190, .5f, .15f, 0, straight);
    MotionRibbon(.5f, 1, 80, 190, .5f, .15f, .5f, curved);
    Check(fabsf(root[0]) < .00001f && fabsf(root[1]) < .00001f, "soft mesh root remains fixed to its bone");
    Check(fabsf(straight[0]) < .00001f && fabsf(straight[1] - 161.5f) < .001f, "zero bend exactly reproduces the original rigid image");
    Check(curved[0] > 20 && curved[1] < straight[1], "soft attachment bends along its length");
}

/* 验证30/60/144显示刷新率共用同一物理轨迹；只读插值不能改变位移或动作时钟。 */
static void TestDisplayRates(void)
{
    const int rates[] = {30, 60, 144}; // 不同显示刷新率
    float reference_phase = 0; // 首种刷新率的四秒步态相位
    for (int test = 0; test < 3; ++test) { // 当前显示频率
        Player player, visual; // 物理与显示位置
        ActorAnimation animation; // 相同输入的逻辑状态
        MotionState motion; // 持久动作
        SkeletalLayer layers[2]; // 显示姿态
        double remainder = 0; // 与正式循环一样保留不足一个物理步的时间
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        MotionInit(&motion, &player, &animation);
        for (int frame = 0; frame < rates[test] * 4; ++frame) { // 四秒持续向左走
            remainder += 1.0 / rates[test];
            while (remainder + 1e-12 >= 1.0 / 120) {
                Tick(&player, &animation, &motion, (PlayerInput){.move = -1});
                remainder -= 1.0 / 120;
            }
            MotionSample(&motion, (float)(remainder * 120), &visual, layers);
            Check(isfinite(visual.x) && layers[0].pose.scale[0] > 0, "every display rate produces a usable interpolated pose");
        }
        Check(fabsf(player.x + 400) < .05f, "four seconds of walking has the same physical distance at every display rate");
        if (test == 0) reference_phase = motion.phase;
        else Check(fabsf(motion.phase - reference_phase) < .00001f, "display refresh rate never changes gait progression");
    }
}

/* 验证衣发在停止后继续回摆并最终接近待机，不把惯性当同步正弦装饰。 */
static void TestFollowThrough(void)
{
    Player player; // 真实角色
    ActorAnimation animation; // 逻辑状态
    MotionState motion; // 附件惯性
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    for (int step = 0; step < 120; ++step) Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true}); // 建立跑步中的衣发后拖
    for (int step = 0; step < 12; ++step) Tick(&player, &animation, &motion, (PlayerInput){0}); // 停止移动但衣发继续恢复
    Check(fabsf(motion.current[0].bend[0]) > .01f && fabsf(motion.cloth_speed[0]) > .01f, "hair follows through after the body stops");
    for (int step = 0; step < 240; ++step) Tick(&player, &animation, &motion, (PlayerInput){0}); // 等待惯性衰减到待机微动
    Check(fabsf(motion.current[0].bend[0]) < .03f, "secondary motion settles without persistent large oscillation");
}

/* 复现跑步中松键一个物理步立刻继续，空中的摆腿不能被新起步强制砸到地面。 */
static void TestRapidRestart(void)
{
    float maximum = 0; // 扫描不同停跑时机的最大脚踝单步纵向变化
    for (int delay = 1; delay < 120; ++delay) { // 走遍跑步周期和起步阶段
        Player player, visual; // 实际与绘制角色
        ActorAnimation animation; // 原输入状态机
        MotionState motion; // 持久动作
        SkeletalLayer layers[2]; // 当前最终姿态
        SkeletonJoint before[16], after[16]; // 重新按键前后的世界关节
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        MotionInit(&motion, &player, &animation);
        for (int step = 0; step < delay; ++step) Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true}); // 停止前的实际跑步
        Tick(&player, &animation, &motion, (PlayerInput){0});
        MotionSample(&motion, 1, &visual, layers);
        SkeletonWorld(1, &layers[0].pose, before);
        Tick(&player, &animation, &motion, (PlayerInput){.move = -1, .run = true});
        MotionSample(&motion, 1, &visual, layers);
        SkeletonWorld(1, &layers[0].pose, after);
        for (int side = 0; side < 2; ++side) maximum = fmaxf(maximum, fabsf(after[12 + side].y - before[12 + side].y) * SkeletonReferenceScale(1)); // 两脚实际纵向跳变量
    }
    printf("rapid restart foot vertical maximum: %.3f px/tick\n", maximum);
    Check(maximum < 2, "restarting while a foot is airborne cannot snap it to the floor");
}

/* 执行真实动作连续性回归；无参数，任一行为失败返回1。 */
int main(void)
{
    TestWalkRunCut();
    TestPhaseAndFeet();
    TestSamplingAndInterrupts();
    TestRibbon();
    TestGroundedTransitions();
    TestDisplayRates();
    TestFollowThrough();
    TestRapidRestart();
    printf("motion: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
