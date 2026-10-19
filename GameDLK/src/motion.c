#include "motion.h"
#include <math.h>
#include <string.h>

#define MOTION_PI 3.14159265358979323846f // 连续运动使用的圆周率

/* 夹紧参数：value为输入，low/high为允许边界。 */
static float Limit(float value, float low, float high)
{
    return fminf(high, fmaxf(low, value));
}

/* 平滑过渡：value为归一进度，两端速度为零。 */
static float Ease(float value)
{
    value = Limit(value, 0, 1);
    return value * value * (3 - 2 * value);
}

/* 合并仅资源编号不同的同类动作：player/animation提供真实动作和转身状态。 */
static int Group(const Player *player, const ActorAnimation *animation)
{
    if (animation->clip >= 27) return 6;
    if (player->action == ACTION_DASH) return 4;
    if (player->action == ACTION_JUMP) return 3;
    if (player->action == ACTION_LAND) return 5;
    if (animation->crouch_pose > 0 || player->crouching) return 2;
    return player->action == ACTION_WALK || player->action == ACTION_RUN ? 1 : 0;
}

/* 求单腿目标：pose为已混合上身，side为左右腿，x/y为512画布内脚踝目标。 */
static void Leg(SkeletonPose *pose, int side, float x, float y)
{
    const float dx = x - (256 + (side ? 3 : -3) + pose->root[0]); // 脚踝到对应髋关节的横向距离
    const float dy = y - 254 - pose->root[1]; // 当前身体高度对应的腿长
    const float distance = Limit(hypotf(dx, dy), .001f, 199.99f); // 等长双段腿可达半径
    const float thigh = atan2f(-dx, dy) + acosf(distance / 200); // 同向屈膝的大腿世界角
    const float shin = atan2f(-(dx + sinf(thigh) * 100), dy - cosf(thigh) * 100); // 小腿世界角
    pose->angle[8 + side] = thigh;
    pose->angle[10 + side] = shin - thigh;
    pose->angle[12 + side] = -shin;
    pose->scale[8 + side] = pose->scale[10 + side] = 1;
}

