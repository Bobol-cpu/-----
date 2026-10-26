/* ============================================================================
 *  贪吃蛇 · raylib 图形版 v2 (Snake + raylib)
 *
 *  v3 新增地图系统（地图 = 布局 + 机制，与难度解耦）：
 *    - 草原：空场无遮挡，可开穿墙，纯拼速度
 *    - 迷宫：规则柱阵，每吃 4 个食物随机震塌一片墙，地图越玩越开
 *    - 虫洞：两对传送门，头部钻进去立刻从配对门出来
 *    - 岩浆：每 5 秒随机两行喷发（预警 1.6s → 爆发 2.6s），踩中即死
 *
 *  主动技能（全部用来救场、降低难度）：
 *    - J 护盾：8 秒内替你挡下一次致命碰撞，原地停住并给你转向的机会
 *    - K 慢放：4 秒内每格耗时放大 1.85 倍，反应时间更充裕
 *    - L 穿身：5 秒内可以穿过自己的身体（墙和岩浆仍然致命）
 *    三个技能各自独立冷却，HUD 右上角有冷却条与生效呼吸描边
 *    技能生效期间，整条蛇会切换成"吉祥物"外形：大圆头 + 黑豆眼 + 微笑 + 暖黄身体
 *
 *  v2 相比 v1 新增：
 *    - 开始菜单：三档难度（经典 / 疾速 / 地狱）+ 穿墙开关
 *    - 地狱难度随机生成障碍砖块，撞上即死
 *    - 金苹果：每吃 5 个普通食物出现一次，限时 7 秒，+50 分
 *    - 连击系统：3 秒内连续吃到食物，得分倍率最高 3 倍
 *    - 等级：每吃 5 个食物升一级，速度递增
 *    - 冲击波光环、蛇头辉光、障碍砖块、金苹果星芒
 *    - 结算面板显示 长度 / 最大连击 / 用时
 *    - 更多程序化合成音效（吃、金苹果、菜单、死亡）
 *
 *  操作：
 *    菜单：上下选择   左右调整   回车开始
 *    游戏：方向键 或 WASD 移动，J 护盾 / K 慢放 / L 穿身，P 或空格 暂停，Q 退出
 *    暂停：P 或空格 继续，M 返回菜单
 *    结束：R 重新开始，M 返回菜单，Q 退出
 *
 *  编译（Windows + MinGW，raylib 6.2 官方预编译包）：
 *    gcc snake_raylib.c -o snake_raylib.exe -O2 -std=c99 ^
 *        -I"D:/raylib-6.2_win64_mingw-w64/include" ^
 *        -L"D:/raylib-6.2_win64_mingw-w64/lib" ^
 *        -lraylibdll -lopengl32 -lgdi32 -lwinmm
 *
 *  开发自检：
 *    SNAKE_SHOT=帧号   跑到该帧截图到 snake_shot.png 后退出
 *    SNAKE_AUTO=难度   跳过菜单直接开局（0 经典 / 1 疾速 / 2 地狱）
 * ==========================================================================*/

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

/* ------------------------------ 可调参数 ------------------------------ */
#define CELL      34                     /* 每格像素 */
#define BOARD_W   26                     /* 棋盘宽（格） */
#define BOARD_H   17                     /* 棋盘高（格） */
#define MAX_LEN   (BOARD_W * BOARD_H)    /* 蛇的最大长度 */
#define MAX_WALLS 200                    /* 障碍块上限 */

#define WINDOW_W  980
#define WINDOW_H  720
#define HUD_H     76

#define BEST_FILE "snake_best.txt"       /* 最高分存档 */

#define GOLDEN_EVERY  5                  /* 每吃几个普通食物出一个金苹果 */
#define GOLDEN_LIFE   7.0f               /* 金苹果存活时间（秒） */
#define GOLDEN_POINTS 50                 /* 金苹果分值 */

#define COMBO_WINDOW  3.0f               /* 连击有效窗口（秒） */
#define LEVEL_STEP    5                  /* 每吃几个升一级 */

#define CRUMBLE_EVERY 4                  /* 迷宫：每吃几个食物震塌一片墙 */
#define LAVA_IDLE     5.0f               /* 岩浆：平静时长（秒） */
#define LAVA_WARN     1.6f               /* 岩浆：预警时长（秒） */
#define LAVA_ACTIVE   2.6f               /* 岩浆：喷发时长（秒） */

/* 三个主动技能：全部用来救场，降低难度 */
#define SHIELD_TIME   8.0f               /* 护盾：可吸收一次致命碰撞的窗口 */
#define SHIELD_CD    14.0f
#define SLOW_TIME     4.0f               /* 慢放：持续时长 */
#define SLOW_CD      10.0f
#define SLOW_FACTOR   1.85f              /* 慢放时每格耗时放大倍数（越大越慢） */
#define GHOST_TIME    5.0f               /* 穿身：可穿过自己身体的窗口 */
#define GHOST_CD     14.0f

/* ------------------------------ 方向定义 ------------------------------ */
enum { DIR_UP = 0, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

static const int DX[4] = {  0,  1,  0, -1 };
static const int DY[4] = { -1,  0,  1,  0 };

/* ------------------------------ 配色 ------------------------------ */
static const Color C_BG_TOP    = {  20,  23,  34, 255 };
static const Color C_BG_BOTTOM = {  10,  11,  17, 255 };
static const Color C_PANEL     = {  26,  29,  40, 255 };
static const Color C_PANEL_ED  = {  52,  58,  78, 255 };
static const Color C_TEXT      = { 235, 240, 250, 255 };
static const Color C_TEXT_DIM  = { 140, 150, 172, 255 };
static const Color C_HEAD      = { 128, 242, 168, 255 };
static const Color C_BODY      = {  56, 186, 166, 255 };
static const Color C_TAIL      = {  28, 104, 122, 255 };
static const Color C_FOOD      = { 255,  96,  96, 255 };
static const Color C_GOLD      = { 255, 202,  88, 255 };
static const Color C_ACCENT    = { 120, 240, 160, 255 };
static const Color C_WALL      = {  74,  81, 106, 255 };
static const Color C_WALL_TOP  = { 112, 122, 152, 255 };
static const Color C_PORTAL_A  = { 120, 200, 255, 255 };
static const Color C_PORTAL_B  = { 214, 138, 255, 255 };
static const Color C_LAVA      = { 255, 132,  62, 255 };
static const Color C_MASCOT      = { 247, 201, 105, 255 };   /* 技能形态：暖黄 */
static const Color C_MASCOT_DARK = { 219, 162,  70, 255 };

/* ------------------------------ 数据结构 ------------------------------ */
typedef struct { int x, y; } Cell;

typedef struct {
    Cell body[MAX_LEN];                  /* body[0] 是蛇头 */
    Cell prev[MAX_LEN];                  /* 上一格位置，用于插值动画 */
    int  len;
    int  dir;
} Snake;

#define MAX_PARTS 320
typedef struct {
    Vector2 pos, vel;
    float   life, max_life, size;
    Color   color;
} Particle;

typedef struct {
    Vector2 pos;
    float   life;
    char    text[16];
} Popup;

#define MAX_RINGS 10
typedef struct {
    Vector2 pos;
    float   r, max_r, life;
    Color   color;
} Ring;

typedef struct {
    Snake  snake;
    Cell   food;

    bool   golden_on;
    Cell   golden;
    float  golden_life;

    Cell   walls[MAX_WALLS];
    int    wall_n;

    int    map;                          /* 当前地图 */
    Cell   portals[4];                   /* 虫洞：0<->1 一对，2<->3 一对 */
    bool   has_portals;
    bool   lava_rows[BOARD_H];           /* 岩浆：正在喷发的行 */
    int    lava_phase;                   /* 0 平静 / 1 预警 / 2 喷发 */
    float  lava_timer;

    int    score, best;
    int    eaten;                        /* 吃掉的普通食物数 */
    int    combo, best_combo;
    float  combo_timer;
    int    level;

    bool   paused, dead, new_record, win;
    bool   wrap;                         /* 穿墙模式 */
    int    diff;                         /* 难度索引 */

    float  step, step_min;               /* 秒 / 格 */
    float  move_timer;
    float  t;                            /* 插值进度 0..1 */
    int    turn_q[2];                    /* 转向队列，最多缓存两次转向 */
    int    turn_n;

    float  shake, pop;
    float  play_time;

    float  sk_shield_cd, sk_shield_on;   /* 护盾：冷却 / 生效剩余 */
    float  sk_slow_cd,   sk_slow_on;     /* 慢放 */
    float  sk_ghost_cd,  sk_ghost_on;    /* 穿身 */

    Particle parts[MAX_PARTS];
    Popup    popups[12];
    Ring     rings[MAX_RINGS];
} Game;

typedef struct {
    int  sel;                            /* 0 地图 / 1 难度 / 2 穿墙 / 3 开始 */
    int  diff;
    int  map;
    bool wrap;
} Menu;

typedef enum { SC_MENU = 0, SC_PLAY, SC_DEAD } Scene;

typedef struct {
    const char *zh;
    const char *en;
    float step;                          /* 初始秒/格 */
    float step_min;                      /* 最快秒/格 */
    float tick;                          /* 每吃一个提速多少秒 */
} Difficulty;

static const Difficulty DIFFS[3] = {
    { "经典", "CLASSIC", 0.150f, 0.070f, 0.0040f },
    { "疾速", "TURBO",   0.100f, 0.046f, 0.0030f },
    { "地狱", "HELL",    0.115f, 0.052f, 0.0045f }
};

/* ------------------------------ 地图 ------------------------------ */
typedef enum { MAP_MEADOW = 0, MAP_MAZE, MAP_PORTAL, MAP_LAVA, MAP_COUNT } MapId;

typedef struct {
    const char *zh;                      /* 名字 */
    const char *en;
    const char *desc_zh;                 /* 一句话机制说明 */
    const char *desc_en;
    bool        allow_wrap;              /* 是否允许开穿墙 */
} MapDef;

static const MapDef MAPS[MAP_COUNT] = {
    { "草原", "MEADOW", "空场无障碍 纯拼速度",   "open field, pure speed", true },
    { "迷宫", "MAZE",   "柱阵穿行 吃食震塌墙",   "pillar maze, walls crumble", false },
    { "虫洞", "PORTAL", "两对传送门 进出瞬移",   "two portal pairs", false },
    { "岩浆", "LAVA",   "岩浆周期喷发 踩中即死", "erupting lava rows", true }
};

/* ------------------------------ 全局状态 ------------------------------ */
static Font  g_font;
static bool  g_cjk = false;              /* 中文字体是否可用 */
static bool  g_audio = false;
static float g_time = 0.0f;              /* 全局动画时间 */

static Sound g_snd_eat, g_snd_dead, g_snd_gold, g_snd_ui, g_snd_level;
static Sound g_snd_portal, g_snd_lava;

/* 界面里出现的全部中文，用于生成字体子集（重复字符会在加载时自动去重） */
static const char *UI_TEXT =
    "贪吃蛇难度经典疾速地狱穿墙开关开始游戏上下选择左右调整回车"
    "得分最高长度等级速度新纪录连击"
    "已暂停按或空格继续返回菜单"
    "游戏结束本次历史最大用时长秒来通关退出重"
    "草原迷宫虫洞岩浆地图空场无障碍纯拼柱阵穿行吃食震塌墙两对传送门进瞬移周期喷发踩即中死"
    "护盾慢放身";

static const char *T(const char *zh, const char *en)
{
    return g_cjk ? zh : en;
}

/* ============================== 小工具 ============================== */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 格子中心坐标 */
static Vector2 cell_center(Cell c)
{
    const float bw = (float)(BOARD_W * CELL);
    float bx = (WINDOW_W - bw) * 0.5f;
    float by = (float)HUD_H + 18.0f;
    return (Vector2){ bx + (c.x + 0.5f) * CELL, by + (c.y + 0.5f) * CELL };
}

static Rectangle board_rect(void)
{
    float bw = (float)(BOARD_W * CELL);
    float bh = (float)(BOARD_H * CELL);
    return (Rectangle){ (WINDOW_W - bw) * 0.5f, (float)HUD_H + 18.0f, bw, bh };
}

static float text_w(const char *text, float size, float spacing)
{
    return MeasureTextEx(g_font, text, size, spacing).x;
}

/* 居中绘制一行文字 */
static void draw_text_center(const char *text, float y, float size, float spacing, Color color)
{
    Vector2 m = MeasureTextEx(g_font, text, size, spacing);
    DrawTextEx(g_font, text, (Vector2){ (WINDOW_W - m.x) * 0.5f, y }, size, spacing, color);
}

/* ============================ 字体 ============================ */

/* 加载中文字体；只烘焙界面用到的字形，避免加载整个 CJK 字库 */
static Font load_ui_font(void)
{
    static const char *candidates[] = {
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/Deng.ttf"
    };
    const char *path = NULL;
    int i, cjk_count = 0, n = 0;

    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (FileExists(candidates[i])) {
            path = candidates[i];
            break;
        }
    }
    if (path == NULL) return GetFontDefault();

