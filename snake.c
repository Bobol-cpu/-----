#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
#else
# include <termios.h>
# include <unistd.h>
#endif

/* ------------------------------ 可调参数 ------------------------------ */
#define BOARD_W     30                     /* 棋盘宽度（格） */
#define BOARD_H     20                     /* 棋盘高度（格） */
#define MAX_LEN     (BOARD_W * BOARD_H)    /* 蛇的最大长度 */
#define START_DELAY 140                    /* 初始速度：每走一格耗时(ms)，越小越快 */
#define MIN_DELAY   45                     /* 最快速度上限 */
#define SPEED_STEP  6                      /* 每吃一个食物加快的毫秒数 */
#define TICK_MS     5                      /* 输入轮询间隔(ms) */

/* ------------------------------ 方向定义 ------------------------------ */
enum { DIR_UP = 0, DIR_RIGHT, DIR_DOWN, DIR_LEFT };

static const int DX[4] = {  0,  1,  0, -1 };
static const int DY[4] = { -1,  0,  1,  0 };

/* ------------------------------ 按键定义 ------------------------------ */
enum {
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_PAUSE, KEY_RESTART, KEY_QUIT
};

/* ------------------------------ 数据结构 ------------------------------ */
typedef struct {
    int x, y;
} Cell;

typedef struct {
    Cell body[MAX_LEN];   /* body[0] 是蛇头，body[len-1] 是蛇尾 */
    int  len;             /* 当前长度 */
    int  dir;             /* 当前方向 */
} Snake;

/* ====================== 终端 / 控制台相关工具函数 ====================== */

#ifndef _WIN32
static struct termios g_old_term;
#endif

static void cursor_visible(int visible);   /* 前置声明 */

/* 初始化控制台：Windows 下切换为 UTF-8 输出代码页；
 * Linux 下把终端设为「非规范 + 无回显」模式，实现按键即时读取 */
static void console_init(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   /* 否则中文提示在 GBK 代码页控制台里会乱码 */
#else
    struct termios t;
    tcgetattr(STDIN_FILENO, &g_old_term);
    t = g_old_term;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

/* 程序退出前恢复终端状态并重新显示光标 */
static void console_restore(void)
{
#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old_term);
#endif
    cursor_visible(1);
}

/* 把光标移动到指定坐标（左上角为 0,0），避免整屏清屏造成的闪烁 */
static void gotoxy(int x, int y)
{
#ifdef _WIN32
    COORD pos;
    pos.X = (SHORT)x;
    pos.Y = (SHORT)y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
#else
    printf("\033[%d;%dH", y + 1, x + 1);
#endif
}

/* 显示 / 隐藏光标 */
static void cursor_visible(int visible)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    if (GetConsoleCursorInfo(h, &info)) {
        info.bVisible = visible ? TRUE : FALSE;
        SetConsoleCursorInfo(h, &info);
    }
#else
    fputs(visible ? "\033[?25h" : "\033[?25l", stdout);
    fflush(stdout);
#endif
}

/* 毫秒级睡眠 */
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

/* 清屏并把光标复位到左上角 */
static void clear_screen(void)
{
#ifdef _WIN32
    system("cls");
#else
    fputs("\033[2J", stdout);
#endif
    gotoxy(0, 0);
}

/* 非阻塞读取一个按键，返回统一的按键编号 */
static int key_poll(void)
{
    int c = KEY_NONE;

#ifdef _WIN32
    if (!_kbhit()) return KEY_NONE;
    c = _getch();
    if (c == 0 || c == 0xE0) {          /* 方向键等扩展键的前导字节 */
        switch (_getch()) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            default: return KEY_NONE;
        }
    }
#else
    {
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) return KEY_NONE;
        c = (int)ch;
        if (c == 27) {                  /* ESC 或方向键转义序列 */
            unsigned char s1 = 0, s2 = 0;
            if (read(STDIN_FILENO, &s1, 1) == 1 &&
                read(STDIN_FILENO, &s2, 1) == 1 && s1 == '[') {
                switch (s2) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                }
            }
            return KEY_QUIT;            /* 单独按下 ESC：退出 */
        }
    }
