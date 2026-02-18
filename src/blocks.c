/*
 * blocks.c — Tetris-style falling blocks
 * C + SDL2 + Emscripten → WebAssembly
 */

#include <SDL2/SDL.h>
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define COLS       10
#define ROWS       20
#define CELL       28
#define SIDEBAR    120
#define W          (COLS * CELL + SIDEBAR)
#define H          (ROWS * CELL)
#define BOARD_W    (COLS * CELL)

/* 7 tetrominoes — each defined as 4 (x,y) offsets from pivot */
static const int PIECES[7][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}},  /* I */
    {{0,0},{1,0},{0,1},{1,1}},  /* O */
    {{1,0},{0,1},{1,1},{2,1}},  /* T */
    {{0,0},{1,0},{1,1},{2,1}},  /* S */
    {{1,0},{2,0},{0,1},{1,1}},  /* Z */
    {{0,0},{0,1},{1,1},{2,1}},  /* L */
    {{2,0},{0,1},{1,1},{2,1}},  /* J */
};

/* Colours per piece */
static const Uint8 COLORS[7][3] = {
    {0x00,0xF0,0xF0}, /* I — cyan  */
    {0xF0,0xF0,0x00}, /* O — yellow*/
    {0xA0,0x00,0xF0}, /* T — purple*/
    {0x00,0xF0,0x00}, /* S — green */
    {0xF0,0x00,0x00}, /* Z — red   */
    {0xF0,0xA0,0x00}, /* L — orange*/
    {0x00,0x00,0xF0}, /* J — blue  */
};

typedef struct {
    int board[ROWS][COLS];      /* 0=empty, 1-7=colour index */
    int px, py;                 /* current piece position */
    int piece[4][2];            /* current piece cells */
    int ptype;                  /* 0-6 */
    int next_type;
    int next_piece[4][2];
    Uint32 last_drop;
    int    drop_ms;             /* ms between automatic drops */
    int    score, lines, level;
    int    dead;
    SDL_Window   *win;
    SDL_Renderer *ren;
    int    key_left, key_right, key_down;
    Uint32 key_left_t, key_right_t;
    int    rot_pressed, hard_pressed;
} Game;

static Game g;

/* ─── Piece helpers ──────────────────────────────────────────── */

static void load_piece(int type, int piece[4][2]) {
    for (int i = 0; i < 4; i++) {
        piece[i][0] = PIECES[type][i][0];
        piece[i][1] = PIECES[type][i][1];
    }
}

static int collides(int piece[4][2], int px, int py) {
    for (int i = 0; i < 4; i++) {
        int x = px + piece[i][0];
        int y = py + piece[i][1];
        if (x < 0 || x >= COLS || y >= ROWS) return 1;
        if (y >= 0 && g.board[y][x]) return 1;
    }
    return 0;
}

static void stamp(void) {
    for (int i = 0; i < 4; i++) {
        int x = g.px + g.piece[i][0];
        int y = g.py + g.piece[i][1];
        if (y >= 0) g.board[y][x] = g.ptype + 1;
    }
}

static int clear_lines(void) {
    int cleared = 0;
    for (int r = ROWS-1; r >= 0; ) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!g.board[r][c]) { full=0; break; }
        if (full) {
            memmove(g.board[1], g.board[0], sizeof(int)*COLS*r);
            memset(g.board[0], 0, sizeof(int)*COLS);
            cleared++;
        } else {
            r--;
        }
    }
    return cleared;
}

static void rotate_piece(int piece[4][2], int out[4][2]) {
    /* rotate 90° CW around pivot (1,1) */
    for (int i = 0; i < 4; i++) {
        int ox = piece[i][0] - 1;
        int oy = piece[i][1] - 1;
        out[i][0] =  oy + 1;
        out[i][1] = -ox + 1;
    }
}

static void spawn_piece(void) {
    g.ptype = g.next_type;
    memcpy(g.piece, g.next_piece, sizeof(g.piece));
    g.px = COLS/2 - 2;
    g.py = 0;

    g.next_type = rand() % 7;
    load_piece(g.next_type, g.next_piece);

    if (collides(g.piece, g.px, g.py)) g.dead = 1;
}