    {
        int *cjk = LoadCodepoints(UI_TEXT, &cjk_count);
        int  total = 95 + (cjk ? cjk_count : 0);
        int *cps = (int *)MemAlloc((unsigned int)(total * (int)sizeof(int)));
        Font f;

        if (cps == NULL) {
            if (cjk) UnloadCodepoints(cjk);
            return GetFontDefault();
        }

        for (i = 32; i <= 126; ++i) cps[n++] = i;          /* 可打印 ASCII */

        /* 汉字逐个去重，避免重复烘焙同一个字形 */
        for (i = 0; i < cjk_count; ++i) {
            int j, dup = 0;
            for (j = 0; j < n; ++j) {
                if (cps[j] == cjk[i]) { dup = 1; break; }
            }
            if (!dup) cps[n++] = cjk[i];
        }

        f = LoadFontEx(path, 44, cps, n);
        MemFree(cps);
        if (cjk) UnloadCodepoints(cjk);

        if (f.texture.id == 0 || f.glyphCount == 0) return GetFontDefault();
        g_cjk = true;
        return f;
    }
}

/* ============================ 音效 ============================ */

/* 在内存里合成一段 PCM 波形并载入为 Sound，省去外部音频素材 */
static Sound make_tone(float f0, float f1, float seconds, float volume, float harmonics)
{
    const int rate = 22050;
    int frames = (int)(rate * seconds);
    int data_bytes, total, i;
    unsigned char *buf;
    short *pcm;
    Wave wave;
    Sound snd;
    double phase = 0.0;
    unsigned int u32;
    unsigned short u16;

    if (frames < 1) frames = 1;
    data_bytes = frames * 2;
    total = 44 + data_bytes;
    buf = (unsigned char *)MemAlloc((unsigned int)total);
    if (buf == NULL) {
        Sound empty = { 0 };
        return empty;
    }

    /* ---- 44 字节 WAV 头（单声道 16bit PCM） ---- */
    memcpy(buf, "RIFF", 4);
    u32 = (unsigned int)(36 + data_bytes); memcpy(buf + 4,  &u32, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    u32 = 16;                   memcpy(buf + 16, &u32, 4);
    u16 = 1;                    memcpy(buf + 20, &u16, 2);   /* PCM */
    u16 = 1;                    memcpy(buf + 22, &u16, 2);   /* 单声道 */
    u32 = (unsigned int)rate;   memcpy(buf + 24, &u32, 4);
    u32 = (unsigned int)(rate * 2); memcpy(buf + 28, &u32, 4);
    u16 = 2;                    memcpy(buf + 32, &u16, 2);   /* block align */
    u16 = 16;                   memcpy(buf + 34, &u16, 2);   /* bits per sample */
    memcpy(buf + 36, "data", 4);
    u32 = (unsigned int)data_bytes; memcpy(buf + 40, &u32, 4);

    /* ---- 波形：频率滑音 + 平方衰减包络 + 三次谐波 ---- */
    pcm = (short *)(buf + 44);
    for (i = 0; i < frames; ++i) {
        float k = (float)i / (float)frames;
        float f = f0 + (f1 - f0) * k;
        float env, attack, s;

        phase += 2.0 * (double)PI * (double)f / (double)rate;
        env = (1.0f - k) * (1.0f - k);
        attack = k < 0.02f ? k / 0.02f : 1.0f;
        s = (float)sin(phase) * (1.0f - harmonics) + (float)sin(phase * 3.0) * harmonics;
        s = s * env * attack * volume;
        pcm[i] = (short)(clampf(s, -1.0f, 1.0f) * 32767.0f);
    }

    wave = LoadWaveFromMemory(".wav", buf, total);
    snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    MemFree(buf);
    return snd;
}

static void init_audio(void)
{
    InitAudioDevice();
    g_audio = IsAudioDeviceReady();
    if (!g_audio) return;

    g_snd_eat   = make_tone(660.0f, 1180.0f, 0.09f, 0.35f, 0.20f);
    g_snd_dead  = make_tone(420.0f,  90.0f, 0.45f, 0.40f, 0.20f);
    g_snd_gold  = make_tone(880.0f, 1760.0f, 0.22f, 0.32f, 0.35f);
    g_snd_ui    = make_tone(520.0f,  640.0f, 0.05f, 0.22f, 0.10f);
    g_snd_level = make_tone(520.0f,  980.0f, 0.30f, 0.26f, 0.30f);
    g_snd_portal = make_tone(300.0f, 960.0f, 0.14f, 0.26f, 0.45f);
    g_snd_lava   = make_tone( 95.0f,  52.0f, 0.55f, 0.40f, 0.45f);
}

static void free_audio(void)
{
    if (!g_audio) return;
    if (g_snd_eat.frameCount   > 0) UnloadSound(g_snd_eat);
    if (g_snd_dead.frameCount  > 0) UnloadSound(g_snd_dead);
    if (g_snd_gold.frameCount  > 0) UnloadSound(g_snd_gold);
    if (g_snd_ui.frameCount    > 0) UnloadSound(g_snd_ui);
    if (g_snd_level.frameCount > 0) UnloadSound(g_snd_level);
    if (g_snd_portal.frameCount > 0) UnloadSound(g_snd_portal);
    if (g_snd_lava.frameCount > 0) UnloadSound(g_snd_lava);
    CloseAudioDevice();
}

static void play_sound(Sound s)
{
    if (g_audio && s.frameCount > 0) PlaySound(s);
}

/* ============================== 存档 ============================== */

static int load_best(void)
{
    char *txt;
    int v = 0;

    if (!FileExists(BEST_FILE)) return 0;
    txt = LoadFileText(BEST_FILE);
    if (txt != NULL) {
        v = atoi(txt);
        UnloadFileText(txt);
    }
    return v < 0 ? 0 : v;
}

static void save_best(int v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    SaveFileText(BEST_FILE, buf);
}

/* ============================== 棋盘查询 ============================== */

static bool cell_has_wall(const Game *g, Cell c)
{
    int i;
    for (i = 0; i < g->wall_n; ++i) {
        if (g->walls[i].x == c.x && g->walls[i].y == c.y) return true;
    }
    return false;
}

static bool cell_in_snake(const Game *g, Cell c, int limit)
{
    int i;
    for (i = 0; i < limit && i < g->snake.len; ++i) {
        if (g->snake.body[i].x == c.x && g->snake.body[i].y == c.y) return true;
    }
    return false;
}

/* 随机取一个空格（排除蛇、障碍、食物、金苹果） */
static bool random_free_cell(const Game *g, Cell *out)
{
    static Cell cells[MAX_LEN];
    int n = 0, x, y;

    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            Cell c;
            c.x = x;
            c.y = y;
            if (cell_has_wall(g, c)) continue;
            if (cell_in_snake(g, c, g->snake.len)) continue;
            if (c.x == g->food.x && c.y == g->food.y) continue;
            if (g->golden_on && c.x == g->golden.x && c.y == g->golden.y) continue;
            if (g->has_portals) {
                int k, on_portal = 0;
                for (k = 0; k < 4; ++k) {
                    if (c.x == g->portals[k].x && c.y == g->portals[k].y) { on_portal = 1; break; }
                }
                if (on_portal) continue;
            }
            cells[n++] = c;
        }
    }
    if (n == 0) return false;
    *out = cells[GetRandomValue(0, n - 1)];
    return true;
}

