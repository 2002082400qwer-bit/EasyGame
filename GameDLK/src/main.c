#define WIN32_LEAN_AND_MEAN // 仅包含必要的Windows声明
#define NOGDI // 避免Windows绘图接口与raylib函数重名
#define NOUSER // 避免Windows窗口接口与raylib函数重名
#define APP_WIDTH 1280 // 固定逻辑画布宽度
#define APP_HEIGHT 720 // 固定逻辑画布高度
#define FIXED_STEP (1.0f / 120.0f) // 每次角色物理更新的秒数
#define PATH_CAPACITY 32768 // Windows宽字符路径缓冲区容量

#include <windows.h>
#include "raylib.h"
#include "rlgl.h"
#include "player.h"
#include "animation.h"
#include "skeletal_renderer.h"
#include "motion_preview.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef SkeletalRenderer Sprites; // 实时固定拆件资源，沿用场景绘制参数名称

typedef struct VerifyScene {
    const char *name; // 截图与报告使用的场景名称
    PlayerInput setup; // 正式动作前的持续预备输入
    int setup_steps; // 预备动作更新次数
    PlayerInput input; // 场景中持续保持的输入，触发键仅在第一步生效
    int steps; // 截图前模拟的固定时间步数量
    PlayerAction expected; // 场景期望出现的动作状态
    ActorMotion motion; // 场景期望播放的动画资源类型
} VerifyScene;

static const Color ink = {13, 20, 31, 255}; // 背景深蓝色
static const Color panel = {21, 32, 46, 240}; // 信息卡片底色
static const Color muted = {133, 155, 172, 255}; // 次要说明文字颜色
static const Color paper = {233, 241, 242, 255}; // 主要文字颜色
static const Color accent = {246, 104, 93, 255}; // 角色动作强调色
static const Color mint = {112, 217, 184, 255}; // 地面和就绪状态强调色
static const PlayerWorld world = {48.0f, 1232.0f, 600.0f, 640.0f}; // 固定测试场边界及地面
static wchar_t root[PATH_CAPACITY]; // 从可执行文件位置获得的工程根目录

/* 定位工程根目录：程序位于build目录中，向上两级得到资源目录；无参数。 */
static bool FindRoot(void)
{
    const DWORD length = GetModuleFileNameW(NULL, root, PATH_CAPACITY); // 可执行文件路径长度
    if (length == 0 || length >= PATH_CAPACITY) return false;
    for (int level = 0; level < 2; ++level) { // 移除文件名及build目录的次数
        wchar_t *slash = wcsrchr(root, L'\\'); // 当前路径最后的目录分隔符
        if (slash == NULL) return false;
        *slash = L'\0';
    }
    return true;
}

/* 绘制辅助文字：text为内容，x/y为左上坐标，size为像素字号，color为字色。 */
static void Label(const char *text, int x, int y, int size, Color color)
{
    DrawText(text, x, y, size, color);
}

/* 绘制固定测试场：time 为视觉特效时间，角色物理不使用该参数。 */
static void DrawStage(float time)
{
    ClearBackground(ink);
    DrawRectangleGradientV(0, 195, APP_WIDTH, 405, (Color){13, 20, 31, 255}, (Color){25, 46, 55, 255});
    for (int column = 0; column <= 16; ++column) { // 背景纵向参考网格列序号
        DrawLine(column * 80, 202, column * 80, 600, (Color){43, 62, 72, 65});
    }
    for (int row = 0; row < 6; ++row) { // 背景横向参考网格行序号
        DrawLine(0, 240 + row * 72, APP_WIDTH, 240 + row * 72, (Color){43, 62, 72, 55});
    }
    DrawCircle(1090, 294, 48, (Color){47, 81, 87, 110});
    DrawCircleLines(1090, 294, 58, (Color){71, 109, 111, 130});
    DrawTriangle((Vector2){0, 568}, (Vector2){310, 568}, (Vector2){148, 384}, (Color){32, 54, 62, 255});
    DrawTriangle((Vector2){168, 568}, (Vector2){624, 568}, (Vector2){422, 324}, (Color){29, 49, 59, 255});
    DrawTriangle((Vector2){690, 568}, (Vector2){1180, 568}, (Vector2){925, 367}, (Color){31, 54, 61, 255});
    DrawTriangle((Vector2){1000, 568}, (Vector2){1280, 568}, (Vector2){1208, 415}, (Color){35, 59, 65, 255});
    DrawRectangle(0, 560, APP_WIDTH, 40, (Color){17, 30, 39, 255});
    DrawRectangle(0, 600, APP_WIDTH, 120, (Color){18, 28, 40, 255});
    DrawRectangle(0, 600, APP_WIDTH, 3, mint);
    DrawRectangle(0, 603, APP_WIDTH, 11, (Color){33, 63, 66, 255});
    for (int mark = 0; mark < 32; ++mark) { // 地面距离刻度序号
        const int x = mark * 40; // 当前地面刻度的水平像素坐标
        DrawLine(x, 615, x, mark % 4 == 0 ? 629 : 621, (Color){87, 120, 129, 130});
    }
    for (int particle = 0; particle < 12; ++particle) { // 背景微光粒子序号
        const float x = fmodf(particle * 107.0f + time * 9.0f, 1280.0f); // 随时间缓慢漂移的粒子横坐标
        const float y = 350.0f + sinf(particle * 2.3f + time * 0.8f) * 115.0f; // 粒子纵向轻微起伏位置
        DrawCircleV((Vector2){x, y}, 1.5f, (Color){144, 194, 177, 85});
    }
    Label("TRAINING GROUND", 38, 557, 13, muted);
    Label("0 m", 46, 635, 12, muted);
    Label("LIVE BONES  /  FIXED 120 Hz", 1006, 635, 12, muted);
}

