#include "skeleton.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

/* 验证公共循环进度边界不跨动作读表；无参数。 */
static void TestLoopBoundaries(void)
{
    const float inputs[] = {-1e-8f, -1e-9f, -1e-30f, -FLT_MIN, -0.0f, 0.0f,
                            1.0f, -1.0f, 100000000.0f, FLT_MAX, -FLT_MAX,
                            1048576.25f, -1048576.25f}; /* 微小负数、整周期和大进度。 */
    const float expected[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .25f, .75f}; /* 等价的安全周期相位。 */
    SkeletonPose actual; /* 边界输入采样结果。 */
    SkeletonPose reference; /* 等价安全相位结果。 */
    for (int view = 0; view < SKELETON_VIEW_COUNT; ++view) { /* 当前测试视角。 */
        for (int action = SKELETON_IDLE; action <= SKELETON_CROUCH; ++action) { /* 包含最后动作的越界风险。 */
            for (unsigned int index = 0; index < sizeof(inputs) / sizeof(inputs[0]); ++index) { /* 当前公共边界。 */
                SkeletonSample(view, action, inputs[index], 1, &actual);
                SkeletonSample(view, action, expected[index], 1, &reference);
                for (int axis = 0; axis < 2; ++axis) { /* 根与胸偏移分量。 */
                    assert(fabsf(actual.root[axis] - reference.root[axis]) < .0001f);
                    assert(fabsf(actual.chest[axis] - reference.chest[axis]) < .0001f);
                }
                for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 角度与纵向尺度。 */
                    assert(fabsf(actual.angle[part] - reference.angle[part]) < .0001f);
                    assert(fabsf(actual.scale[part] - reference.scale[part]) < .0001f);
                }
            }
        }
    }
}

/* 验证父节点纵向缩放与胸局部偏移的组合顺序；无参数。 */
static void TestParentScale(void)
{
    SkeletonPose pose = {0}; /* 手工构造的父链输入。 */
    SkeletonJoint joints[SKELETON_PART_COUNT]; /* 父链输出。 */
    float chest_x; /* 胸关节的独立基准横坐标。 */
    float chest_y; /* 胸关节的独立基准纵坐标。 */
    for (int part = 0; part < SKELETON_PART_COUNT; ++part) pose.scale[part] = 1.0f; /* 每部件初始不缩放。 */
    pose.root[0] = 10.0f;
    pose.root[1] = 20.0f;
    pose.angle[2] = .2f;
    pose.angle[1] = .5f;
    pose.scale[2] = .75f;
    pose.scale[1] = .8f;
    pose.chest[0] = 3.0f;
    pose.chest[1] = 4.0f;
    chest_x = 266.0f + cosf(.2f) * 3.0f - sinf(.2f) * -36.0f;
    chest_y = 267.0f + sinf(.2f) * 3.0f + cosf(.2f) * -36.0f;
    SkeletonWorld(1, &pose, joints);
    assert(fabsf(joints[1].x - chest_x) < .0001f);
    assert(fabsf(joints[1].y - chest_y) < .0001f);
    assert(fabsf(joints[0].x - (chest_x - sinf(.7f) * -78.4f)) < .0001f);
    assert(fabsf(joints[0].y - (chest_y + cosf(.7f) * -78.4f)) < .0001f);
    assert(fabsf(joints[0].angle - .7f) < .0001f);
}

/* 验证循环接缝、连续小数帧、非循环夹紧及跑步双脚腾空；无参数。 */
static void TestSampling(void)
{
    SkeletonPose first; /* 前一帧或首端点。 */
    SkeletonPose last; /* 后一帧或末端点。 */
    SkeletonPose middle; /* 连续中点或被夹紧姿势。 */
    SkeletonJoint joints[SKELETON_PART_COUNT]; /* 当前世界关节。 */
    for (int view = 0; view < SKELETON_VIEW_COUNT; ++view) { /* 当前测试视角。 */
        int flying = 0; /* 双脚离地的跑步采样数量。 */
        SkeletonSample(view, SKELETON_WALK, 10.0f / 60.0f, 1, &first);
        SkeletonSample(view, SKELETON_WALK, 11.0f / 60.0f, 1, &last);
        SkeletonSample(view, SKELETON_WALK, 10.5f / 60.0f, 1, &middle);
        assert(fabsf(middle.root[1] - .5f * (first.root[1] + last.root[1])) < .0001f);
        SkeletonSample(view, SKELETON_WALK, .99999f, 1, &first);
        SkeletonSample(view, SKELETON_WALK, .00001f, 1, &last);
        assert(fabsf(first.angle[8] - last.angle[8]) < .002f);
        SkeletonSample(view, SKELETON_JUMP, 1.0f, 0, &first);
        SkeletonSample(view, SKELETON_JUMP, 3.0f, 0, &last);
        assert(fabsf(first.root[1] - last.root[1]) < .0001f);
        for (int frame = 0; frame < 60; ++frame) { /* 跑步采样帧。 */
            SkeletonSample(view, SKELETON_RUN, frame / 60.0f, 1, &middle);
            SkeletonWorld(view, &middle, joints);
            if (joints[12].y < 446.0f && joints[13].y < 446.0f) ++flying;
        }
        assert(flying > 0);
        SkeletonSample(view, SKELETON_IDLE, 0.0f, 1, &first);
        last = first;
        first.angle[0] = 3.13f;
        last.angle[0] = -3.13f;
        SkeletonBlend(&first, &last, .5f, &first);
        assert(first.angle[0] > 3.13f && first.angle[0] < 3.15f);
    }
}

