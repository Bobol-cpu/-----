/* ============================================================
 *  贪吃蛇（简化版）
 *  编译：gcc snake_simple.c -o snake
 *  操作：WASD / 方向键 移动   Q 退出   R 重开
 *
 *  核心思路与完整版一致：
 *    1) 棋盘是纯逻辑的 W×H 网格，越界即撞墙（数值判界，无墙实体）
 *    2) 蛇用数组当队列：body[0] 是头，整体后移一格再把新头放到 0
 *    3) 字符终端像打字机：整帧拼进缓冲区，归位到左上角后一次写出
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

#define W      30             /* 棋盘宽（格） */
#define H      20             /* 棋盘高（格） */
#define MAXLEN (W * H)        /* 蛇的最大长度 */
#define DELAY  120            /* 每走一格耗时(ms) */

typedef struct { int x, y; } Pt;

static Pt  body[MAXLEN];      /* body[0]=蛇头, body[len-1]=蛇尾 */
static int len, dir;          /* dir: 0上 1右 2下 3左 */
static int fx, fy;            /* 食物 */
static int score, over;       /* 得分、是否结束 */

static const int DX[4] = { 0, 1, 0, -1 };
static const int DY[4] = { -1, 0, 1, 0 };

/* ======================= 控制台 ======================= */

static void gotoxy(int x, int y)
{
#ifdef _WIN32
    COORD p;
    p.X = (SHORT)x;
    p.Y = (SHORT)y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), p);
#else
    printf("\033[%d;%dH", y + 1, x + 1);
#endif
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

#ifdef _WIN32
static void term_init(void) { SetConsoleOutputCP(CP_UTF8); }
static void term_done(void) { }
#else
static struct termios old_term;
static void term_init(void)              /* 关行缓冲与回显，让按键即时可读 */
{
    struct termios t;
    tcgetattr(STDIN_FILENO, &old_term);
    t = old_term;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
static void term_done(void) { tcsetattr(STDIN_FILENO, TCSANOW, &old_term); }
#endif

/* 读一个键：-1 无, 0-3 方向, 4 退出, 5 重开 */
static int read_key(void)
{
    int c;
#ifdef _WIN32
    if (!_kbhit()) return -1;
    c = _getch();
    if (c == 0 || c == 0xE0) {           /* 方向键是两字节，前导符后才是扫描码 */
        switch (_getch()) {
            case 72: return 0;
            case 77: return 1;
            case 80: return 2;
            case 75: return 3;
            default: return -1;
        }
    }
#else
    {
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) return -1;
        c = (int)ch;
        if (c == 27) {                   /* ESC 或 ESC[A~D 转义序列 */
            unsigned char a = 0, b = 0;
            if (read(STDIN_FILENO, &a, 1) == 1 && a == '[' &&
                read(STDIN_FILENO, &b, 1) == 1) {
                switch (b) {
                    case 'A': return 0;
                    case 'C': return 1;
                    case 'B': return 2;
                    case 'D': return 3;
                }
            }
            return 4;
        }
    }
#endif
    switch (c) {
        case 'w': case 'W': return 0;
        case 'd': case 'D': return 1;
        case 's': case 'S': return 2;
        case 'a': case 'A': return 3;
        case 'q': case 'Q': return 4;
        case 'r': case 'R': return 5;
        default:            return -1;
    }
}

/* ======================= 渲染 ======================= */