#endif

    switch (c) {
        case 'w': case 'W':          return KEY_UP;
        case 's': case 'S':          return KEY_DOWN;
        case 'a': case 'A':          return KEY_LEFT;
        case 'd': case 'D':          return KEY_RIGHT;
        case 'p': case 'P': case ' ': return KEY_PAUSE;
        case 'r': case 'R':          return KEY_RESTART;
        case 'q': case 'Q':          return KEY_QUIT;
        default:                     return KEY_NONE;
    }
}

/* ============================== 游戏逻辑 ============================== */

/* 把方向编号换算成按键编号 */
static int dir_from_key(int key)
{
    switch (key) {
        case KEY_UP:    return DIR_UP;
        case KEY_RIGHT: return DIR_RIGHT;
        case KEY_DOWN:  return DIR_DOWN;
        default:        return DIR_LEFT;
    }
}

/* 反方向 */
static int opposite(int dir)
{
    return (dir + 2) % 4;
}

/* 初始化蛇：长度 3，位于棋盘中央，朝右 */
static void init_snake(Snake *s)
{
    s->len = 3;
    s->dir = DIR_RIGHT;
    s->body[0].x = BOARD_W / 2;
    s->body[0].y = BOARD_H / 2;
    s->body[1].x = s->body[0].x - 1;
    s->body[1].y = s->body[0].y;
    s->body[2].x = s->body[0].x - 2;
    s->body[2].y = s->body[0].y;
}

