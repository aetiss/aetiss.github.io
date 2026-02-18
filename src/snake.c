/*
 * snake.c — Classic Snake game
 * C + SDL2 + Emscripten → WebAssembly
 */

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define COLS      20
#define ROWS      20
#define CELL      20
#define W         (COLS * CELL)
#define H         (ROWS * CELL)
#define MAX_LEN   (COLS * ROWS)

/* ─── Colours ────────────────────────────────────────────────── */
#define C_BG    0x0e, 0x0e, 0x1a, 0xff
#define C_GRID  0x18, 0x18, 0x28, 0xff
#define C_HEAD  0x00, 0xff, 0x80
#define C_BODY  0x00, 0xcc, 0x60
#define C_FOOD  0xff, 0x40, 0x40

typedef struct { int x, y; } Pt;

typedef struct {
    Pt   body[MAX_LEN];
    int  len;
    int  dx, dy;
    int  ndx, ndy;   /* queued direction */
    Pt   food;
    int  dead;
    int  score;
    Uint32 last_tick;
    int  tick_ms;    /* ms per move */
    SDL_Window   *win;
    SDL_Renderer *ren;
} Game;

static Game g;

/* ─── Helpers ────────────────────────────────────────────────── */

static void spawn_food(void) {
    /* pick a random cell not occupied by snake */
    int tries = 0;
    while (tries++ < 1000) {
        int fx = rand() % COLS;
        int fy = rand() % ROWS;
        int ok = 1;
        for (int i = 0; i < g.len; i++) {
            if (g.body[i].x == fx && g.body[i].y == fy) { ok = 0; break; }
        }
        if (ok) { g.food.x = fx; g.food.y = fy; return; }
    }
}

static void init_game(void) {
    memset(g.body, 0, sizeof(g.body));
    g.len      = 3;
    g.dx = 1; g.dy  = 0;
    g.ndx= 1; g.ndy = 0;
    g.dead     = 0;
    g.score    = 0;
    g.tick_ms  = 150;
    g.last_tick= SDL_GetTicks();
    /* initial body in the middle */
    g.body[0] = (Pt){ COLS/2,     ROWS/2 };
    g.body[1] = (Pt){ COLS/2 - 1, ROWS/2 };
    g.body[2] = (Pt){ COLS/2 - 2, ROWS/2 };
    spawn_food();
}

/* Draw a single filled cell */
static void draw_cell(int x, int y, Uint8 r, Uint8 g2, Uint8 b, int inset) {
    SDL_Rect rect = { x * CELL + inset, y * CELL + inset,
                      CELL - inset * 2, CELL - inset * 2 };
    SDL_SetRenderDrawColor(g.ren, r, g2, b, 255);
    SDL_RenderFillRect(g.ren, &rect);
}

/* Simple digit bitmap font (5×7) for score display */
static const Uint8 digits[10][7] = {
    {0x7E,0x42,0x42,0x42,0x42,0x42,0x7E}, /* 0 */
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C}, /* 1 */
    {0x7E,0x02,0x02,0x7E,0x40,0x40,0x7E}, /* 2 */
    {0x7E,0x02,0x02,0x3E,0x02,0x02,0x7E}, /* 3 */
    {0x42,0x42,0x42,0x7E,0x02,0x02,0x02}, /* 4 */
    {0x7E,0x40,0x40,0x7E,0x02,0x02,0x7E}, /* 5 */
    {0x7E,0x40,0x40,0x7E,0x42,0x42,0x7E}, /* 6 */
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x10}, /* 7 */
    {0x7E,0x42,0x42,0x7E,0x42,0x42,0x7E}, /* 8 */
    {0x7E,0x42,0x42,0x7E,0x02,0x02,0x7E}, /* 9 */
};

static void draw_digit(int d, int px, int py, Uint8 r, Uint8 gg, Uint8 b) {
    SDL_SetRenderDrawColor(g.ren, r, gg, b, 255);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            if ((digits[d][row] >> (7 - col)) & 1) {
                SDL_Rect p = { px + col*2, py + row*2, 2, 2 };
                SDL_RenderFillRect(g.ren, &p);
            }
        }
    }
}

