/* ============================================================================
 *  贪吃蛇 · raylib 图形版 (Snake + raylib)
 *
 *  相比字符版，这一版加入了：
 *    - 60FPS 渲染，格子之间做插值动画（不再是"一格一格跳"）
 *    - 蛇身连贯圆角、渐变色、向尾部收细、带瞳孔的眼睛、吃食时头部放大
 *    - 发光脉动食物 + 旋转光环
 *    - 吃到食物时的粒子爆散与 "+10" 飘字
 *    - 死亡时屏幕震动 + 红色粒子
 *    - HUD：得分 / 最高 / 长度 / 速度条，最高分写入文件
 *    - 程序化合成音效（不依赖任何素材文件）
 *    - 中文字体（simhei.ttf），找不到时自动退回英文界面
 *
 *  编译（Windows + MinGW，raylib 6.2 官方预编译包）：
 *    gcc snake_raylib.c -o snake_raylib.exe -O2 ^
 *        -I"D:/raylib-6.2_win64_mingw-w64/include" ^
 *        -L"D:/raylib-6.2_win64_mingw-w64/lib" ^
 *        -lraylibdll -lopengl32 -lgdi32 -lwinmm
 *    （把 raylib.dll 一起放到 exe 旁边；若你的 MinGW 是 UCRT 版，
 *      可改用静态库 -lraylib，免去 DLL）
 *
 *  操作：方向键 / WASD 移动，P 或空格 暂停，R 重开，Q 或 ESC 退出
 *  开发自检：设置环境变量 SNAKE_SHOT=帧号，程序会在该帧截图后退出。
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

#define WINDOW_W  980
#define WINDOW_H  720
#define HUD_H     76

#define STEP_START 0.145f                /* 初始速度：每格耗时（秒） */
#define STEP_MIN   0.055f                /* 最快速度 */
#define STEP_DEC   0.006f                /* 每吃一个食物提速 */

#define BEST_FILE  "snake_best.txt"      /* 最高分存档 */

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
static const Color C_ACCENT    = { 120, 240, 160, 255 };

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

typedef struct {
    Snake  snake;
    Cell   food;
    int    score, best;
    bool   paused, dead, new_record;
    float  step;                         /* 秒 / 格 */
    float  move_timer;
    float  t;                            /* 插值进度 0..1 */
    int    turn_q[2];                    /* 转向队列，最多缓存两次转向 */
    int    turn_n;
    float  time;
    float  shake;
    float  pop;                          /* 吃到食物时头部的放大动画 */
    Particle parts[MAX_PARTS];
    Popup    popups[8];
} Game;

/* ------------------------------ 全局状态 ------------------------------ */
static Font g_font;
static bool g_cjk = false;               /* 中文字体是否可用 */
static bool g_audio = false;
static Sound g_snd_eat, g_snd_dead;

/* 界面里出现的全部中文，用于生成字体子集 */
static const char *UI_TEXT = "贪吃蛇得分最高长度速度已暂停按或空格继续游戏结束本次历史重新开始退出新纪录";

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

/* 居中绘制一行文字 */
static void draw_text_center(const char *text, float y, float size, float spacing, Color color)
{
    Vector2 m = MeasureTextEx(g_font, text, size, spacing);
    DrawTextEx(g_font, text, (Vector2){ (WINDOW_W - m.x) * 0.5f, y }, size, spacing, color);
}

/* ============================ 字体与音效 ============================ */

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
        for (i = 0; i < cjk_count; ++i) cps[n++] = cjk[i]; /* 界面用到的汉字 */

        f = LoadFontEx(path, 44, cps, n);
        MemFree(cps);
        if (cjk) UnloadCodepoints(cjk);

        if (f.texture.id == 0 || f.glyphCount == 0) return GetFontDefault();
        g_cjk = true;
        return f;
    }
}