/* 根据地面位移和接触相位求侧面双脚：state保存支撑锚点，player为世界位置，layer为过渡后姿态。 */
static void Feet(MotionState *state, const Player *player, SkeletalLayer *layer)
{
    const float scale = SkeletonReferenceScale(1); // 固定骨骼到世界像素标尺
    const float period = .58f - .12f * state->run_mix; // 一次左右腿完整周期，走跑连续变化
    const float contact = .60f - .30f * state->run_mix; // 跑步缩短支撑期并产生双脚腾空
    const float speed = PLAYER_WALK_SPEED + (PLAYER_RUN_SPEED - PLAYER_WALK_SPEED) * state->run_mix; // 与物理速度对应的步幅
    const float entrance = state->duration > 0 ? Ease(state->elapsed / state->duration) : 1; // 起步从原站姿逐渐展开步幅
    const float span = speed * period * contact / scale * entrance; // 支撑期内身体走过的骨骼距离
    const float sign = layer->mirror ? -1 : 1; // 侧面原图朝左，向右时镜像世界位移
    float x[2], y[2]; // 两只脚的最终目标
    SkeletonJoint previous[16]; // 上一步真实关节，不能从理想轨迹猜测离地点
    SkeletonWorld(1, &state->previous[0].pose, previous);
    for (int side = 0; side < 2; ++side) { // 当前左右腿
        const float phase = fmodf(state->phase + side * .5f, 1); // 双腿相差半个周期
        const float old_world = state->previous_player.x + sign * (previous[12 + side].x - 256) * scale; // 实际上一物理步的脚踝世界横坐标
        if (phase < state->foot_phase[side] || state->group != 1) {
            state->swinging[side] = false;
            state->contact_limit[side] = contact;
        }
        if (state->group != 1 && previous[12 + side].y < 450.95f) {
            state->swinging[side] = true; // 快速停跑再起步时，原来悬空的脚不能直接变成支撑脚
            state->takeoff_world[side] = old_world;
            state->takeoff_y[side] = previous[12 + side].y;
            state->takeoff_phase[side] = phase;
        }
        if (!state->contact[side] && !state->swinging[side]) state->foot_world[side] = old_world;
        const float reach = fabsf(sign * (state->foot_world[side] - player->x) / scale); // 支撑脚相对身体已拖后的距离
        if (!state->swinging[side] && (phase >= state->contact_limit[side] || reach > 112)) {
            state->swinging[side] = true;
            state->takeoff_world[side] = old_world;
            state->takeoff_y[side] = previous[12 + side].y;
            state->takeoff_phase[side] = phase;
        }
        const bool planted = !state->swinging[side]; // 摆腿开始后不能因走跑接触比例变化而中途重新落脚
        const float swing = planted ? 0 : Limit((phase - state->takeoff_phase[side]) / fmaxf(.01f, 1 - state->takeoff_phase[side]), 0, 1); // 从真实离地时刻开始的摆腿进度
        const float landing = player->x + player->vx * period * (1 - phase) + sign * ((side ? 3 : -3) - span * .5f) * scale; // 按实际运动方向预测下次落脚点
        const float moving = state->takeoff_world[side] + (landing - state->takeoff_world[side]) * Ease(swing); // 从真实离地点平滑返回落脚点
        x[side] = 256 + sign * (moving - player->x) / scale;
        y[side] = planted ? 451 : state->takeoff_y[side] + (451 - state->takeoff_y[side]) * Ease(swing)
            - (36 + 80 * state->run_mix) * sinf(MOTION_PI * swing) * sinf(MOTION_PI * swing) * entrance;
        if (planted) {
            x[side] = 256 + sign * (state->foot_world[side] - player->x) / scale;
        }
        state->contact[side] = planted;
        state->foot_phase[side] = phase;
        const float dx = Limit(x[side] - 256 - (side ? 3 : -3) - layer->pose.root[0], -198, 198); // 当前脚到髋的水平距离
        const float minimum = y[side] - 254 - sqrtf(199.5f * 199.5f - dx * dx); // 避免抬高身体使脚越出腿长
        layer->pose.root[1] = fmaxf(layer->pose.root[1], minimum);
    }
    for (int side = 0; side < 2; ++side) Leg(&layer->pose, side, x[side], y[side]); // 混合完成后再约束脚，不用混合腿角冒充接地
}

/* 解析阻尼弹簧：position/velocity为惯性状态，target为目标角，dt为固定步；避免欧拉积分能量发散。 */
static void Spring(float *position, float *velocity, float target, float dt)
{
    const float frequency = 17, damping = .62f; // 衣发频率与欠阻尼比例，允许停止后小幅回摆
    const float decay = expf(-damping * frequency * dt); // 阻尼衰减
    const float rate = frequency * sqrtf(1 - damping * damping); // 阻尼后的振动频率
    const float distance = *position - target; // 当前偏离目标的角度
    const float coefficient = (*velocity + damping * frequency * distance) / rate; // 正弦项系数
    const float cosine = cosf(rate * dt), sine = sinf(rate * dt); // 本步解析旋转量
    *position = target + decay * (distance * cosine + coefficient * sine);
    *velocity = decay * ((*velocity) * cosine - (damping * frequency * coefficient + distance * rate) * sine);
}

/* 初始化动作与惯性：state为输出，player/animation为当前状态。 */
void MotionInit(MotionState *state, const Player *player, const ActorAnimation *animation)
{
    *state = (MotionState){0};
    state->count = SkeletalEvaluate(player, animation, state->current);
    state->previous_count = state->count;
    state->previous_player = state->player = *player;
    state->group = Group(player, animation);
    memcpy(state->previous, state->current, sizeof(state->current));
    memcpy(state->source, state->current, sizeof(state->current));
    for (int part = 0; part < 3; ++part) state->cloth[part] = state->current[0].pose.angle[part == 0 ? 3 : 13 + part]; // 发梢及两片衣摆初始角
    state->initialized = true;
}