/* ------------------------------ 地图构建 ------------------------------ */

static void add_wall(Game *g, int x, int y)
{
    int i;
    if (g->wall_n >= MAX_WALLS) return;
    for (i = 0; i < g->wall_n; ++i) {
        if (g->walls[i].x == x && g->walls[i].y == y) return;   /* 去重 */
    }
    g->walls[g->wall_n].x = x;
    g->walls[g->wall_n].y = y;
    ++g->wall_n;
}

/* 迷宫：4 格一组的方柱阵，出生口袋与右侧出发走廊留空 */
static void build_maze(Game *g)
{
    const int cx = BOARD_W / 2, cy = BOARD_H / 2;
    int x, y, dx, dy;

    for (y = 1; y + 1 < BOARD_H; y += 4) {
        for (x = 1; x + 1 < BOARD_W; x += 4) {
            if (abs(x - cx) <= 3 && abs(y - cy) <= 2) continue;
            if (abs(x + 1 - cx) <= 3 && abs(y + 1 - cy) <= 2) continue;
            for (dy = 0; dy < 2; ++dy) {
                for (dx = 0; dx < 2; ++dx) add_wall(g, x + dx, y + dy);
            }
        }
    }
}

/* 虫洞：两对传送门，0<->1 一对，2<->3 一对 */
static void build_portals(Game *g)
{
    g->portals[0].x = 3;  g->portals[0].y = 3;
    g->portals[1].x = 22; g->portals[1].y = 13;
    g->portals[2].x = 22; g->portals[2].y = 3;
    g->portals[3].x = 3;  g->portals[3].y = 13;
    g->has_portals = true;
}

static void lava_clear(Game *g)
{
    int i;
    for (i = 0; i < BOARD_H; ++i) g->lava_rows[i] = false;
}

/* ============================== 游戏逻辑 ============================== */

static int opposite(int dir)
{
    return (dir + 2) % 4;
}

static void update_best(Game *g)
{
    if (g->score > g->best) {
        g->best = g->score;
        g->new_record = true;
        save_best(g->best);
    }
}

static void place_food(Game *g)
{
    Cell c;
    if (random_free_cell(g, &c)) g->food = c;
}

static void spawn_golden(Game *g)
{
    Cell c;
    if (random_free_cell(g, &c)) {
        g->golden = c;
        g->golden_on = true;
        g->golden_life = GOLDEN_LIFE;
    }
}

static void spawn_burst(Game *g, Vector2 p, Color c, int count)
{
    int spawned = 0, i;

    for (i = 0; i < MAX_PARTS && spawned < count; ++i) {
        Particle *pt = &g->parts[i];
        float ang, spd;

        if (pt->life > 0.0f) continue;
        ang = (float)GetRandomValue(0, 359) * (PI / 180.0f);
        spd = (float)GetRandomValue(45, 175);
        pt->pos = p;
        pt->vel = (Vector2){ cosf(ang) * spd, sinf(ang) * spd };
        pt->max_life = 0.35f + (float)GetRandomValue(0, 55) / 100.0f;
        pt->life = pt->max_life;
        pt->size = (float)GetRandomValue(2, 5);
        pt->color = c;
        ++spawned;
    }
}

static void spawn_ring(Game *g, Vector2 p, float max_r, Color c)
{
    int i;
    for (i = 0; i < MAX_RINGS; ++i) {
        if (g->rings[i].life <= 0.0f) {
            g->rings[i].pos = p;
            g->rings[i].r = 6.0f;
            g->rings[i].max_r = max_r;
            g->rings[i].life = 0.45f;
            g->rings[i].color = c;
            return;
        }
    }
}

static void add_popup_text(Game *g, Vector2 p, const char *text)
{
    int i;
    for (i = 0; i < 12; ++i) {
        if (g->popups[i].life <= 0.0f) {
            g->popups[i].pos = p;
            g->popups[i].life = 0.9f;
            snprintf(g->popups[i].text, sizeof(g->popups[i].text), "%s", text);
            return;
        }
    }
}

static void add_popup_score(Game *g, Vector2 p, int points)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "+%d", points);
    add_popup_text(g, p, buf);
}

/* 迷宫：震塌一片墙（3x3 邻域），地图越玩越开阔 */
static void crumble_wall(Game *g)
{
    Cell c;
    int idx, j;

    if (g->wall_n <= 4) return;
    idx = GetRandomValue(0, g->wall_n - 1);
    c = g->walls[idx];

    for (j = g->wall_n - 1; j >= 0; --j) {
        if (abs(g->walls[j].x - c.x) <= 1 && abs(g->walls[j].y - c.y) <= 1) {
            spawn_burst(g, cell_center(g->walls[j]), (Color){ 150, 160, 190, 255 }, 3);
            g->walls[j] = g->walls[g->wall_n - 1];
            --g->wall_n;
        }
    }
    spawn_ring(g, cell_center(c), 58.0f, (Color){ 150, 160, 190, 255 });
}

static void reset_game(Game *g, int diff, bool wrap, int map)
{
    int cx = BOARD_W / 2, cy = BOARD_H / 2, i;
    int best = g->best;

    if (diff < 0 || diff > 2) diff = 0;
    if (map < 0 || map >= MAP_COUNT) map = 0;
    if (!MAPS[map].allow_wrap) wrap = false;

    g->snake.len = 3;
    g->snake.dir = DIR_RIGHT;
    for (i = 0; i < 3; ++i) {
        g->snake.body[i].x = cx - i;
        g->snake.body[i].y = cy;
        g->snake.prev[i] = g->snake.body[i];
    }

    g->diff = diff;
    g->wrap = wrap;
    g->step = DIFFS[diff].step;
    g->step_min = DIFFS[diff].step_min;

    g->score = 0;
    g->best = best;
    g->eaten = 0;
    g->combo = 0;
    g->best_combo = 0;
    g->combo_timer = 0.0f;
    g->level = 1;

    g->golden_on = false;
    g->golden_life = 0.0f;
    g->golden.x = -1;
    g->golden.y = -1;

    g->paused = false;
    g->dead = false;
    g->new_record = false;
    g->win = false;

    g->move_timer = 0.0f;
    g->t = 0.0f;
    g->turn_n = 0;
    g->shake = 0.0f;
    g->pop = 0.0f;
    g->play_time = 0.0f;

    g->sk_shield_cd = 0.0f;
    g->sk_shield_on = 0.0f;
    g->sk_slow_cd = 0.0f;
    g->sk_slow_on = 0.0f;
    g->sk_ghost_cd = 0.0f;
    g->sk_ghost_on = 0.0f;

    g->food.x = -1;
    g->food.y = -1;

    g->map = map;
    g->has_portals = false;
    g->lava_phase = 0;
    g->lava_timer = LAVA_IDLE;
    lava_clear(g);

    g->wall_n = 0;
    if (map == MAP_MAZE) build_maze(g);
    else if (map == MAP_PORTAL) build_portals(g);
    place_food(g);

    for (i = 0; i < MAX_PARTS; ++i) g->parts[i].life = 0.0f;
    for (i = 0; i < 12; ++i) g->popups[i].life = 0.0f;
    for (i = 0; i < MAX_RINGS; ++i) g->rings[i].life = 0.0f;
}

/* 缓存转向：允许连按两次，但绝不接受 180° 掉头 */
static void queue_turn(Game *g, int d)
{
    int base = (g->turn_n > 0) ? g->turn_q[g->turn_n - 1] : g->snake.dir;

    if (d == base || d == opposite(base)) return;
    if (g->turn_n < 2) g->turn_q[g->turn_n++] = d;
}

static void grow_tail(Game *g, Cell old_tail)
{
    if (g->snake.len < MAX_LEN) {
        g->snake.body[g->snake.len] = old_tail;
        g->snake.prev[g->snake.len] = old_tail;
        ++g->snake.len;
    }
}

static void die(Game *g)
{
    g->dead = true;
    g->shake = 0.5f;
    spawn_burst(g, cell_center(g->snake.body[0]), (Color){ 255, 90, 90, 255 }, 30);
    spawn_ring(g, cell_center(g->snake.body[0]), 90.0f, (Color){ 255, 110, 110, 255 });
    update_best(g);
    play_sound(g_snd_dead);
}

