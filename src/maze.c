/*
 * maze.c — Wolfenstein-style 3D raycasting maze
 * C + SDL3 + Emscripten → WebAssembly
 *
 * DDA (Digital Differential Analyzer) raycasting.
 * Each screen column = one ray cast from player position/angle.
 */

#include <SDL3/SDL.h>
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W           640
#define H           400
#define MAP_W       16
#define MAP_H       16
#define FOV         (M_PI / 3.0)
#define MOVE_SPD    0.04
#define ROT_SPD     0.03
#define MM_SCALE    10
#define MM_X        (W - MAP_W * MM_SCALE - 8)
#define MM_Y        8

static const int MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,0,0,1,1,0,0,0,1,0,0,1},
    {1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,1,0,1,1,0,0,0,0,1,1,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,1},
    {1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1},
    {1,0,1,0,0,1,0,1,0,0,0,1,0,1,0,1},
    {1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1},
    {1,0,1,1,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,1,0,0,1,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,1,0,1,1,0,0,0,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

typedef struct {
    double px, py, pa;
    int    key_w, key_s, key_a, key_d;
    int    key_left, key_right;
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *fb;
    Uint32       *pixels;
} Game;

static Game g;

/* ─── DDA Raycaster ───────────────────────────────────────────── */

static void cast_rays(void) {
    for (int col = 0; col < W; col++) {
        double ray_angle = (g.pa - FOV/2.0) + ((double)col / W) * FOV;
        double rdx = cos(ray_angle);
        double rdy = sin(ray_angle);

        int map_x = (int)g.px;
        int map_y = (int)g.py;

        double delta_x = rdx == 0.0 ? 1e30 : fabs(1.0/rdx);
        double delta_y = rdy == 0.0 ? 1e30 : fabs(1.0/rdy);

        double side_x, side_y;
        int step_x, step_y;

        if (rdx < 0) { step_x = -1; side_x = (g.px - map_x) * delta_x; }
        else          { step_x =  1; side_x = (map_x + 1.0 - g.px) * delta_x; }
        if (rdy < 0) { step_y = -1; side_y = (g.py - map_y) * delta_y; }
        else          { step_y =  1; side_y = (map_y + 1.0 - g.py) * delta_y; }

        int hit = 0, side = 0;
        while (!hit) {
            if (side_x < side_y) { side_x += delta_x; map_x += step_x; side = 0; }
            else                  { side_y += delta_y; map_y += step_y; side = 1; }
            if (map_x < 0 || map_x >= MAP_W || map_y < 0 || map_y >= MAP_H) break;
            if (MAP[map_y][map_x]) hit = 1;
        }

        double perp = side == 0
            ? (map_x - g.px + (1 - step_x) / 2.0) / rdx
            : (map_y - g.py + (1 - step_y) / 2.0) / rdy;
        if (perp < 0.01) perp = 0.01;

        int wall_h = (int)(H / perp);
        int top    = (H - wall_h) / 2;
        int bot    = (H + wall_h) / 2;

        /* ceiling */
        for (int y = 0; y < top && y < H; y++)
            g.pixels[y * W + col] = 0xFF181828;

        /* floor */
        for (int y = (bot < 0 ? 0 : bot); y < H; y++)
            g.pixels[y * W + col] = 0xFF282838;

        /* wall with distance shading */
        double shade = 1.0 - fmin(perp / 8.0, 0.85);
        Uint8 wr, wg, wb;
        if (side == 0) { wr=(Uint8)(0x55*shade); wg=(Uint8)(0x88*shade); wb=(Uint8)(0xAA*shade); }
        else           { wr=(Uint8)(0x33*shade); wg=(Uint8)(0x55*shade); wb=(Uint8)(0x77*shade); }
        Uint32 wc = 0xFF000000 | ((Uint32)wr<<16) | ((Uint32)wg<<8) | wb;

        int dt = top < 0 ? 0 : top;
        int db = bot > H ? H : bot;
        for (int y = dt; y < db; y++)
            g.pixels[y * W + col] = wc;
    }
}

/* ─── Minimap ─────────────────────────────────────────────────── */

static void draw_minimap(void) {
    for (int my = 0; my < MAP_H; my++) {
        for (int mx = 0; mx < MAP_W; mx++) {
            SDL_FRect r = { (float)(MM_X + mx*MM_SCALE), (float)(MM_Y + my*MM_SCALE),
                            (float)(MM_SCALE-1), (float)(MM_SCALE-1) };
            if (MAP[my][mx]) SDL_SetRenderDrawColor(g.ren, 0x55, 0x88, 0xAA, 200);
            else              SDL_SetRenderDrawColor(g.ren, 0x10, 0x10, 0x1A, 200);
            SDL_RenderFillRect(g.ren, &r);
        }
    }
    /* player */
    float pdx = (float)(MM_X + g.px * MM_SCALE);
    float pdy = (float)(MM_Y + g.py * MM_SCALE);
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xFF, 0x00, 255);
    SDL_FRect pd = { pdx-2, pdy-2, 5, 5 };
    SDL_RenderFillRect(g.ren, &pd);
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xAA, 0x00, 255);
    SDL_RenderLine(g.ren, pdx, pdy,
        pdx + (float)(cos(g.pa)*8), pdy + (float)(sin(g.pa)*8));
}

