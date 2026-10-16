#ifndef MX002_SKELETON_H
#define MX002_SKELETON_H /* 实时骨骼纯数学接口保护。 */

#define SKELETON_PART_COUNT 16 /* 每视角固定拆件数量。 */
#define SKELETON_VIEW_COUNT 3 /* 正面、侧面、背面数量。 */

typedef enum SkeletonAction {
    SKELETON_IDLE, /* 呼吸待机。 */
    SKELETON_WALK, /* 慢走循环。 */
    SKELETON_RUN, /* 跑步循环。 */
    SKELETON_JUMP, /* 跳跃收腿。 */
    SKELETON_DASH, /* 冲刺及恢复。 */
    SKELETON_CROUCH /* 下蹲及起身参考动作。 */
} SkeletonAction;

typedef struct SkeletonPose {
    float root[2]; /* 骨盆相对绑定的横纵位移。 */
    float angle[SKELETON_PART_COUNT]; /* 各部件局部转角，单位弧度。 */
    float scale[SKELETON_PART_COUNT]; /* 各部件纵向缩放。 */
    float chest[2]; /* 胸关节相对绑定的局部横纵偏移。 */
} SkeletonPose;

typedef struct SkeletonBone {
    int parent; /* 父部件索引，根为-1。 */
    float x, y; /* 相对父关节的绑定坐标。 */
    float width, height; /* 512基准坐标中的绘制尺寸。 */
    float pivot_x, pivot_y; /* 原图归一化旋转枢轴。 */
    int source_width, source_height; /* 校验PNG原始像素尺寸。 */
} SkeletonBone;

typedef struct SkeletonJoint {
    float x, y; /* 512基准画布的世界关节坐标。 */
    float angle; /* 累加父链后的世界弧度。 */
} SkeletonJoint;

/* view为0正/1侧/2背，返回只读绑定。 */
const SkeletonBone *SkeletonRig(int view);
/* part为固定部件索引，返回资源文件名主体。 */
const char *SkeletonPartName(int part);
/* view为视角，depth为从后到前0..15层，返回部件索引。 */
int SkeletonDrawOrder(int view, int depth);
/* view为视角，返回512坐标到208像素固定站姿的缩放。 */
float SkeletonReferenceScale(int view);
/* view/action为视角动作，progress为连续进度，loop决定环绕，out接收姿势。 */
void SkeletonSample(int view, int action, float progress, int loop, SkeletonPose *out);
/* first/last为端点姿势，weight为0..1权重，out允许与端点相同。 */
void SkeletonBlend(const SkeletonPose *first, const SkeletonPose *last, float weight, SkeletonPose *out);
/* view为视角，depth为0站立到1深蹲，out接收双脚接地的连续姿势。 */
void SkeletonCrouch(int view, float depth, SkeletonPose *out);
/* view为绑定视角，pose为局部姿势，joints接收全部父链变换。 */
void SkeletonWorld(int view, const SkeletonPose *pose, SkeletonJoint joints[SKELETON_PART_COUNT]);

#endif