static void step_game(Game *g)
{
    Cell head = g->snake.body[0];
    Cell old_tail = g->snake.body[g->snake.len - 1];
    Cell nh;
    bool eat = false, eat_gold = false, hit = false;
    int i, limit;

    if (g->turn_n > 0) {                       /* 取出队列里缓存的转向 */
        g->snake.dir = g->turn_q[0];
        g->turn_q[0] = g->turn_q[1];
        --g->turn_n;
    }

    nh.x = head.x + DX[g->snake.dir];
    nh.y = head.y + DY[g->snake.dir];

    if (g->wrap) {                             /* 穿墙 */
        if (nh.x < 0) nh.x = BOARD_W - 1;
        else if (nh.x >= BOARD_W) nh.x = 0;
        if (nh.y < 0) nh.y = BOARD_H - 1;
        else if (nh.y >= BOARD_H) nh.y = 0;
    } else if (nh.x < 0 || nh.x >= BOARD_W || nh.y < 0 || nh.y >= BOARD_H) {
        hit = true;                            /* 撞墙（边界） */
    }

    if (!hit && g->has_portals) {
        int k;
        for (k = 0; k < 4; ++k) {
            if (nh.x == g->portals[k].x && nh.y == g->portals[k].y) {
                Vector2 from = cell_center(nh);
                Color pc = (k < 2) ? C_PORTAL_A : C_PORTAL_B;
                nh = g->portals[k ^ 1];        /* 从配对门出来 */
                spawn_ring(g, from, 46.0f, pc);
                spawn_ring(g, cell_center(nh), 46.0f, pc);
                play_sound(g_snd_portal);
                break;
            }
        }
    }

    if (!hit && cell_has_wall(g, nh)) hit = true;   /* 撞障碍块 */

    /* 岩浆：喷发期间踩进去即死 */
    if (!hit && g->map == MAP_LAVA && g->lava_phase == 2 && g->lava_rows[nh.y]) hit = true;

    if (!hit) {
        eat = (nh.x == g->food.x && nh.y == g->food.y);
        eat_gold = (g->golden_on && nh.x == g->golden.x && nh.y == g->golden.y);
        limit = (eat || eat_gold) ? g->snake.len : g->snake.len - 1;
        /* 穿身生效期间忽略自身碰撞 */
        if (g->sk_ghost_on <= 0.0f) {
            for (i = 1; i < limit; ++i) {
                if (g->snake.body[i].x == nh.x && g->snake.body[i].y == nh.y) {
                    hit = true;                    /* 咬到自己 */
                    break;
                }
            }
        }
    }

    if (hit) {
        /* 护盾：吃掉这一次致命碰撞，原地不动，给玩家一个转向的机会 */
        if (g->sk_shield_on > 0.0f) {
            g->sk_shield_on = 0.0f;
            g->move_timer = 0.0f;
            g->shake = 0.35f;
            g->pop = 1.0f;
            spawn_ring(g, cell_center(head), 48.0f, (Color){ 110, 190, 255, 255 });
            spawn_burst(g, cell_center(head), (Color){ 110, 190, 255, 255 }, 14);
            add_popup_text(g, cell_center(head), T("护盾", "BLOCK"));
            play_sound(g_snd_portal);
            return;
        }
        die(g);
        return;
    }

    /* 记录旧位置用于插值，然后整体前移 */
    memcpy(g->snake.prev, g->snake.body, sizeof(Cell) * (size_t)g->snake.len);
    for (i = g->snake.len - 1; i > 0; --i) g->snake.body[i] = g->snake.body[i - 1];
    g->snake.body[0] = nh;

    if (eat) {
        int pts, old_level = g->level;
        float mult;

        grow_tail(g, old_tail);
        ++g->eaten;

        /* 连击 */
        g->combo = (g->combo_timer > 0.0f) ? g->combo + 1 : 1;
        if (g->combo > g->best_combo) g->best_combo = g->combo;
        g->combo_timer = COMBO_WINDOW;

        mult = 1.0f + 0.2f * (float)(g->combo - 1);
        if (mult > 3.0f) mult = 3.0f;
        pts = (int)(10.0f * mult + 0.5f);
        g->score += pts;

        g->level = 1 + g->eaten / LEVEL_STEP;
        g->step = clampf(g->step - DIFFS[g->diff].tick, g->step_min, DIFFS[g->diff].step);

        g->pop = 1.0f;
        spawn_burst(g, cell_center(g->food), C_FOOD, 18);
        spawn_ring(g, cell_center(g->food), 46.0f, C_FOOD);
        add_popup_score(g, cell_center(nh), pts);
        play_sound(g_snd_eat);

        if (g->level > old_level) play_sound(g_snd_level);
        if (g->eaten % GOLDEN_EVERY == 0) spawn_golden(g);
        if (g->map == MAP_MAZE && (g->eaten % CRUMBLE_EVERY) == 0) crumble_wall(g);

        update_best(g);
        if (!random_free_cell(g, &g->food)) {     /* 棋盘被填满 */
            g->win = true;
            g->dead = true;
        }
    } else if (eat_gold) {
        grow_tail(g, old_tail);
        g->score += GOLDEN_POINTS;
        g->golden_on = false;
        g->golden_life = 0.0f;

        g->pop = 1.0f;
        spawn_burst(g, cell_center(g->golden), C_GOLD, 30);
        spawn_ring(g, cell_center(g->golden), 80.0f, C_GOLD);
        add_popup_score(g, cell_center(nh), GOLDEN_POINTS);
        play_sound(g_snd_gold);
        update_best(g);
    }
}

static void update_effects(Game *g, float dt)
{
    int i;

    for (i = 0; i < MAX_PARTS; ++i) {
        Particle *pt = &g->parts[i];
        if (pt->life <= 0.0f) continue;
        pt->life -= dt;
        pt->pos.x += pt->vel.x * dt;
        pt->pos.y += pt->vel.y * dt;
        pt->vel.y += 420.0f * dt;              /* 一点重力 */
        pt->vel.x *= 0.96f;
        pt->vel.y *= 0.98f;
    }

    for (i = 0; i < 12; ++i) {
        if (g->popups[i].life > 0.0f) {
            g->popups[i].life -= dt;
            g->popups[i].pos.y -= 34.0f * dt;
        }
    }

    for (i = 0; i < MAX_RINGS; ++i) {
        Ring *r = &g->rings[i];
        if (r->life <= 0.0f) continue;
        r->life -= dt;
        r->r += (r->max_r - r->r) * clampf(dt * 7.0f, 0.0f, 1.0f);
    }

    if (g->shake > 0.0f) g->shake = clampf(g->shake - dt * 2.2f, 0.0f, 1.0f);
    if (g->pop > 0.0f)   g->pop = clampf(g->pop - dt * 4.5f, 0.0f, 1.0f);

    if (g->dead) return;

    /* 连击计时 */
    if (g->combo_timer > 0.0f) {
        g->combo_timer -= dt;
        if (g->combo_timer <= 0.0f) {
            g->combo_timer = 0.0f;
            g->combo = 0;
        }
    }

    /* 金苹果计时 */
    if (g->golden_on) {
        g->golden_life -= dt;
        if (g->golden_life <= 0.0f) {
            g->golden_on = false;
            g->golden_life = 0.0f;
        }
    }
}

/* 岩浆节奏：平静 -> 预警 -> 喷发 -> 平静 */
static void update_lava(Game *g, float dt)
{
    int a, b;

    if (g->map != MAP_LAVA || g->dead || g->paused) return;

    g->lava_timer -= dt;
    if (g->lava_timer > 0.0f) return;

    if (g->lava_phase == 0) {
        lava_clear(g);
        a = GetRandomValue(0, BOARD_H - 1);
        b = GetRandomValue(0, BOARD_H - 1);
        if (b == a) b = (a + 4) % BOARD_H;
        g->lava_rows[a] = true;
        g->lava_rows[b] = true;
        g->lava_phase = 1;
        g->lava_timer = LAVA_WARN;
        play_sound(g_snd_ui);
    } else if (g->lava_phase == 1) {
        g->lava_phase = 2;
        g->lava_timer = LAVA_ACTIVE;
        play_sound(g_snd_lava);
    } else {
        g->lava_phase = 0;
        g->lava_timer = LAVA_IDLE;
        lava_clear(g);
    }
}

static void update_skills(Game *g, float dt)
{
    if (g->sk_shield_cd > 0.0f) g->sk_shield_cd -= dt;
    if (g->sk_slow_cd   > 0.0f) g->sk_slow_cd   -= dt;
    if (g->sk_ghost_cd  > 0.0f) g->sk_ghost_cd  -= dt;
    if (g->sk_shield_on > 0.0f) g->sk_shield_on -= dt;
    if (g->sk_slow_on   > 0.0f) g->sk_slow_on   -= dt;
    if (g->sk_ghost_on  > 0.0f) g->sk_ghost_on  -= dt;
}

static void update_game(Game *g, float dt)
{
    int guard = 0;
    float step_eff;

    if (g->dead) { g->t = 1.0f; return; }
    if (g->paused) return;

    update_lava(g, dt);
    update_skills(g, dt);

    g->play_time += dt;
    g->move_timer += dt;

    step_eff = g->step * (g->sk_slow_on > 0.0f ? SLOW_FACTOR : 1.0f);

    while (g->move_timer >= step_eff && guard++ < 8) {
        g->move_timer -= step_eff;
        step_game(g);
        if (g->dead) { g->t = 1.0f; return; }
        step_eff = g->step * (g->sk_slow_on > 0.0f ? SLOW_FACTOR : 1.0f);
    }
    g->t = clampf(g->move_timer / step_eff, 0.0f, 1.0f);
}

/* ============================== 绘制 ============================== */

static void seg_blob(Vector2 p, Vector2 o, float size, Color c)
{
    Rectangle r = { p.x + o.x - size * 0.5f, p.y + o.y - size * 0.5f, size, size };
    DrawRectangleRounded(r, 0.42f, 8, c);
}

/* 连接相邻两节的矩形，让蛇身连成一条而不是一串珠子 */
static void seg_link(Vector2 a, Vector2 b, Vector2 o, float th, Color c)
{
    float x = fminf(a.x, b.x) + o.x - th * 0.5f;
    float y = fminf(a.y, b.y) + o.y - th * 0.5f;
    float w = fabsf(a.x - b.x) + th;
    float h = fabsf(a.y - b.y) + th;
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.35f, 6, c);
}

static void draw_background(void)
{
    DrawRectangleGradientV(0, 0, WINDOW_W, WINDOW_H, C_BG_TOP, C_BG_BOTTOM);
    DrawCircleGradient((Vector2){ WINDOW_W * 0.5f, -80.0f }, 560.0f,
                       (Color){ 60, 92, 145, 45 }, (Color){ 60, 92, 145, 0 });
}

/* 稳定的伪随机（同一 i 永远得到同一个值，保证装饰不闪） */
static float hash01(int i)
{
    unsigned int h = (unsigned int)i * 2654435761u;
    h ^= h >> 13;
    return (float)(h % 10000u) / 10000.0f;
}

