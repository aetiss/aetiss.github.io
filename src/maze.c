/*
 * maze.c — Wolfenstein-style 3D raycasting maze
 * C + SDL2 + Emscripten → WebAssembly
 *
 * Algorithm: DDA (Digital Differential Analyzer) raycasting
 * Classic technique from the early 1990s — every vertical strip of the
 * screen corresponds to one ray cast from the player position.
 */

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W         640
#define H         400
#define MAP_W     16
#define MAP_H     16
#define FOV       (M_PI / 3.0)   /* 60° field of view */
#define MOVE_SPD  0.04
#define ROT_SPD   0.03
#define MINIMAP_SCALE 10
#define MINIMAP_X     (W - MAP_W * MINIMAP_SCALE - 8)
#define MINIMAP_Y     8

/* ─── Map ─────────────────────────────────────────────────────── */
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

/* Wall colours: north/south sides are darker for depth effect */
static const Uint8 WALL_NS[3] = {0x55, 0x88, 0xAA};
static const Uint8 WALL_EW[3] = {0x33, 0x55, 0x77};

typedef struct {
    double px, py;   /* player position */
    double pa;       /* player angle (radians) */
    int    key_w, key_s, key_a, key_d;
    int    key_left, key_right;
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *fb;        /* pixel framebuffer */
    Uint32       *pixels;    /* raw ARGB pixels */
} Game;

static Game g;

/* ─── DDA Raycasting ──────────────────────────────────────────── */

static void cast_rays(void) {
    int pitch = W;
    for (int col = 0; col < W; col++) {
        /* ray angle for this column */
        double ray_angle = (g.pa - FOV/2.0) + ((double)col / W) * FOV;
        double rdx = cos(ray_angle);
        double rdy = sin(ray_angle);

        /* DDA setup */
        int map_x = (int)g.px;
        int map_y = (int)g.py;

        /* how far to walk to cross one grid cell in each axis */
        double delta_x = rdx == 0.0 ? 1e30 : fabs(1.0 / rdx);
        double delta_y = rdy == 0.0 ? 1e30 : fabs(1.0 / rdy);

        double side_x, side_y;
        int step_x, step_y;

        if (rdx < 0) { step_x = -1; side_x = (g.px - map_x) * delta_x; }
        else          { step_x =  1; side_x = (map_x + 1.0 - g.px) * delta_x; }
        if (rdy < 0) { step_y = -1; side_y = (g.py - map_y) * delta_y; }
        else          { step_y =  1; side_y = (map_y + 1.0 - g.py) * delta_y; }

        /* DDA march */
        int hit = 0, side = 0;
        while (!hit) {
            if (side_x < side_y) { side_x += delta_x; map_x += step_x; side = 0; }
            else                  { side_y += delta_y; map_y += step_y; side = 1; }
            if (map_x < 0 || map_x >= MAP_W || map_y < 0 || map_y >= MAP_H) break;
            if (MAP[map_y][map_x] > 0) hit = 1;
        }

        /* perpendicular distance (avoids fisheye) */
        double perp = side == 0
            ? (map_x - g.px + (1 - step_x) / 2.0) / rdx
            : (map_y - g.py + (1 - step_y) / 2.0) / rdy;
        if (perp < 0.01) perp = 0.01;

        int wall_h = (int)(H / perp);
        int top    = (H - wall_h) / 2;
        int bot    = (H + wall_h) / 2;

        /* ceiling (dark) */
        for (int y = 0; y < top && y < H; y++)
            g.pixels[y * pitch + col] = 0xFF181828;

        /* floor (slightly lighter) */
        for (int y = bot < 0 ? 0 : bot; y < H; y++)
            g.pixels[y * pitch + col] = 0xFF282838;

        /* wall strip */
        const Uint8 *wc = side ? WALL_EW : WALL_NS;
        /* distance shading */
        double shade = 1.0 - fmin(perp / 8.0, 0.85);
        Uint8  wr = (Uint8)(wc[0] * shade);
        Uint8  wg = (Uint8)(wc[1] * shade);
        Uint8  wb = (Uint8)(wc[2] * shade);
        Uint32 col_argb = (0xFF000000) | (wr << 16) | (wg << 8) | wb;

        int draw_top = top < 0 ? 0 : top;
        int draw_bot = bot > H ? H : bot;
        for (int y = draw_top; y < draw_bot; y++)
            g.pixels[y * pitch + col] = col_argb;
    }
}

/* ─── Minimap ─────────────────────────────────────────────────── */

