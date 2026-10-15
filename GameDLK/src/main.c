#define WIN32_LEAN_AND_MEAN // 仅包含必要的Windows声明
#define NOGDI // 避免Windows绘图接口与raylib函数重名
#define NOUSER // 避免Windows窗口接口与raylib函数重名
#define APP_WIDTH 1280 // 固定逻辑画布宽度
#define APP_HEIGHT 720 // 固定逻辑画布高度
#define FIXED_STEP (1.0f / 120.0f) // 每次角色物理更新的秒数
#define FRAME_SIZE 256 // 角色图集单帧的像素尺寸
#define PATH_CAPACITY 32768 // Windows宽字符路径缓冲区容量
#define ATLAS_COLUMNS 12 // 六十帧图集的列数
#define TEXTURE_CACHE_SIZE 4 // 按需保留四张图集，控制显存占用

#include <windows.h>
#include "raylib.h"
#include "rlgl.h"
#include "player.h"
#include "animation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct Sprites {
    Texture2D textures[TEXTURE_CACHE_SIZE]; // 最近使用的图集纹理，最多约六十MiB显存
    int clips[TEXTURE_CACHE_SIZE]; // 每个缓存槽对应的图集编号
    unsigned long stamps[TEXTURE_CACHE_SIZE]; // 每个槽最近一次访问的序号
    unsigned long clock; // 单调递增的缓存访问计数
    bool failed; // 解码或纹理上传失败后停止游戏并返回错误
} Sprites;

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

/* 打开新图集：name为ASCII资源名，返回文件；宽字符路径保证中文目录可以读取。 */
static FILE *OpenAtlas(const char *name)
{
    wchar_t path[PATH_CAPACITY]; // PNG完整文件路径
    swprintf(path, PATH_CAPACITY, L"%ls\\图片\\游戏素材\\diluc-redrawn-60\\atlases\\%hs.png", root, name);
    return _wfopen(path, L"rb");
}

/* 检查完整资源清单：启动时逐个检查PNG签名和3072乘1280尺寸，缺失时报告具体名称。 */
static bool ValidateAtlases(void)
{
    const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10}; // PNG标准签名
    for (int clip = 0; clip < ANIMATION_CLIP_COUNT; ++clip) { // 待检查的图集编号
        const char *name = AnimationClipName(clip); // 图集文件名
        FILE *file = OpenAtlas(name); // 当前资源文件
        unsigned char header[24] = {0}; // PNG签名、IHDR类型及宽高
        const bool read = file != NULL && fread(header, 1, sizeof(header), file) == sizeof(header); // 是否读到完整头部
        if (file != NULL) fclose(file);
        if (!read || memcmp(header, signature, 8) != 0 || memcmp(header + 12, "IHDR", 4) != 0 ||
            memcmp(header + 16, "\x00\x00\x0c\x00\x00\x00\x05\x00", 8) != 0) {
            fprintf(stderr, "Missing or invalid 3072x1280 atlas: %s.png\n", name);
            return false;
        }
    }
    return true;
}

/* 解码并上传PNG：name为资源名称，返回有效纹理或零值；实际解码时再次验证尺寸。 */
static Texture2D ReadAtlas(const char *name)
{
    FILE *file = OpenAtlas(name); // 通过中文路径打开的PNG文件
    if (file == NULL) return (Texture2D){0};
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return (Texture2D){0}; }
    const long size = ftell(file); // 图集文件字节数
    rewind(file);
    if (size <= 0 || size > 16000000) { fclose(file); return (Texture2D){0}; }
    unsigned char *bytes = malloc((size_t)size); // 图片解码前的压缩字节缓冲区
    if (bytes == NULL) { fclose(file); return (Texture2D){0}; }
    const size_t read_size = fread(bytes, 1, (size_t)size, file); // 实际读取的字节数
    fclose(file);
    if (read_size != (size_t)size) { free(bytes); return (Texture2D){0}; }
    Image image = LoadImageFromMemory(".png", bytes, (int)size); // raylib内存解码后的图片
    free(bytes);
    Texture2D texture = {0}; // 返回给渲染器的图集纹理
    if (image.data != NULL && image.width == 3072 && image.height == 1280) {
        texture = LoadTextureFromImage(image);
        if (texture.id != 0) {
            SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
        }
    }
    if (image.data != NULL) UnloadImage(image);
    return texture;
}