static void init_game(void) {
    memset(g.board, 0, sizeof(g.board));
    g.score  = 0;
    g.lines  = 0;
    g.level  = 1;
    g.dead   = 0;
    g.drop_ms = 500;
    g.key_left = g.key_right = g.key_down = 0;
    g.rot_pressed = g.hard_pressed = 0;
    g.last_drop = SDL_GetTicks();

    g.next_type = rand() % 7;
    load_piece(g.next_type, g.next_piece);
    spawn_piece();
}

/* ─── Input / Update / Render ────────────────────────────────── */

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            switch (ev.key.keysym.sym) {
                case SDLK_LEFT:  case SDLK_a: g.key_left=1; g.key_left_t=SDL_GetTicks(); break;
                case SDLK_RIGHT: case SDLK_d: g.key_right=1; g.key_right_t=SDL_GetTicks(); break;
                case SDLK_DOWN:  case SDLK_s: g.key_down=1; break;
                case SDLK_UP:    case SDLK_w: {
                    /* rotate */
                    int tmp[4][2];
                    rotate_piece(g.piece, tmp);
                    if (!collides(tmp, g.px, g.py))
                        memcpy(g.piece, tmp, sizeof(g.piece));
                } break;
                case SDLK_SPACE: {
                    /* hard drop */
                    while (!collides(g.piece, g.px, g.py+1)) g.py++;
                    stamp();
                    int cl = clear_lines();
                    g.lines += cl;
                    static const int pts[5]={0,100,300,500,800};
                    g.score += (cl < 5 ? pts[cl] : 800) * g.level;
                    g.level = 1 + g.lines / 10;
                    g.drop_ms = 500 - (g.level-1)*40;
                    if (g.drop_ms < 60) g.drop_ms = 60;
                    g.last_drop = SDL_GetTicks();
                    spawn_piece();
                } break;
                case SDLK_r: init_game(); break;
            }
        }
        if (ev.type == SDL_KEYUP) {
            switch (ev.key.keysym.sym) {
                case SDLK_LEFT:  case SDLK_a: g.key_left=0; break;
                case SDLK_RIGHT: case SDLK_d: g.key_right=0; break;
                case SDLK_DOWN:  case SDLK_s: g.key_down=0; break;
            }
        }
    }
}

static void update(void) {
    if (g.dead) return;
    Uint32 now = SDL_GetTicks();

    /* lateral movement (with auto-repeat after 150ms) */
    static Uint32 last_h = 0;
    if ((g.key_left && (int)(now-g.key_left_t)>150) ||
        (g.key_right && (int)(now-g.key_right_t)>150)) {
        if ((int)(now-last_h) > 60) {
            int dx = g.key_left ? -1 : 1;
            if (!collides(g.piece, g.px+dx, g.py)) g.px += dx;
            last_h = now;
        }
    }

    /* gravity */
    int ms = g.key_down ? 60 : g.drop_ms;
    if ((int)(now - g.last_drop) >= ms) {
        g.last_drop = now;
        if (!collides(g.piece, g.px, g.py+1)) {
            g.py++;
        } else {
            stamp();
            int cl = clear_lines();
            g.lines += cl;
            static const int pts[5]={0,100,300,500,800};
            g.score += (cl < 5 ? pts[cl] : 800) * g.level;
            g.level = 1 + g.lines / 10;
            g.drop_ms = 500 - (g.level-1)*40;
            if (g.drop_ms < 60) g.drop_ms = 60;
            spawn_piece();
        }
    }
}

/* Draw a filled cell with a slight inner highlight */
static void draw_cell(int x, int y, Uint8 r, Uint8 gg, Uint8 b) {
    SDL_Rect rect = { x*CELL+1, y*CELL+1, CELL-2, CELL-2 };
    SDL_SetRenderDrawColor(g.ren, r, gg, b, 255);
    SDL_RenderFillRect(g.ren, &rect);
    SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 60);
    SDL_RenderDrawLine(g.ren, rect.x, rect.y, rect.x+rect.w-1, rect.y);
    SDL_RenderDrawLine(g.ren, rect.x, rect.y, rect.x, rect.y+rect.h-1);
}

