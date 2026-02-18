/*
 * snake.c — Classic Snake game
 * C + SDL3 + Emscripten → WebAssembly
 */

#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define COLS    20
#define ROWS    20
#define CELL    20
#define W       (COLS * CELL)
#define H       (ROWS * CELL)
#define MAX_LEN (COLS * ROWS)

typedef struct { int x, y; } Pt;

typedef struct {
    Pt     body[MAX_LEN];
    int    len;
    int    dx, dy;
    int    ndx, ndy;
    Pt     food;
    int    dead;
    int    score;
    Uint64 last_tick;
    int    tick_ms;
    SDL_Window   *win;
    SDL_Renderer *ren;
} Game;

static Game g;

/* ─── Helpers ─────────────────────────────────────────────────── */

static void spawn_food(void) {
    int tries = 0;
    while (tries++ < 1000) {
        int fx = rand() % COLS, fy = rand() % ROWS;
        int ok = 1;
        for (int i = 0; i < g.len; i++)
            if (g.body[i].x == fx && g.body[i].y == fy) { ok = 0; break; }
        if (ok) { g.food.x = fx; g.food.y = fy; return; }
    }
}

static void init_game(void) {
    memset(g.body, 0, sizeof(g.body));
    g.len     = 3;
    g.dx = 1;  g.dy  = 0;
    g.ndx= 1;  g.ndy = 0;
    g.dead    = 0;
    g.score   = 0;
    g.tick_ms = 150;
    g.last_tick = SDL_GetTicks();
    g.body[0] = (Pt){ COLS/2,     ROWS/2 };
    g.body[1] = (Pt){ COLS/2 - 1, ROWS/2 };
    g.body[2] = (Pt){ COLS/2 - 2, ROWS/2 };
    spawn_food();
}

/* 5×7 pixel digit font */
static const Uint8 digits[10][7] = {
    {0x7E,0x42,0x42,0x42,0x42,0x42,0x7E},
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C},
    {0x7E,0x02,0x02,0x7E,0x40,0x40,0x7E},
    {0x7E,0x02,0x02,0x3E,0x02,0x02,0x7E},
    {0x42,0x42,0x42,0x7E,0x02,0x02,0x02},
    {0x7E,0x40,0x40,0x7E,0x02,0x02,0x7E},
    {0x7E,0x40,0x40,0x7E,0x42,0x42,0x7E},
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x10},
    {0x7E,0x42,0x42,0x7E,0x42,0x42,0x7E},
    {0x7E,0x42,0x42,0x7E,0x02,0x02,0x7E},
};

static void draw_digit(int d, float px, float py, Uint8 r, Uint8 gg, Uint8 b) {
    SDL_SetRenderDrawColor(g.ren, r, gg, b, 255);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            if ((digits[d][row] >> (7 - col)) & 1) {
                SDL_FRect p = { px + col*2.0f, py + row*2.0f, 2.0f, 2.0f };
                SDL_RenderFillRect(g.ren, &p);
            }
        }
    }
}

static void draw_score(void) {
    int s = g.score;
    int buf[6]; int nd = 0;
    if (s == 0) buf[nd++] = 0;
    while (s > 0) { buf[nd++] = s % 10; s /= 10; }
    for (int i = 0; i < nd/2; i++) {
        int t = buf[i]; buf[i] = buf[nd-1-i]; buf[nd-1-i] = t;
    }
    for (int i = 0; i < nd; i++)
        draw_digit(buf[i], 4.0f + i * 16.0f, 4.0f, 0x88, 0xFF, 0xCC);
}

static void draw_cell(float x, float y, Uint8 r, Uint8 gg, Uint8 b, float inset) {
    SDL_FRect rect = { x * CELL + inset, y * CELL + inset,
                       CELL - inset*2, CELL - inset*2 };
    SDL_SetRenderDrawColor(g.ren, r, gg, b, 255);
    SDL_RenderFillRect(g.ren, &rect);
}

/* ─── Game loop ───────────────────────────────────────────────── */

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            switch (ev.key.key) {
                case SDLK_UP:    case SDLK_W: if(g.dy==0){g.ndx= 0;g.ndy=-1;} break;
                case SDLK_DOWN:  case SDLK_S: if(g.dy==0){g.ndx= 0;g.ndy= 1;} break;
                case SDLK_LEFT:  case SDLK_A: if(g.dx==0){g.ndx=-1;g.ndy= 0;} break;
                case SDLK_RIGHT: case SDLK_D: if(g.dx==0){g.ndx= 1;g.ndy= 0;} break;
                case SDLK_R: init_game(); break;
            }
        }
    }
}

static void update(void) {
    if (g.dead) return;
    Uint64 now = SDL_GetTicks();
    if ((int)(now - g.last_tick) < g.tick_ms) return;
    g.last_tick = now;

    g.dx = g.ndx; g.dy = g.ndy;
    Pt head = { g.body[0].x + g.dx, g.body[0].y + g.dy };

    if (head.x < 0 || head.x >= COLS || head.y < 0 || head.y >= ROWS) { g.dead = 1; return; }
    for (int i = 0; i < g.len - 1; i++)
        if (g.body[i].x == head.x && g.body[i].y == head.y) { g.dead = 1; return; }

    int ate = (head.x == g.food.x && head.y == g.food.y);
    memmove(g.body + 1, g.body, sizeof(Pt) * (ate ? g.len : g.len - 1));
    g.body[0] = head;
    if (ate) {
        if (g.len < MAX_LEN) g.len++;
        g.score++;
        if (g.tick_ms > 60) g.tick_ms -= 3;
        spawn_food();
    }
}

static void render(void) {
    /* background */
    SDL_SetRenderDrawColor(g.ren, 0x0e, 0x0e, 0x1a, 255);
    SDL_RenderClear(g.ren);

    /* grid */
    SDL_SetRenderDrawColor(g.ren, 0x18, 0x18, 0x28, 255);
    for (int x = 0; x <= W; x += CELL)
        SDL_RenderLine(g.ren, (float)x, 0, (float)x, (float)H);
    for (int y = 0; y <= H; y += CELL)
        SDL_RenderLine(g.ren, 0, (float)y, (float)W, (float)y);

    /* food */
    draw_cell((float)g.food.x, (float)g.food.y, 0xFF, 0x40, 0x40, 3.0f);

    /* snake body */
    for (int i = g.len - 1; i >= 1; i--)
        draw_cell((float)g.body[i].x, (float)g.body[i].y, 0x00, 0xCC, 0x60, 2.0f);

    /* snake head */
    draw_cell((float)g.body[0].x, (float)g.body[0].y, 0x00, 0xFF, 0x80, 1.0f);

    draw_score();

    /* game over dim */
    if (g.dead) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 160);
        SDL_FRect ov = {0, 0, (float)W, (float)H};
        SDL_RenderFillRect(g.ren, &ov);
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(g.ren);
}

static void main_loop(void) { handle_input(); update(); render(); }

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));
    SDL_Init(SDL_INIT_VIDEO);
    g.win = SDL_CreateWindow("Snake", W, H, 0);
    g.ren = SDL_CreateRenderer(g.win, NULL);
    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