/* 连续动作推进：state为持久状态，player/animation为本步物理，dt为秒；渲染采样不修改它。 */
void MotionUpdate(MotionState *state, const Player *player, const ActorAnimation *animation, float dt)
{
    if (!state->initialized) MotionInit(state, player, animation);
    if (dt <= 0 || !isfinite(dt)) return;
    dt = fminf(dt, .1f);
    memcpy(state->previous, state->current, sizeof(state->current));
    state->previous_count = state->count;
    state->previous_player = state->player;
    const int group = Group(player, animation); // 本步语义动作组
    const float distance = fabsf(player->x - state->player.x); // 本步实际位移，被墙挡住时为零
    const float response = 1 - expf(-16 * dt); // 走跑交融速度，与显示帧率无关
    state->run_mix += ((player->action == ACTION_RUN ? 1.0f : 0.0f) - state->run_mix) * response;
    if (group == 1) {
        if (state->group != 1) state->phase = .25f; // 从双脚站立开始时使用承重中段，避免锁脚整段拖到腿长之外
        const float speed = PLAYER_WALK_SPEED + (PLAYER_RUN_SPEED - PLAYER_WALK_SPEED) * state->run_mix; // 步态标定速度
        state->phase = fmodf(state->phase + distance / (speed * (.58f - .12f * state->run_mix)), 1);
    }
    SkeletalLayer target[2]; // 本步目标姿态
    const int count = SkeletalEvaluate(player, animation, target); // 保留全部35状态逻辑
    if (group == 1) {
        SkeletonPose walk, run; // 同一相位上的走跑端点
        SkeletonSample(target[0].view, SKELETON_WALK, state->phase, 1, &walk);
        SkeletonSample(target[0].view, SKELETON_RUN, state->phase, 1, &run);
        SkeletonBlend(&walk, &run, state->run_mix, &target[0].pose);
    }
    const bool compatible = count == state->count && target[0].view == state->current[0].view && target[0].mirror == state->current[0].mirror; // 只在同一绑定内混合角度
    if (!compatible || group != state->group) {
        memcpy(state->source, state->current, sizeof(state->current));
        state->elapsed = 0;
        state->duration = group == 4 ? .045f : group == 3 || group == 5 ? .06f : .12f;
        if (!compatible) {
            state->duration = 0; // 正背转向仍交给已有双层视图过渡，禁止跨绑定拉扯
            state->contact[0] = state->contact[1] = false;
            for (int part = 0; part < 3; ++part) { // 改视图时重置不同绑定的惯性
                state->cloth[part] = target[0].pose.angle[part == 0 ? 3 : 13 + part];
                state->cloth_speed[part] = 0;
            }
        }
    }
    const float blend = state->duration > 0 ? Ease(state->elapsed / state->duration) : 1; // 从当前实际姿势开始，允许再次中断
    for (int layer = 0; layer < count; ++layer) { // 本步每个固定视图
        state->current[layer] = target[layer];
        if (compatible && count == 1) SkeletonBlend(&state->source[layer].pose, &target[layer].pose, blend, &state->current[layer].pose);
        if (compatible && count == 1) {
            for (int part = 0; part < 8; ++part) { // 上身关键姿势限制单步角速度，避免采样拐点再次制造硬切
                if (part == 2 || part == 3) continue;
                const float angle = state->previous[layer].pose.angle[part]; // 前一步实际姿势，而非理想关键帧
                const float difference = remainderf(state->current[layer].pose.angle[part] - angle, 2 * MOTION_PI); // 最短目标角差
                state->current[layer].pose.angle[part] = angle + Limit(difference, -22 * dt, 22 * dt);
            }
        }
        if (state->current[layer].view == 1) {
            state->current[layer].pose.chest[0] = 52 * sinf(state->current[layer].pose.angle[1]);
            state->current[layer].pose.chest[1] = 52 * (1 - cosf(state->current[layer].pose.angle[1]));
        }
    }
    if (group == 1 && count == 1 && target[0].view == 1) Feet(state, player, &state->current[0]);
    else {
        state->contact[0] = state->contact[1] = false;
        state->swinging[0] = state->swinging[1] = false;
        if (player->grounded && count == 1) {
            SkeletonJoint feet[16]; // 非行走过渡的最终脚踝
            SkeletonWorld(state->current[0].view, &state->current[0].pose, feet);
            state->current[0].pose.root[1] -= fmaxf(0, fmaxf(feet[12].y, feet[13].y) - 451); // 混合蹲姿等动作后禁止鞋底穿地
        }
    }
    if (count == 1) {
        for (int slot = 0; slot < 3; ++slot) { // 三块柔性附件各自保存惯性
            const int part = slot == 0 ? 3 : 13 + slot; // 固定部件编号
            const float desired = state->current[0].pose.angle[part]; // 随主体运动的目标角
            Spring(&state->cloth[slot], &state->cloth_speed[slot], desired, dt);
            state->current[0].pose.angle[part] = state->cloth[slot];
            state->current[0].bend[slot] = Limit((desired - state->cloth[slot]) * 1.2f, -.55f, .55f);
        }
    }
    state->elapsed += dt;
    state->group = group;
    state->count = count;
    state->player = *player;
}