/* 在内存里合成一段 PCM 波形并载入为 Sound，省去外部音频素材 */
static Sound make_tone(float f0, float f1, float seconds, float volume)
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
    u32 = 16;                  memcpy(buf + 16, &u32, 4);
    u16 = 1;                   memcpy(buf + 20, &u16, 2);   /* PCM */
    u16 = 1;                   memcpy(buf + 22, &u16, 2);   /* 单声道 */
    u32 = (unsigned int)rate;  memcpy(buf + 24, &u32, 4);
    u32 = (unsigned int)(rate * 2); memcpy(buf + 28, &u32, 4);
    u16 = 2;                   memcpy(buf + 32, &u16, 2);   /* block align */
    u16 = 16;                  memcpy(buf + 34, &u16, 2);   /* bits per sample */
    memcpy(buf + 36, "data", 4);
    u32 = (unsigned int)data_bytes; memcpy(buf + 40, &u32, 4);

    /* ---- 波形：频率滑音 + 平方衰减包络 + 一点三次谐波 ---- */
    pcm = (short *)(buf + 44);
    for (i = 0; i < frames; ++i) {
        float k = (float)i / (float)frames;
        float f = f0 + (f1 - f0) * k;
        float env, attack, s;

        phase += 2.0 * (double)PI * (double)f / (double)rate;
        env = (1.0f - k) * (1.0f - k);
        attack = k < 0.02f ? k / 0.02f : 1.0f;
        s = (float)sin(phase) * 0.8f + (float)sin(phase * 3.0) * 0.2f;
        s = s * env * attack * volume;
        pcm[i] = (short)(clampf(s, -1.0f, 1.0f) * 32767.0f);
    }

    wave = LoadWaveFromMemory(".wav", buf, total);
    snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    MemFree(buf);
    return snd;
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

/* ============================== 游戏逻辑 ============================== */

static int opposite(int dir)
{
    return (dir + 2) % 4;
}

static void place_food(Game *g)
{
    static Cell free_cells[MAX_LEN];
    int n = 0, x, y, i;

    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            bool occupied = false;
            for (i = 0; i < g->snake.len; ++i) {
                if (g->snake.body[i].x == x && g->snake.body[i].y == y) {
                    occupied = true;
                    break;
                }
            }
            if (!occupied) {
                free_cells[n].x = x;
                free_cells[n].y = y;
                ++n;
            }
        }
    }
    if (n > 0) g->food = free_cells[GetRandomValue(0, n - 1)];
}

static void reset_game(Game *g)
{
    int cx = BOARD_W / 2, cy = BOARD_H / 2, i;

    g->snake.len = 3;
    g->snake.dir = DIR_RIGHT;
    for (i = 0; i < 3; ++i) {
        g->snake.body[i].x = cx - i;
        g->snake.body[i].y = cy;
        g->snake.prev[i] = g->snake.body[i];
    }

    g->score = 0;
    g->paused = false;
    g->dead = false;
    g->new_record = false;
    g->step = STEP_START;
    g->move_timer = 0.0f;
    g->t = 0.0f;
    g->turn_n = 0;
    g->shake = 0.0f;
    g->pop = 0.0f;

    for (i = 0; i < MAX_PARTS; ++i) g->parts[i].life = 0.0f;
    for (i = 0; i < 8; ++i) g->popups[i].life = 0.0f;

    place_food(g);
}

