#ifndef MX002_PLAYER_H // 防止角色接口被重复包含
#define MX002_PLAYER_H // 角色接口头文件保护标记

#include <stdbool.h>

#define PLAYER_WALK_SPEED 260.0f // 正常行走速度，单位像素每秒
#define PLAYER_CROUCH_SPEED 95.0f // 下蹲移动速度，单位像素每秒
#define PLAYER_JUMP_SPEED 680.0f // 起跳速度的绝对值
#define PLAYER_GRAVITY 1800.0f // 向下的重力加速度
#define PLAYER_DASH_SPEED 900.0f // 冲刺水平速度
#define PLAYER_DASH_DURATION 0.18f // 每次冲刺的持续秒数
#define PLAYER_DASH_COOLDOWN 0.60f // 从开始冲刺计算的冷却秒数
#define PLAYER_WIDTH 48.0f // 角色碰撞框宽度
#define PLAYER_STAND_HEIGHT 156.0f // 站立碰撞框高度
#define PLAYER_CROUCH_HEIGHT 98.0f // 下蹲碰撞框高度

typedef enum PlayerAction {
    ACTION_IDLE, // 原地待机
    ACTION_WALK, // 正常行走
    ACTION_JUMP, // 空中跳跃或下落
    ACTION_CROUCH, // 地面下蹲及蹲行
    ACTION_DASH, // 水平冲刺
    ACTION_LAND // 落地恢复
} PlayerAction;

typedef struct PlayerWorld {
    float left; // 角色中心能够到达的最左位置
    float right; // 角色中心能够到达的最右位置
    float ground; // 地面脚底纵坐标，向下为正
    float spawn; // 初始角色中心横坐标
} PlayerWorld;

typedef struct PlayerInput {
    int move; // 左为-1、右为1、无移动为0
    bool jump; // 本次物理更新是否有新的跳跃按键事件
    bool crouch; // 是否持续按住下蹲键
    bool dash; // 本次物理更新是否有新的冲刺按键事件
} PlayerInput;

typedef struct Player {
    float x; // 角色脚底中心的横坐标
    float y; // 角色脚底中心的纵坐标
    float vx; // 当前水平速度
    float vy; // 当前垂直速度
    int facing; // 面向左为-1，面向右为1
    int dash_direction; // 本次冲刺锁定的水平朝向
    bool grounded; // 脚底是否接触地面
    bool crouching; // 是否采用下蹲碰撞框
    bool air_dash_used; // 本次离地期间是否已使用空中冲刺
    float dash_remaining; // 冲刺剩余时间
    float dash_cooldown; // 冲刺冷却剩余时间
    float landing_remaining; // 落地动画剩余时间
    float action_time; // 当前动作已持续时间
    float jump_time; // 本次离地已持续时间
    PlayerAction action; // 当前渲染动作
} Player;

/* 初始化角色：player 为待初始化控制器，world 提供出生点和地面位置。 */
void PlayerInit(Player *player, const PlayerWorld *world);
/* 更新一次物理：player 为角色，input 为本步输入，world 为场地，dt 为秒数。 */
void PlayerUpdate(Player *player, PlayerInput input, const PlayerWorld *world, float dt);
/* 返回碰撞框高度：player 指向需要查询的角色。 */
float PlayerHeight(const Player *player);
/* 返回当前图集帧索引：player 提供动作、速度和动作时间。 */
int PlayerFrame(const Player *player);
/* 返回动作名称：action 为需要显示的动作标识。 */
const char *PlayerActionName(PlayerAction action);

#endif
