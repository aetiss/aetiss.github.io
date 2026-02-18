/*
 * breakout.c — Breakout / Arkanoid clone
 * C + SDL3 + Emscripten → WebAssembly
 */

#include <SDL3/SDL.h>
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W           480
#define H           480
#define PAD_W       80.0f
#define PAD_H       10.0f
#define PAD_Y       (H - 36.0f)
#define PAD_SPEED   7.0f
#define BALL_R      6.0f
#define BRICK_COLS  10
#define BRICK_ROWS  6
#define BRICK_W     (W / BRICK_COLS)
#define BRICK_H     18
#define BRICK_OFF_Y 48.0f

static const Uint8 ROW_COLORS[BRICK_ROWS][3] = {
    {0xFF, 0x40, 0x60},
    {0xFF, 0x80, 0x20},
    {0xFF, 0xCC, 0x00},
    {0x60, 0xDD, 0x40},
    {0x20, 0xBB, 0xFF},
    {0xAA, 0x60, 0xFF},
};

typedef struct {
    float bx, by;          /* ball */
    float bdx, bdy;
    float pad_x;
    int   key_left, key_right;
    int   bricks[BRICK_ROWS][BRICK_COLS];
    int   score, lives;
    int   dead, win;
    SDL_Window   *win_ptr;
    SDL_Renderer *ren;
} Game;

static Game g;

static void init_game(void) {
    g.pad_x    = W / 2.0f;
    g.bx       = W / 2.0f;
    g.by       = H - 80.0f;
    g.bdx      = 3.5f;
    g.bdy      = -3.5f;
    g.dead     = 0;
    g.win      = 0;
    g.score    = 0;
    g.lives    = 3;
    g.key_left = 0;
    g.key_right= 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            g.bricks[r][c] = 1;
}

static int bricks_left(void) {
    int n = 0;
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            if (g.bricks[r][c]) n++;
    return n;
}

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            switch (ev.key.key) {
                case SDLK_LEFT:  case SDLK_A: g.key_left  = 1; break;
                case SDLK_RIGHT: case SDLK_D: g.key_right = 1; break;
                case SDLK_R: init_game(); break;
            }
        }
        if (ev.type == SDL_EVENT_KEY_UP) {
            switch (ev.key.key) {
                case SDLK_LEFT:  case SDLK_A: g.key_left  = 0; break;
                case SDLK_RIGHT: case SDLK_D: g.key_right = 0; break;
            }
        }
        if (ev.type == SDL_EVENT_MOUSE_MOTION)
            g.pad_x = ev.motion.x;
    }
}

static void update(void) {
    if (g.dead || g.win) return;

    if (g.key_left)  g.pad_x -= PAD_SPEED;
    if (g.key_right) g.pad_x += PAD_SPEED;
    float half = PAD_W / 2.0f;
    if (g.pad_x < half)     g.pad_x = half;
    if (g.pad_x > W - half) g.pad_x = W - half;

    g.bx += g.bdx;
    g.by += g.bdy;

    if (g.bx - BALL_R < 0)   { g.bx = BALL_R;      g.bdx =  fabsf(g.bdx); }
    if (g.bx + BALL_R > W)   { g.bx = W - BALL_R;  g.bdx = -fabsf(g.bdx); }
    if (g.by - BALL_R < 0)   { g.by = BALL_R;       g.bdy =  fabsf(g.bdy); }

    if (g.by + BALL_R > H) {
        g.lives--;
        if (g.lives <= 0) { g.dead = 1; return; }
        g.bx = g.pad_x; g.by = PAD_Y - 40.0f;
        g.bdx = (rand()%2 ? 1 : -1) * 3.5f;
        g.bdy = -3.5f;
    }

    /* paddle */
    if (g.by + BALL_R >= PAD_Y &&
        g.by - BALL_R <= PAD_Y + PAD_H &&
        g.bx >= g.pad_x - half - BALL_R &&
        g.bx <= g.pad_x + half + BALL_R &&
        g.bdy > 0.0f) {
        float offset = (g.bx - g.pad_x) / half;
        float speed  = sqrtf(g.bdx*g.bdx + g.bdy*g.bdy);
        g.bdx = offset * speed * 1.1f;
        g.bdy = -fabsf(g.bdy);
        g.by  = PAD_Y - BALL_R;
    }

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!g.bricks[r][c]) continue;
            float bx = (float)(c * BRICK_W);
            float by = BRICK_OFF_Y + r * BRICK_H;
            float cx = g.bx < bx ? bx : (g.bx > bx+BRICK_W ? bx+BRICK_W : g.bx);
            float cy = g.by < by ? by : (g.by > by+BRICK_H ? by+BRICK_H : g.by);
            float dx = g.bx - cx, dy = g.by - cy;
            if (dx*dx + dy*dy < BALL_R*BALL_R) {
                g.bricks[r][c] = 0;
                g.score += 10;
                if (fabsf(dx) > fabsf(dy)) g.bdx = -g.bdx;
                else                        g.bdy = -g.bdy;
            }
        }
    }
    if (bricks_left() == 0) g.win = 1;
}

