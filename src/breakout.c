/*
 * breakout.c — Breakout game
 * C + SDL2 + Emscripten → WebAssembly
 */

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W         480
#define H         480
#define PAD_W     80
#define PAD_H     10
#define PAD_Y     (H - 36)
#define PAD_SPEED 7
#define BALL_R    6
#define BRICK_COLS 10
#define BRICK_ROWS  6
#define BRICK_W   (W / BRICK_COLS)
#define BRICK_H   18
#define BRICK_OFF_Y 48   /* top margin */

/* colours: R,G,B per brick row */
static const Uint8 row_colors[BRICK_ROWS][3] = {
    {0xFF,0x40,0x60},
    {0xFF,0x80,0x20},
    {0xFF,0xCC,0x00},
    {0x60,0xDD,0x40},
    {0x20,0xBB,0xFF},
    {0xAA,0x60,0xFF},
};

typedef struct {
    float x, y;      /* ball position */
    float dx, dy;    /* ball velocity */
    float pad_x;     /* paddle centre x */
    int   key_left, key_right;
    int   bricks[BRICK_ROWS][BRICK_COLS];
    int   live;
    int   score;
    int   lives;
    int   dead, win;
    SDL_Window   *win_ptr;
    SDL_Renderer *ren;
} Game;

static Game g;

static void init_game(void) {
    g.pad_x   = W / 2.0f;
    g.x       = W / 2.0f;
    g.y       = H - 80.0f;
    g.dx      = 3.5f;
    g.dy      = -3.5f;
    g.dead    = 0;
    g.win     = 0;
    g.score   = 0;
    g.lives   = 3;
    g.key_left= 0; g.key_right = 0;
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
        if (ev.type == SDL_KEYDOWN) {
            switch (ev.key.keysym.sym) {
                case SDLK_LEFT:  case SDLK_a: g.key_left  = 1; break;
                case SDLK_RIGHT: case SDLK_d: g.key_right = 1; break;
                case SDLK_r: init_game(); break;
            }
        }
        if (ev.type == SDL_KEYUP) {
            switch (ev.key.keysym.sym) {
                case SDLK_LEFT:  case SDLK_a: g.key_left  = 0; break;
                case SDLK_RIGHT: case SDLK_d: g.key_right = 0; break;
            }
        }
        if (ev.type == SDL_MOUSEMOTION) {
            g.pad_x = (float)ev.motion.x;
        }
    }
}

static void update(void) {
    if (g.dead || g.win) return;

    /* paddle movement */
    if (g.key_left)  g.pad_x -= PAD_SPEED;
    if (g.key_right) g.pad_x += PAD_SPEED;
    float half = PAD_W / 2.0f;
    if (g.pad_x < half)   g.pad_x = half;
    if (g.pad_x > W - half) g.pad_x = W - half;

    /* ball movement */
    g.x += g.dx;
    g.y += g.dy;

    /* wall bounce */
    if (g.x - BALL_R < 0)    { g.x = BALL_R;    g.dx =  fabsf(g.dx); }
    if (g.x + BALL_R > W)    { g.x = W-BALL_R;  g.dx = -fabsf(g.dx); }
    if (g.y - BALL_R < 0)    { g.y = BALL_R;    g.dy =  fabsf(g.dy); }

    /* ball fell below paddle */
    if (g.y + BALL_R > H) {
        g.lives--;
        if (g.lives <= 0) { g.dead = 1; return; }
        g.x = g.pad_x; g.y = PAD_Y - 40;
        g.dx = (rand()%2 ? 1 : -1) * 3.5f;
        g.dy = -3.5f;
    }

    /* paddle collision */
    if (g.y + BALL_R >= PAD_Y &&
        g.y + BALL_R <= PAD_Y + PAD_H + fabsf(g.dy) &&
        g.x >= g.pad_x - half - BALL_R &&
        g.x <= g.pad_x + half + BALL_R &&
        g.dy > 0) {
        float offset = (g.x - g.pad_x) / half;  /* -1..1 */
        float speed  = sqrtf(g.dx*g.dx + g.dy*g.dy);
        g.dx = offset * speed * 1.1f;
        g.dy = -fabsf(g.dy);
        g.y  = PAD_Y - BALL_R;
    }

    /* brick collision */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!g.bricks[r][c]) continue;
            float bx = c * BRICK_W;
            float by = BRICK_OFF_Y + r * BRICK_H;
            /* AABB + circle check */
            float cx = g.x < bx ? bx : (g.x > bx+BRICK_W ? bx+BRICK_W : g.x);
            float cy = g.y < by ? by : (g.y > by+BRICK_H ? by+BRICK_H : g.y);
            float dx = g.x - cx, dy = g.y - cy;
            if (dx*dx + dy*dy < (float)(BALL_R*BALL_R)) {
                g.bricks[r][c] = 0;
                g.score += 10;
                /* bounce based on which side was hit */
                if (fabsf(dx) > fabsf(dy)) g.dx = -g.dx;
                else                        g.dy = -g.dy;
            }
        }
    }
    if (bricks_left() == 0) g.win = 1;
}