/* 在空白格中随机放置食物；棋盘已满时返回 0 */
static int place_food(const Snake *s, Cell *food)
{
    static Cell free_cells[MAX_LEN];
    int n = 0;
    int x, y, i;

    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            int occupied = 0;
            for (i = 0; i < s->len; ++i) {
                if (s->body[i].x == x && s->body[i].y == y) {
                    occupied = 1;
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

    if (n == 0) return 0;               /* 没有空格，通关 */
    *food = free_cells[rand() % n];
    return 1;
}

/* 把格式化文本追加到绘图缓冲区（带边界保护） */
static void put(char **p, size_t *left, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(*p, *left, fmt, ap);
    va_end(ap);

    if (n < 0) return;
    if ((size_t)n >= *left) {           /* 缓冲区不够：截断到末尾 */
        *p += *left - 1;
        *left = 1;
    } else {
        *p += (size_t)n;
        *left -= (size_t)n;
    }
}

/* 一次性画出整个画面：先拼进缓冲区，再一次性输出，杜绝闪烁 */
static void draw(const Snake *s, Cell food, int score, int best, int delay, int paused)
{
    static char buf[8192];
    char *p = buf;
    size_t left = sizeof(buf);
    int x, y, i;

    /* 上边框 */
    put(&p, &left, "+");
    for (x = 0; x < BOARD_W; ++x) put(&p, &left, "-");
    put(&p, &left, "+\n");

    /* 棋盘主体 + 右侧状态栏 */
    for (y = 0; y < BOARD_H; ++y) {
        put(&p, &left, "|");
        for (x = 0; x < BOARD_W; ++x) {
            char c = ' ';
            if (s->body[0].x == x && s->body[0].y == y) {
                c = '@';                                   /* 蛇头 */
            } else if (food.x == x && food.y == y) {
                c = '*';                                   /* 食物 */
            } else {
                for (i = 1; i < s->len; ++i) {
                    if (s->body[i].x == x && s->body[i].y == y) {
                        c = 'o';                               /* 蛇身 */
                        break;
                    }
                }
            }
            put(&p, &left, "%c", c);
        }
        put(&p, &left, "|");

        switch (y) {
            case 0: put(&p, &left, "   得分 : %d", score);            break;
            case 1: put(&p, &left, "   长度 : %d", s->len);           break;
            case 2: put(&p, &left, "   最高 : %d", best);             break;
            case 3: put(&p, &left, "   速度 : %d ms/格", delay);      break;
            case 5: put(&p, &left, "   ↑↓←→ / WASD  移动");          break;
            case 6: put(&p, &left, "   P / 空格      暂停");          break;
            case 7: put(&p, &left, "   Q / ESC       退出");          break;
        }
        put(&p, &left, "\n");
    }

    /* 下边框 */
    put(&p, &left, "+");
    for (x = 0; x < BOARD_W; ++x) put(&p, &left, "-");
    put(&p, &left, "+\n");

    if (paused) put(&p, &left, "   *** 已暂停，按 P 或空格继续 ***\n");
    else        put(&p, &left, "\n");   /* 占位，擦掉上一帧可能残留的提示 */

    gotoxy(0, 0);
    fwrite(buf, 1, (size_t)(p - buf), stdout);
    fflush(stdout);
}

/* ================================ 主程序 ================================ */

int main(void)
{
    Snake s;
    Cell  food;
    int   best = 0;

    console_init();
    atexit(console_restore);            /* 无论怎么退出都能还原终端 */
    cursor_visible(0);
    clear_screen();
    srand((unsigned)time(NULL));

    for (;;) {                          /* 每轮 = 一局游戏 */
        int score = 0;
        int delay = START_DELAY;
        int paused = 0, over = 0, win = 0, quit = 0;
        int pending_dir;

        init_snake(&s);
        pending_dir = s.dir;
        place_food(&s, &food);
        draw(&s, food, score, best, delay, 0);

        /* ---------------------------- 单局主循环 ---------------------------- */
        while (!over && !quit) {
            int elapsed = 0;

            /* 等待一帧的时间，期间不断轮询按键 */
            while (elapsed < delay) {
                int k = key_poll();

                if (k == KEY_QUIT) {
                    quit = 1;
                    break;
                } else if (k == KEY_PAUSE) {
                    paused = !paused;
                } else if (k >= KEY_UP && k <= KEY_RIGHT) {
                    int d = dir_from_key(k);
                    if (d != opposite(s.dir)) pending_dir = d;   /* 禁止 180° 掉头 */
                }

                sleep_ms(TICK_MS);
                elapsed += TICK_MS;
            }
            if (quit) break;

            /* 暂停：画面照常刷新，但不推进蛇 */
            if (paused) {
                draw(&s, food, score, best, delay, 1);
                continue;
            }

            s.dir = pending_dir;

            {
                Cell nh;
                nh.x = s.body[0].x + DX[s.dir];
                nh.y = s.body[0].y + DY[s.dir];

                if (nh.x < 0 || nh.x >= BOARD_W || nh.y < 0 || nh.y >= BOARD_H) {
                    over = 1;                                   /* 撞墙 */
                } else {
                    int eat   = (nh.x == food.x && nh.y == food.y);
                    int limit = eat ? s.len : s.len - 1;        /* 不吃食物时蛇尾会移开 */
                    int i;

                    for (i = 1; i < limit; ++i) {
                        if (s.body[i].x == nh.x && s.body[i].y == nh.y) {
                            over = 1;                               /* 咬到自己 */
                            break;
                        }
                    }

                    if (!over) {
                        /* 整体后移一格，再把新蛇头放到最前面 */
                        for (i = s.len - 1; i > 0; --i) s.body[i] = s.body[i - 1];
                        s.body[0] = nh;

                        if (eat) {
                            if (s.len < MAX_LEN) ++s.len;
                            score += 10;
                            if (score > best) best = score;

                            delay -= SPEED_STEP;                    /* 越吃越快 */
                            if (delay < MIN_DELAY) delay = MIN_DELAY;

                            if (!place_food(&s, &food)) {           /* 棋盘被填满 */
                                win = 1;
                                over = 1;
                            }
                        }
                    }
                }
            }

            draw(&s, food, score, best, delay, 0);
        }

        if (quit) break;                /* 玩家主动退出 */

        /* ---------------------------- 结算界面 ---------------------------- */
        gotoxy(0, BOARD_H + 2);
        if (win) {
            printf("  *** 恭喜通关！蛇填满了整个棋盘，得分 %d ***\n", score);
        } else {
            printf("  *** 游戏结束！本次得分 %d，历史最高 %d ***\n", score, best);
        }
        printf("  按 R 重新开始，按 Q 退出 ...\n");
        fflush(stdout);

        for (;;) {
            int k = key_poll();
            if (k == KEY_QUIT) { quit = 1; break; }
            if (k == KEY_RESTART) break;
            sleep_ms(30);
        }
        if (quit) break;

        clear_screen();
    }

    gotoxy(0, BOARD_H + 4);
    printf("  感谢游玩，再见！\n");
    return 0;
}
