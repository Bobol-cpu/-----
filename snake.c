#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

#define W      30             /* 棋盘宽 */
#define H      20             /* 棋盘高 */
#define MAXLEN (W * H)        /* 蛇最长 */
#define DELAY  120            /* 每格耗时(ms) */

typedef struct { int x, y; } Pt;

static Pt   body[MAXLEN];     /* body[0]=头 */
static int  len, dir;         /* dir: 0上 1右 2下 3左 */
static int  fx, fy, score, over;
static char scr[H][W];        /* 每帧的字符网格 */

static const int DX[4] = { 0, 1, 0, -1 };
static const int DY[4] = { -1, 0, 1, 0 };

/* ------------------------- 控制台 ------------------------- */
static void gotoxy(int x, int y)
{
#ifdef _WIN32
    COORD p = { (SHORT)x, (SHORT)y };
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
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
#endif
}

#ifdef _WIN32
static void term_init(void) { SetConsoleOutputCP(CP_UTF8); }
static void term_done(void) { }
#else
static struct termios saved;
static void term_init(void)               /* 关行缓冲与回显，按键即时可读 */
{
    struct termios t;
    tcgetattr(0, &saved);
    t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
}
static void term_done(void) { tcsetattr(0, TCSANOW, &saved); }
#endif

/* 读键：-1 无, 0-3 方向, 4 退出, 5 重开 */
static int read_key(void)
{
    int c;
#ifdef _WIN32
    if (!_kbhit()) return -1;
    c = _getch();
    if (c == 0 || c == 0xE0) {                /* 方向键两字节：72上 77右 80下 75左 */
        int s = _getch();
        return s == 72 ? 0 : s == 77 ? 1 : s == 80 ? 2 : s == 75 ? 3 : -1;
    }
#else
    {
        unsigned char ch;
        if (read(0, &ch, 1) != 1) return -1;
        c = (int)ch;
        if (c == 27) {                        /* ESC[A~D；单独 ESC 视为退出 */
            unsigned char a = 0, b = 0;
            if (read(0, &a, 1) != 1 || read(0, &b, 1) != 1 || a != '[') return 4;
            return b == 'A' ? 0 : b == 'C' ? 1 : b == 'B' ? 2 : b == 'D' ? 3 : -1;
        }
    }
#endif
    c = tolower(c);
    if (c == 'w') return 0;
    if (c == 'd') return 1;
    if (c == 's') return 2;
    if (c == 'a') return 3;
    if (c == 'q') return 4;
    if (c == 'r') return 5;
    return -1;
}

/* ------------------------- 渲染 ------------------------- */
static void draw(void)
{
    static char buf[2048];
    char *p = buf;
    int y, i;

    memset(scr, ' ', sizeof scr);                    /* 1) 只画格子内容 */
    for (i = len - 1; i >= 0; --i) scr[body[i].y][body[i].x] = i ? 'o' : '@';
    scr[fy][fx] = '*';

    *p++ = '+'; memset(p, '-', W); p += W; *p++ = '+'; *p++ = '\n';   /* 2) 加边框 */
    for (y = 0; y < H; ++y) {
        *p++ = '|'; memcpy(p, scr[y], W); p += W;
        *p++ = '|'; *p++ = '\n';
    }
    *p++ = '+'; memset(p, '-', W); p += W; *p++ = '+'; *p++ = '\n';

    if (over) p += sprintf(p, " 游戏结束！得分 %d   按 R 重开，按 Q 退出\n", score);
    else      p += sprintf(p, " 得分 %d   长度 %d\n", score, len);

    gotoxy(0, 0);                                    /* 3) 归位后一次写出 */
    fwrite(buf, 1, (size_t)(p - buf), stdout);
    fflush(stdout);
}

/* ------------------------- 逻辑 ------------------------- */
static int free_at(int x, int y)
{
    int i;
    for (i = 0; i < len; ++i)
        if (body[i].x == x && body[i].y == y) return 0;
    return 1;
}

static void reset(void)
{
    int i;
    len = 3; dir = 1; score = 0; over = 0;
    for (i = 0; i < len; ++i) { body[i].x = W / 2 - i; body[i].y = H / 2; }
}

static int put_food(void)                 /* 随机取点，撞蛇就重试；满盘返回 0 */
{
    int t;
    for (t = 0; t < 500; ++t) {
        int x = rand() % W, y = rand() % H;
        if (free_at(x, y)) { fx = x; fy = y; return 1; }
    }
    return 0;
}

/* ------------------------- 主程序 ------------------------- */
int main(void)
{
    int i, k, t, el, nx, ny, eat, limit, hit;

    term_init();
    atexit(term_done);
    srand((unsigned)time(NULL));

    for (;;) {                                       /* 每轮 = 一局 */
        reset();
        put_food();
        draw();

        while (!over) {
            k = -1;                                  /* 等一帧，顺便收按键 */
            for (el = 0; el < DELAY; el += 5) {
                t = read_key();
                if (t == 4) return 0;
                if (t >= 0 && t <= 3 && t != (dir + 2) % 4) k = t;
                sleep_ms(5);
            }
            if (k >= 0) dir = k;

            nx  = body[0].x + DX[dir];               /* 先判死，再移动 */
            ny  = body[0].y + DY[dir];
            eat = (nx == fx && ny == fy);

            if (nx < 0 || nx >= W || ny < 0 || ny >= H) { over = 1; break; }

            limit = eat ? len : len - 1;             /* 不吃时蛇尾会移开，不算撞 */
            hit = 0;
            for (i = 1; i < limit; ++i)
                if (body[i].x == nx && body[i].y == ny) { hit = 1; break; }
            if (hit) { over = 1; break; }

            if (eat && len < MAXLEN) ++len;          /* 头插尾删 */
            for (i = len - 1; i > 0; --i) body[i] = body[i - 1];
            body[0].x = nx;
            body[0].y = ny;

            if (eat) { score += 10; if (!put_food()) over = 1; }
            draw();
        }

        draw();                                      /* 补一帧显示"游戏结束" */
        while ((k = read_key()) != 5) {
            if (k == 4) return 0;
            sleep_ms(30);
        }
    }
}