/* ─── Crosshair ───────────────────────────────────────────────── */

static void draw_crosshair(void) {
    SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 160);
    SDL_RenderLine(g.ren, W/2.0f-8, H/2.0f, W/2.0f+8, H/2.0f);
    SDL_RenderLine(g.ren, W/2.0f, H/2.0f-8, W/2.0f, H/2.0f+8);
    SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);
}

/* ─── Input / Update / Render ─────────────────────────────────── */

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            switch (ev.key.key) {
                case SDLK_W: case SDLK_UP:    g.key_w     = 1; break;
                case SDLK_S: case SDLK_DOWN:   g.key_s     = 1; break;
                case SDLK_A:                    g.key_a     = 1; break;
                case SDLK_D:                    g.key_d     = 1; break;
                case SDLK_LEFT:                 g.key_left  = 1; break;
                case SDLK_RIGHT:                g.key_right = 1; break;
            }
        }
        if (ev.type == SDL_EVENT_KEY_UP) {
            switch (ev.key.key) {
                case SDLK_W: case SDLK_UP:    g.key_w     = 0; break;
                case SDLK_S: case SDLK_DOWN:   g.key_s     = 0; break;
                case SDLK_A:                    g.key_a     = 0; break;
                case SDLK_D:                    g.key_d     = 0; break;
                case SDLK_LEFT:                 g.key_left  = 0; break;
                case SDLK_RIGHT:                g.key_right = 0; break;
            }
        }
        if (ev.type == SDL_EVENT_MOUSE_MOTION)
            g.pa += ev.motion.xrel * 0.003f;
    }
}

static int solid(double x, double y) {
    int mx = (int)x, my = (int)y;
    if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return 1;
    return MAP[my][mx];
}

static void update(void) {
    if (g.key_left)  g.pa -= ROT_SPD;
    if (g.key_right) g.pa += ROT_SPD;

    double cdx = cos(g.pa), cdy = sin(g.pa);
    double ldx = cos(g.pa - M_PI/2), ldy = sin(g.pa - M_PI/2);
    double nx = g.px, ny = g.py;

    if (g.key_w) { nx += cdx*MOVE_SPD; ny += cdy*MOVE_SPD; }
    if (g.key_s) { nx -= cdx*MOVE_SPD; ny -= cdy*MOVE_SPD; }
    if (g.key_a) { nx += ldx*MOVE_SPD; ny += ldy*MOVE_SPD; }
    if (g.key_d) { nx -= ldx*MOVE_SPD; ny -= ldy*MOVE_SPD; }

    double m = 0.25;
    if (!solid(nx, g.py)) g.px = nx;
    if (!solid(g.px, ny)) g.py = ny;
    /* prevent corner clipping */
    if (solid(g.px, g.py)) { g.px = floor(g.px)+m; g.py = floor(g.py)+m; }
    (void)m;
}

static void render(void) {
    cast_rays();
    SDL_UpdateTexture(g.fb, NULL, g.pixels, W * sizeof(Uint32));
    SDL_RenderTexture(g.ren, g.fb, NULL, NULL);
    draw_minimap();
    draw_crosshair();
    SDL_RenderPresent(g.ren);
}

static void main_loop(void) { handle_input(); update(); render(); }

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_Init(SDL_INIT_VIDEO);
    g.win = SDL_CreateWindow("Maze", W, H, 0);
    g.ren = SDL_CreateRenderer(g.win, NULL);
    g.fb  = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, W, H);
    g.pixels = (Uint32 *)malloc(W * H * sizeof(Uint32));
    g.px = 1.5; g.py = 1.5; g.pa = 0.0;
    emscripten_set_main_loop(main_loop, 0, 1);
    free(g.pixels);
    SDL_DestroyTexture(g.fb);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