/* 获取图集缓存：sprites为缓存状态，clip为资源编号；满时释放最久未访问的图集。 */
static Texture2D GetAtlas(Sprites *sprites, int clip)
{
    int slot = 0; // 需要复用的缓存槽
    for (int index = 0; index < TEXTURE_CACHE_SIZE; ++index) { // 检查已有纹理及最久未访问槽
        if (sprites->textures[index].id != 0 && sprites->clips[index] == clip) {
            sprites->stamps[index] = ++sprites->clock;
            return sprites->textures[index];
        }
        if (sprites->stamps[index] < sprites->stamps[slot]) slot = index;
    }
    if (sprites->textures[slot].id != 0) UnloadTexture(sprites->textures[slot]);
    const char *name = AnimationClipName(clip); // 待上传的资源名称
    sprites->textures[slot] = ReadAtlas(name);
    sprites->clips[slot] = clip;
    sprites->stamps[slot] = ++sprites->clock;
    if (sprites->textures[slot].id == 0) {
        fprintf(stderr, "Could not decode/upload sprite atlas: %s.png\n", name);
        sprites->failed = true;
    }
    return sprites->textures[slot];
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
    Label("SIDE VIEW  /  FIXED 120 Hz", 1006, 635, 12, muted);
}

/* 绘制角色单层：sprites为缓存，player为物理，animation为选帧，opacity为透明度，offset为横向偏移。 */
static void DrawActor(Sprites *sprites, const Player *player, const ActorAnimation *animation, float opacity, float offset)
{
    const int frame = animation->frame; // 当前六十帧动画的帧序号
    const Texture2D texture = GetAtlas(sprites, animation->clip); // 当前动作图集，按需加载
    if (texture.id == 0) return;
    Rectangle source = {(float)(frame % ATLAS_COLUMNS * FRAME_SIZE), (float)(frame / ATLAS_COLUMNS * FRAME_SIZE), FRAME_SIZE, FRAME_SIZE}; // 十二列五行图集源矩形
    if (animation->mirror) source.width = -source.width;
    const Rectangle destination = {player->x + offset, player->y, FRAME_SIZE, FRAME_SIZE}; // 以脚底锚点定位的目标矩形
    const Vector2 origin = {FRAME_SIZE * 0.5f, FRAME_SIZE * 0.92f}; // 素材清单指定的共同脚底锚点
    DrawTexturePro(texture, source, destination, origin, 0.0f, Fade(WHITE, opacity));
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

/* 绘制操作面板：player提供物理，animation提供图集及帧号，debug为碰撞框开关。 */
static void DrawHud(const Player *player, const ActorAnimation *animation, bool debug)
{
    Label("MX002", 38, 27, 17, mint);
    Label("DILUC / MOVEMENT LAB", 38, 53, 28, paper);
    Label("C + RAYLIB   /   35 ANIMATIONS x 60 FRAMES", 39, 92, 13, muted);
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
    Label(TextFormat("FRAME %02d / 59", animation->frame), 1114, 152, 12, muted);
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

/* 写入单个GPU验证结果：report为文件，count为已有记录数，name为名称，animation为绘制状态，passed为断言结果。 */
static void ReportScene(FILE *report, int *count, const char *name, const ActorAnimation *animation, bool passed)
{
    fprintf(report, "%s{\"name\":\"%s\",\"clip\":\"%s\",\"frame\":%d,\"mirror\":%s,\"passed\":%s}",
        *count > 0 ? "," : "", name, AnimationClipName(animation->clip), animation->frame,
        animation->mirror ? "true" : "false", passed ? "true" : "false");
    ++*count;
    printf("GPU scene %s: %s\n", name, passed ? "PASS" : "FAIL");
}

/* 执行真实GPU验证：sprites为缓存；检查全部图集、三方向动作链及八组转向，并输出截图和报告。 */
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
    fprintf(report, "{\"raylib\":\"%s\",\"atlasCount\":35,\"framesPerAtlas\":60,\"textureCacheLimit\":4,\"scenes\":[", RAYLIB_VERSION);
    for (int clip = 0; clip < ANIMATION_CLIP_COUNT; ++clip) { // 逐个解码上传并实际绘制的图集编号
        Player player; // 本场景独立的角色状态
        ActorAnimation animation; // 直接检查源图集首、中、末帧的绘制状态
        PlayerInit(&player, &world);
        AnimationInit(&animation, VIEW_LEFT);
        animation.clip = clip;
        char name[128]; // 当前图集验证截图名称
        snprintf(name, sizeof(name), "atlas-%s", AnimationClipName(clip));
        bool saved = false; // 中间帧截图是否成功
        ActorAnimation snapshot = animation; // 与中间帧截图保持一致的报告状态
        const int frames[] = {0, 29, 59}; // 需要实际绘制的帧序号
        for (int index = 0; index < 3; ++index) { // 当前首中末帧检查序号
            animation.frame = frames[index];
            const bool captured = DrawFrame(sprites, &player, &animation, true, 0.0f, index == 1 ? name : NULL); // 真实绘制并截取中间帧
            if (index == 1) { saved = captured; snapshot = animation; }
        }
        const bool passed = saved && !sprites->failed; // 实际解码、GPU上传、三帧绘制与截图检查结果
        ReportScene(report, &count, name, &snapshot, passed);
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
            ReportScene(report, &count, name, &animation, passed);
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
        ReportScene(report, &count, name, &snapshot, passed);
        all_passed = all_passed && passed;
    }
    fprintf(report, "],\"checks\":%d,\"passed\":%s}\n", count, all_passed ? "true" : "false");
    if (fclose(report) != 0) all_passed = false;
    return all_passed ? 0 : 3;
}

/* 释放图集：sprites为已经部分或全部加载的资源，各纹理仅释放一次。 */
static void UnloadSprites(Sprites *sprites)
{
    for (int index = 0; index < TEXTURE_CACHE_SIZE; ++index) { // 待释放的缓存槽
        if (sprites->textures[index].id != 0) UnloadTexture(sprites->textures[index]);
        sprites->textures[index] = (Texture2D){0};
    }
}

/* 程序入口：argc为参数数量，argv为参数列表；--verify执行隐藏窗口GPU验证。 */
int main(int argc, char **argv)
{
    const bool verify = argc > 1 && strcmp(argv[1], "--verify") == 0; // 是否执行自动截图验证
    if (!FindRoot()) { fprintf(stderr, "Cannot locate project root.\n"); return 2; }
    if (!ValidateAtlases()) return 2;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | (verify ? FLAG_WINDOW_HIDDEN : FLAG_VSYNC_HINT));
    InitWindow(APP_WIDTH, APP_HEIGHT, "MX002 | C + raylib Movement Lab");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return 2; }
    const char *limit_text = getenv("MX002_FPS_LIMIT"); // 手动性能验证时可选的渲染帧率限制
    SetTargetFPS(verify ? 240 : (limit_text ? atoi(limit_text) : 120));
    Sprites sprites = {0}; // 三十五组图集共享的按需纹理缓存
    GetAtlas(&sprites, AnimationClip(VIEW_LEFT, MOTION_IDLE));
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
    Player player; // 当前可操控角色
    ActorAnimation animation; // 当前角色的六十帧动画与转向状态
    PlayerInit(&player, &world);
    AnimationInit(&animation, VIEW_LEFT);
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
            pending_jump = false;
            pending_dash = false;
            accumulator -= FIXED_STEP;
        }
        DrawFrame(&sprites, &player, &animation, debug, visual_time, NULL);
    }
    UnloadSprites(&sprites);
    CloseWindow();
    return sprites.failed ? 2 : 0;
}