/* 实时绘制角色：sprites为48拆件，player/animation为状态，opacity/offset为残影参数。 */
static void DrawActor(Sprites *sprites, const Player *player, const ActorAnimation *animation, float opacity, float offset)
{
    SkeletalRendererDraw(sprites, player, animation, opacity, offset);
}

/* 绘制角色及碰撞框：sprites为缓存，player为物理，animation为动画，debug控制碰撞显示。 */
static void DrawPlayer(Sprites *sprites, const Player *player, const ActorAnimation *animation, bool debug)
{
    const float height = world.ground - player->y; // 角色当前离地高度
    const float shadow = fmaxf(0.25f, 1.0f - height / 230.0f); // 随离地高度缩小和变淡的阴影比例
    DrawEllipse((int)player->x, 599, 40.0f * shadow, 5.0f * shadow, (Color){5, 10, 16, 150});
    if (player->action == ACTION_DASH) {
        DrawActor(sprites, player, animation, 0.10f, -player->dash_direction * 60.0f);
        DrawActor(sprites, player, animation, 0.22f, -player->dash_direction * 30.0f);
    }
    DrawActor(sprites, player, animation, 1.0f, 0.0f);
    if (debug) {
        const Rectangle body = {player->x - PLAYER_WIDTH * 0.5f, player->y - PlayerHeight(player), PLAYER_WIDTH, PlayerHeight(player)}; // 与角色脚底对齐的真实碰撞矩形
        DrawRectangleLinesEx(body, 1.5f, mint);
        DrawCircleV((Vector2){player->x, player->y}, 3, accent);
        Label("FOOT PIVOT", (int)player->x + 10, (int)player->y + 6, 12, paper);
    }
}

/* 绘制按键卡片：key为键名，description为用途，x/y为位置，width为卡片宽度。 */
static void KeyCard(const char *key, const char *description, int x, int y, int width)
{
    DrawRectangleRounded((Rectangle){(float)x, (float)y, (float)width, 42}, 0.18f, 4, panel);
    DrawRectangleRoundedLinesEx((Rectangle){(float)x, (float)y, (float)width, 42}, 0.18f, 4, 1, (Color){50, 70, 83, 255});
    Label(key, x + 13, y + 13, 16, paper);
    Label(description, x + width - MeasureText(description, 14) - 13, y + 14, 14, muted);
}