/* 缓存转向：允许连按两次，但绝不接受 180° 掉头 */
static void queue_turn(Game *g, int d)
{
    int base = (g->turn_n > 0) ? g->turn_q[g->turn_n - 1] : g->snake.dir;

    if (d == base || d == opposite(base)) return;
    if (g->turn_n < 2) g->turn_q[g->turn_n++] = d;
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

static void add_popup(Game *g, Vector2 p, const char *text)
{
    int i;
    for (i = 0; i < 8; ++i) {
        if (g->popups[i].life <= 0.0f) {
            g->popups[i].pos = p;
            g->popups[i].life = 0.9f;
            snprintf(g->popups[i].text, sizeof(g->popups[i].text), "%s", text);
            return;
        }
    }
}

static void step_game(Game *g)
{
    Cell head = g->snake.body[0];
    Cell old_tail = g->snake.body[g->snake.len - 1];
    Cell nh;
    bool eat = false, hit = false;
    int i, limit;

    if (g->turn_n > 0) {                       /* 取出队列里缓存的转向 */
        g->snake.dir = g->turn_q[0];
        g->turn_q[0] = g->turn_q[1];
        --g->turn_n;
    }

    nh.x = head.x + DX[g->snake.dir];
    nh.y = head.y + DY[g->snake.dir];

    if (nh.x < 0 || nh.x >= BOARD_W || nh.y < 0 || nh.y >= BOARD_H) {
        hit = true;                            /* 撞墙 */
    } else {
        eat = (nh.x == g->food.x && nh.y == g->food.y);
        limit = eat ? g->snake.len : g->snake.len - 1;  /* 不吃时尾巴会移开 */
        for (i = 1; i < limit; ++i) {
            if (g->snake.body[i].x == nh.x && g->snake.body[i].y == nh.y) {
                hit = true;                    /* 咬到自己 */
                break;
            }
        }
    }

    if (hit) {
        g->dead = true;
        g->shake = 0.45f;
        spawn_burst(g, cell_center(head), (Color){ 255, 90, 90, 255 }, 26);
        if (g->score > g->best) {
            g->best = g->score;
            g->new_record = true;
            save_best(g->best);
        }
        if (g_audio) PlaySound(g_snd_dead);
        return;
    }

    /* 记录旧位置用于插值，然后整体前移 */
    memcpy(g->snake.prev, g->snake.body, sizeof(Cell) * (size_t)g->snake.len);
    for (i = g->snake.len - 1; i > 0; --i) g->snake.body[i] = g->snake.body[i - 1];
    g->snake.body[0] = nh;

    if (eat) {
        if (g->snake.len < MAX_LEN) {
            g->snake.body[g->snake.len] = old_tail;    /* 新尾巴长在原尾处 */
            g->snake.prev[g->snake.len] = old_tail;
            ++g->snake.len;
        }
        g->score += 10;
        if (g->score > g->best) {
            g->best = g->score;
            g->new_record = true;
            save_best(g->best);
        }
        g->step = clampf(g->step - STEP_DEC, STEP_MIN, STEP_START);
        g->pop = 1.0f;
        spawn_burst(g, cell_center(g->food), C_FOOD, 18);
        add_popup(g, cell_center(nh), "+10");
        if (g_audio) PlaySound(g_snd_eat);
        place_food(g);
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

    for (i = 0; i < 8; ++i) {
        if (g->popups[i].life > 0.0f) {
            g->popups[i].life -= dt;
            g->popups[i].pos.y -= 34.0f * dt;
        }
    }

    if (g->shake > 0.0f) g->shake = clampf(g->shake - dt * 2.2f, 0.0f, 1.0f);
    if (g->pop > 0.0f)   g->pop = clampf(g->pop - dt * 4.5f, 0.0f, 1.0f);
}

static void update_game(Game *g, float dt)
{
    int guard = 0;

    if (g->dead) { g->t = 1.0f; return; }
    if (g->paused) return;

    g->move_timer += dt;
    while (g->move_timer >= g->step && guard++ < 8) {
        g->move_timer -= g->step;
        step_game(g);
        if (g->dead) { g->t = 1.0f; return; }
    }
    g->t = clampf(g->move_timer / g->step, 0.0f, 1.0f);
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

static void draw_board(void)
{
    Rectangle br = board_rect();
    Rectangle panel = { br.x - 10.0f, br.y - 10.0f, br.width + 20.0f, br.height + 20.0f };
    int x, y;

    DrawRectangleRounded((Rectangle){ panel.x + 4.0f, panel.y + 7.0f, panel.width, panel.height },
                         0.035f, 8, (Color){ 0, 0, 0, 70 });
    DrawRectangleRounded(panel, 0.035f, 8, C_PANEL);
    DrawRectangleRoundedLinesEx(panel, 0.035f, 8, 2.0f, C_PANEL_ED);

    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            if (((x + y) & 1) == 0) {
                DrawRectangle((int)(br.x + x * CELL), (int)(br.y + y * CELL),
                              CELL, CELL, (Color){ 255, 255, 255, 7 });
            }
        }
    }
}

static void draw_food(const Game *g)
{
    Vector2 c = cell_center(g->food);
    float pulse = 0.5f + 0.5f * sinf(g->time * 5.0f);
    float a0 = g->time * 140.0f;

    DrawCircleGradient(c, 26.0f + pulse * 5.0f, Fade(C_FOOD, 0.30f), Fade(C_FOOD, 0.0f));
    DrawCircleV(c, 9.0f + pulse * 1.8f, C_FOOD);
    DrawCircleV((Vector2){ c.x - 3.0f, c.y - 3.5f }, 2.6f, Fade(RAYWHITE, 0.85f));
    DrawRing(c, 15.0f, 17.5f, a0, a0 + 100.0f, 28, Fade(C_FOOD, 0.55f));
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
    int i;

    for (i = 0; i < s->len; ++i) {
        Vector2 a = cell_center(s->prev[i]);
        Vector2 b = cell_center(s->body[i]);
        float k = (s->len > 1) ? (float)i / (float)(s->len - 1) : 0.0f;

        pos[i] = (Vector2){ a.x + (b.x - a.x) * g->t, a.y + (b.y - a.y) * g->t };
        col[i] = (i == 0) ? C_HEAD : ColorLerp(C_BODY, C_TAIL, k);
        /* 从头到尾逐渐收细，看起来更像一条蛇而不是一根管子 */
        sz[i] = base * (1.0f - 0.26f * k);
        if (i == 0) sz[i] += g->pop * 5.0f;
    }

    /* 阴影层 */
    for (i = 1; i < s->len; ++i) {
        seg_link(pos[i - 1], pos[i], shadow, fminf(sz[i - 1], sz[i]) * 0.98f, (Color){ 0, 0, 0, 60 });
    }
    for (i = 0; i < s->len; ++i) seg_blob(pos[i], shadow, sz[i], (Color){ 0, 0, 0, 60 });

    /* 本体层：先画连接，再画每一节 */
    for (i = 1; i < s->len; ++i) {
        seg_link(pos[i - 1], pos[i], zero, fminf(sz[i - 1], sz[i]) * 0.98f, col[i]);
    }
    for (i = 1; i < s->len; ++i) seg_blob(pos[i], zero, sz[i], col[i]);

    /* 头部：沿运动方向略拉长，看起来像个小脑袋 */
    {
        Vector2 p = pos[0];
        Vector2 d = { (float)DX[s->dir], (float)DY[s->dir] };
        Vector2 perp = { -d.y, d.x };
        float hw = sz[0] * (1.0f + 0.16f * fabsf(d.x));
        float hh = sz[0] * (1.0f + 0.16f * fabsf(d.y));
        int e;

        DrawRectangleRounded((Rectangle){ p.x - hw * 0.5f, p.y - hh * 0.5f, hw, hh },
                             0.46f, 10, C_HEAD);
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

static void draw_popups(const Game *g)
{
    int i;
    for (i = 0; i < 8; ++i) {
        const Popup *pu = &g->popups[i];
        Vector2 m;
        if (pu->life <= 0.0f) continue;
        m = MeasureTextEx(g_font, pu->text, 20.0f, 1.0f);
        DrawTextEx(g_font, pu->text,
                   (Vector2){ pu->pos.x - m.x * 0.5f, pu->pos.y - 12.0f },
                   20.0f, 1.0f, Fade((Color){ 255, 214, 110, 255 }, clampf(pu->life / 0.9f, 0.0f, 1.0f)));
    }
}

static void draw_hud(const Game *g)
{
    char buf[32];
    Rectangle bar;
    float sp;

    DrawRectangle(0, HUD_H - 1, WINDOW_W, 1, (Color){ 255, 255, 255, 14 });

    /* 得分 */
    DrawTextEx(g_font, T("得分", "SCORE"), (Vector2){ 48.0f, 12.0f }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->score);
    DrawTextEx(g_font, buf, (Vector2){ 48.0f, 30.0f }, 34.0f, 2.0f, C_TEXT);

    /* 最高分 */
    DrawTextEx(g_font, T("最高", "BEST"), (Vector2){ 300.0f, 12.0f }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->best);
    DrawTextEx(g_font, buf, (Vector2){ 300.0f, 30.0f }, 34.0f, 2.0f, (Color){ 150, 200, 255, 255 });

    if (g->new_record) {
        float a = 0.55f + 0.45f * sinf(g->time * 9.0f);
        float w = MeasureTextEx(g_font, buf, 34.0f, 2.0f).x;
        DrawTextEx(g_font, T("新纪录", "NEW!"), (Vector2){ 300.0f + w + 14.0f, 42.0f },
                   17.0f, 1.0f, Fade((Color){ 255, 210, 90, 255 }, a));
    }

    /* 长度 */
    DrawTextEx(g_font, T("长度", "LEN"), (Vector2){ 500.0f, 12.0f }, 17.0f, 1.0f, C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "%d", g->snake.len);
    DrawTextEx(g_font, buf, (Vector2){ 500.0f, 30.0f }, 34.0f, 2.0f, C_TEXT);

    /* 速度条 */
    sp = clampf((STEP_START - g->step) / (STEP_START - STEP_MIN), 0.0f, 1.0f);
    DrawTextEx(g_font, T("速度", "SPEED"), (Vector2){ 672.0f, 12.0f }, 17.0f, 1.0f, C_TEXT_DIM);
    bar = (Rectangle){ 672.0f, 46.0f, 150.0f, 10.0f };
    DrawRectangleRounded(bar, 0.5f, 10, (Color){ 255, 255, 255, 20 });
    if (sp > 0.03f) {
        DrawRectangleRounded((Rectangle){ bar.x, bar.y, bar.width * sp, bar.height }, 0.5f, 10, C_ACCENT);
    }

    /* 底部操作提示 */
    draw_text_center(T("P 暂停    R 重开    Q 退出", "P pause    R restart    Q quit"),
                     (float)(WINDOW_H - 28), 17.0f, 1.0f, C_TEXT_DIM);
}

static void draw_pause_overlay(void)
{
    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, (Color){ 8, 10, 16, 175 });
    draw_text_center(T("已暂停", "PAUSED"), 300.0f, 54.0f, 4.0f, C_TEXT);
    draw_text_center(T("按 P 或空格继续", "press P or SPACE to resume"), 380.0f, 21.0f, 2.0f, C_TEXT_DIM);
}

static void draw_dead_overlay(const Game *g)
{
    Rectangle p = { (float)WINDOW_W * 0.5f - 230.0f, 250.0f, 460.0f, 268.0f };
    char buf[64];

    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, (Color){ 6, 8, 13, 190 });

    DrawRectangleRounded((Rectangle){ p.x + 4.0f, p.y + 7.0f, p.width, p.height },
                         0.075f, 10, (Color){ 0, 0, 0, 100 });
    DrawRectangleRounded(p, 0.075f, 10, (Color){ 28, 32, 45, 250 });
    DrawRectangleRoundedLinesEx(p, 0.075f, 10, 2.0f, (Color){ 72, 80, 104, 255 });

    draw_text_center(T("游戏结束", "GAME OVER"), p.y + 30.0f, 44.0f, 3.0f, (Color){ 255, 118, 118, 255 });

    snprintf(buf, sizeof(buf), "%s   %d", T("本次得分", "SCORE"), g->score);
    draw_text_center(buf, p.y + 104.0f, 26.0f, 2.0f, C_TEXT);

    snprintf(buf, sizeof(buf), "%s   %d", T("历史最高", "BEST"), g->best);
    draw_text_center(buf, p.y + 146.0f, 26.0f, 2.0f, (Color){ 150, 200, 255, 255 });

    draw_text_center(T("按 R 重新开始        Q 退出", "R restart        Q quit"),
                     p.y + 208.0f, 19.0f, 1.0f, C_TEXT_DIM);
}

/* ============================== 输入 ============================== */

static void handle_input(Game *g, bool *running)
{
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) { *running = false; return; }
    if (IsKeyPressed(KEY_R)) { reset_game(g); return; }
    if (g->dead) return;

    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_SPACE)) g->paused = !g->paused;
    if (g->paused) return;

    if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W)) queue_turn(g, DIR_UP);
    else if (IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_S)) queue_turn(g, DIR_DOWN);
    else if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) queue_turn(g, DIR_LEFT);
    else if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) queue_turn(g, DIR_RIGHT);
}