/* 只读插值：state持有相邻物理姿态，alpha为余量，player/layers接收可绘制结果。 */
int MotionSample(const MotionState *state, float alpha, Player *player, SkeletalLayer layers[2])
{
    const float weight = Limit(alpha, 0, 1); // 余量插值权重
    *player = state->player;
    player->x = state->previous_player.x + (state->player.x - state->previous_player.x) * weight;
    player->y = state->previous_player.y + (state->player.y - state->previous_player.y) * weight;
    for (int index = 0; index < state->count; ++index) { // 当前输出视角
        layers[index] = state->current[index];
        if (state->count == state->previous_count && layers[index].view == state->previous[index].view && layers[index].mirror == state->previous[index].mirror) {
            SkeletonBlend(&state->previous[index].pose, &state->current[index].pose, weight, &layers[index].pose);
            if (layers[index].view == 1) {
                layers[index].pose.chest[0] = 52 * sinf(layers[index].pose.angle[1]);
                layers[index].pose.chest[1] = 52 * (1 - cosf(layers[index].pose.angle[1]));
            }
            layers[index].opacity = state->previous[index].opacity + (state->current[index].opacity - state->previous[index].opacity) * weight;
            layers[index].width_scale = state->previous[index].width_scale + (state->current[index].width_scale - state->previous[index].width_scale) * weight;
            for (int slot = 0; slot < 3; ++slot) layers[index].bend[slot] = state->previous[index].bend[slot] + (state->current[index].bend[slot] - state->previous[index].bend[slot]) * weight; // 插值附件弯曲
        }
        if (player->grounded && state->count == 1) {
            SkeletonJoint feet[16]; // 插值局部角度后重新核对最终地面，不只约束物理端点
            SkeletonWorld(layers[index].view, &layers[index].pose, feet);
            layers[index].pose.root[1] -= fmaxf(0, fmaxf(feet[12].y, feet[13].y) - 451);
        }
    }
    return state->count;
}

/* 沿固定拆件生成柔性网格点：u/v为纹理位置，尺寸/枢轴保持绑定，bend控制末端弯曲。 */
void MotionRibbon(float u, float v, float width, float height, float pivot_x, float pivot_y, float bend, float out[2])
{
    const float tail = Limit((v - pivot_y) / fmaxf(.01f, 1 - pivot_y), 0, 1); // 枢轴以下连续变形，固定根部及根部切线
    out[0] = (u - pivot_x) * width + sinf(bend) * height * .45f * tail * tail;
    out[1] = (v - pivot_y) * height - (1 - cosf(bend)) * height * .18f * tail * tail;
}
