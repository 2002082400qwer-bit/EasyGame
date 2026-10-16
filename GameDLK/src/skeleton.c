#include "skeleton.h"
#include <math.h>
#include <stddef.h>
#include "skeleton_data.h"

/* 夹紧连续参数；value为输入，low/high为闭区间边界。 */
static float Clamp(float value, float low, float high)
{
    return fminf(high, fmaxf(low, value));
}

/* 选择安全视角；view为调用方输入的视角编号。 */
static int ViewIndex(int view)
{
    return view >= 0 && view < SKELETON_VIEW_COUNT ? view : 0;
}

/* 查询只读绑定；view为0正/1侧/2背。 */
const SkeletonBone *SkeletonRig(int view)
{
    return skeleton_rigs[ViewIndex(view)];
}

/* 查询资源名称；part为固定部件编号，非法输入返回空指针。 */
const char *SkeletonPartName(int part)
{
    return part >= 0 && part < SKELETON_PART_COUNT ? skeleton_names[part] : NULL;
}

/* 查询遮挡顺序；view为视角，depth为从后向前的层号。 */
int SkeletonDrawOrder(int view, int depth)
{
    return depth >= 0 && depth < SKELETON_PART_COUNT ? skeleton_orders[ViewIndex(view)][depth] : -1;
}

/* 返回固定站姿倍率；view为绑定视角，所有动作共用此倍率。 */
float SkeletonReferenceScale(int view)
{
    return skeleton_reference_scales[ViewIndex(view)];
}

/* 解包一行关节数据；values为连续浮点行，out接收姿势。 */
static void Unpack(const float *values, SkeletonPose *out)
{
    out->root[0] = values[0];
    out->root[1] = values[1];
    for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 当前部件索引。 */
        out->angle[part] = values[2 + part];
        out->scale[part] = values[18 + part];
    }
    out->chest[0] = values[34];
    out->chest[1] = values[35];
}

/* 在两姿势间连续混合；first/last为端点，weight为末姿权重，out允许原地写入。 */
void SkeletonBlend(const SkeletonPose *first, const SkeletonPose *last, float weight, SkeletonPose *out)
{
    const float blend = Clamp(weight, 0.0f, 1.0f); /* 夹紧后的末姿权重。 */
    SkeletonPose result; /* 防止输出覆盖尚未读取的端点。 */
    if (!first || !last || !out) return;
    for (int axis = 0; axis < 2; ++axis) { /* 根位移与胸偏移分量。 */
        result.root[axis] = first->root[axis] + (last->root[axis] - first->root[axis]) * blend;
        result.chest[axis] = first->chest[axis] + (last->chest[axis] - first->chest[axis]) * blend;
    }
    for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 当前混合部件。 */
        const float delta = remainderf(last->angle[part] - first->angle[part], 6.283185307179586f); /* 最短角差。 */
        result.angle[part] = first->angle[part] + delta * blend;
        result.scale[part] = first->scale[part] + (last->scale[part] - first->scale[part]) * blend;
    }
    *out = result;
}

/* 连续时间采样；view/action为视角动作，progress为进度，loop选择周期环绕，out接收结果。 */
void SkeletonSample(int view, int action, float progress, int loop, SkeletonPose *out)
{
    float position; /* 在六十采样行中的连续位置。 */
    int first; /* 前一采样索引。 */
    int last; /* 后一采样索引。 */
    SkeletonPose start; /* 前采样解包值。 */
    SkeletonPose end; /* 后采样解包值。 */
    if (!out) return;
    if (action < SKELETON_IDLE || action > SKELETON_CROUCH) action = SKELETON_IDLE;
    if (!isfinite(progress)) progress = 0.0f;
    if (loop) {
        float phase = fmodf(progress, 1.0f); /* 先取余以免大进度相减损失周期小数。 */
        if (phase < 0.0f) phase += 1.0f;
        position = phase * SKELETON_SAMPLE_COUNT;
        /* 极小负进度加一或乘六十可能舍入到周期末端，必须回到首帧，禁止索引六十。 */
        if (position >= SKELETON_SAMPLE_COUNT) position = 0.0f;
    } else {
        position = Clamp(progress, 0.0f, 1.0f) * (SKELETON_SAMPLE_COUNT - 1);
    }
    first = (int)position;
    last = first + 1;
    if (last >= SKELETON_SAMPLE_COUNT) last = loop ? 0 : SKELETON_SAMPLE_COUNT - 1;
    Unpack(skeleton_samples[ViewIndex(view)][action][first], &start);
    Unpack(skeleton_samples[ViewIndex(view)][action][last], &end);
    SkeletonBlend(&start, &end, position - first, out);
}