/* 绘制操作面板：player提供物理，animation提供逻辑动作及阶段，debug为碰撞框开关。 */
static void DrawHud(const Player *player, const ActorAnimation *animation, bool debug)
{
    Label("MX002", 38, 27, 17, mint);
    Label("DILUC / MOVEMENT LAB", 38, 53, 28, paper);
    Label("LIVE SKELETON  /  48 PARTS  /  35 STATES", 39, 92, 13, muted);
    DrawRectangleRounded((Rectangle){895, 26, 347, 158}, 0.10f, 5, panel);
    Label("CURRENT ACTION", 916, 45, 12, muted);
    Label(PlayerActionName(player->action), 914, 68, 33, accent);
    Label(player->grounded ? "GROUND" : "AIRBORNE", 1120, 81, 13, mint);
    Label("DASH", 916, 123, 13, muted);
    DrawRectangleRounded((Rectangle){978, 124, 150, 10}, 0.6f, 6, (Color){41, 57, 72, 255});
    const float ready = 1.0f - player->dash_cooldown / PLAYER_DASH_COOLDOWN; // 冲刺冷却进度，满格表示可用
    if (ready > 0) DrawRectangleRounded((Rectangle){978, 124, 150 * ready, 10}, 0.6f, 6, mint);
    Label(player->dash_cooldown > 0 ? TextFormat("%.2fs", player->dash_cooldown) : "READY", 1147, 121, 14, paper);
    Label(AnimationViewName(animation->view), 916, 152, 12, muted);
    Label(TextFormat("POSE %02d", animation->frame), 1114, 152, 12, muted);
    KeyCard("A / D", "WALK", 38, 127, 188);
    KeyCard("SPACE", "JUMP", 238, 127, 188);
    KeyCard("S / DOWN", "CROUCH", 438, 127, 198);
    KeyCard("SHIFT", "DASH", 648, 127, 188);
    Label("CTRL  RUN   /   1 FRONT   2 LEFT   3 BACK   4 RIGHT", 39, 182, 12, muted);
    Label(AnimationClipName(animation->clip), 916, 188, 12, mint);
    DrawRectangle(38, 661, 1204, 1, (Color){51, 69, 82, 255});
    Label("R  RESET", 38, 680, 13, paper);
    Label(debug ? "F1  COLLIDER ON" : "F1  COLLIDER OFF", 187, 680, 13, debug ? mint : muted);
    Label("ESC  EXIT", 409, 680, 13, muted);
    Label("CROUCH + SHIFT  >  DASH + STAND", 899, 680, 12, muted);
}

/* 声明截图接口：name为截图名称；绘制结束交换缓冲前调用，确保读到当前帧。 */
static bool SaveScreenshot(const char *name);

/* 绘制完整帧：sprites为缓存，player为物理，animation为动画，debug控制碰撞框，time为视觉时间，capture为可选截图名。 */
static bool DrawFrame(Sprites *sprites, const Player *player, const ActorAnimation *animation, bool debug, float time, const char *capture)
{
    BeginDrawing();
    DrawStage(time);
    DrawPlayer(sprites, player, animation, debug);
    DrawHud(player, animation, debug);
    const bool saved = capture == NULL || SaveScreenshot(capture); // 交换前读取当前绘制缓冲，防止截图落后一帧
    EndDrawing();
    return saved;
}

/* 保存真实窗口截图：name为ASCII场景名称；宽字符输出避免工程路径编码问题。 */
static bool SaveScreenshot(const char *name)
{
    rlDrawRenderBatchActive();
    Image screenshot = LoadImageFromScreen(); // 当前GPU帧缓冲区读取的图片
    int byte_count = 0; // PNG内存编码结果的字节数
    unsigned char *bytes = ExportImageToMemory(screenshot, ".png", &byte_count); // 编码后的PNG字节
    UnloadImage(screenshot);
    if (bytes == NULL || byte_count <= 0) return false;
    wchar_t path[PATH_CAPACITY]; // 验证截图的完整宽字符路径
    swprintf(path, PATH_CAPACITY, L"%ls\\verification\\%hs.png", root, name);
    FILE *file = _wfopen(path, L"wb"); // 截图输出文件
    bool saved = false; // 文件是否完整写入
    if (file != NULL) {
        saved = fwrite(bytes, 1, (size_t)byte_count, file) == (size_t)byte_count;
        if (fclose(file) != 0) saved = false;
    }
    MemFree(bytes);
    return saved;
}

/* 确定性推进输入：player为物理，animation为动画，input为保持输入，steps为更新次数。 */
static void Simulate(Player *player, ActorAnimation *animation, PlayerInput input, int steps)
{
    for (int step = 0; step < steps; ++step) { // 当前模拟时间步序号
        PlayerUpdate(player, input, &world, FIXED_STEP);
        AnimationUpdate(animation, player, FIXED_STEP);
        input.jump = false;
        input.dash = false;
    }
}