/* 每张地图的棋盘装饰层 */
static void draw_board_decor(const Game *g, Rectangle br)
{
    int i;

    switch (g->map) {
    case MAP_MEADOW:
        /* 草地：一丛丛小 V 形草叶 */
        for (i = 0; i < 90; ++i) {
            float fx = br.x + hash01(i * 3 + 1) * br.width;
            float fy = br.y + hash01(i * 3 + 2) * br.height;
            float hh = 4.0f + hash01(i * 3 + 3) * 5.0f;
            Color c = Fade((Color){ 118, 200, 138, 255 }, 0.16f + 0.12f * hash01(i + 7));
            DrawLineEx((Vector2){ fx, fy }, (Vector2){ fx - 2.0f, fy - hh }, 1.2f, c);
            DrawLineEx((Vector2){ fx, fy }, (Vector2){ fx + 2.4f, fy - hh * 0.8f }, 1.2f, c);
        }
        break;

    case MAP_MAZE:
        /* 石板缝：细网格 */
        for (i = 0; i <= BOARD_W; ++i) {
            float x = br.x + (float)i * CELL;
            DrawLineEx((Vector2){ x, br.y }, (Vector2){ x, br.y + br.height },
                       1.0f, (Color){ 255, 255, 255, 9 });
        }
        for (i = 0; i <= BOARD_H; ++i) {
            float y = br.y + (float)i * CELL;
            DrawLineEx((Vector2){ br.x, y }, (Vector2){ br.x + br.width, y },
                       1.0f, (Color){ 255, 255, 255, 9 });
        }
        break;

    case MAP_PORTAL:
        /* 星空：中心星云 + 闪烁星点 */
        DrawCircleGradient((Vector2){ br.x + br.width * 0.5f, br.y + br.height * 0.5f },
                           br.width * 0.55f,
                           Fade((Color){ 120, 90, 220, 255 }, 0.10f),
                           Fade((Color){ 120, 90, 220, 255 }, 0.0f));
        for (i = 0; i < 70; ++i) {
            float fx = br.x + hash01(i * 5 + 11) * br.width;
            float fy = br.y + hash01(i * 5 + 12) * br.height;
            float tw = 0.5f + 0.5f * sinf(g_time * 1.8f + hash01(i * 5 + 13) * 6.28f);
            float r = 0.8f + hash01(i * 5 + 14) * 1.6f;
            DrawCircleV((Vector2){ fx, fy }, r,
                        Fade((Color){ 210, 200, 255, 255 }, 0.22f + 0.5f * tw));
        }
        break;

    case MAP_LAVA:
        /* 底部热辉 + 向上飘的余烬 */
        DrawRectangleGradientV((int)br.x, (int)(br.y + br.height * 0.55f),
                               (int)br.width, (int)(br.height * 0.45f),
                               Fade((Color){ 255, 110, 60, 255 }, 0.0f),
                               Fade((Color){ 255, 110, 60, 255 }, 0.10f));
        for (i = 0; i < 46; ++i) {
            float speed = 22.0f + hash01(i * 7 + 2) * 40.0f;
            float fx = br.x + hash01(i * 7 + 3) * br.width;
            float fy = br.y + br.height -
                       fmodf(g_time * speed + hash01(i * 7 + 4) * br.height, br.height);
            float a = 0.22f + 0.5f * hash01(i * 7 + 5);
            DrawCircleV((Vector2){ fx, fy }, 1.2f + hash01(i * 7 + 6) * 1.8f,
                        Fade((Color){ 255, 168, 92, 255 }, a));
        }
        break;
    }
}

static void draw_board(const Game *g)
{
    Rectangle br = board_rect();
    Rectangle panel = { br.x - 10.0f, br.y - 10.0f, br.width + 20.0f, br.height + 20.0f };
    Color panel_col = C_PANEL;
    Color edge_col = C_PANEL_ED;
    Color cell_a = { 30, 33, 44, 255 };
    Color cell_b = { 26, 29, 39, 255 };
    int x, y;

    switch (g->map) {
    case MAP_MEADOW:
        panel_col = (Color){  22,  34,  26, 255 };
        edge_col  = (Color){  54,  92,  66, 255 };
        cell_a    = (Color){  30,  46,  34, 255 };
        cell_b    = (Color){  26,  40,  30, 255 };
        break;
    case MAP_MAZE:
        panel_col = (Color){  26,  28,  35, 255 };
        edge_col  = (Color){  78,  84, 102, 255 };
        cell_a    = (Color){  33,  35,  44, 255 };
        cell_b    = (Color){  29,  31,  39, 255 };
        break;
    case MAP_PORTAL:
        panel_col = (Color){  19,  17,  36, 255 };
        edge_col  = (Color){  88,  72, 146, 255 };
        cell_a    = (Color){  27,  24,  48, 255 };
        cell_b    = (Color){  23,  21,  42, 255 };
        break;
    case MAP_LAVA:
        panel_col = (Color){  38,  21,  19, 255 };
        edge_col  = (Color){ 124,  60,  42, 255 };
        cell_a    = (Color){  46,  27,  23, 255 };
        cell_b    = (Color){  41,  24,  21, 255 };
        break;
    }

    DrawRectangleRounded((Rectangle){ panel.x + 4.0f, panel.y + 7.0f, panel.width, panel.height },
                         0.035f, 8, (Color){ 0, 0, 0, 70 });
    DrawRectangleRounded(panel, 0.035f, 8, panel_col);
    DrawRectangleRoundedLinesEx(panel, 0.035f, 8, 2.0f, edge_col);

    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            DrawRectangle((int)(br.x + x * CELL), (int)(br.y + y * CELL), CELL, CELL,
                          (((x + y) & 1) == 0) ? cell_a : cell_b);
        }
    }

    draw_board_decor(g, br);

    /* 慢放：棋盘描边泛起蓝光 */
    if (g->sk_slow_on > 0.0f) {
        float a = 0.28f + 0.18f * sinf(g_time * 6.0f);
        DrawRectangleRoundedLinesEx(panel, 0.035f, 8, 2.5f,
                                    Fade((Color){ 140, 170, 255, 255 }, a));
    }
}

/* 岩浆：预警时闪烁警示线，喷发时整行熔岩流动 */
static void draw_lava(const Game *g)
{
    Rectangle br = board_rect();
    int y, x;

    if (g->map != MAP_LAVA || g->lava_phase == 0) return;

    for (y = 0; y < BOARD_H; ++y) {
        Rectangle r;
        if (!g->lava_rows[y]) continue;
        r = (Rectangle){ br.x, br.y + (float)y * CELL, br.width, (float)CELL };

        if (g->lava_phase == 1) {
            float a = 0.16f + 0.22f * (0.5f + 0.5f * sinf(g_time * 16.0f));
            DrawRectangleRec(r, Fade(C_LAVA, a));
            DrawRectangleRec((Rectangle){ r.x, r.y, r.width, 2.0f }, Fade(C_GOLD, 0.8f));
            DrawRectangleRec((Rectangle){ r.x, r.y + r.height - 2.0f, r.width, 2.0f }, Fade(C_GOLD, 0.8f));
        } else {
            float t = g_time * 3.2f;
            DrawRectangleRec(r, Fade((Color){ 205, 55, 40, 255 }, 0.45f));
            for (x = 0; x < BOARD_W; x += 2) {
                float wob = sinf(t + (float)x * 0.8f + (float)y) * 5.0f;
                float rad = 8.0f + 2.0f * sinf(t * 1.7f + (float)x);
                DrawCircleV((Vector2){ r.x + ((float)x + 0.5f) * CELL + wob,
                                       r.y + r.height * 0.5f },
                            rad, Fade(C_LAVA, 0.30f));
            }
            DrawRectangleRec((Rectangle){ r.x, r.y, r.width, 2.0f }, Fade(C_LAVA, 0.95f));
            DrawRectangleRec((Rectangle){ r.x, r.y + r.height - 2.0f, r.width, 2.0f }, Fade(C_LAVA, 0.95f));
        }
    }
}

/* 虫洞：两对反向旋转的光环 */
static void draw_portals(const Game *g)
{
    int i;

    if (!g->has_portals) return;

    for (i = 0; i < 4; ++i) {
        Vector2 c = cell_center(g->portals[i]);
        Color col = (i < 2) ? C_PORTAL_A : C_PORTAL_B;
        float a0 = g_time * 160.0f * ((i & 1) ? 1.0f : -1.0f);

        DrawCircleGradient(c, 22.0f, Fade(col, 0.28f), Fade(col, 0.0f));
        DrawRing(c, 10.0f, 13.5f, a0, a0 + 220.0f, 32, Fade(col, 0.9f));
        DrawRing(c, 5.0f, 7.5f, a0 + 120.0f, a0 + 300.0f, 24, Fade(col, 0.55f));
        DrawCircleV(c, 2.6f, Fade(RAYWHITE, 0.9f));
    }
}

static void draw_walls(const Game *g)
{
    int i;
    for (i = 0; i < g->wall_n; ++i) {
        Vector2 c = cell_center(g->walls[i]);
        float sz = (float)CELL - 5.0f;
        Rectangle r = { c.x - sz * 0.5f, c.y - sz * 0.5f, sz, sz };
        Rectangle top = { r.x + 3.0f, r.y + 2.5f, r.width - 6.0f, 3.0f };

        DrawRectangleRounded((Rectangle){ r.x + 1.5f, r.y + 2.5f, r.width, r.height },
                             0.26f, 6, (Color){ 0, 0, 0, 70 });
        DrawRectangleRounded(r, 0.26f, 6, C_WALL);
        DrawRectangleRounded(top, 0.8f, 4, Fade(C_WALL_TOP, 0.85f));
    }
}

static void draw_food(const Game *g)
{
    Vector2 c = cell_center(g->food);
    float pulse = 0.5f + 0.5f * sinf(g_time * 5.0f);
    float a0 = g_time * 140.0f;

    DrawCircleGradient(c, 26.0f + pulse * 5.0f, Fade(C_FOOD, 0.30f), Fade(C_FOOD, 0.0f));
    DrawCircleV(c, 9.0f + pulse * 1.8f, C_FOOD);
    DrawCircleV((Vector2){ c.x - 3.0f, c.y - 3.5f }, 2.6f, Fade(RAYWHITE, 0.85f));
    DrawRing(c, 15.0f, 17.5f, a0, a0 + 100.0f, 28, Fade(C_FOOD, 0.55f));
}

static void draw_golden(const Game *g)
{
    Vector2 c;
    float pulse, frac, r;
    int i;

    if (!g->golden_on) return;

    c = cell_center(g->golden);
    pulse = 0.5f + 0.5f * sinf(g_time * 7.0f);
    frac = clampf(g->golden_life / GOLDEN_LIFE, 0.0f, 1.0f);

    DrawCircleGradient(c, 30.0f + pulse * 6.0f, Fade(C_GOLD, 0.34f), Fade(C_GOLD, 0.0f));
    DrawCircleV(c, 10.0f + pulse * 2.0f, C_GOLD);
    DrawCircleV((Vector2){ c.x - 3.2f, c.y - 3.8f }, 2.8f, Fade(RAYWHITE, 0.9f));

    /* 四道旋转的小星芒 */
    for (i = 0; i < 4; ++i) {
        float a = g_time * 150.0f + i * 90.0f;
        float rad = a * (PI / 180.0f);
        Vector2 sp = { c.x + cosf(rad) * 16.0f, c.y + sinf(rad) * 16.0f };
        DrawCircleV(sp, 1.8f + pulse * 0.9f, Fade(C_GOLD, 0.9f));
    }

    /* 剩余时间环 */
    r = 21.0f;
    DrawRing(c, r, r + 2.4f, -90.0f, -90.0f + 360.0f * frac, 40, Fade(C_GOLD, 0.85f));
}