static void render_text_score(void) {
    /* Draw tiny pixel score – reuse simple bar method */
    SDL_SetRenderDrawColor(g.ren, 0xAA, 0xAA, 0xAA, 255);
    /* just a score bar proportional to score */
    int bar = g.score * W / (BRICK_ROWS * BRICK_COLS * 10 + 1);
    SDL_Rect b = {0, H-3, bar, 3};
    SDL_SetRenderDrawColor(g.ren, 0x60, 0xDD, 0x80, 255);
    SDL_RenderFillRect(g.ren, &b);
}

static void render(void) {
    SDL_SetRenderDrawColor(g.ren, 0x0a, 0x0a, 0x14, 255);
    SDL_RenderClear(g.ren);

    /* bricks */
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!g.bricks[r][c]) continue;
            SDL_Rect rect = { c*BRICK_W+1, BRICK_OFF_Y+r*BRICK_H+1,
                              BRICK_W-2, BRICK_H-2 };
            const Uint8 *col = row_colors[r];
            SDL_SetRenderDrawColor(g.ren, col[0], col[1], col[2], 255);
            SDL_RenderFillRect(g.ren, &rect);
            /* highlight */
            SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 60);
            SDL_RenderDrawLine(g.ren, rect.x, rect.y, rect.x+rect.w-1, rect.y);
        }
    }

    /* paddle */
    SDL_Rect pad = { (int)(g.pad_x - PAD_W/2), PAD_Y, PAD_W, PAD_H };
    SDL_SetRenderDrawColor(g.ren, 0xCC, 0xCC, 0xFF, 255);
    SDL_RenderFillRect(g.ren, &pad);

    /* ball */
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xFF, 0xFF, 255);
    int bx = (int)g.x, by = (int)g.y, br = BALL_R;
    for (int dy = -br; dy <= br; dy++)
        for (int dx = -br; dx <= br; dx++)
            if (dx*dx+dy*dy <= br*br)
                SDL_RenderDrawPoint(g.ren, bx+dx, by+dy);

    /* lives dots */
    for (int i = 0; i < g.lives; i++) {
        SDL_Rect dot = { 6 + i*14, H-14, 8, 8 };
        SDL_SetRenderDrawColor(g.ren, 0xCC, 0xCC, 0xFF, 255);
        SDL_RenderFillRect(g.ren, &dot);
    }

    render_text_score();

    /* overlays */
    if (g.dead || g.win) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 160);
        SDL_Rect ov = {0, 0, W, H};
        SDL_RenderFillRect(g.ren, &ov);
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(g.ren);
}

static void main_loop(void) {
    handle_input();
    update();
    render();
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(42);
    SDL_Init(SDL_INIT_VIDEO);
    g.win_ptr = SDL_CreateWindow("Breakout",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN);
    g.ren = SDL_CreateRenderer(g.win_ptr, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetWindowTitle(g.win_ptr, "Breakout");
    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win_ptr);
    SDL_Quit();
    return 0;
}
