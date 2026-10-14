#define WIN32_LEAN_AND_MEAN // 仅包含必要的Windows声明
#define NOGDI // 避免Windows绘图接口与raylib函数重名
#define NOUSER // 避免Windows窗口接口与raylib函数重名
#define APP_WIDTH 1280 // 固定逻辑画布宽度
#define APP_HEIGHT 720 // 固定逻辑画布高度
#define FIXED_STEP (1.0f / 120.0f) // 每次角色物理更新的秒数
#define FRAME_SIZE 256 // 角色图集单帧的像素尺寸
#define PATH_CAPACITY 32768 // Windows宽字符路径缓冲区容量

#include <windows.h>
#include "raylib.h"
#include "player.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct Sprites {
    Texture2D walk; // 慢走图集
    Texture2D jump; // 跳跃图集，兼作临时待机和下蹲
    Texture2D dash; // 冲刺图集
} Sprites;

typedef struct VerifyScene {
    const char *name; // 截图与报告使用的场景名称
    PlayerInput input; // 场景中持续保持的输入，触发键仅在第一步生效
    int steps; // 截图前模拟的固定时间步数量
    PlayerAction expected; // 场景期望出现的动作状态
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

/* 加载PNG图集：suffix 为工程内宽字符相对路径，以宽字符文件接口兼容中文目录。 */
static Texture2D ReadAtlas(const wchar_t *suffix)
{
    wchar_t path[PATH_CAPACITY]; // PNG完整文件路径
    swprintf(path, PATH_CAPACITY, L"%ls\\%ls", root, suffix);
    FILE *file = _wfopen(path, L"rb"); // 通过中文路径打开的PNG文件
    if (file == NULL) {
        fprintf(stderr, "Missing sprite atlas. Check the image folder and README.\n");
        return (Texture2D){0};
    }
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
    if (image.data != NULL && image.width == 1024 && image.height == 512) {
        texture = LoadTextureFromImage(image);
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }
    if (image.data != NULL) UnloadImage(image);
    return texture;
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

/* 选择并绘制角色：sprites为图集，player为物理状态，opacity为透明度，offset为残影偏移。 */
static void DrawActor(const Sprites *sprites, const Player *player, float opacity, float offset)
{
    const int frame = PlayerFrame(player); // 当前动作的图集帧序号
    Texture2D texture = sprites->jump; // 默认采用跳跃图集中的待机或空中帧
    if (player->action == ACTION_WALK) texture = sprites->walk;
    if (player->action == ACTION_DASH) texture = sprites->dash;
    Rectangle source = {(float)(frame % 4 * FRAME_SIZE), (float)(frame / 4 * FRAME_SIZE), FRAME_SIZE, FRAME_SIZE}; // 四列两行图集中的源矩形
    if (player->facing > 0) source.width = -source.width;
    const Rectangle destination = {player->x + offset, player->y, FRAME_SIZE, FRAME_SIZE}; // 以脚底锚点定位的目标矩形
    const Vector2 origin = {FRAME_SIZE * 0.5f, FRAME_SIZE * 0.92f}; // 素材清单指定的共同脚底锚点
    DrawTexturePro(texture, source, destination, origin, 0.0f, Fade(WHITE, opacity));
}

/* 绘制角色及调试信息：sprites为资源，player为状态，debug决定是否显示真实碰撞框。 */
static void DrawPlayer(const Sprites *sprites, const Player *player, bool debug)
{
    const float height = world.ground - player->y; // 角色当前离地高度
    const float shadow = fmaxf(0.25f, 1.0f - height / 230.0f); // 随离地高度缩小和变淡的阴影比例
    DrawEllipse((int)player->x, 599, 40.0f * shadow, 5.0f * shadow, (Color){5, 10, 16, 150});
    if (player->action == ACTION_DASH) {
        DrawActor(sprites, player, 0.10f, -player->dash_direction * 60.0f);
        DrawActor(sprites, player, 0.22f, -player->dash_direction * 30.0f);
    }
    DrawActor(sprites, player, 1.0f, 0.0f);
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

/* 绘制动作面板：player提供状态及冷却，debug表示碰撞框开关。 */
static void DrawHud(const Player *player, bool debug)
{
    Label("MX002", 38, 27, 17, mint);
    Label("DILUC / MOVEMENT LAB", 38, 53, 28, paper);
    Label("C + RAYLIB   /   FIRST PLAYABLE", 39, 92, 13, muted);
    DrawRectangleRounded((Rectangle){895, 26, 347, 158}, 0.10f, 5, panel);
    Label("CURRENT ACTION", 916, 45, 12, muted);
    Label(PlayerActionName(player->action), 914, 68, 33, accent);
    Label(player->grounded ? "GROUND" : "AIRBORNE", 1120, 81, 13, mint);
    Label("DASH", 916, 123, 13, muted);
    DrawRectangleRounded((Rectangle){978, 124, 150, 10}, 0.6f, 6, (Color){41, 57, 72, 255});
    const float ready = 1.0f - player->dash_cooldown / PLAYER_DASH_COOLDOWN; // 冲刺冷却进度，满格表示可用
    if (ready > 0) DrawRectangleRounded((Rectangle){978, 124, 150 * ready, 10}, 0.6f, 6, mint);
    Label(player->dash_cooldown > 0 ? TextFormat("%.2fs", player->dash_cooldown) : "READY", 1147, 121, 14, paper);
    Label(player->facing < 0 ? "FACING LEFT" : "FACING RIGHT", 916, 152, 12, muted);
    Label(TextFormat("FRAME %02d", PlayerFrame(player)), 1147, 152, 12, muted);
    KeyCard("A / D", "WALK", 38, 127, 188);
    KeyCard("SPACE", "JUMP", 238, 127, 188);
    KeyCard("S / DOWN", "CROUCH", 438, 127, 198);
    KeyCard("SHIFT", "DASH", 648, 127, 188);
    Label("HOLD TO CROUCH   /   PRESS TO JUMP OR DASH", 39, 182, 12, muted);
    DrawRectangle(38, 661, 1204, 1, (Color){51, 69, 82, 255});
    Label("R  RESET", 38, 680, 13, paper);
    Label(debug ? "F1  COLLIDER ON" : "F1  COLLIDER OFF", 187, 680, 13, debug ? mint : muted);
    Label("ESC  EXIT", 409, 680, 13, muted);
    Label("IDLE + CROUCH: TEMPORARY POSES", 876, 680, 13, muted);
}

/* 绘制一帧：sprites为图集，player为状态，debug为碰撞显示开关，time为特效时间。 */
static void DrawFrame(const Sprites *sprites, const Player *player, bool debug, float time)
{
    BeginDrawing();
    DrawStage(time);
    DrawPlayer(sprites, player, debug);
    DrawHud(player, debug);
    EndDrawing();
}

/* 保存真实窗口截图：name为ASCII场景名称；宽字符输出避免工程路径编码问题。 */
static bool SaveScreenshot(const char *name)
{
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

/* 执行GPU动作验证：sprites为已加载图集，通过相同控制器和渲染路径输出截图、JSON报告。 */
static int Verify(const Sprites *sprites)
{
    wchar_t directory[PATH_CAPACITY]; // 验证产物输出目录
    swprintf(directory, PATH_CAPACITY, L"%ls\\verification", root);
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 3;
    wchar_t report_path[PATH_CAPACITY]; // 验证JSON文件的完整路径
    swprintf(report_path, PATH_CAPACITY, L"%ls\\report.json", directory);
    FILE *report = _wfopen(report_path, L"wb"); // 本次GPU动作验证报告
    if (report == NULL) return 3;
    const VerifyScene scenes[] = { // 不同动作的确定性输入与截图时刻
        {"idle", {0}, 30, ACTION_IDLE},
        {"walk-left", {.move = -1}, 45, ACTION_WALK},
        {"walk-right", {.move = 1}, 45, ACTION_WALK},
        {"jump", {.move = 1, .jump = true}, 30, ACTION_JUMP},
        {"crouch", {.crouch = true}, 20, ACTION_CROUCH},
        {"dash", {.move = 1, .dash = true}, 12, ACTION_DASH},
        {"land", {.jump = true}, 94, ACTION_LAND}
    };
    bool all_passed = true; // 所有动作、截图与报告写入是否成功
    fprintf(report, "{\"raylib\":\"%s\",\"atlasCount\":3,\"scenes\":[", RAYLIB_VERSION);
    for (size_t index = 0; index < sizeof(scenes) / sizeof(scenes[0]); ++index) { // 当前验证场景序号
        Player player; // 本场景独立的角色状态
        PlayerInit(&player, &world);
        PlayerInput input = scenes[index].input; // 首步触发输入及后续持续输入
        for (int step = 0; step < scenes[index].steps; ++step) { // 当前场景的物理更新序号
            PlayerUpdate(&player, input, &world, FIXED_STEP);
            input.jump = false;
            input.dash = false;
            DrawFrame(sprites, &player, true, step * FIXED_STEP);
        }
        const bool saved = SaveScreenshot(scenes[index].name); // 当前场景截图是否写入成功
        const bool passed = player.action == scenes[index].expected && saved; // 状态断言和截图断言结果
        all_passed = all_passed && passed;
        fprintf(report, "%s{\"name\":\"%s\",\"state\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"frame\":%d,\"passed\":%s}",
            index > 0 ? "," : "", scenes[index].name, PlayerActionName(player.action), player.x, player.y,
            PlayerFrame(&player), passed ? "true" : "false");
        printf("GPU scene %s: %s\n", scenes[index].name, passed ? "PASS" : "FAIL");
    }
    fprintf(report, "],\"passed\":%s}\n", all_passed ? "true" : "false");
    if (fclose(report) != 0) all_passed = false;
    return all_passed ? 0 : 3;
}

/* 释放图集：sprites为已经部分或全部加载的资源，各纹理仅释放一次。 */
static void UnloadSprites(Sprites *sprites)
{
    if (sprites->walk.id != 0) UnloadTexture(sprites->walk);
    if (sprites->jump.id != 0) UnloadTexture(sprites->jump);
    if (sprites->dash.id != 0) UnloadTexture(sprites->dash);
}

/* 程序入口：argc为参数数量，argv为参数列表；--verify执行隐藏窗口GPU验证。 */
int main(int argc, char **argv)
{
    const bool verify = argc > 1 && strcmp(argv[1], "--verify") == 0; // 是否执行自动截图验证
    if (!FindRoot()) { fprintf(stderr, "Cannot locate project root.\n"); return 2; }
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | (verify ? FLAG_WINDOW_HIDDEN : FLAG_VSYNC_HINT));
    InitWindow(APP_WIDTH, APP_HEIGHT, "MX002 | C + raylib Movement Lab");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return 2; }
    const char *limit_text = getenv("MX002_FPS_LIMIT"); // 手动性能验证时可选的渲染帧率限制
    SetTargetFPS(verify ? 240 : (limit_text ? atoi(limit_text) : 120));
    Sprites sprites = { // 工程内三张原始侧面图集
        ReadAtlas(L"图片\\游戏素材\\diluc-game-sprites\\diluc\\atlases\\256\\side_slow_walk.png"),
        ReadAtlas(L"图片\\游戏素材\\diluc-game-sprites\\diluc\\atlases\\256\\side_jump.png"),
        ReadAtlas(L"图片\\游戏素材\\diluc-game-sprites\\diluc\\atlases\\256\\side_dash.png")
    };
    if (sprites.walk.id == 0 || sprites.jump.id == 0 || sprites.dash.id == 0) {
        fprintf(stderr, "Could not load all three 1024x512 sprite atlases.\n");
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
    PlayerInit(&player, &world);
    float accumulator = 0.0f; // 尚未用于物理更新的渲染时间累计
    float visual_time = 0.0f; // 背景视觉特效累计时间
    bool pending_jump = false; // 等待下一次物理更新消费的跳跃触发事件
    bool pending_dash = false; // 等待下一次物理更新消费的冲刺触发事件
    bool debug = false; // 是否显示角色碰撞框
    while (!WindowShouldClose()) {
        float elapsed = GetFrameTime(); // 当前渲染帧耗时
        if (elapsed > 0.1f) elapsed = 0.1f;
        visual_time += elapsed;
        accumulator += elapsed;
        pending_jump = pending_jump || IsKeyPressed(KEY_SPACE);
        pending_dash = pending_dash || IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT);
        if (IsKeyPressed(KEY_F1)) debug = !debug;
        if (IsKeyPressed(KEY_R)) {
            PlayerInit(&player, &world);
            pending_jump = false;
            pending_dash = false;
            accumulator = 0.0f;
        }
        PlayerInput input = { // 持续输入和独立保存的按下事件
            .move = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)),
            .crouch = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)
        };
        while (accumulator >= FIXED_STEP) {
            input.jump = pending_jump;
            input.dash = pending_dash;
            PlayerUpdate(&player, input, &world, FIXED_STEP);
            pending_jump = false;
            pending_dash = false;
            accumulator -= FIXED_STEP;
        }
        DrawFrame(&sprites, &player, debug, visual_time);
    }
    UnloadSprites(&sprites);
    CloseWindow();
    return 0;
}