/* Ghost piece (shadow showing where piece will land) */
static void draw_ghost(void) {
    int gy = g.py;
    while (!collides(g.piece, g.px, gy+1)) gy++;
    for (int i = 0; i < 4; i++) {
        int cx = g.px + g.piece[i][0];
        int cy = gy  + g.piece[i][1];
        if (cy < 0) continue;
        SDL_Rect rect = { cx*CELL+1, cy*CELL+1, CELL-2, CELL-2 };
        SDL_SetRenderDrawColor(g.ren, 80, 80, 80, 255);
        SDL_RenderFillRect(g.ren, &rect);
    }
}

static void render(void) {
    /* background */
    SDL_SetRenderDrawColor(g.ren, 0x08, 0x08, 0x12, 255);
    SDL_RenderClear(g.ren);

    /* board background */
    SDL_SetRenderDrawColor(g.ren, 0x10, 0x10, 0x1C, 255);
    SDL_Rect board_bg = {0, 0, BOARD_W, H};
    SDL_RenderFillRect(g.ren, &board_bg);

    /* grid lines */
    SDL_SetRenderDrawColor(g.ren, 0x1A, 0x1A, 0x28, 255);
    for (int c = 1; c < COLS; c++) SDL_RenderDrawLine(g.ren, c*CELL, 0, c*CELL, H);
    for (int r = 1; r < ROWS; r++) SDL_RenderDrawLine(g.ren, 0, r*CELL, BOARD_W, r*CELL);

    /* board cells */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int t = g.board[r][c];
            if (!t) continue;
            const Uint8 *col = COLORS[t-1];
            draw_cell(c, r, col[0], col[1], col[2]);
        }
    }

    /* ghost */
    draw_ghost();

    /* current piece */
    for (int i = 0; i < 4; i++) {
        int cx = g.px + g.piece[i][0];
        int cy = g.py + g.piece[i][1];
        if (cy < 0) continue;
        const Uint8 *col = COLORS[g.ptype];
        draw_cell(cx, cy, col[0], col[1], col[2]);
    }

    /* sidebar */
    SDL_SetRenderDrawColor(g.ren, 0x12, 0x12, 0x20, 255);
    SDL_Rect sb = {BOARD_W, 0, SIDEBAR, H};
    SDL_RenderFillRect(g.ren, &sb);

    /* next piece preview */
    SDL_SetRenderDrawColor(g.ren, 0x20, 0x20, 0x35, 255);
    SDL_Rect nb = {BOARD_W+10, 10, SIDEBAR-20, 80};
    SDL_RenderFillRect(g.ren, &nb);

    for (int i = 0; i < 4; i++) {
        int cx = g.next_piece[i][0];
        int cy = g.next_piece[i][1];
        SDL_Rect cell = { BOARD_W+20 + cx*20, 20 + cy*20, 18, 18 };
        const Uint8 *col = COLORS[g.next_type];
        SDL_SetRenderDrawColor(g.ren, col[0], col[1], col[2], 255);
        SDL_RenderFillRect(g.ren, &cell);
    }

    /* level/lines progress bar */
    int bar_h = (g.lines % 10) * (H - 100) / 10;
    SDL_Rect bar = {BOARD_W+10, H-10-bar_h, SIDEBAR-20, bar_h};
    SDL_SetRenderDrawColor(g.ren, 0x60, 0xDD, 0x80, 200);
    SDL_RenderFillRect(g.ren, &bar);

    /* dead overlay */
    if (g.dead) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 180);
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
    srand((unsigned)time(NULL));
    SDL_Init(SDL_INIT_VIDEO);
    g.win = SDL_CreateWindow("Blocks",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN);
    g.ren = SDL_CreateRenderer(g.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