/* 写入实时GPU结果：report/count为报告，name为场景，animation/player为截图时状态，sprites为已加载部件。 */
static void ReportScene(FILE *report, int *count, const char *name, const ActorAnimation *animation, bool passed, const Sprites *sprites, const Player *player)
{
    SkeletalLayer layers[2]; // 与本次截图对应的真实骨骼姿势
    const int layer_count = SkeletalEvaluate(player, animation, layers); // 本场景一层或转向两层
    fprintf(report, "%s{\"name\":\"%s\",\"clip\":\"%s\",\"frame\":%d,\"standingReferenceHeight\":208,\"layers\":%d,\"loadedParts\":%u,\"mirror\":%s,\"passed\":%s}",
        *count > 0 ? "," : "", name, AnimationClipName(animation->clip), animation->frame,
        layer_count, sprites->texture_count, animation->mirror ? "true" : "false", passed ? "true" : "false");
    ++*count;
    printf("GPU scene %s: %s\n", name, passed ? "PASS" : "FAIL");
}

/* 执行真实GPU验证：sprites为缓存；检查全部逻辑姿态、三方向动作链及八组转向，并输出截图和报告。 */
static int Verify(Sprites *sprites)
{
    wchar_t directory[PATH_CAPACITY]; // 验证产物输出目录
    swprintf(directory, PATH_CAPACITY, L"%ls\\verification", root);
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 3;
    wchar_t report_path[PATH_CAPACITY]; // 验证JSON文件的完整路径
    swprintf(report_path, PATH_CAPACITY, L"%ls\\report.json", directory);
    FILE *report = _wfopen(report_path, L"wb"); // 本次GPU动作验证报告
    if (report == NULL) return 3;
    const VerifyScene scenes[] = { // 三方向都要执行的真实控制器输入链
        {"idle", {0}, 0, {0}, 30, ACTION_IDLE, MOTION_IDLE},
        {"walk", {0}, 0, {.move = -1}, 45, ACTION_WALK, MOTION_WALK},
        {"run", {0}, 0, {.move = -1, .run = true}, 45, ACTION_RUN, MOTION_RUN},
        {"jump-start", {0}, 0, {.jump = true}, 6, ACTION_JUMP, MOTION_LAUNCH},
        {"jump-air", {0}, 0, {.jump = true}, 30, ACTION_JUMP, MOTION_JUMP},
        {"land", {0}, 0, {.jump = true}, 94, ACTION_LAND, MOTION_JUMP},
        {"crouch", {0}, 0, {.crouch = true}, 30, ACTION_CROUCH, MOTION_RISE},
        {"rise", {.crouch = true}, 30, {0}, 8, ACTION_IDLE, MOTION_RISE},
        {"dash-start", {0}, 0, {.dash = true}, 1, ACTION_DASH, MOTION_DASH_START},
        {"dash", {0}, 0, {.dash = true}, 12, ACTION_DASH, MOTION_DASH},
        {"crouch-dash", {.crouch = true}, 30, {.crouch = true, .dash = true}, 12, ACTION_DASH, MOTION_CROUCH_DASH},
        {"crouch-dash-recover", {.crouch = true}, 30, {.crouch = true, .dash = true}, 26, ACTION_IDLE, MOTION_CROUCH_DASH},
        {"crouch-dash-standing", {.crouch = true}, 30, {.crouch = true, .dash = true}, 90, ACTION_IDLE, MOTION_IDLE}
    };
    bool all_passed = true; // 所有动作、截图与报告写入是否成功
    int count = 0; // 已记录的GPU检查数量
    fprintf(report, "{\"raylib\":\"%s\",\"renderer\":\"live-skeleton\",\"logicalStateCount\":35,\"resourceCount\":48,\"textureBytes\":%llu,\"scenes\":[", RAYLIB_VERSION, sprites->texture_bytes);
    for (int clip = 0; clip < ANIMATION_CLIP_COUNT; ++clip) { // 逐个实际绘制的逻辑动作编号
        Player player; // 本场景独立的角色状态
        ActorAnimation animation; // 直接检查逻辑动作首、中、末阶段的绘制状态
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        animation.clip = clip;
        char name[128]; // 当前逻辑姿态验证截图名称
        snprintf(name, sizeof(name), "pose-%s", AnimationClipName(clip));
        bool saved = false; // 中间帧截图是否成功
        ActorAnimation snapshot = animation; // 与中间帧截图保持一致的报告状态
        const int frames[] = {0, 29, 59}; // 需要实际绘制的帧序号
        for (int index = 0; index < 3; ++index) { // 当前首中末帧检查序号
            animation.frame = frames[index];
            animation.action_time = animation.frame / 60.0f;
            animation.crouch_pose = 1 - animation.frame / 59.0f;
            const bool captured = DrawFrame(sprites, &player, &animation, true, 0.0f, index == 1 ? name : NULL); // 真实绘制并截取中间帧
            if (index == 1) { saved = captured; snapshot = animation; }
        }
        const bool passed = saved && !sprites->failed && sprites->texture_count == 48; // 真实部件加载、绘制与截图共同通过
        ReportScene(report, &count, name, &snapshot, passed, sprites, &player);
        all_passed = all_passed && passed;
    }
    const ActorView views[] = {VIEW_FRONT, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT}; // 检查三个原画方向以及右侧镜像
    for (int direction = 0; direction < 4; ++direction) { // 当前角色朝向
        for (size_t index = 0; index < sizeof(scenes) / sizeof(scenes[0]); ++index) { // 当前动作链场景
            const VerifyScene *scene = &scenes[index]; // 输入及期望结果
            Player player; // 本场景独立角色
            ActorAnimation animation; // 本场景独立动画
            PlayerInit(&player, &world);
            if (views[direction] == VIEW_RIGHT) player.facing = 1;
            AnimationInit(&animation, views[direction]);
            Simulate(&player, &animation, scene->setup, scene->setup_steps);
            PlayerInput input = scene->input; // 根据侧面朝向调整行走方向
            if (views[direction] == VIEW_RIGHT) input.move = -input.move;
            Simulate(&player, &animation, input, scene->steps);
            char name[128]; // 当前方向及动作的截图名称
            snprintf(name, sizeof(name), "%s-%s", AnimationViewName(views[direction]), scene->name);
            const bool saved = DrawFrame(sprites, &player, &animation, true, 0.0f, name); // 与当前状态一致的真实GPU截图
            const bool passed = player.action == scene->expected && animation.clip == AnimationClip(views[direction], scene->motion) &&
                animation.frame >= 0 && animation.frame < 60 && animation.mirror == (views[direction] == VIEW_RIGHT) &&
                !sprites->failed && saved; // 状态、实际资源、镜像、有效帧和截图共同通过
            ReportScene(report, &count, name, &animation, passed, sprites, &player);
            all_passed = all_passed && passed;
        }
    }
    const ActorView origins[] = {VIEW_FRONT, VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_BACK, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT}; // 转身起点
    const ActorView targets[] = {VIEW_LEFT, VIEW_FRONT, VIEW_RIGHT, VIEW_FRONT, VIEW_LEFT, VIEW_BACK, VIEW_RIGHT, VIEW_BACK}; // 转身终点
    for (int index = 0; index < 8; ++index) { // 当前转向资源编号
        Player player; // 原地转身角色
        ActorAnimation animation; // 九十度转身动画
        PlayerInit(&player, &world);
        AnimationInit(&animation, origins[index]);
        AnimationFace(&animation, targets[index]);
        Simulate(&player, &animation, (PlayerInput){0}, 10);
        const char *name = AnimationClipName(27 + index); // 正式转向资源名
        const bool saved = DrawFrame(sprites, &player, &animation, true, 0.0f, name); // 转向中间帧截图
        bool passed = animation.clip == 27 + index && !animation.mirror && !sprites->failed && saved; // 转向中间状态及截图检查
        const ActorAnimation snapshot = animation; // 保存与截图一致的报告状态
        Simulate(&player, &animation, (PlayerInput){0}, 40);
        passed = passed && animation.view == targets[index] && animation.clip == AnimationClip(targets[index], MOTION_IDLE);
        ReportScene(report, &count, name, &snapshot, passed, sprites, &player);
        all_passed = all_passed && passed;
    }
    fprintf(report, "],\"checks\":%d,\"passed\":%s}\n", count, all_passed ? "true" : "false");
    if (fclose(report) != 0) all_passed = false;
    return all_passed ? 0 : 3;
}

