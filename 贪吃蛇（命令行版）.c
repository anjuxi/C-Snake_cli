/* 贪吃蛇（命令行版）
 * Windows 控制台小游戏，用方向键或 WASD 控制。
 * 编译：gcc "贪吃蛇（命令行版）.c" -O2 -o snake.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <conio.h>
#include <windows.h>

#define GAME_WIDTH 80       // 棋盘宽，窗口按它加上右侧信息栏。
#define GAME_HEIGHT 22
#define INITIAL_LENGTH 4
#define INITIAL_SPEED 150   // 毫秒/帧
#define MIN_SPEED 50        // 帧间隔的上下限，免得调到没法玩。
#define MAX_SPEED 300
#define SPEED_STEP 10
#define FOOD_SCORE 10

#define PANEL_X (GAME_WIDTH + 4)
#define WINDOW_COLS (GAME_WIDTH + 40)
#define WINDOW_LINES (GAME_HEIGHT + 8)

#define HIGH_SCORE_FILE "snake_highscore.txt"

// 控制台颜色：7 白、11 青、10 绿、2 暗绿、12 红、14 黄。
#define CLR_DEFAULT 7
#define CLR_BORDER 11
#define CLR_HEAD 10
#define CLR_BODY 2
#define CLR_FOOD 12
#define CLR_TITLE 14
#define CLR_WARN 12
#define CLR_MENU 10

enum Direction {
    STOP = 0,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point body[GAME_WIDTH * GAME_HEIGHT];
    int length;
    enum Direction dir;
    enum Direction next_dir; // 缓存下一次转向，一帧只转一次。
} Snake;

typedef struct {
    Snake snake;
    Point food;
    Point old_tail; // 上一帧的尾巴，增量重绘时擦掉。
    bool food_new;
    bool game_over;
    bool win;
    bool quit;
    bool grew;      // 这一帧吃到食物，尾巴不动。
    bool boost;
    int score;
    int high_score;
    int speed;
} Game;

static void setupConsole(void);
static void loadHighScore(Game *game);
static void initGame(Game *game);
static void showStartScreen(const Game *game);
static void drawBorder(void);
static void drawInfoPanel(void);
static void drawSnakeFull(const Game *game);
static void drawInfo(const Game *game);
static void processInput(Game *game);
static void updateGame(Game *game);
static void drawFrame(Game *game);
static void checkBoost(Game *game);
static void waitWithInput(Game *game, int milliseconds);
static void saveHighScore(const Game *game);
static void showGameOver(const Game *game);

static void setColor(int color);
static void gotoxy(int x, int y);
static void putCell(int x, int y, int color, char ch);
static void hideCursor(void);
static void showCursor(void);
static void tryTurn(Game *game, enum Direction next);
static bool snakeOccupies(const Game *game, int x, int y);
static bool spawnFood(Game *game);
static void drawFood(const Game *game);

int main(void) {
    setupConsole();
    hideCursor();

    Game game;
    loadHighScore(&game);
    initGame(&game);

    showStartScreen(&game);
    system("cls");

    drawBorder();
    drawInfoPanel();
    drawSnakeFull(&game);
    drawFood(&game);
    game.food_new = false;
    drawInfo(&game);

    while (!game.game_over) {
        processInput(&game);
        if (game.game_over) {
            break;
        }

        updateGame(&game);
        if (game.game_over) {
            break;
        }

        drawFrame(&game);
        drawInfo(&game);
        checkBoost(&game);

        // 长按加速时延减半，最低留 10ms。
        int sleep_time = game.boost ? game.speed / 2 : game.speed;
        if (sleep_time < 10) {
            sleep_time = 10;
        }
        waitWithInput(&game, sleep_time);
    }

    saveHighScore(&game);
    showCursor();
    showGameOver(&game);

    return 0;
}

// 设置控制台编码、标题和窗口大小。
static void setupConsole(void) {
    // 统一 UTF-8，省得中文乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"贪吃蛇游戏（命令行版）");

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD target_buffer = {WINDOW_COLS, WINDOW_LINES};
    SMALL_RECT target_window = {0, 0, WINDOW_COLS - 1, WINDOW_LINES - 1};

    // 缓冲区不够就先扩容，否则缩窗口会失败。
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(handle, &info)) {
        if (info.dwSize.X < WINDOW_COLS || info.dwSize.Y < WINDOW_LINES) {
            COORD bigger;
            bigger.X = info.dwSize.X < WINDOW_COLS ? WINDOW_COLS : info.dwSize.X;
            bigger.Y = info.dwSize.Y < WINDOW_LINES ? WINDOW_LINES : info.dwSize.Y;
            SetConsoleScreenBufferSize(handle, bigger);
        }
    }

    // 先定窗口再收缓冲区；API 不生效就退回 mode 命令。
    if (!SetConsoleWindowInfo(handle, TRUE, &target_window) ||
        !SetConsoleScreenBufferSize(handle, target_buffer)) {
        char command[64];
        snprintf(command, sizeof(command), "mode con cols=%d lines=%d", WINDOW_COLS, WINDOW_LINES);
        system(command);
    }
}

// 读最高分，文件不存在或内容不对就当 0。
static void loadHighScore(Game *game) {
    game->high_score = 0;

    FILE *file = fopen(HIGH_SCORE_FILE, "r");
    if (file == NULL) {
        // 第一次运行没有存档。
        return;
    }

    int value = 0;
    int max_score = GAME_WIDTH * GAME_HEIGHT * FOOD_SCORE;
    if (fscanf(file, "%d", &value) == 1 && value >= 0 && value <= max_score) {
        game->high_score = value;
    }
    fclose(file);
}

static void initGame(Game *game) {
    // 不设种子，每局食物位置都一样。
    srand((unsigned)time(NULL));

    Snake *snake = &game->snake;
    snake->length = INITIAL_LENGTH;
    snake->dir = RIGHT;
    snake->next_dir = RIGHT;

    // 蛇头在中间，身体往左摆。
    for (int i = 0; i < snake->length; i++) {
        snake->body[i].x = GAME_WIDTH / 2 - i;
        snake->body[i].y = GAME_HEIGHT / 2;
    }

    game->old_tail.x = -1;
    game->old_tail.y = -1;
    game->food_new = false;
    game->game_over = false;
    game->win = false;
    game->quit = false;
    game->grew = false;
    game->boost = false;
    game->score = 0;
    game->speed = INITIAL_SPEED;

    spawnFood(game);
}

static void showStartScreen(const Game *game) {
    system("cls");

    setColor(CLR_TITLE);
    printf("\n");
    printf("  ____              _        \n");
    printf(" / ___| _ __   __ _| | _____ \n");
    printf(" \\___ \\| '_ \\ / _` | |/ / _ \\\n");
    printf("  ___) | | | | (_| |   <  __/\n");
    printf(" |____/|_| |_|\\__,_|_|\\_\\___|\n");
    printf("\n");

    setColor(CLR_BORDER);
    printf("              贪吃蛇游戏（命令行版）\n\n");

    setColor(CLR_DEFAULT);
    printf("  玩法：吃到食物加 %d 分，撞墙或撞到自己就结束。\n", FOOD_SCORE);
    printf("  操作：方向键 / WASD 移动，按住方向键临时加速。\n");
    printf("        P 暂停，+ / - 调整基础速度，X 退出游戏。\n\n");

    setColor(CLR_TITLE);
    printf("  最高纪录：%d\n\n", game->high_score);

    setColor(CLR_MENU);
    printf("  按任意键开始......");
    setColor(CLR_DEFAULT);

    _getch();
}

// 边框用半角字符，全角会把宽度撑成两倍。
static void drawBorder(void) {
    setColor(CLR_BORDER);

    gotoxy(0, 0);
    putchar('+');
    for (int i = 0; i < GAME_WIDTH; i++) {
        putchar('-');
    }
    putchar('+');

    gotoxy(0, GAME_HEIGHT + 1);
    putchar('+');
    for (int i = 0; i < GAME_WIDTH; i++) {
        putchar('-');
    }
    putchar('+');

    for (int y = 1; y <= GAME_HEIGHT; y++) {
        putCell(0, y, CLR_BORDER, '|');
        putCell(GAME_WIDTH + 1, y, CLR_BORDER, '|');
    }

    setColor(CLR_DEFAULT);
}

// 信息栏里不变的文字，开局画一次。
static void drawInfoPanel(void) {
    setColor(CLR_TITLE);
    gotoxy(PANEL_X, 2);
    printf("贪吃蛇（命令行版）");

    setColor(CLR_DEFAULT);
    gotoxy(PANEL_X, 10);
    printf("操作说明:");
    gotoxy(PANEL_X, 11);
    printf("  方向键 / WASD  移动");
    gotoxy(PANEL_X, 12);
    printf("  按住方向键       加速");
    gotoxy(PANEL_X, 13);
    printf("  P  暂停 / 继续");
    gotoxy(PANEL_X, 14);
    printf("  + -  调整基础速度");
    gotoxy(PANEL_X, 15);
    printf("  X  退出游戏");

    gotoxy(PANEL_X, 17);
    printf("游戏状态:");
    gotoxy(PANEL_X, 18);
    setColor(CLR_MENU);
    printf("游戏中...");
    setColor(CLR_DEFAULT);
}

static void drawSnakeFull(const Game *game) {
    // 从尾巴往头画，最后画蛇头。
    for (int i = game->snake.length - 1; i >= 1; i--) {
        putCell(game->snake.body[i].x, game->snake.body[i].y, CLR_BODY, 'o');
    }
    putCell(game->snake.body[0].x, game->snake.body[0].y, CLR_HEAD, '@');
}

// 值变了才重画，省写屏也防残影。
static void drawInfo(const Game *game) {
    static int last_score = -1;
    static int last_high_score = -1;
    static int last_length = -1;
    static int last_speed = -1;
    static int last_boost = -1;

    if (game->score != last_score) {
        gotoxy(PANEL_X, 4);
        setColor(CLR_TITLE);
        printf("当前得分: %-6d", game->score);
        last_score = game->score;
    }

    if (game->high_score != last_high_score) {
        gotoxy(PANEL_X, 5);
        setColor(CLR_DEFAULT);
        printf("最高得分: %-6d", game->high_score);
        last_high_score = game->high_score;
    }

    if (game->snake.length != last_length) {
        gotoxy(PANEL_X, 6);
        setColor(CLR_DEFAULT);
        printf("蛇的长度: %-6d", game->snake.length);
        last_length = game->snake.length;
    }

    if (game->speed != last_speed) {
        gotoxy(PANEL_X, 7);
        setColor(CLR_DEFAULT);
        printf("移动间隔: %3d ms", game->speed);
        last_speed = game->speed;
    }

    if ((int)game->boost != last_boost) {
        gotoxy(PANEL_X, 8);
        setColor(game->boost ? CLR_TITLE : CLR_DEFAULT);
        // “是 (2倍速)”比“否”宽，后面补空格盖掉旧字。
        printf("加速状态: %s        ", game->boost ? "是 (2倍速)" : "否");
        last_boost = (int)game->boost;
        setColor(CLR_DEFAULT);
    }
}

// 方向键在 _getch 里先返回 0 或 224，要再读一次。
static void processInput(Game *game) {
    if (!_kbhit()) {
        return;
    }

    int key = _getch();
    if (key == 0 || key == 224) {
        key = _getch();
        switch (key) {
            case 72: tryTurn(game, UP); break;
            case 80: tryTurn(game, DOWN); break;
            case 75: tryTurn(game, LEFT); break;
            case 77: tryTurn(game, RIGHT); break;
            default: break;
        }
        return;
    }

    switch (key) {
        case 'w': case 'W': tryTurn(game, UP); break;
        case 's': case 'S': tryTurn(game, DOWN); break;
        case 'a': case 'A': tryTurn(game, LEFT); break;
        case 'd': case 'D': tryTurn(game, RIGHT); break;
        case 'p': case 'P': {
            setColor(CLR_TITLE);
            gotoxy(PANEL_X, 20);
            printf("游戏已暂停，按任意键继续");
            _getch();
            // 提示占 24 格，多补两个空格。
            gotoxy(PANEL_X, 20);
            printf("                          ");
            setColor(CLR_DEFAULT);
            break;
        }
        case '+': case '=':
            if (game->speed > MIN_SPEED) {
                game->speed -= SPEED_STEP;
            }
            break;
        case '-': case '_':
            if (game->speed < MAX_SPEED) {
                game->speed += SPEED_STEP;
            }
            break;
        case 'x': case 'X':
            game->quit = true;
            game->game_over = true;
            break;
        default: break;
    }
}

static void updateGame(Game *game) {
    Snake *snake = &game->snake;

    // 本帧才采用缓存的方向。
    snake->dir = snake->next_dir;

    // 记下尾巴，没吃到食物就擦掉。
    game->old_tail = snake->body[snake->length - 1];
    game->grew = false;

    // 从尾巴往前挪，不然蛇头会把身体覆盖掉。
    for (int i = snake->length - 1; i > 0; i--) {
        snake->body[i] = snake->body[i - 1];
    }

    switch (snake->dir) {
        case LEFT: snake->body[0].x--; break;
        case RIGHT: snake->body[0].x++; break;
        case UP: snake->body[0].y--; break;
        case DOWN: snake->body[0].y++; break;
        default: break;
    }

    // 吃到食物。
    if (snake->body[0].x == game->food.x && snake->body[0].y == game->food.y) {
        game->score += FOOD_SCORE;
        if (game->score > game->high_score) {
            game->high_score = game->score;
        }

        // 变长的那节补在旧尾巴位置。
        snake->length++;
        snake->body[snake->length - 1] = game->old_tail;
        game->grew = true;
        game->old_tail.x = -1;
        game->old_tail.y = -1;

        // 每 50 分快 5ms。
        if (game->score % 50 == 0 && game->speed > MIN_SPEED) {
            game->speed -= 5;
        }

        // 生成不了食物说明棋盘满了，算通关。
        if (!spawnFood(game)) {
            game->win = true;
            game->game_over = true;
            return;
        }
    }

    // 撞墙。
    if (snake->body[0].x <= 0 || snake->body[0].x > GAME_WIDTH ||
        snake->body[0].y <= 0 || snake->body[0].y > GAME_HEIGHT) {
        game->game_over = true;
        return;
    }

    // 撞到自己，从第二节比。
    for (int i = 1; i < snake->length; i++) {
        if (snake->body[0].x == snake->body[i].x &&
            snake->body[0].y == snake->body[i].y) {
            game->game_over = true;
            return;
        }
    }
}

// 增量重绘：擦尾巴、旧蛇头降级、画新蛇头。
static void drawFrame(Game *game) {
    if (!game->grew && game->old_tail.x > 0 && game->old_tail.y > 0) {
        putCell(game->old_tail.x, game->old_tail.y, CLR_DEFAULT, ' ');
    }

    if (game->snake.length > 1) {
        putCell(game->snake.body[1].x, game->snake.body[1].y, CLR_BODY, 'o');
    }

    putCell(game->snake.body[0].x, game->snake.body[0].y, CLR_HEAD, '@');

    // 新食物只画一次。
    if (game->food_new) {
        drawFood(game);
        game->food_new = false;
    }
}

// GetAsyncKeyState 最高位为 1 表示按住。
static void checkBoost(Game *game) {
    bool pressed = (GetAsyncKeyState(VK_UP) & 0x8000) ||
                   (GetAsyncKeyState(VK_DOWN) & 0x8000) ||
                   (GetAsyncKeyState(VK_LEFT) & 0x8000) ||
                   (GetAsyncKeyState(VK_RIGHT) & 0x8000) ||
                   (GetAsyncKeyState('W') & 0x8000) ||
                   (GetAsyncKeyState('A') & 0x8000) ||
                   (GetAsyncKeyState('S') & 0x8000) ||
                   (GetAsyncKeyState('D') & 0x8000);

    game->boost = pressed;
}

// 把等待切片，按键不用等整帧才响应。
static void waitWithInput(Game *game, int milliseconds) {
    const int slice = 5;
    int elapsed = 0;

    while (elapsed < milliseconds && !game->game_over) {
        int step = milliseconds - elapsed;
        if (step > slice) {
            step = slice;
        }

        Sleep(step);
        elapsed += step;
        processInput(game);
    }
}

static void saveHighScore(const Game *game) {
    FILE *file = fopen(HIGH_SCORE_FILE, "w");
    if (file == NULL) {
        return;
    }

    fprintf(file, "%d\n", game->high_score);
    fclose(file);
}

static void showGameOver(const Game *game) {
    system("cls");

    if (game->win) {
        setColor(CLR_HEAD);
        printf("\n\n   YOU WIN!\n\n");
        printf("   棋盘被填满了，没有留下一格空隙。\n\n");
    } else if (game->quit) {
        setColor(CLR_BORDER);
        printf("\n\n   BYE!\n\n");
        printf("   你退出了这一局。\n\n");
    } else {
        setColor(CLR_WARN);
        printf("\n");
        printf("  ____    _    __  __ _____    _____     _______ ____  \n");
        printf(" / ___|  / \\  |  \\/  | ____|  / _ \\ \\   / / ____|  _ \\ \n");
        printf("| |  _  / _ \\ | |\\/| |  _|   | | | \\ \\ / /|  _| | |_) |\n");
        printf("| |_| |/ ___ \\| |  | | |___  | |_| | \\ V / | |___|  _ < \n");
        printf(" \\____/_/   \\_\\_|  |_|_____|  \\___/   \\_/  |_____|_| \\_\\\n");
        printf("\n");
    }

    setColor(CLR_TITLE);
    printf("   最终得分：%d\n", game->score);
    printf("   最高纪录：%d\n", game->high_score);
    printf("   蛇的长度：%d\n\n", game->snake.length);

    setColor(CLR_MENU);
    printf("   按任意键退出......");
    setColor(CLR_DEFAULT);

    _getch();
}

static void setColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (WORD)color);
}

static void gotoxy(int x, int y) {
    COORD coord;
    coord.X = (SHORT)x;
    coord.Y = (SHORT)y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

// 越界直接跳过。
static void putCell(int x, int y, int color, char ch) {
    if (x < 0 || x >= WINDOW_COLS || y < 0 || y >= WINDOW_LINES) {
        return;
    }

    gotoxy(x, y);
    setColor(color);
    putchar(ch);
}

static void hideCursor(void) {
    CONSOLE_CURSOR_INFO cursor_info = {100, FALSE};
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursor_info);
}

static void showCursor(void) {
    CONSOLE_CURSOR_INFO cursor_info = {100, TRUE};
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursor_info);
}

// 不许 180 度掉头。
static void tryTurn(Game *game, enum Direction next) {
    if ((next == LEFT && game->snake.dir == RIGHT) ||
        (next == RIGHT && game->snake.dir == LEFT) ||
        (next == UP && game->snake.dir == DOWN) ||
        (next == DOWN && game->snake.dir == UP)) {
        return;
    }

    game->snake.next_dir = next;
}

static bool snakeOccupies(const Game *game, int x, int y) {
    for (int i = 0; i < game->snake.length; i++) {
        if (game->snake.body[i].x == x && game->snake.body[i].y == y) {
            return true;
        }
    }
    return false;
}

// 随机找不到空位就顺序扫，棋盘满了返回 false。
static bool spawnFood(Game *game) {
    int total = GAME_WIDTH * GAME_HEIGHT;
    if (game->snake.length >= total) {
        return false;
    }

    // 先随机试几次，蛇短时基本一两次就中。
    for (int attempt = 0; attempt < 200; attempt++) {
        int x = rand() % GAME_WIDTH + 1;
        int y = rand() % GAME_HEIGHT + 1;
        if (!snakeOccupies(game, x, y)) {
            game->food.x = x;
            game->food.y = y;
            game->food_new = true;
            return true;
        }
    }

    // 蛇长了随机容易反复撞身体，改成扫描。
    for (int y = 1; y <= GAME_HEIGHT; y++) {
        for (int x = 1; x <= GAME_WIDTH; x++) {
            if (!snakeOccupies(game, x, y)) {
                game->food.x = x;
                game->food.y = y;
                game->food_new = true;
                return true;
            }
        }
    }

    return false;
}

static void drawFood(const Game *game) {
    putCell(game->food.x, game->food.y, CLR_FOOD, '*');
}