/* 验证实时骨骼接口、父链和持续接地；无参数。 */
int main(void)
{
    SkeletonPose pose; /* 当前采样姿势。 */
    SkeletonJoint joints[SKELETON_PART_COUNT]; /* 世界关节结果。 */
    SkeletonPose first; /* 动作首姿势。 */
    SkeletonPose last; /* 动作末姿势。 */
    SkeletonPose mixed; /* 插值姿势。 */
    int checks = 0; /* 已执行断言计数。 */
    TestLoopBoundaries();
    TestParentScale();
    TestSampling();
    for (int view = 0; view < SKELETON_VIEW_COUNT; ++view) { /* 当前视角。 */
        const SkeletonBone *rig = SkeletonRig(view); /* 当前绑定。 */
        unsigned int order = 0; /* 图层索引去重掩码。 */
        assert(rig[0].parent == 1 && rig[1].parent == 2 && rig[2].parent == -1);
        assert(SkeletonReferenceScale(view) > 0.3f && SkeletonReferenceScale(view) < 1.0f);
        for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 当前部件。 */
            const int index = SkeletonDrawOrder(view, part); /* 当前深度部件。 */
            assert(index >= 0 && index < SKELETON_PART_COUNT);
            order |= 1u << index;
            assert(SkeletonPartName(part) && rig[part].source_width > 0 && rig[part].source_height > 0);
            assert(rig[part].pivot_x >= 0.0f && rig[part].pivot_x <= 1.0f);
        }
        assert(order == 65535u);
        for (int action = SKELETON_IDLE; action <= SKELETON_CROUCH; ++action) { /* 当前基础动作。 */
            SkeletonSample(view, action, 0.0f, 1, &first);
            SkeletonSample(view, action, 1.0f, 1, &last);
            assert(fabsf(first.root[1] - last.root[1]) < 0.0001f);
            for (int frame = 0; frame <= 240; ++frame) { /* 连续四分之一帧采样。 */
                SkeletonSample(view, action, frame / 240.0f, 0, &pose);
                SkeletonWorld(view, &pose, joints);
                for (int part = 0; part < SKELETON_PART_COUNT; ++part) { /* 检查的世界关节。 */
                    assert(isfinite(joints[part].x) && isfinite(joints[part].y) && isfinite(joints[part].angle));
                    assert(pose.scale[part] > 0.0f);
                    ++checks;
                }
            }
        }
        for (int step = 0; step <= 100; ++step) { /* 连续下蹲深度。 */
            SkeletonCrouch(view, step / 100.0f, &pose);
            SkeletonWorld(view, &pose, joints);
            assert(fabsf(joints[12].y - 451.0f) < 0.001f);
            assert(fabsf(joints[13].y - 451.0f) < 0.001f);
            assert(fabsf(joints[12].angle) < 0.001f && fabsf(joints[13].angle) < 0.001f);
            if (step == 0) {
                SkeletonSample(view, SKELETON_IDLE, 0.0f, 1, &first);
                assert(fabsf(pose.root[1] - first.root[1]) < 0.0001f);
            }
            if (step == 100 && view != 1) assert(pose.angle[8] > 0.0f && pose.angle[9] < 0.0f);
            ++checks;
        }
        SkeletonSample(view, SKELETON_IDLE, 0.0f, 1, &first);
        SkeletonSample(view, SKELETON_RUN, 0.25f, 1, &last);
        SkeletonBlend(&first, &last, 0.25f, &mixed);
        assert(fabsf(mixed.root[1] - (first.root[1] * .75f + last.root[1] * .25f)) < .0001f);
        SkeletonWorld(view, &first, joints);
        assert(fabsf(joints[0].y - 94.0f) < .001f);
    }
    printf("skeleton: %d assertions and binding/blend checks passed\n", checks);
    return 0;
}