static void draw_score(void) {
    int s = g.score;
    int digits_buf[6]; int nd = 0;
    if (s == 0) { digits_buf[nd++] = 0; }
    while (s > 0) { digits_buf[nd++] = s % 10; s /= 10; }
    /* reverse */
    for (int i = 0; i < nd/2; i++) {
        int tmp = digits_buf[i];
        digits_buf[i] = digits_buf[nd-1-i];
        digits_buf[nd-1-i] = tmp;
    }
    int x = 4, y = 4;
    for (int i = 0; i < nd; i++) {
        draw_digit(digits_buf[i], x, y, 0x88, 0xff, 0xcc);
        x += 16;
    }
}

/* ─── Game loop callbacks ─────────────────────────────────────── */

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYDOWN) {
            switch (ev.key.keysym.sym) {
                case SDLK_UP:    case SDLK_w: if (g.dy== 0){g.ndx= 0;g.ndy=-1;} break;
                case SDLK_DOWN:  case SDLK_s: if (g.dy== 0){g.ndx= 0;g.ndy= 1;} break;
                case SDLK_LEFT:  case SDLK_a: if (g.dx== 0){g.ndx=-1;g.ndy= 0;} break;
                case SDLK_RIGHT: case SDLK_d: if (g.dx== 0){g.ndx= 1;g.ndy= 0;} break;
                case SDLK_r: init_game(); break;
            }
        }
    }
}

static void update(void) {
    if (g.dead) return;
    Uint32 now = SDL_GetTicks();
    if ((int)(now - g.last_tick) < g.tick_ms) return;
    g.last_tick = now;

    /* commit queued direction */
    g.dx = g.ndx; g.dy = g.ndy;

    Pt head = { g.body[0].x + g.dx, g.body[0].y + g.dy };

    /* wall collision */
    if (head.x < 0 || head.x >= COLS || head.y < 0 || head.y >= ROWS) {
        g.dead = 1; return;
    }
    /* self collision (skip tail – it will move) */
    for (int i = 0; i < g.len - 1; i++) {
        if (g.body[i].x == head.x && g.body[i].y == head.y) { g.dead = 1; return; }
    }

    /* food? */
    int ate = (head.x == g.food.x && head.y == g.food.y);

    /* shift body */
    memmove(g.body + 1, g.body, sizeof(Pt) * (ate ? g.len : g.len - 1));
    g.body[0] = head;
    if (ate) {
        if (g.len < MAX_LEN) g.len++;
        g.score++;
        if (g.tick_ms > 60) g.tick_ms -= 3;  /* speed up */
        spawn_food();
    }
}

static void render(void) {
    /* background */
    SDL_SetRenderDrawColor(g.ren, C_BG);
    SDL_RenderClear(g.ren);

    /* subtle grid */
    SDL_SetRenderDrawColor(g.ren, C_GRID);
    for (int x = 0; x <= W; x += CELL) SDL_RenderDrawLine(g.ren, x, 0, x, H);
    for (int y = 0; y <= H; y += CELL) SDL_RenderDrawLine(g.ren, 0, y, W, y);

    /* food */
    draw_cell(g.food.x, g.food.y, C_FOOD, 3);

    /* snake body */
    for (int i = g.len - 1; i >= 1; i--) {
        draw_cell(g.body[i].x, g.body[i].y, C_BODY, 2);
    }
    /* snake head */
    draw_cell(g.body[0].x, g.body[0].y, C_HEAD, 1);

    /* score */
    draw_score();

    /* game over overlay */
    if (g.dead) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 160);
        SDL_Rect overlay = {0, 0, W, H};
        SDL_RenderFillRect(g.ren, &overlay);
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);

        /* "DEAD" in pixel digits using big red blocks */
        int cx = W/2 - 30, cy = H/2 - 20;
        draw_digit(g.score / 10 % 10, cx,      cy, 0xff,0x40,0x40);
        draw_digit(g.score      % 10, cx + 16, cy, 0xff,0x40,0x40);
    }

    SDL_RenderPresent(g.ren);
}

static void main_loop(void) {
    handle_input();
    update();
    render();
}

/* ─── Entry point ─────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    SDL_Init(SDL_INIT_VIDEO);
    g.win = SDL_CreateWindow("Snake", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             W, H, SDL_WINDOW_SHOWN);
    g.ren = SDL_CreateRenderer(g.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);

    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