/* 技能生效时的吉祥物外形：大圆头 + 黑豆眼 + 微笑，纯程序化绘制，不用贴图 */
static void draw_mascot_head(const Game *g, Vector2 p, float r)
{
    Color body = C_MASCOT;
    Color shade = C_MASCOT_DARK;
    float eye_dx = r * 0.36f;
    float eye_dy = -r * 0.16f;
    float eye_r = r * 0.30f;
    float pupil_r = eye_r * 0.66f;
    unsigned char ga;
    int e;

    if (g->sk_ghost_on > 0.0f) {
        body.a = 180;
        shade.a = 180;
    }
    ga = body.a;

    /* 落影 + 头 */
    DrawCircleV((Vector2){ p.x + 2.5f, p.y + 4.5f }, r, (Color){ 0, 0, 0, 45 });
    DrawCircleV(p, r, body);

    /* 球体感：边缘压暗 + 左上高光（都控制在头内部，避免外溢成光晕） */
    DrawCircleGradient(p, r, Fade(shade, 0.0f), Fade(shade, 0.45f));
    DrawCircleGradient((Vector2){ p.x - r * 0.28f, p.y - r * 0.30f }, r * 0.45f,
                       Fade(RAYWHITE, 0.22f), Fade(RAYWHITE, 0.0f));

    /* 两只黑豆眼（朝镜头，带高光） */
    for (e = -1; e <= 1; e += 2) {
        Vector2 eye = { p.x + eye_dx * (float)e, p.y + eye_dy };
        Vector2 pupil = { eye.x - eye_r * 0.14f, eye.y - eye_r * 0.12f };

        DrawCircleV(eye, eye_r, (Color){ 252, 252, 250, ga });
        DrawCircleV(pupil, pupil_r, (Color){ 22, 24, 30, ga });
        DrawCircleV((Vector2){ pupil.x - pupil_r * 0.36f, pupil.y - pupil_r * 0.34f },
                    pupil_r * 0.30f, (Color){ 255, 255, 255, ga });
    }

    /* 微笑：下半圆的一小段弧 */
    DrawRing((Vector2){ p.x, p.y + r * 0.10f }, r * 0.30f, r * 0.36f,
             35.0f, 145.0f, 24, (Color){ 74, 54, 30, ga });
}

static void draw_snake(const Game *g)
{
    const Snake *s = &g->snake;
    Vector2 pos[MAX_LEN];
    Color   col[MAX_LEN];
    float   sz[MAX_LEN];
    Vector2 shadow = { 2.0f, 3.0f };
    Vector2 zero = { 0.0f, 0.0f };
    float   base = (float)(CELL - 6);
    float   mascot_r = 0.0f;
    bool    mascot;
    int i;

    /* 任一技能生效时，整条蛇换成吉祥物外形 */
    mascot = !g->dead &&
             (g->sk_shield_on > 0.0f || g->sk_slow_on > 0.0f || g->sk_ghost_on > 0.0f);

    for (i = 0; i < s->len; ++i) {
        Vector2 a = cell_center(s->prev[i]);
        Vector2 b = cell_center(s->body[i]);
        float k = (s->len > 1) ? (float)i / (float)(s->len - 1) : 0.0f;

        /* 穿墙那一格会跨整个棋盘，直接跳到新位置，不做插值 */
        if (fabsf(b.x - a.x) > CELL * 1.5f || fabsf(b.y - a.y) > CELL * 1.5f) a = b;

        pos[i] = (Vector2){ a.x + (b.x - a.x) * g->t, a.y + (b.y - a.y) * g->t };
        if (mascot) {
            col[i] = (i == 0) ? C_MASCOT : ColorLerp(C_MASCOT, C_MASCOT_DARK, k);
        } else {
            col[i] = (i == 0) ? C_HEAD : ColorLerp(C_BODY, C_TAIL, k);
        }
        if (g->sk_ghost_on > 0.0f) col[i].a = 150;      /* 穿身：半透明 */
        /* 从头到尾逐渐收细，看起来更像一条蛇而不是一根管子 */
        sz[i] = base * (1.0f - 0.26f * k);
        if (i == 0) sz[i] += g->pop * 5.0f;
    }

    mascot_r = (base + g->pop * 5.0f) * 0.70f + 10.0f + g->pop * 4.0f;

    /* 蛇头辉光 */
    if (!g->dead) {
        Color glow = mascot ? C_MASCOT : C_HEAD;
        float gr = mascot ? mascot_r * 1.20f : 30.0f + g->pop * 10.0f;
        DrawCircleGradient(pos[0], gr, Fade(glow, 0.16f), Fade(glow, 0.0f));
    }

    /* 阴影层 */
    for (i = 1; i < s->len; ++i) {
        if (fabsf(pos[i].x - pos[i - 1].x) > CELL * 1.6f ||
            fabsf(pos[i].y - pos[i - 1].y) > CELL * 1.6f) continue;
        seg_link(pos[i - 1], pos[i], shadow, fminf(sz[i - 1], sz[i]) * 0.98f, (Color){ 0, 0, 0, 60 });
    }
    for (i = 0; i < s->len; ++i) seg_blob(pos[i], shadow, sz[i], (Color){ 0, 0, 0, 60 });

    /* 本体层：先画连接，再画每一节 */
    for (i = 1; i < s->len; ++i) {
        if (fabsf(pos[i].x - pos[i - 1].x) > CELL * 1.6f ||
            fabsf(pos[i].y - pos[i - 1].y) > CELL * 1.6f) continue;
        seg_link(pos[i - 1], pos[i], zero, fminf(sz[i - 1], sz[i]) * 0.98f, col[i]);
    }
    for (i = 1; i < s->len; ++i) seg_blob(pos[i], zero, sz[i], col[i]);

    /* 死亡时整体压暗 */
    if (g->dead) {
        for (i = 1; i < s->len; ++i) seg_blob(pos[i], zero, sz[i], Fade((Color){ 12, 14, 20, 255 }, 0.45f));
    }

    /* 护盾：套在蛇头上的呼吸光环 */
    if (g->sk_shield_on > 0.0f) {
        float a = 0.35f + 0.25f * sinf(g_time * 8.0f);
        float rr = mascot ? mascot_r + 6.0f : 21.0f;
        DrawRing(pos[0], rr, rr + 3.0f, 0.0f, 360.0f, 48, Fade((Color){ 110, 190, 255, 255 }, a));
    }

    /* 头部：普通形态是小圆脑袋，技能形态换成吉祥物 */
    if (mascot) {
        draw_mascot_head(g, pos[0], mascot_r);
    } else {
        Vector2 p = pos[0];
        Vector2 d = { (float)DX[s->dir], (float)DY[s->dir] };
        Vector2 perp = { -d.y, d.x };
        float hw = sz[0] * (1.0f + 0.16f * fabsf(d.x));
        float hh = sz[0] * (1.0f + 0.16f * fabsf(d.y));
        Color head = g->dead ? (Color){ 150, 90, 96, 255 } : C_HEAD;
        int e;

        if (g->sk_ghost_on > 0.0f) head.a = 160;

        DrawRectangleRounded((Rectangle){ p.x - hw * 0.5f, p.y - hh * 0.5f, hw, hh },
                             0.46f, 10, head);
        DrawCircleV((Vector2){ p.x - 5.0f, p.y - 6.0f }, 3.4f, Fade(RAYWHITE, 0.16f));

        for (e = -1; e <= 1; e += 2) {
            Vector2 eye = { p.x + d.x * 4.5f + perp.x * 6.0f * e,
                            p.y + d.y * 4.5f + perp.y * 6.0f * e };
            Vector2 pupil = { eye.x + d.x * 1.7f, eye.y + d.y * 1.7f };
            DrawCircleV(eye, 4.2f, (Color){ 250, 252, 255, 255 });
            DrawCircleV(pupil, 2.1f, (Color){ 18, 24, 32, 255 });
        }
    }
}

static void draw_particles(const Game *g)
{
    int i;
    for (i = 0; i < MAX_PARTS; ++i) {
        const Particle *pt = &g->parts[i];
        if (pt->life <= 0.0f) continue;
        DrawCircleV(pt->pos, pt->size, Fade(pt->color, clampf(pt->life / pt->max_life, 0.0f, 1.0f)));
    }
}

static void draw_rings(const Game *g)
{
    int i;
    for (i = 0; i < MAX_RINGS; ++i) {
        const Ring *r = &g->rings[i];
        float a;
        if (r->life <= 0.0f) continue;
        a = clampf(r->life / 0.45f, 0.0f, 1.0f);
        DrawRing(r->pos, r->r - 2.5f, r->r, 0.0f, 360.0f, 48, Fade(r->color, a * 0.7f));
    }
}

static void draw_popups(const Game *g)
{
    int i;
    for (i = 0; i < 12; ++i) {
        const Popup *pu = &g->popups[i];
        Vector2 m;
        if (pu->life <= 0.0f) continue;
        m = MeasureTextEx(g_font, pu->text, 20.0f, 1.0f);
        DrawTextEx(g_font, pu->text,
                   (Vector2){ pu->pos.x - m.x * 0.5f, pu->pos.y - 12.0f },
                   20.0f, 1.0f, Fade((Color){ 255, 214, 110, 255 }, clampf(pu->life / 0.9f, 0.0f, 1.0f)));
    }
}

