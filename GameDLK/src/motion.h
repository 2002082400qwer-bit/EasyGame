#ifndef MX002_MOTION_H // 连续动作接口保护
#define MX002_MOTION_H // 连续动作接口标识

#include "skeletal_actor.h"

typedef struct MotionState {
    SkeletalLayer previous[2], current[2], source[2]; // 前后物理姿态和可中断过渡的实际起点
    Player previous_player, player; // 与姿态对应的前后世界位置
    int count, previous_count, group; // 当前/前次层数及动作语义组
    float phase, run_mix, elapsed, duration; // 持久步态相位、跑步混合和过渡时间
    float cloth[3], cloth_speed[3]; // 发梢与两片衣摆的阻尼角和角速度
    float foot_world[2]; // 两只支撑脚锁定的世界横坐标
    float takeoff_world[2], takeoff_y[2], takeoff_phase[2], foot_phase[2], contact_limit[2]; // 从实际二维离地点衔接摆腿，并记录每步接触比例
    bool swinging[2]; // 一次摆腿开始后直到相位回绕才允许再次落脚
    bool contact[2], initialized; // 上步接地标记及初始化标记
} MotionState;

/* 初始化持久动作：state接收状态，player/animation为当前实际角色，不产生额外物理位移。 */
void MotionInit(MotionState *state, const Player *player, const ActorAnimation *animation);
/* 固定步更新：state持有可中断动作和衣发惯性，player/animation为更新后的真实输入结果，dt为秒。 */
void MotionUpdate(MotionState *state, const Player *player, const ActorAnimation *animation, float dt);
/* 只读绘制采样：state为前后状态，alpha为物理余量比例，player/layers输出插值位置和至多两层。 */
int MotionSample(const MotionState *state, float alpha, Player *player, SkeletalLayer layers[2]);
/* 计算柔性拆件网格顶点：u/v为纹理比例，width/height为尺寸，pivot为归一枢轴，bend为弯曲量，out为局部坐标。 */
void MotionRibbon(float u, float v, float width, float height, float pivot_x, float pivot_y, float bend, float out[2]);

#endif
