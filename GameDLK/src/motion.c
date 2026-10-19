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

/* 根据地面位移和接触计时求侧面双脚：state保存支撑锚点，player为世界位置，layer为过渡后姿态，dt为固定步秒数。 */
static void Feet(MotionState *state, const Player *player, SkeletalLayer *layer, float dt)
{
    const float scale = SkeletonReferenceScale(1); // 固定骨骼到世界像素标尺
    const float period = .58f - .12f * state->run_mix; // 一次左右腿完整周期，走跑连续变化
    const float contact = .60f - .30f * state->run_mix; // 跑步缩短支撑期并产生双脚腾空
    const float speed = PLAYER_WALK_SPEED + (PLAYER_RUN_SPEED - PLAYER_WALK_SPEED) * state->run_mix; // 与物理速度对应的步幅
    const float entrance = state->duration > 0 ? Ease(state->elapsed / state->duration) : 1; // 起步从原站姿逐渐展开步幅
    const float span = speed * period * contact / scale * entrance; // 支撑期内身体走过的骨骼距离
    const float sign = layer->mirror ? -1 : 1; // 侧面原图朝左，向右时镜像世界位移
    const float old_sign = state->previous[0].mirror ? -1 : 1; // 真实上一帧朝向，反向时不能用新镜像读取旧世界脚位
    const bool restart = state->group != 1 || layer->mirror != state->previous[0].mirror; // 起步或反向重新建立双脚接触
    float x[2], y[2]; // 两只脚的最终目标
    SkeletonJoint previous[16]; // 上一步真实关节，不能从理想轨迹猜测离地点
    SkeletonWorld(1, &state->previous[0].pose, previous);
    for (int side = 0; side < 2; ++side) { // 当前左右腿
        const float phase = fmodf(state->phase + side * .5f, 1); // 双腿相差半个周期
        const float old_world = state->previous_player.x + old_sign * (previous[12 + side].x - 256) * scale; // 实际上一物理步的脚踝世界横坐标
        if (restart) {
            state->swinging[side] = previous[12 + side].y < 450.95f || phase >= contact;
            state->stance_time[side] = state->swinging[side] ? 0 : phase * period;
            state->swing_time[side] = 0;
            state->swing_duration[side] = fmaxf(.16f, period * (1 - phase)); // 反向即使接近全身周期末，也给脚保留真实的回收时间
            state->foot_world[side] = old_world;
            state->takeoff_world[side] = old_world;
            state->takeoff_y[side] = previous[12 + side].y;
            state->takeoff_phase[side] = phase;
        } else if (state->swinging[side]) {
            state->swing_time[side] += dt;
            if (state->swing_time[side] >= state->swing_duration[side]) {
                state->swinging[side] = false;
                state->stance_time[side] = 0;
                state->foot_world[side] = old_world;
            }
        } else {
            state->stance_time[side] += dt;
        }
        const float reach = fabsf(sign * (state->foot_world[side] - player->x) / scale); // 支撑脚相对身体已拖后的距离
        if (!state->swinging[side] && (state->stance_time[side] >= period * contact || reach > 112)) {
            state->swinging[side] = true;
            state->takeoff_world[side] = old_world;
            state->takeoff_y[side] = previous[12 + side].y;
            state->takeoff_phase[side] = phase;
            state->swing_time[side] = 0;
            state->swing_duration[side] = fmaxf(.16f, period * (1 - contact));
        }
        const bool planted = !state->swinging[side]; // 摆腿开始后不能因走跑接触比例变化而中途重新落脚
        const float swing = planted ? 0 : Limit(state->swing_time[side] / fmaxf(.16f, state->swing_duration[side]), 0, 1); // 每只脚使用真实摆腿秒数，独立于全身相位回绕
        const float landing = player->x + player->vx * fmaxf(0, state->swing_duration[side] - state->swing_time[side]) + sign * ((side ? 3 : -3) - span * .5f) * scale; // 按剩余摆腿时间预测下次落脚点
        const float moving = state->takeoff_world[side] + (landing - state->takeoff_world[side]) * Ease(swing); // 从真实离地点平滑返回落脚点
        x[side] = 256 + sign * (moving - player->x) / scale;
        const float lift = 36 + 80 * state->run_mix; // 当前动作完整摆腿的最大离地高度
        const float budget = Limit((state->takeoff_y[side] - (451 - lift)) / lift, 0, 1); // 原来已经抬起的脚只使用剩余高度，禁止每次反向叠加一次抬腿
        y[side] = planted ? 451 : state->takeoff_y[side] + (451 - state->takeoff_y[side]) * Ease(swing)
            - lift * budget * sinf(MOTION_PI * swing) * sinf(MOTION_PI * swing) * entrance;
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

/* 从肩/髋到中间关节再到末端重建投影：pose为输出，first/second为两段，start/middle/end为实际局部画布目标，length为原绑定长度。 */
static void ProjectChain(SkeletonPose *pose, int first, int second, const SkeletonJoint *start, float middle_x, float middle_y, float end_x, float end_y, float first_length, float second_length, float parent_angle)
{
    const float a = atan2f(-(middle_x - start->x), middle_y - start->y); // 第一段世界角
    const float b = atan2f(-(end_x - middle_x), end_y - middle_y); // 第二段世界角
    pose->angle[first] = a - parent_angle;
    pose->angle[second] = b - a;
    pose->scale[first] = fmaxf(.001f, hypotf(middle_x - start->x, middle_y - start->y) / first_length);
    pose->scale[second] = fmaxf(.001f, hypotf(end_x - middle_x, end_y - middle_y) / second_length);
    if (first >= 8) pose->angle[first + 4] = -b; // 鞋底保持水平，肢体投影缩短不改变全身横向尺度
}

/* 反向时过渡完整肢体投影：state为起点关节，layer为最终姿态；保留脚目标并平滑膝肘，不压扁整个人。 */
static void ReverseProjection(const MotionState *state, SkeletalLayer *layer)
{
    if (state->reverse_time <= 0 || layer->view != 1) return;
    const float weight = Ease(1 - state->reverse_time / .14f); // 新方向投影权重
    const float sign = layer->mirror ? -1 : 1; // 新朝向的屏幕投影符号
    SkeletonJoint joints[16]; // 当前目标关节
    SkeletonWorld(1, &layer->pose, joints);
    for (int side = 0; side < 2; ++side) { // 两条腿的脚保持接触约束，膝盖从原世界位置移动
        const int thigh = 8 + side, knee = 10 + side, foot = 12 + side; // 双段腿索引
        const float x = 256 + sign * (state->reverse_joints[knee].x + (sign * (joints[knee].x - 256) - state->reverse_joints[knee].x) * weight); // 插值后的膝投影横坐标
        const float y = state->reverse_joints[knee].y + (joints[knee].y - state->reverse_joints[knee].y) * weight; // 插值后的膝高度
        ProjectChain(&layer->pose, thigh, knee, &joints[thigh], x, y, joints[foot].x, joints[foot].y, 100, 100, joints[2].angle);
    }
    for (int side = 0; side < 2; ++side) { // 手臂同样过渡肘与手，而非只限制上臂角度
        const int arm = 4 + side, elbow = 6 + side; // 肩/肘对应的骨骼索引
        const float hand_x = joints[elbow].x - sinf(joints[elbow].angle) * 68.4f * layer->pose.scale[elbow]; // 当前目标手末端
        const float hand_y = joints[elbow].y + cosf(joints[elbow].angle) * 68.4f * layer->pose.scale[elbow]; // 当前目标手高度
        const float x = 256 + sign * (state->reverse_joints[elbow].x + (sign * (joints[elbow].x - 256) - state->reverse_joints[elbow].x) * weight); // 过渡后的肘横坐标
        const float y = state->reverse_joints[elbow].y + (joints[elbow].y - state->reverse_joints[elbow].y) * weight; // 过渡后的肘高度
        const float end_x = 256 + sign * (state->reverse_hands[side][0] + (sign * (hand_x - 256) - state->reverse_hands[side][0]) * weight); // 过渡后的手横坐标
        const float end_y = state->reverse_hands[side][1] + (hand_y - state->reverse_hands[side][1]) * weight; // 过渡后的手高度
        ProjectChain(&layer->pose, arm, elbow, &joints[arm], x, y, end_x, end_y, 56, 68.4f, joints[1].angle);
    }
    SkeletonWorld(1, &layer->pose, joints);
    for (int slot = 0; slot < 3; ++slot) { // 柔性附件根部和世界角也连续反射，避免发梢突然跳到头另一侧
        const int part = slot == 0 ? 3 : 13 + slot; // 发梢或衣摆
        const int parent = SkeletonRig(1)[part].parent; // 该附件的父骨骼
        const float x = 256 + sign * (state->reverse_joints[part].x + (sign * (joints[part].x - 256) - state->reverse_joints[part].x) * weight); // 根部连续横坐标
        const float y = state->reverse_joints[part].y + (joints[part].y - state->reverse_joints[part].y) * weight; // 根部连续纵坐标
        const float angle = state->reverse_joints[part].angle + remainderf(sign * joints[part].angle - state->reverse_joints[part].angle, 2 * MOTION_PI) * weight; // 连续世界投影角
        layer->pose.angle[part] = sign * angle - joints[parent].angle;
        layer->shift[slot][0] = x - joints[part].x;
        layer->shift[slot][1] = y - joints[part].y;
    }
}

/* 对显示中点的实际关节插值：first/last为物理端点，weight为显示余量，out为已插值上身和根位置的姿态。 */
static void InterpolateLimbs(const SkeletonPose *first, const SkeletonPose *last, float weight, SkeletonPose *out)
{
    SkeletonJoint a[16], b[16], root[16]; // 两端实际关节与当前父关节
    SkeletonWorld(1, first, a);
    SkeletonWorld(1, last, b);
    SkeletonWorld(1, out, root);
    for (int side = 0; side < 2; ++side) { // 双腿按膝/踝实际位置插值，避免角度插值引起滑脚
        const int knee = 10 + side, foot = 12 + side; // 膝与踝关节索引
        const float x = a[knee].x + (b[knee].x - a[knee].x) * weight, y = a[knee].y + (b[knee].y - a[knee].y) * weight; // 当前膝目标
        const float end_x = a[foot].x + (b[foot].x - a[foot].x) * weight, end_y = a[foot].y + (b[foot].y - a[foot].y) * weight; // 当前踝目标
        ProjectChain(out, 8 + side, knee, &root[8 + side], x, y, end_x, end_y, 100, 100, root[2].angle);
    }
    for (int side = 0; side < 2; ++side) { // 两臂按肘/手目标插值，保持近身弧线
        const int elbow = 6 + side; // 当前肘关节
        const float x = a[elbow].x + (b[elbow].x - a[elbow].x) * weight, y = a[elbow].y + (b[elbow].y - a[elbow].y) * weight; // 插值后的肘
        const float ax = a[elbow].x - sinf(a[elbow].angle) * 68.4f * first->scale[elbow], ay = a[elbow].y + cosf(a[elbow].angle) * 68.4f * first->scale[elbow]; // 前一手末端
        const float bx = b[elbow].x - sinf(b[elbow].angle) * 68.4f * last->scale[elbow], by = b[elbow].y + cosf(b[elbow].angle) * 68.4f * last->scale[elbow]; // 后一手末端
        ProjectChain(out, 4 + side, elbow, &root[4 + side], x, y, ax + (bx - ax) * weight, ay + (by - ay) * weight, 56, 68.4f, root[1].angle);
    }
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
        if (target[0].view == 1) {
            const float pulse = cosf(4 * MOTION_PI * state->phase); // 左右脚每次承重均产生一次躯干俯仰
            const float lean = -(25 + 4 * pulse) * MOTION_PI / 180; // 21至29度的主动前倾，区别于直立快走
            target[0].pose.angle[1] += (lean - target[0].pose.angle[1]) * state->run_mix;
            target[0].pose.angle[0] = -target[0].pose.angle[1] * .78f; // 头部反向补偿，让视线保持前方
            target[0].pose.root[0] = -2 * state->run_mix;
        }
    }
    if (count == 1 && target[0].view == 1 && state->current[0].view == 1 && target[0].mirror != state->current[0].mirror) {
        SkeletonWorld(1, &state->current[0].pose, state->reverse_joints);
        const float old_sign = state->current[0].mirror ? -1 : 1; // 上个朝向的世界投影
        for (int side = 0; side < 2; ++side) { // 保存手的末端，而不只保存肘
            const int elbow = 6 + side; // 当前前臂
            state->reverse_hands[side][0] = old_sign * (state->reverse_joints[elbow].x - 256 - sinf(state->reverse_joints[elbow].angle) * 68.4f * state->current[0].pose.scale[elbow]);
            state->reverse_hands[side][1] = state->reverse_joints[elbow].y + cosf(state->reverse_joints[elbow].angle) * 68.4f * state->current[0].pose.scale[elbow];
        }
        for (int part = 0; part < 16; ++part) { // 将旧局部关节转换成独立于镜像的投影起点
            state->reverse_joints[part].x = old_sign * (state->reverse_joints[part].x - 256);
            state->reverse_joints[part].angle *= old_sign;
        }
        for (int slot = 0; slot < 3; ++slot) { // 反复反向时继承附件已经插值过的实际根部
            const int part = slot == 0 ? 3 : 13 + slot; // 当前附件
            state->reverse_joints[part].x += old_sign * state->current[0].shift[slot][0];
            state->reverse_joints[part].y += state->current[0].shift[slot][1];
        }
        state->reverse_time = .14f;
        state->reverse_torso = -state->current[0].pose.angle[1];
        state->reverse_head = -state->current[0].pose.angle[0];
    }
    if (count == 1 && target[0].view == 1 && group == 1 && state->reverse_time > 0) {
        const float hold = Ease(state->reverse_time / .14f); // 先保留原世界倾身，再以支撑转换推进反向
        target[0].pose.angle[1] += (state->reverse_torso - target[0].pose.angle[1]) * hold;
        target[0].pose.angle[0] += (state->reverse_head - target[0].pose.angle[0]) * hold;
    }
    state->reverse_time = fmaxf(0, state->reverse_time - dt);
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
    if (group == 1 && count == 1 && target[0].view == 1) {
        Feet(state, player, &state->current[0], dt);
        const float elbow = 1.35f; // 跑步屈肘，保持肘向前弯的轮廓
        const float center = -atan2f(68.4f * sinf(elbow), 56 + 68.4f * cosf(elbow)); // 抵消前臂屈肘自身前移
        const float forward = Limit((sinf(state->current[0].pose.angle[8]) - sinf(state->current[0].pose.angle[9])) / .95f, -1, 1); // 由真实大腿前后关系协调交叉摆臂
        for (int side = 0; side < 2; ++side) { // 上臂与同侧腿反向摆，禁止顺拐
            const float desired = center - (side ? -forward : forward) * 1.3f - state->current[0].pose.angle[1]; // 跑步实际肩关节目标
            const int arm = 4 + side; // 左右上臂索引
            const float blended = state->current[0].pose.angle[arm] + (desired - state->current[0].pose.angle[arm]) * state->run_mix; // 走跑同相位交融
            const float old = compatible ? state->previous[0].pose.angle[arm] : blended; // 同朝向下限制单步摆臂速度
            state->current[0].pose.angle[arm] = old + Limit(blended - old, -22 * dt, 22 * dt);
            state->current[0].pose.angle[6 + side] += (elbow - state->current[0].pose.angle[6 + side]) * state->run_mix;
        }
    }
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
        ReverseProjection(state, &state->current[0]);
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
            for (int slot = 0; slot < 3; ++slot) { // 插值附件弯曲及根部连续投影
                layers[index].bend[slot] = state->previous[index].bend[slot] + (state->current[index].bend[slot] - state->previous[index].bend[slot]) * weight;
                for (int axis = 0; axis < 2; ++axis) layers[index].shift[slot][axis] = state->previous[index].shift[slot][axis] + (state->current[index].shift[slot][axis] - state->previous[index].shift[slot][axis]) * weight; // 根部逐轴插值
            }
            if (layers[index].view == 1 && state->count == 1) InterpolateLimbs(&state->previous[index].pose, &state->current[index].pose, weight, &layers[index].pose);
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
