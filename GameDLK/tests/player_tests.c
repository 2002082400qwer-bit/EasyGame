#include "player.h"
#include <math.h>
#include <stdio.h>

static int failures = 0; // 本次测试中的失败断言数量
static int checks = 0; // 本次测试中的总断言数量
static const PlayerWorld world = {48.0f, 1232.0f, 600.0f, 640.0f}; // 测试场地边界和出生点

/* 检查行为条件：condition 为结果，name 为失败时显示的测试名称。 */
static void Check(bool condition, const char *name)
{
    ++checks;
    if (!condition) {
        ++failures;
        printf("FAIL: %s\n", name);
    }
}

/* 比较浮点值：actual 为实际值，expected 为期望值，允许轻微累计舍入。 */
static bool Near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.1f;
}

/* 模拟持续输入：player 为角色，input 为保持输入，steps 为更新次数，dt 为时间步。 */
static void Step(Player *player, PlayerInput input, int steps, float dt)
{
    for (int step = 0; step < steps; ++step) { // 当前模拟物理更新序号
        PlayerUpdate(player, input, &world, dt);
        input.jump = false;
        input.dash = false;
    }
}

/* 测试左右走和停止：验证真实位移、面向，以及停止后回到待机。 */
static void TestWalk(void)
{
    Player player; // 当前测试角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.move = -1}, 60, 1.0f / 120.0f);
    Check(Near(player.x, 510.0f), "walk left moves 130 pixels in half a second");
    Check(player.facing == -1 && player.action == ACTION_WALK, "left walk state");
    Step(&player, (PlayerInput){.move = 1}, 60, 1.0f / 120.0f);
    Check(Near(player.x, 640.0f) && player.facing == 1, "walk right mirrors direction");
    Step(&player, (PlayerInput){0}, 1, 1.0f / 120.0f);
    Check(player.vx == 0.0f && player.action == ACTION_IDLE, "release movement stops immediately");
}

/* 测试跳跃和真实落地：空中再次跳跃不能重置垂直速度。 */
static void TestJump(void)
{
    Player player; // 当前测试角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.jump = true}, 12, 1.0f / 120.0f);
    Check(!player.grounded && player.y < world.ground && player.vy < 0, "jump takes off under gravity");
    Check(player.action == ACTION_JUMP, "jump reports airborne state");
    const float before = player.vy; // 空中再次按跳跃前的垂直速度
    Step(&player, (PlayerInput){.jump = true}, 1, 1.0f / 120.0f);
    Check(player.vy > before, "no double jump while airborne");
    Step(&player, (PlayerInput){0}, 120, 1.0f / 120.0f);
    Check(player.grounded && Near(player.y, world.ground) && player.vy == 0, "jump lands without sinking");
    Check(!player.air_dash_used && player.action == ACTION_IDLE, "landing restores grounded idle");
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.move = 1, .jump = true}, 94, 1.0f / 120.0f);
    Check(player.grounded && player.action == ACTION_LAND, "moving jump keeps landing recovery before walking");
}

/* 测试下蹲：脚底位置不变、碰撞高度降低、蹲行变慢并阻止直接起跳。 */
static void TestCrouch(void)
{
    Player player; // 当前测试角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.move = 1, .crouch = true, .jump = true}, 60, 1.0f / 120.0f);
    Check(player.crouching && player.action == ACTION_CROUCH, "crouch overrides grounded jump");
    Check(Near(PlayerHeight(&player), PLAYER_CROUCH_HEIGHT) && Near(player.y, world.ground), "crouch keeps feet and reduces collider");
    Check(Near(player.x, 687.5f), "crouch movement is slower");
    Step(&player, (PlayerInput){0}, 1, 1.0f / 120.0f);
    Check(!player.crouching && Near(PlayerHeight(&player), PLAYER_STAND_HEIGHT), "release crouch restores standing height");
}

/* 测试冲刺：持续距离、冷却、锁定方向和松开输入后的结束状态。 */
static void TestDash(void)
{
    Player player; // 当前测试角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.move = 1, .dash = true}, 1, 1.0f / 120.0f);
    Check(player.action == ACTION_DASH && Near(player.vx, PLAYER_DASH_SPEED), "dash starts in movement direction");
    Step(&player, (PlayerInput){.move = -1}, 10, 1.0f / 120.0f);
    Check(player.facing == 1 && player.x > world.spawn, "dash locks facing despite opposite input");
    Step(&player, (PlayerInput){0}, 20, 1.0f / 120.0f);
    Check(player.dash_remaining == 0 && player.action != ACTION_DASH, "dash ends after configured duration");
    Check(Near(player.x, world.spawn + PLAYER_DASH_SPEED * PLAYER_DASH_DURATION), "dash has exact configured distance");
    Step(&player, (PlayerInput){.dash = true}, 1, 1.0f / 120.0f);
    Check(player.action != ACTION_DASH, "dash cannot restart before cooldown");
    Step(&player, (PlayerInput){0}, 80, 1.0f / 120.0f);
    Step(&player, (PlayerInput){.dash = true}, 1, 1.0f / 120.0f);
    Check(player.action == ACTION_DASH, "dash can restart after cooldown");
}