/* ============================== 主程序 ============================== */

int main(void)
{
    Game g;
    bool running = true;
    int  shot_at = 0, frame_no = 0;
    const char *env_shot;

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_W, WINDOW_H, "Snake | raylib");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);                 /* 退出键自己处理 */
    if (!IsWindowReady()) return 1;

    g_font = load_ui_font();

    InitAudioDevice();
    g_audio = IsAudioDeviceReady();
    if (g_audio) {
        g_snd_eat = make_tone(660.0f, 1180.0f, 0.09f, 0.35f);
        g_snd_dead = make_tone(420.0f, 90.0f, 0.45f, 0.40f);
    }

    memset(&g, 0, sizeof(g));
    g.best = load_best();
    reset_game(&g);

    /* 无人值守渲染自检：SNAKE_SHOT=帧号，到该帧截图并退出 */
    env_shot = getenv("SNAKE_SHOT");
    if (env_shot != NULL) shot_at = atoi(env_shot);

    while (running && !WindowShouldClose()) {
        float dt = GetFrameTime();

        if (dt > 0.1f) dt = 0.1f;         /* 防止窗口拖动后一帧跳太远 */
        g.time += dt;

        handle_input(&g, &running);
        update_game(&g, dt);
        update_effects(&g, dt);

        BeginDrawing();
        ClearBackground(C_BG_BOTTOM);
        draw_background();
        draw_board();

        {
            Vector2 off = { 0.0f, 0.0f };
            if (g.shake > 0.0f) {
                float amp = g.shake * 9.0f;
                off.x = (float)GetRandomValue(-100, 100) / 100.0f * amp;
                off.y = (float)GetRandomValue(-100, 100) / 100.0f * amp;
            }

            if (off.x != 0.0f || off.y != 0.0f) {
                /* 死亡震动：棋盘内容整体偏移 */
                Camera2D cam = { 0 };
                cam.offset = off;
                cam.zoom = 1.0f;
                BeginMode2D(cam);
                draw_food(&g);
                draw_snake(&g);
                draw_particles(&g);
                draw_popups(&g);
                EndMode2D();
            } else {
                draw_food(&g);
                draw_snake(&g);
                draw_particles(&g);
                draw_popups(&g);
            }
        }

        draw_hud(&g);
        if (g.paused && !g.dead) draw_pause_overlay();
        if (g.dead) draw_dead_overlay(&g);
        EndDrawing();

        if (shot_at > 0 && ++frame_no >= shot_at) {
            TakeScreenshot("snake_shot.png");
            running = false;
        }
    }

    if (g_audio) {
        if (g_snd_eat.frameCount > 0)  UnloadSound(g_snd_eat);
        if (g_snd_dead.frameCount > 0) UnloadSound(g_snd_dead);
    }
    if (g_font.texture.id != GetFontDefault().texture.id) UnloadFont(g_font);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