/* 求等长双段腿接地角；dx/dy为脚踝相对髋目标，bend为膝弯方向，thigh/shin/foot接收局部角。 */
static void LegAngles(float dx, float dy, float bend, float *thigh, float *shin, float *foot)
{
    const float distance = Clamp(hypotf(dx, dy), 0.000001f, 199.9999f); /* 腿可到达距离。 */
    const float heading = atan2f(-dx, dy); /* 目标线相对向下轴的角度。 */
    const float opening = acosf(distance / 200.0f); /* 大腿与目标线的夹角。 */
    float global_shin; /* 小腿世界角。 */
    *thigh = heading + bend * opening;
    global_shin = atan2f(-(dx + sinf(*thigh) * 100.0f), dy - cosf(*thigh) * 100.0f);
    *shin = global_shin - *thigh;
    *foot = -global_shin;
}

/* 连续下蹲并重解接地双腿；view为视角，depth为0站立到1深蹲，out为输出。 */
void SkeletonCrouch(int view, float depth, SkeletonPose *out)
{
    SkeletonPose standing; /* 深度零的标准待机。 */
    SkeletonPose crouched; /* 深度一的参考深蹲。 */
    const float amount = Clamp(depth, 0.0f, 1.0f); /* 连续下蹲程度。 */
    const int selected = ViewIndex(view); /* 合法视角编号。 */
    if (!out) return;
    SkeletonSample(selected, SKELETON_IDLE, 0.0f, 1, &standing);
    SkeletonSample(selected, SKELETON_CROUCH, 29.0f / 59.0f, 0, &crouched);
    SkeletonBlend(&standing, &crouched, amount, out);
    for (int side = 0; side < 2; ++side) { /* 左右接地腿。 */
        const float bend = selected == 1 || side == 0 ? 1.0f : -1.0f; /* 正背双膝外弯、侧面同向。 */
        const float dx = (side == 0 ? -2.0f : 2.0f) * amount; /* 轻微展足距离。 */
        LegAngles(dx, 451.0f - 254.0f - out->root[1], bend,
                  &out->angle[8 + side], &out->angle[10 + side], &out->angle[12 + side]);
        out->scale[8 + side] = 1.0f;
        out->scale[10 + side] = 1.0f;
    }
    if (amount == 0.0f) { /* 站姿端点精确保持原始待机。 */
        *out = standing;
    }
    if (selected == 1) {
        out->chest[0] = 52.0f * sinf(out->angle[1]);
        out->chest[1] = 52.0f * (1.0f - cosf(out->angle[1]));
    }
}

/* 递归解析非拓扑排列父链；part为当前骨骼，rig/pose为输入，joints/done为结果缓存。 */
static void Resolve(int part, const SkeletonBone *rig, const SkeletonPose *pose, SkeletonJoint *joints, unsigned char *done)
{
    const SkeletonBone *bone = &rig[part]; /* 当前绑定记录。 */
    float x = bone->x + (part == 1 ? pose->chest[0] : 0.0f); /* 局部横坐标含胸偏移。 */
    float y = bone->y + (part == 1 ? pose->chest[1] : 0.0f); /* 局部纵坐标含胸偏移。 */
    if (done[part]) return;
    if (bone->parent < 0) {
        joints[part] = (SkeletonJoint){x + pose->root[0], y + pose->root[1], pose->angle[part]};
    } else {
        const int parent = bone->parent; /* 父节点索引。 */
        float cosine; /* 父关节角余弦。 */
        float sine; /* 父关节角正弦。 */
        Resolve(parent, rig, pose, joints, done);
        cosine = cosf(joints[parent].angle);
        sine = sinf(joints[parent].angle);
        y *= pose->scale[parent];
        joints[part].x = joints[parent].x + cosine * x - sine * y;
        joints[part].y = joints[parent].y + sine * x + cosine * y;
        joints[part].angle = joints[parent].angle + pose->angle[part];
    }
    done[part] = 1;
}

/* 合成全部世界关节；view为视角，pose为局部姿势，joints为十六关节输出数组。 */
void SkeletonWorld(int view, const SkeletonPose *pose, SkeletonJoint joints[SKELETON_PART_COUNT])
{
    unsigned char done[SKELETON_PART_COUNT] = {0}; /* 本次父链解析缓存。 */
    if (!pose || !joints) return;
    for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 待解析的部件索引。 */
        Resolve(part, SkeletonRig(view), pose, joints, done);
    }
}