static void render(void) {
    SDL_SetRenderDrawColor(g.ren, 0x0a, 0x0a, 0x14, 255);
    SDL_RenderClear(g.ren);

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!g.bricks[r][c]) continue;
            SDL_FRect rect = { (float)(c*BRICK_W)+1, BRICK_OFF_Y + r*BRICK_H+1,
                               (float)BRICK_W-2, (float)BRICK_H-2 };
            const Uint8 *col = ROW_COLORS[r];
            SDL_SetRenderDrawColor(g.ren, col[0], col[1], col[2], 255);
            SDL_RenderFillRect(g.ren, &rect);
            SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 50);
            SDL_RenderLine(g.ren, rect.x, rect.y, rect.x+rect.w-1, rect.y);
        }
    }

    /* paddle */
    SDL_FRect pad = { g.pad_x - PAD_W/2, PAD_Y, PAD_W, PAD_H };
    SDL_SetRenderDrawColor(g.ren, 0xCC, 0xCC, 0xFF, 255);
    SDL_RenderFillRect(g.ren, &pad);

    /* ball (circle via points) */
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xFF, 0xFF, 255);
    for (float dy = -BALL_R; dy <= BALL_R; dy += 1.0f)
        for (float dx = -BALL_R; dx <= BALL_R; dx += 1.0f)
            if (dx*dx + dy*dy <= BALL_R*BALL_R)
                SDL_RenderPoint(g.ren, g.bx+dx, g.by+dy);

    /* lives dots */
    for (int i = 0; i < g.lives; i++) {
        SDL_FRect dot = { 6.0f + i*14, (float)(H-14), 8, 8 };
        SDL_SetRenderDrawColor(g.ren, 0xCC, 0xCC, 0xFF, 255);
        SDL_RenderFillRect(g.ren, &dot);
    }

    /* score bar */
    int max_score = BRICK_ROWS * BRICK_COLS * 10;
    float bar = (float)g.score * W / max_score;
    SDL_FRect sb = {0, (float)(H-3), bar, 3};
    SDL_SetRenderDrawColor(g.ren, 0x60, 0xDD, 0x80, 255);
    SDL_RenderFillRect(g.ren, &sb);

    /* overlay */
    if (g.dead || g.win) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 160);
        SDL_FRect ov = {0, 0, W, H};
        SDL_RenderFillRect(g.ren, &ov);
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(g.ren);
}

static void main_loop(void) { handle_input(); update(); render(); }

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(42);
    SDL_Init(SDL_INIT_VIDEO);
    g.win_ptr = SDL_CreateWindow("Breakout", W, H, 0);
    g.ren     = SDL_CreateRenderer(g.win_ptr, NULL);
    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win_ptr);
    SDL_Quit();
    return 0;
}