/* 整帧拼进缓冲区 → 归位 → 一次写出，避免闪烁 */
static void draw(void)
{
    static char buf[2048];
    char *p = buf;
    int x, y, i;

    *p++ = '+';
    for (x = 0; x < W; ++x) *p++ = '-';
    *p++ = '+'; *p++ = '\n';

    for (y = 0; y < H; ++y) {
        *p++ = '|';
        for (x = 0; x < W; ++x) {
            char ch = ' ';
            if (body[0].x == x && body[0].y == y)      ch = '@';   /* 蛇头 */
            else if (x == fx && y == fy)               ch = '*';   /* 食物 */
            else for (i = 1; i < len; ++i)
                if (body[i].x == x && body[i].y == y) { ch = 'o'; break; }
            *p++ = ch;
        }
        *p++ = '|'; *p++ = '\n';
    }

    *p++ = '+';
    for (x = 0; x < W; ++x) *p++ = '-';
    *p++ = '+'; *p++ = '\n';

    /* 状态栏就是棋盘下方那一行，每帧重写，所以不会有残留 */
    if (over) p += sprintf(p, " 游戏结束！得分 %d   按 R 重开，按 Q 退出\n", score);
    else      p += sprintf(p, " 得分 %d   长度 %d\n", score, len);

    gotoxy(0, 0);
    fwrite(buf, 1, (size_t)(p - buf), stdout);
    fflush(stdout);
}

/* ======================= 逻辑 ======================= */

static void reset(void)
{
    len   = 3;
    dir   = 1;                        /* 朝右 */
    score = 0;
    over  = 0;
    body[0].x = W / 2;     body[0].y = H / 2;
    body[1].x = W / 2 - 1; body[1].y = H / 2;
    body[2].x = W / 2 - 2; body[2].y = H / 2;
}

/* 在空格上随机放食物；棋盘已满返回 0。
 * 用"随机取点、撞上蛇就重试"的写法，比枚举空格短得多 */
static int put_food(void)
{
    int t, x, y, i;
    for (t = 0; t < 500; ++t) {
        x = rand() % W;
        y = rand() % H;
        for (i = 0; i < len; ++i)
            if (body[i].x == x && body[i].y == y) break;
        if (i == len) { fx = x; fy = y; return 1; }
    }
    return 0;
}

/* ======================= 主程序 ======================= */

int main(void)
{
    int i, k, elapsed, nx, ny, eat, hit, limit;

    term_init();
    atexit(term_done);                /* 异常退出也能还原终端 */
    srand((unsigned)time(NULL));

    for (;;) {                        /* 每轮 = 一局 */
        reset();
        put_food();
        draw();

        while (!over) {
            /* --- 等一帧，期间不停轮询按键 --- */
            k = -1;
            elapsed = 0;
            while (elapsed < DELAY) {
                int t = read_key();
                if (t == 4) return 0;                       /* Q：退出 */
                if (t >= 0 && t <= 3 && t != (dir + 2) % 4) /* 禁止 180° 掉头 */
                    k = t;
                sleep_ms(5);
                elapsed += 5;
            }
            if (k >= 0) dir = k;

            /* --- 算新蛇头，先判死再移动 --- */
            nx  = body[0].x + DX[dir];
            ny  = body[0].y + DY[dir];
            eat = (nx == fx && ny == fy);

            if (nx < 0 || nx >= W || ny < 0 || ny >= H) { over = 1; break; }

            limit = eat ? len : len - 1;   /* 不吃食物时蛇尾会移开，不算撞 */
            hit = 0;
            for (i = 1; i < limit; ++i)
                if (body[i].x == nx && body[i].y == ny) { hit = 1; break; }
            if (hit) { over = 1; break; }

            /* --- 头插尾删：先扩容（吃到食物时），再从尾向头搬一格 --- */
            if (eat && len < MAXLEN) ++len;
            for (i = len - 1; i > 0; --i) body[i] = body[i - 1];
            body[0].x = nx;
            body[0].y = ny;

            if (eat) {
                score += 10;
                if (!put_food()) over = 1;   /* 棋盘被填满 */
            }
            draw();
        }

        draw();                       /* 补一帧，让状态栏显示"游戏结束" */

        for (;;) {                    /* 结算：等 R 或 Q */
            int t = read_key();
            if (t == 4) return 0;
            if (t == 5) break;
            sleep_ms(30);
        }
    }
}