static void draw_minimap(void) {
    int s = MINIMAP_SCALE;
    for (int my = 0; my < MAP_H; my++) {
        for (int mx = 0; mx < MAP_W; mx++) {
            SDL_Rect r = { MINIMAP_X + mx*s, MINIMAP_Y + my*s, s-1, s-1 };
            if (MAP[my][mx]) SDL_SetRenderDrawColor(g.ren, 0x55, 0x88, 0xAA, 200);
            else              SDL_SetRenderDrawColor(g.ren, 0x10, 0x10, 0x1A, 200);
            SDL_RenderFillRect(g.ren, &r);
        }
    }
    /* player dot */
    int pdx = MINIMAP_X + (int)(g.px * s);
    int pdy = MINIMAP_Y + (int)(g.py * s);
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xFF, 0x00, 255);
    SDL_Rect pd = { pdx-2, pdy-2, 5, 5 };
    SDL_RenderFillRect(g.ren, &pd);
    /* direction line */
    SDL_SetRenderDrawColor(g.ren, 0xFF, 0xAA, 0x00, 255);
    SDL_RenderDrawLine(g.ren, pdx, pdy,
        pdx + (int)(cos(g.pa)*8), pdy + (int)(sin(g.pa)*8));
}

/* ─── Crosshair ───────────────────────────────────────────────── */

static void draw_crosshair(void) {
    SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 160);
    SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
    SDL_RenderDrawLine(g.ren, W/2-8, H/2, W/2+8, H/2);
    SDL_RenderDrawLine(g.ren, W/2, H/2-8, W/2, H/2+8);
    SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_NONE);
}

/* ─── Input / Update / Render ────────────────────────────────── */

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYDOWN) {
            switch (ev.key.keysym.sym) {
                case SDLK_w: case SDLK_UP:    g.key_w = 1; break;
                case SDLK_s: case SDLK_DOWN:   g.key_s = 1; break;
                case SDLK_a:                    g.key_a = 1; break;
                case SDLK_d:                    g.key_d = 1; break;
                case SDLK_LEFT:                 g.key_left  = 1; break;
                case SDLK_RIGHT:                g.key_right = 1; break;
            }
        }
        if (ev.type == SDL_KEYUP) {
            switch (ev.key.keysym.sym) {
                case SDLK_w: case SDLK_UP:    g.key_w = 0; break;
                case SDLK_s: case SDLK_DOWN:   g.key_s = 0; break;
                case SDLK_a:                    g.key_a = 0; break;
                case SDLK_d:                    g.key_d = 0; break;
                case SDLK_LEFT:                 g.key_left  = 0; break;
                case SDLK_RIGHT:                g.key_right = 0; break;
            }
        }
        if (ev.type == SDL_MOUSEMOTION) {
            g.pa += ev.motion.xrel * 0.003;
        }
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

    double nx = g.px, ny = g.py;
    double cdx = cos(g.pa), cdy = sin(g.pa);
    double ldx = cos(g.pa - M_PI/2), ldy = sin(g.pa - M_PI/2);

    if (g.key_w) { nx += cdx * MOVE_SPD; ny += cdy * MOVE_SPD; }
    if (g.key_s) { nx -= cdx * MOVE_SPD; ny -= cdy * MOVE_SPD; }
    if (g.key_a) { nx += ldx * MOVE_SPD; ny += ldy * MOVE_SPD; }
    if (g.key_d) { nx -= ldx * MOVE_SPD; ny -= ldy * MOVE_SPD; }

    /* wall sliding */
    double margin = 0.25;
    if (!solid(nx, g.py)) g.px = nx;
    else if (!solid(nx, g.py + (g.py > ny + margin ? -margin : margin))) {
        g.px = nx;
        g.py += (g.py > ny + margin ? -margin : margin);
    }
    if (!solid(g.px, ny)) g.py = ny;
    else if (!solid(g.px + (g.px > nx + margin ? -margin : margin), ny)) {
        g.py += (g.px > nx + margin ? -margin : margin);
        g.py = ny;
    }
}

static void render(void) {
    /* cast rays into pixel buffer */
    cast_rays();

    /* upload framebuffer to texture */
    SDL_UpdateTexture(g.fb, NULL, g.pixels, W * sizeof(Uint32));
    SDL_RenderCopy(g.ren, g.fb, NULL, NULL);

    /* minimap on top */
    draw_minimap();
    draw_crosshair();

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
    SDL_Init(SDL_INIT_VIDEO);
    g.win = SDL_CreateWindow("Maze",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H, SDL_WINDOW_SHOWN);
    g.ren = SDL_CreateRenderer(g.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    /* streaming texture as pixel framebuffer */
    g.fb = SDL_CreateTexture(g.ren, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, W, H);
    g.pixels = (Uint32 *)malloc(W * H * sizeof(Uint32));

    /* start position — centre of first open cell */
    g.px = 1.5; g.py = 1.5;
    g.pa = 0.0;

    emscripten_set_main_loop(main_loop, 0, 1);

    free(g.pixels);
    SDL_DestroyTexture(g.fb);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