/* 技能槽：冷却填充 + 生效时呼吸描边 */
static void draw_skill_chip(float x, float y, const char *key, float cd, float cd_max,
                            float active, Color col)
{
    Rectangle r = { x, y, 44.0f, 22.0f };
    float frac = (cd <= 0.0f) ? 1.0f : clampf(1.0f - cd / cd_max, 0.0f, 1.0f);
    bool ready = (cd <= 0.0f);
    Vector2 tw;

    DrawRectangleRounded(r, 0.35f, 8, (Color){ 255, 255, 255, 12 });
    if (frac > 0.02f) {
        DrawRectangleRounded((Rectangle){ r.x, r.y, r.width * frac, r.height }, 0.35f, 8,
                             Fade(col, active > 0.0f ? 0.45f : (ready ? 0.28f : 0.16f)));
    }
    if (active > 0.0f) {
        float pulse = 0.55f + 0.45f * sinf(g_time * 10.0f);
        DrawRectangleRoundedLinesEx(r, 0.35f, 8, 1.8f, Fade(col, pulse));
    } else {
        DrawRectangleRoundedLinesEx(r, 0.35f, 8, 1.0f,
                                    (Color){ 255, 255, 255, ready ? 40 : 20 });
    }

    tw = MeasureTextEx(g_font, key, 13.0f, 1.0f);
    DrawTextEx(g_font, key, (Vector2){ r.x + (r.width - tw.x) * 0.5f, r.y + 4.0f },
               13.0f, 1.0f, ready ? Fade(C_TEXT, 0.95f) : Fade(C_TEXT, 0.42f));
}

static void draw_hud(const Game *g)
{
    char buf[32];
    float y_label = 12.0f, y_value = 30.0f;

    DrawRectangle(0, HUD_H - 1, WINDOW_W, 1, (Color){ 255, 255, 255, 14 });

    /* 得分 */
    DrawTextEx(g_font, T("得分", "SCORE"), (Vector2){ 48.0f, y_label }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->score);
    DrawTextEx(g_font, buf, (Vector2){ 48.0f, y_value }, 32.0f, 2.0f, C_TEXT);

    if (g->combo >= 2) {                       /* 连击徽标跟在分数右边 */
        char cbuf[32];
        float w = text_w(buf, 32.0f, 2.0f);
        float a = 0.6f + 0.4f * sinf(g_time * 10.0f);
        snprintf(cbuf, sizeof(cbuf), "%s x%d", T("连击", "COMBO"), g->combo);
        DrawTextEx(g_font, cbuf, (Vector2){ 48.0f + w + 12.0f, 38.0f }, 16.0f, 1.0f,
                   Fade(C_GOLD, a));
    }

    /* 最高分 */
    DrawTextEx(g_font, T("最高", "BEST"), (Vector2){ 250.0f, y_label }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->best);
    DrawTextEx(g_font, buf, (Vector2){ 250.0f, y_value }, 32.0f, 2.0f, (Color){ 150, 200, 255, 255 });

    if (g->new_record) {
        float a = 0.55f + 0.45f * sinf(g_time * 9.0f);
        float w = text_w(buf, 32.0f, 2.0f);
        DrawTextEx(g_font, T("新纪录", "NEW!"), (Vector2){ 250.0f + w + 12.0f, 40.0f },
                   16.0f, 1.0f, Fade((Color){ 255, 210, 90, 255 }, a));
    }

    /* 长度 */
    DrawTextEx(g_font, T("长度", "LEN"), (Vector2){ 450.0f, y_label }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->snake.len);
    DrawTextEx(g_font, buf, (Vector2){ 450.0f, y_value }, 32.0f, 2.0f, C_TEXT);

    /* 等级 */
    DrawTextEx(g_font, T("等级", "LV"), (Vector2){ 600.0f, y_label }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->level);
    DrawTextEx(g_font, buf, (Vector2){ 600.0f, y_value }, 32.0f, 2.0f, C_TEXT);

    /* 速度条 */
    DrawTextEx(g_font, T("速度", "SPEED"), (Vector2){ 728.0f, y_label }, 17.0f, 1.0f, C_TEXT_DIM);
    {
        float sp = clampf((DIFFS[g->diff].step - g->step) /
                          (DIFFS[g->diff].step - DIFFS[g->diff].step_min), 0.0f, 1.0f);
        Rectangle bar = { 728.0f, 46.0f, 96.0f, 10.0f };
        DrawRectangleRounded(bar, 0.5f, 10, (Color){ 255, 255, 255, 20 });
        if (sp > 0.03f) {
            DrawRectangleRounded((Rectangle){ bar.x, bar.y, bar.width * sp, bar.height },
                                 0.5f, 10, C_ACCENT);
        }
    }

    /* 技能槽：J 护盾 / K 慢放 / L 穿身 */
    draw_skill_chip(836.0f, 42.0f, "J", g->sk_shield_cd, SHIELD_CD, g->sk_shield_on,
                    (Color){ 110, 190, 255, 255 });
    draw_skill_chip(884.0f, 42.0f, "K", g->sk_slow_cd, SLOW_CD, g->sk_slow_on,
                    (Color){ 172, 150, 255, 255 });
    draw_skill_chip(932.0f, 42.0f, "L", g->sk_ghost_cd, GHOST_CD, g->sk_ghost_on,
                    (Color){ 255, 172, 92, 255 });

    /* 底部操作提示 */
    draw_text_center(T("J 护盾   K 慢放   L 穿身   P 暂停   Q 退出",
                       "J shield   K slow   L ghost   P pause   Q quit"),
                     (float)(WINDOW_H - 28), 16.0f, 1.0f, C_TEXT_DIM);

    /* 岩浆预警横幅 */
    if (g->map == MAP_LAVA && g->lava_phase == 1) {
        float a = 0.55f + 0.45f * sinf(g_time * 14.0f);
        draw_text_center(T("岩浆喷发", "LAVA INCOMING"), (float)HUD_H + 26.0f, 20.0f, 2.0f,
                         Fade(C_LAVA, a));
    }
}

/* ------------------------------ 菜单 ------------------------------ */

static void draw_menu_option(float y, const char *text, bool selected, float size, bool disabled)
{
    float w = text_w(text, size, 1.0f);
    float x = (WINDOW_W - w) * 0.5f;
    Color col = disabled ? Fade(C_TEXT, 0.28f) : (selected ? C_TEXT : Fade(C_TEXT, 0.55f));

    if (selected) {
        Rectangle r = { x - 30.0f, y - 10.0f, w + 60.0f, size + 20.0f };
        DrawRectangleRounded(r, 0.4f, 12, Fade(C_ACCENT, disabled ? 0.04f : 0.10f));
        DrawRectangleRoundedLinesEx(r, 0.4f, 12, 1.6f, Fade(C_ACCENT, disabled ? 0.18f : 0.45f));
        DrawTextEx(g_font, ">", (Vector2){ r.x - 26.0f, y + 2.0f }, size * 0.8f, 1.0f,
                   Fade(C_ACCENT, disabled ? 0.3f : 1.0f));
    }

    DrawTextEx(g_font, text, (Vector2){ x, y }, size, 1.0f, col);
}

static void draw_menu(const Menu *m)
{
    char buf[96];
    int i;

    /* 背景光斑 */
    for (i = 0; i < 10; ++i) {
        float t = g_time * 0.28f + (float)i * 0.9f;
        float x = WINDOW_W * 0.5f + sinf(t * 1.3f + (float)i) * (210.0f + (float)i * 16.0f);
        float y = 210.0f + cosf(t + (float)i * 1.7f) * 130.0f;
        DrawCircleGradient((Vector2){ x, y }, 70.0f, Fade(C_ACCENT, 0.045f), Fade(C_ACCENT, 0.0f));
    }

    /* 标题 */
    {
        float bob = sinf(g_time * 1.9f) * 4.0f;
        DrawCircleGradient((Vector2){ WINDOW_W * 0.5f, 150.0f + bob }, 190.0f,
                           Fade(C_HEAD, 0.10f), Fade(C_HEAD, 0.0f));
        draw_text_center(T("贪吃蛇", "SNAKE"), 108.0f + bob, 84.0f, 6.0f, C_HEAD);
        draw_text_center("raylib edition", 212.0f + bob, 20.0f, 2.0f, C_TEXT_DIM);
    }

    /* 地图 */
    snprintf(buf, sizeof(buf), "%s    < %s >",
             T("地图", "MAP"),
             g_cjk ? MAPS[m->map].zh : MAPS[m->map].en);
    draw_menu_option(318.0f, buf, m->sel == 0, 27.0f, false);

    /* 地图机制说明 */
    draw_text_center(g_cjk ? MAPS[m->map].desc_zh : MAPS[m->map].desc_en,
                     354.0f, 15.0f, 1.0f, Fade(C_TEXT_DIM, 0.85f));

    /* 难度 */
    snprintf(buf, sizeof(buf), "%s    < %s >",
             T("难度", "DIFFICULTY"),
             g_cjk ? DIFFS[m->diff].zh : DIFFS[m->diff].en);
    draw_menu_option(412.0f, buf, m->sel == 1, 27.0f, false);

    /* 穿墙（部分地图固定关闭） */
    snprintf(buf, sizeof(buf), "%s    < %s >",
             T("穿墙", "WRAP"),
             m->wrap ? T("开", "ON") : T("关", "OFF"));
    draw_menu_option(472.0f, buf, m->sel == 2, 27.0f, !MAPS[m->map].allow_wrap);

    draw_menu_option(538.0f, T("开始游戏", "START"), m->sel == 3, 30.0f, false);

    /* 底部提示 */
    draw_text_center(T("上下选择    左右调整    回车开始", "UP/DOWN select   LEFT/RIGHT change   ENTER start"),
                     620.0f, 17.0f, 1.0f, C_TEXT_DIM);
}

/* ------------------------------ 覆盖层 ------------------------------ */

static void draw_pause_overlay(const Game *g)
{
    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, (Color){ 8, 10, 16, 175 });
    draw_text_center(T("已暂停", "PAUSED"), 296.0f, 54.0f, 4.0f, C_TEXT);
    draw_text_center(T("按 P 或空格继续", "press P or SPACE to resume"), 376.0f, 21.0f, 2.0f, C_TEXT_DIM);
    draw_text_center(T("ESC 返回菜单", "ESC back to menu"), 412.0f, 17.0f, 1.0f, Fade(C_TEXT_DIM, 0.8f));
    (void)g;
}