/* 验证蹲姿冲刺：触发后恢复站立，按住蹲键不应在冲刺结束时再次蹲下。 */
static void TestCrouchDash(void)
{
    Player player; // 接受蹲姿冲刺输入的角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.crouch = true}, 30, 1.0f / 120.0f);
    Step(&player, (PlayerInput){.crouch = true, .dash = true}, 1, 1.0f / 120.0f);
    Check(player.action == ACTION_DASH, "crouch can launch a dash");
    Step(&player, (PlayerInput){.crouch = true}, 90, 1.0f / 120.0f);
    Check(player.action == ACTION_IDLE && !player.crouching, "crouch dash ends standing even while crouch held");
    Step(&player, (PlayerInput){0}, 1, 1.0f / 120.0f);
    Step(&player, (PlayerInput){.crouch = true}, 1, 1.0f / 120.0f);
    Check(player.crouching, "releasing and pressing crouch re-arms crouching");
}

/* 验证跑步：按住跑步键加速，松开立即回到走路，下蹲时仍使用蹲行速度。 */
static void TestRun(void)
{
    Player player; // 当前测试角色
    PlayerInit(&player, &world);
    Step(&player, (PlayerInput){.move = 1, .run = true}, 60, 1.0f / 120.0f);
    Check(Near(player.x, 850.0f) && player.action == ACTION_RUN, "running moves 210 pixels in half a second");
    Step(&player, (PlayerInput){.move = 1}, 1, 1.0f / 120.0f);
    Check(player.action == ACTION_WALK && Near(player.vx, PLAYER_WALK_SPEED), "release run returns to walking");
    Step(&player, (PlayerInput){.move = 1, .run = true, .crouch = true}, 1, 1.0f / 120.0f);
    Check(player.action == ACTION_CROUCH && Near(player.vx, PLAYER_CROUCH_SPEED), "crouch takes priority over run");
}

/* 测试空中冲刺限制：冷却结束也不能在同一次离地中再次空中冲刺。 */
static void TestAirDash(void)
{
    Player player; // 当前测试角色
    PlayerWorld high_world = world; // 增大落地距离以独立验证空中次数限制
    high_world.ground = 2000.0f;
    PlayerInit(&player, &world);
    player.grounded = false;
    PlayerUpdate(&player, (PlayerInput){.dash = true}, &high_world, 1.0f / 120.0f);
    Check(player.air_dash_used && player.action == ACTION_DASH, "air dash consumes aerial charge");
    for (int step = 0; step < 85; ++step) { // 等待空中冲刺冷却结束的更新序号
        PlayerUpdate(&player, (PlayerInput){0}, &high_world, 1.0f / 120.0f);
    }
    PlayerUpdate(&player, (PlayerInput){.dash = true}, &high_world, 1.0f / 120.0f);
    Check(!player.grounded && player.action != ACTION_DASH, "only one aerial dash until landing");
}

/* 测试边界与时间步：长距离移动不会越界，不同频率的模拟位移一致。 */
static void TestBoundariesAndTime(void)
{
    Player fast; // 120赫兹更新的角色
    Player slow; // 60赫兹更新的角色
    PlayerInit(&fast, &world);
    PlayerInit(&slow, &world);
    Step(&fast, (PlayerInput){.move = -1}, 120, 1.0f / 120.0f);
    Step(&slow, (PlayerInput){.move = -1}, 60, 1.0f / 60.0f);
    Check(Near(fast.x, slow.x), "walk is independent of update frequency");
    Step(&fast, (PlayerInput){.move = -1}, 600, 1.0f / 120.0f);
    Check(Near(fast.x, world.left), "left boundary stops movement");
    Step(&fast, (PlayerInput){.move = 1}, 900, 1.0f / 120.0f);
    Check(Near(fast.x, world.right), "right boundary stops movement");
    const float before = fast.x; // 非正时间步前的角色位置
    PlayerUpdate(&fast, (PlayerInput){.move = -1}, &world, 0);
    Check(Near(fast.x, before), "zero time step is ignored");
}

/* 测试程序入口：依次运行控制器行为测试，以退出码报告是否通过。 */
int main(void)
{
    TestWalk();
    TestJump();
    TestCrouch();
    TestDash();
    TestCrouchDash();
    TestRun();
    TestAirDash();
    TestBoundariesAndTime();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
