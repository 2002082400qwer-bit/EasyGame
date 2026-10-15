#include "player.h"

/* 初始化角色：player 为输出角色，world 提供出生点、边界和地面。 */
void PlayerInit(Player *player, const PlayerWorld *world)
{
    *player = (Player){0};
    player->x = world->spawn;
    player->y = world->ground;
    player->grounded = true;
    player->facing = -1;
}

/* 递减非负计时器：remaining 为当前剩余秒数，dt 为经过的时间。 */
static float Countdown(float remaining, float dt)
{
    return remaining > dt + 0.000001f ? remaining - dt : 0.0f;
}

/* 更新动作物理：player 为角色，input 为输入，world 为场地，dt 为时间步秒数。 */
void PlayerUpdate(Player *player, PlayerInput input, const PlayerWorld *world, float dt)
{
    if (dt <= 0.0f) return;

    const PlayerAction previous = player->action; // 更新前的动画状态
    const bool was_grounded = player->grounded; // 本步更新前是否站在地面
    const int move = (input.move > 0) - (input.move < 0); // 归一化后的水平输入
    player->move_intent = move;
    player->landing_remaining = Countdown(player->landing_remaining, dt);
    if (!input.crouch) player->crouch_locked = false;
    player->crouching = input.crouch && !player->crouch_locked && player->grounded && player->dash_remaining == 0.0f;

    if (move != 0 && player->dash_remaining == 0.0f) player->facing = move;
    if (input.jump && player->grounded && !player->crouching) {
        player->grounded = false;
        player->vy = -PLAYER_JUMP_SPEED;
        player->jump_time = 0.0f;
        player->landing_remaining = 0.0f;
    }
    if (input.dash && player->dash_cooldown == 0.0f &&
        (player->grounded || !player->air_dash_used)) {
        player->dash_from_crouch = player->crouching;
        if (player->dash_from_crouch) player->crouch_locked = true;
        player->crouching = false;
        player->dash_direction = player->facing;
        player->dash_remaining = PLAYER_DASH_DURATION;
        player->dash_cooldown = PLAYER_DASH_COOLDOWN;
        if (!player->grounded) player->air_dash_used = true;
    }

    const float dash_step = player->dash_remaining < dt ? player->dash_remaining : dt; // 本步实际冲刺时间
    const float normal_speed = player->crouching ? PLAYER_CROUCH_SPEED : (input.run ? PLAYER_RUN_SPEED : PLAYER_WALK_SPEED); // 非冲刺速度
    const float normal_step = dt - dash_step; // 本步冲刺结束后的普通移动时间
    player->x += player->dash_direction * PLAYER_DASH_SPEED * dash_step + move * normal_speed * normal_step;
    player->dash_remaining = Countdown(player->dash_remaining, dt);
    player->dash_cooldown = Countdown(player->dash_cooldown, dt);
    player->vx = player->dash_remaining > 0.0f ? player->dash_direction * PLAYER_DASH_SPEED : move * normal_speed;

    if (player->x < world->left) {
        player->x = world->left;
        if (player->vx < 0.0f) player->vx = 0.0f;
    }
    if (player->x > world->right) {
        player->x = world->right;
        if (player->vx > 0.0f) player->vx = 0.0f;
    }
    if (!player->grounded) {
        player->jump_time += dt;
        player->y += player->vy * dt + 0.5f * PLAYER_GRAVITY * dt * dt;
        player->vy += PLAYER_GRAVITY * dt;
    }
    if (player->y >= world->ground) {
        player->y = world->ground;
        player->vy = 0.0f;
        player->grounded = true;
        player->air_dash_used = false;
        if (!was_grounded) player->landing_remaining = PLAYER_LAND_DURATION;
    }

    if (player->dash_remaining > 0.0f) player->action = ACTION_DASH;
    else if (!player->grounded) player->action = ACTION_JUMP;
    else if (player->crouching) player->action = ACTION_CROUCH;
    else if (player->landing_remaining > 0.0f) player->action = ACTION_LAND;
    else if (player->vx != 0.0f) player->action = input.run ? ACTION_RUN : ACTION_WALK;
    else player->action = ACTION_IDLE;

    player->action_time = player->action == previous ? player->action_time + dt : 0.0f;
}

/* 查询碰撞框高度：player 的下蹲标记决定高度，脚底锚点保持不变。 */
float PlayerHeight(const Player *player)
{
    return player->crouching ? PLAYER_CROUCH_HEIGHT : PLAYER_STAND_HEIGHT;
}

/* 获取动作标签：action 为状态标识，返回窗口状态面板使用的英文短名称。 */
const char *PlayerActionName(PlayerAction action)
{
    switch (action) {
        case ACTION_IDLE: return "IDLE";
        case ACTION_WALK: return "WALK";
        case ACTION_RUN: return "RUN";
        case ACTION_JUMP: return "JUMP";
        case ACTION_CROUCH: return "CROUCH";
        case ACTION_DASH: return "DASH";
        case ACTION_LAND: return "LAND";
    }
    return "IDLE";
}