static void draw_dead_overlay(const Game *g)
{
    Rectangle p = { (float)WINDOW_W * 0.5f - 250.0f, 214.0f, 500.0f, 330.0f };
    char buf[64];

    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, (Color){ 6, 8, 13, 190 });

    DrawRectangleRounded((Rectangle){ p.x + 4.0f, p.y + 7.0f, p.width, p.height },
                         0.07f, 10, (Color){ 0, 0, 0, 100 });
    DrawRectangleRounded(p, 0.07f, 10, (Color){ 28, 32, 45, 250 });
    DrawRectangleRoundedLinesEx(p, 0.07f, 10, 2.0f, (Color){ 72, 80, 104, 255 });

    draw_text_center(g->win ? T("通关", "YOU WIN") : T("游戏结束", "GAME OVER"),
                     p.y + 26.0f, 42.0f, 3.0f,
                     g->win ? C_GOLD : (Color){ 255, 118, 118, 255 });

    /* 左列 */
    DrawTextEx(g_font, T("本次得分", "SCORE"), (Vector2){ p.x + 52.0f, p.y + 96.0f },
               18.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->score);
    DrawTextEx(g_font, buf, (Vector2){ p.x + 52.0f, p.y + 118.0f }, 34.0f, 2.0f, C_TEXT);

    DrawTextEx(g_font, T("长度", "LEN"), (Vector2){ p.x + 52.0f, p.y + 176.0f },
               18.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->snake.len);
    DrawTextEx(g_font, buf, (Vector2){ p.x + 52.0f, p.y + 198.0f }, 26.0f, 2.0f, C_TEXT);

    /* 右列 */
    DrawTextEx(g_font, T("历史最高", "BEST"), (Vector2){ p.x + 268.0f, p.y + 96.0f },
               18.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->best);
    DrawTextEx(g_font, buf, (Vector2){ p.x + 268.0f, p.y + 118.0f }, 34.0f, 2.0f,
               (Color){ 150, 200, 255, 255 });

    DrawTextEx(g_font, T("最大连击", "MAX COMBO"), (Vector2){ p.x + 268.0f, p.y + 176.0f },
               18.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "x%d", g->best_combo);
    DrawTextEx(g_font, buf, (Vector2){ p.x + 268.0f, p.y + 198.0f }, 26.0f, 2.0f, C_GOLD);

    /* 用时 */
    snprintf(buf, sizeof(buf), "%s  %.1f %s", T("用时", "TIME"), (double)g->play_time, T("秒", "s"));
    draw_text_center(buf, p.y + 248.0f, 17.0f, 1.0f, C_TEXT_DIM);

    draw_text_center(T("按 R 重来        M 返回菜单", "R restart        M menu"),
                     p.y + 288.0f, 18.0f, 1.0f, C_TEXT_DIM);
}

/* ============================== 输入 ============================== */

static void menu_adjust(Menu *m, int dir)
{
    if (m->sel == 0) {
        m->map = (m->map + dir + MAP_COUNT) % MAP_COUNT;
        if (!MAPS[m->map].allow_wrap) m->wrap = false;
        play_sound(g_snd_ui);
    } else if (m->sel == 1) {
        m->diff = (m->diff + dir + 3) % 3;
        play_sound(g_snd_ui);
    } else if (m->sel == 2) {
        if (MAPS[m->map].allow_wrap) {
            m->wrap = !m->wrap;
            play_sound(g_snd_ui);
        }
    }
}

static Scene handle_menu(Menu *m, bool *running, Game *g)
{
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) {
        *running = false;
        return SC_MENU;
    }

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        m->sel = (m->sel + 3) % 4;
        play_sound(g_snd_ui);
    } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        m->sel = (m->sel + 1) % 4;
        play_sound(g_snd_ui);
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) menu_adjust(m, -1);
    else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) menu_adjust(m, 1);

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (m->sel == 3) {
            reset_game(g, m->diff, m->wrap, m->map);
            play_sound(g_snd_ui);
            return SC_PLAY;
        }
        menu_adjust(m, 1);
    }

    return SC_MENU;
}

static void try_skill(Game *g, int which)
{
    if (which == 0) {                          /* 护盾 */
        if (g->sk_shield_cd <= 0.0f && g->sk_shield_on <= 0.0f) {
            g->sk_shield_on = SHIELD_TIME;
            g->sk_shield_cd = SHIELD_CD;
            play_sound(g_snd_ui);
        }
    } else if (which == 1) {                   /* 慢放 */
        if (g->sk_slow_cd <= 0.0f && g->sk_slow_on <= 0.0f) {
            g->sk_slow_on = SLOW_TIME;
            g->sk_slow_cd = SLOW_CD;
            play_sound(g_snd_ui);
        }
    } else {                                   /* 穿身 */
        if (g->sk_ghost_cd <= 0.0f && g->sk_ghost_on <= 0.0f) {
            g->sk_ghost_on = GHOST_TIME;
            g->sk_ghost_cd = GHOST_CD;
            play_sound(g_snd_ui);
        }
    }
}

static Scene handle_play(Game *g, bool *running, Menu *m)
{
    if (IsKeyPressed(KEY_Q)) {
        *running = false;
        return SC_PLAY;
    }

    if (IsKeyPressed(KEY_R)) {
        reset_game(g, g->diff, g->wrap, g->map);
        return SC_PLAY;
    }

    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE)) {
        g->paused = !g->paused;
        play_sound(g_snd_ui);
        return SC_PLAY;
    }

    if (g->paused) {
        if (IsKeyPressed(KEY_M)) {
            m->diff = g->diff;
            m->wrap = g->wrap;
            m->map = g->map;
            g->paused = false;
            return SC_MENU;
        }
        return SC_PLAY;
    }

    if (IsKeyPressed(KEY_J)) try_skill(g, 0);
    if (IsKeyPressed(KEY_K)) try_skill(g, 1);
    if (IsKeyPressed(KEY_L)) try_skill(g, 2);

    if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W)) queue_turn(g, DIR_UP);
    else if (IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_S)) queue_turn(g, DIR_DOWN);
    else if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) queue_turn(g, DIR_LEFT);
    else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) queue_turn(g, DIR_RIGHT);

    return SC_PLAY;
}

static Scene handle_dead(Game *g, bool *running, Menu *m)
{
    if (IsKeyPressed(KEY_Q)) {
        *running = false;
        return SC_DEAD;
    }
    if (IsKeyPressed(KEY_R)) {
        reset_game(g, g->diff, g->wrap, g->map);
        return SC_PLAY;
    }
    if (IsKeyPressed(KEY_M) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        m->diff = g->diff;
        m->wrap = g->wrap;
        m->map = g->map;
        g->dead = false;                  /* 回菜单时清掉死亡状态，否则菜单画不出来 */
        g->paused = false;
        return SC_MENU;
    }
    return SC_DEAD;
}

/* ============================== 主程序 ============================== */

int main(void)
{
    Game   g;
    Menu   menu;
    Scene  scene = SC_MENU;
    bool   running = true;
    int    shot_at = 0, frame_no = 0;
    const char *env_shot, *env_auto;

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_W, WINDOW_H, "Snake v3 | raylib");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);                 /* 退出键自己处理 */
    if (!IsWindowReady()) return 1;

    g_font = load_ui_font();
    init_audio();

    memset(&g, 0, sizeof(g));
    memset(&menu, 0, sizeof(menu));
    menu.sel = 3;                         /* 默认停在"开始游戏" */
    menu.diff = 0;
    menu.map = 0;
    menu.wrap = false;

    g.best = load_best();
    reset_game(&g, menu.diff, menu.wrap, menu.map);

    /* 无人值守渲染自检：SNAKE_AUTO = 难度(0-2) 或 难度+地图(如 "2:1") */
    env_shot = getenv("SNAKE_SHOT");
    if (env_shot != NULL) shot_at = atoi(env_shot);
    env_auto = getenv("SNAKE_AUTO");
    if (env_auto != NULL) {
        const char *colon = strchr(env_auto, ':');
        menu.diff = atoi(env_auto);
        if (colon != NULL) menu.map = atoi(colon + 1);
        if (menu.diff < 0 || menu.diff > 2) menu.diff = 0;
        if (menu.map < 0 || menu.map >= MAP_COUNT) menu.map = 0;
        reset_game(&g, menu.diff, false, menu.map);
        scene = SC_PLAY;
    }

    while (running && !WindowShouldClose()) {
        float dt = GetFrameTime();

        if (dt > 0.1f) dt = 0.1f;         /* 防止窗口拖动后一帧跳太远 */
        g_time += dt;

        if (scene == SC_MENU) {
            scene = handle_menu(&menu, &running, &g);
        } else if (scene == SC_PLAY) {
            Scene next = handle_play(&g, &running, &menu);
            if (next == SC_PLAY) {
                update_game(&g, dt);
                update_effects(&g, dt);
                if (g.dead) scene = SC_DEAD;
            } else {
                scene = next;
            }
        } else {
            scene = handle_dead(&g, &running, &menu);
            update_effects(&g, dt);
        }

        BeginDrawing();
        ClearBackground(C_BG_BOTTOM);
        draw_background();

        if (scene == SC_MENU) {
            draw_menu(&menu);
        } else {
            Vector2 off = { 0.0f, 0.0f };
            Camera2D cam;
            int shake_on = (g.shake > 0.0f && !g.dead);

            if (shake_on) {
                float amp = g.shake * 9.0f;
                off.x = (float)GetRandomValue(-100, 100) / 100.0f * amp;
                off.y = (float)GetRandomValue(-100, 100) / 100.0f * amp;
            }

            draw_board(&g);

            cam.offset = off;
            cam.target = (Vector2){ 0.0f, 0.0f };
            cam.rotation = 0.0f;
            cam.zoom = 1.0f;
            BeginMode2D(cam);
            draw_lava(&g);
            draw_portals(&g);
            draw_walls(&g);
            draw_food(&g);
            draw_golden(&g);
            draw_snake(&g);
            draw_particles(&g);
            draw_rings(&g);
            draw_popups(&g);
            EndMode2D();

            draw_hud(&g);

            if (g.paused && !g.dead && scene == SC_PLAY) draw_pause_overlay(&g);
            if (scene == SC_DEAD || g.dead) draw_dead_overlay(&g);
        }

        EndDrawing();

        if (shot_at > 0 && ++frame_no >= shot_at) {
            TakeScreenshot("snake_shot.png");
            running = false;
        }
    }

    free_audio();
    if (g_font.texture.id != GetFontDefault().texture.id) UnloadFont(g_font);
    CloseWindow();
    return 0;
}