/* 释放固定拆件：sprites为全部或部分加载的资源，各纹理只释放一次。 */
static void UnloadSprites(Sprites *sprites)
{
    SkeletalRendererUnload(sprites);
}

/* 程序入口：argc为参数数量，argv为参数列表；--verify执行隐藏窗口GPU验证。 */
int main(int argc, char **argv)
{
    const bool verify = argc > 1 && strcmp(argv[1], "--verify") == 0; // 是否执行自动截图验证
    const bool preview = argc > 1 && strcmp(argv[1], "--motion-preview") == 0; // 是否导出连续输入的GPU旧新对照
    if (!FindRoot()) { fprintf(stderr, "Cannot locate project root.\n"); return 2; }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | (verify || preview ? FLAG_WINDOW_HIDDEN : FLAG_VSYNC_HINT));
    InitWindow(APP_WIDTH, APP_HEIGHT, "MX002 | C + raylib Movement Lab");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return 2; }
    const char *limit_text = getenv("MX002_FPS_LIMIT"); // 手动性能验证时可选的渲染帧率限制
    SetTargetFPS(verify || preview ? 240 : (limit_text ? atoi(limit_text) : 120));
    Sprites sprites = {0}; // 三方向各16部件的固定纹理集合
    SkeletalRendererLoad(&sprites, root);
    if (sprites.failed) {
        UnloadSprites(&sprites);
        CloseWindow();
        return 2;
    }
    if (verify) {
        const int result = Verify(&sprites); // GPU验证退出码
        UnloadSprites(&sprites);
        CloseWindow();
        return result;
    }
    if (preview) {
        wchar_t directory[PATH_CAPACITY]; // 连续验证截图输出目录
        swprintf(directory, PATH_CAPACITY, L"%ls/verification", root);
        CreateDirectoryW(directory, NULL);
        const int result = MotionPreview(&sprites, root); // 正式GPU渲染及逐帧记录结果
        UnloadSprites(&sprites);
        CloseWindow();
        return result;
    }
    Player player; // 当前可操控角色
    ActorAnimation animation; // 当前角色的逻辑动作与转向状态
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
    MotionState motion; // 保存跨动作相位、惯性及相邻物理步姿势
    MotionInit(&motion, &player, &animation);
    float accumulator = 0.0f; // 尚未用于物理更新的渲染时间累计
    float visual_time = 0.0f; // 背景视觉特效累计时间
    bool pending_jump = false; // 等待下一次物理更新消费的跳跃触发事件
    bool pending_dash = false; // 等待下一次物理更新消费的冲刺触发事件
    bool debug = false; // 是否显示角色碰撞框
    while (!WindowShouldClose() && !sprites.failed) {
        float elapsed = GetFrameTime(); // 当前渲染帧耗时
        if (elapsed > 0.1f) elapsed = 0.1f;
        visual_time += elapsed;
        accumulator += elapsed;
        pending_jump = pending_jump || IsKeyPressed(KEY_SPACE);
        pending_dash = pending_dash || IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT);
        if (IsKeyPressed(KEY_F1)) debug = !debug;
        if (IsKeyPressed(KEY_R)) {
            PlayerInit(&player, &world);
            AnimationInit(&animation, VIEW_LEFT);
            MotionInit(&motion, &player, &animation);
            pending_jump = false;
            pending_dash = false;
            accumulator = 0.0f;
        }
        for (int index = 0; index < 4; ++index) { // 数字一至四选择正、左、背、右朝向
            if (IsKeyPressed(KEY_ONE + index)) {
                AnimationFace(&animation, (ActorView)index);
                if (player.dash_remaining == 0.0f && (index == VIEW_LEFT || index == VIEW_RIGHT)) player.facing = index == VIEW_LEFT ? -1 : 1;
            }
        }
        PlayerInput input = { // 持续输入和独立保存的按下事件
            .move = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
            .crouch = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN),
            .run = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)
        };
        while (accumulator >= FIXED_STEP) {
            input.jump = pending_jump;
            input.dash = pending_dash;
            PlayerUpdate(&player, input, &world, FIXED_STEP);
            AnimationUpdate(&animation, &player, FIXED_STEP);
            MotionUpdate(&motion, &player, &animation, FIXED_STEP);
            pending_jump = false;
            pending_dash = false;
            accumulator -= FIXED_STEP;
        }
        Player visual_player; // 使用物理余量插值世界位置，碰撞和输入仍由固定步决定
        sprites.animated_count = MotionSample(&motion, accumulator / FIXED_STEP, &visual_player, sprites.animated_layers);
        DrawFrame(&sprites, &visual_player, &animation, debug, visual_time, NULL);
    }
    UnloadSprites(&sprites);
    CloseWindow();
    return sprites.failed ? 2 : 0;
}
