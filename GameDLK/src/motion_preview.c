#include "motion_preview.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <wchar.h>

/* 保存当前GPU画面：root为工程目录，frame为对照序号；只输出验证截图，不生成游戏美术。 */
static bool Capture(const wchar_t *root, int frame)
{
    rlDrawRenderBatchActive();
    Image picture = LoadImageFromScreen(); // 当前真实GPU缓冲区
    int count = 0; // 编码字节数
    unsigned char *bytes = ExportImageToMemory(picture, ".png", &count); // 临时PNG数据
    UnloadImage(picture);
    if (!bytes) return false;
    wchar_t path[32768]; // 本帧验证截图路径
    swprintf(path, 32768, L"%ls/verification/motion-%04d.png", root, frame);
    FILE *file = _wfopen(path, L"wb"); // 中文目录输出
    bool ok = file && fwrite(bytes, 1, count, file) == (size_t)count; // 实际写入结果
    if (file && fclose(file) != 0) ok = false;
    MemFree(bytes);
    return ok;
}

/* 回放确定性输入：renderer为同一套部件，root为输出目录；左右面板分别展示旧硬切和新连续管线。 */
int MotionPreview(SkeletalRenderer *renderer, const wchar_t *root)
{
    const PlayerWorld world = {-10000, 10000, 600, 640}; // 无边界干扰的动态试验场
    Player player; // 同一条真实物理轨迹
    ActorAnimation animation; // 同一套35状态输入逻辑
    MotionState motion; // 新管线的持久连续动作
    wchar_t path[32768]; // 连续帧数值记录
    swprintf(path, 32768, L"%ls/verification/motion-trace.jsonl", root);
    FILE *report = _wfopen(path, L"wb"); // 与截图一一对应的只读测量记录
    if (!report) return 3;
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionInit(&motion, &player, &animation);
    bool passed = true; // 全部真实帧是否成功保存
    for (int frame = 0; frame < 432; ++frame) { // 7.2秒60FPS对照，动作按120Hz推进
        if (frame == 300) AnimationFace(&animation, VIEW_FRONT);
        if (frame == 310) AnimationFace(&animation, VIEW_BACK);
        if (frame == 320) AnimationFace(&animation, VIEW_LEFT);
        for (int tick = 0; tick < 2; ++tick) { // 每个显示帧有两个物理步
            PlayerInput input = {.move = frame >= 30 && frame < 210 && frame != 125 ? -1 : frame >= 330 && frame < 420 ? (((frame - 330) / 15) % 2 ? -1 : 1) : 0,
                .run = (frame >= 90 && frame < 125) || frame == 126 || (frame >= 145 && frame < 210) || frame >= 330, .jump = frame == 150 && tick == 0,
                .dash = (frame == 180 || frame == 270) && tick == 0, .crouch = frame >= 240 && frame < 300}; // 起步、走跑、跳冲、停止、蹲冲、反向跑
            PlayerUpdate(&player, input, &world, 1.0f / 120);
            AnimationUpdate(&animation, &player, 1.0f / 120);
            MotionUpdate(&motion, &player, &animation, 1.0f / 120);
        }
        Player visual; // 新管线使用半物理步插值的绘制位置
        const int count = MotionSample(&motion, .5f, &visual, renderer->animated_layers); // 与正式主循环相同的采样接口
        BeginDrawing();
        ClearBackground((Color){13, 20, 31, 255});
        DrawText("SAME INPUT / SAME PHYSICS / SAME 48 PARTS", 250, 30, 24, WHITE);
        DrawText("BASE KEY POSES", 50, 90, 22, (Color){245, 140, 120, 255});
        DrawText("REWORKED RUN + REVERSALS", 690, 90, 22, (Color){112, 217, 184, 255});
        DrawLine(640, 80, 640, 660, (Color){75, 90, 100, 255});
        for (int panel = 0; panel < 2; ++panel) { // 两边各自以跟随镜头观察同一真实位移
            const float physical_x = panel ? visual.x : player.x; // 与对应姿态一致的镜头位置
            for (int mark = -15; mark < 16; ++mark) { // 地面滚动刻度用于观察脚是否打滑
                const float x = panel * 640 + 320 + mark * 60 - fmodf(physical_x, 60); // 固定世界刻度随镜头滚动
                if (x >= panel * 640 + 2 && x < (panel + 1) * 640 - 2) DrawLine((int)x, 600, (int)x, 618, (Color){80, 130, 140, 255});
            }
            DrawLine(panel * 640, 600, (panel + 1) * 640, 600, (Color){112, 217, 184, 255});
            Player drawing = panel ? visual : player; // 同一物理角色映射到左右面板中心
            drawing.x = panel * 640 + 320;
            renderer->animated_count = panel ? count : 0;
            if (player.action == ACTION_DASH) SkeletalRendererDraw(renderer, &drawing, &animation, .18f, -player.dash_direction * 35);
            SkeletalRendererDraw(renderer, &drawing, &animation, 1, 0);
        }
        DrawText(TextFormat("%5.2fs   %s", frame / 60.0f, PlayerActionName(player.action)), 40, 665, 22, WHITE);
        DrawText("WALK / RUN / JUMP-DASH / CROUCH-DASH / VIEW KEYS / RAPID LEFT-RIGHT", 40, 145, 16, (Color){155, 180, 195, 255});
        passed = Capture(root, frame) && passed && !renderer->failed;
        EndDrawing();
        fprintf(report, "{\"frame\":%d,\"action\":\"%s\",\"clip\":%d,\"phase\":%.6f,\"runMix\":%.6f,\"layers\":%d,\"x\":%.6f,\"visualX\":%.6f,\"bend\":%.6f,\"passed\":%s}\n",
            frame, PlayerActionName(player.action), animation.clip, motion.phase, motion.run_mix, count, player.x, visual.x, renderer->animated_layers[0].bend[0], passed ? "true" : "false");
    }
    renderer->animated_count = 0;
    if (fclose(report) != 0) passed = false;
    printf("Motion preview: 432 actual GPU frames, %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 3;
}
