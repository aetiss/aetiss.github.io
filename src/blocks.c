/*
 * blocks.c — Tetris-style falling blocks
 * C + SDL3 + Emscripten → WebAssembly
 */

#include <SDL3/SDL.h>
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define COLS    10
#define ROWS    20
#define CELL    28
#define SIDEBAR 120
#define W       (COLS * CELL + SIDEBAR)
#define H       (ROWS * CELL)
#define BOARD_W (COLS * CELL)

static const int PIECES[7][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}},  /* I */
    {{0,0},{1,0},{0,1},{1,1}},  /* O */
    {{1,0},{0,1},{1,1},{2,1}},  /* T */
    {{0,0},{1,0},{1,1},{2,1}},  /* S */
    {{1,0},{2,0},{0,1},{1,1}},  /* Z */
    {{0,0},{0,1},{1,1},{2,1}},  /* L */
    {{2,0},{0,1},{1,1},{2,1}},  /* J */
};

static const Uint8 COLORS[7][3] = {
    {0x00,0xF0,0xF0},
    {0xF0,0xF0,0x00},
    {0xA0,0x00,0xF0},
    {0x00,0xF0,0x00},
    {0xF0,0x00,0x00},
    {0xF0,0xA0,0x00},
    {0x00,0x00,0xF0},
};

typedef struct {
    int  board[ROWS][COLS];
    int  px, py;
    int  piece[4][2];
    int  ptype;
    int  next_type;
    int  next_piece[4][2];
    Uint64 last_drop;
    int  drop_ms;
    int  score, lines, level;
    int  dead;
    SDL_Window   *win;
    SDL_Renderer *ren;
    int  key_left, key_right, key_down;
    Uint64 key_left_t, key_right_t;
} Game;

static Game g;

static void load_piece(int type, int piece[4][2]) {
    for (int i = 0; i < 4; i++) {
        piece[i][0] = PIECES[type][i][0];
        piece[i][1] = PIECES[type][i][1];
    }
}

static int collides(int piece[4][2], int px, int py) {
    for (int i = 0; i < 4; i++) {
        int x = px + piece[i][0], y = py + piece[i][1];
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
        } else r--;
    }
    return cleared;
}

static void rotate_cw(int in[4][2], int out[4][2]) {
    for (int i = 0; i < 4; i++) {
        out[i][0] =  (in[i][1] - 1) + 1;
        out[i][1] = -(in[i][0] - 1) + 1;
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

static void lock_and_spawn(void) {
    stamp();
    int cl = clear_lines();
    g.lines += cl;
    static const int pts[5] = {0,100,300,500,800};
    g.score += (cl < 5 ? pts[cl] : 800) * g.level;
    g.level = 1 + g.lines / 10;
    g.drop_ms = 500 - (g.level-1)*40;
    if (g.drop_ms < 60) g.drop_ms = 60;
    g.last_drop = SDL_GetTicks();
    spawn_piece();
}

static void init_game(void) {
    memset(g.board, 0, sizeof(g.board));
    g.score = 0; g.lines = 0; g.level = 1;
    g.dead  = 0; g.drop_ms = 500;
    g.key_left = g.key_right = g.key_down = 0;
    g.last_drop = SDL_GetTicks();
    g.next_type = rand() % 7;
    load_piece(g.next_type, g.next_piece);
    spawn_piece();
}

static void handle_input(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            switch (ev.key.key) {
                case SDLK_LEFT:  case SDLK_A:
                    g.key_left = 1; g.key_left_t = SDL_GetTicks();
                    if (!collides(g.piece, g.px-1, g.py)) g.px--;
                    break;
                case SDLK_RIGHT: case SDLK_D:
                    g.key_right = 1; g.key_right_t = SDL_GetTicks();
                    if (!collides(g.piece, g.px+1, g.py)) g.px++;
                    break;
                case SDLK_DOWN:  case SDLK_S: g.key_down = 1; break;
                case SDLK_UP:    case SDLK_W: {
                    int tmp[4][2];
                    rotate_cw(g.piece, tmp);
                    if (!collides(tmp, g.px, g.py)) memcpy(g.piece, tmp, sizeof(g.piece));
                } break;
                case SDLK_SPACE: {
                    while (!collides(g.piece, g.px, g.py+1)) g.py++;
                    lock_and_spawn();
                } break;
                case SDLK_R: init_game(); break;
            }
        }
        if (ev.type == SDL_EVENT_KEY_UP) {
            switch (ev.key.key) {
                case SDLK_LEFT:  case SDLK_A: g.key_left  = 0; break;
                case SDLK_RIGHT: case SDLK_D: g.key_right = 0; break;
                case SDLK_DOWN:  case SDLK_S: g.key_down  = 0; break;
            }
        }
    }
}

static void update(void) {
    if (g.dead) return;
    Uint64 now = SDL_GetTicks();

    /* auto-repeat horizontal */
    static Uint64 last_h = 0;
    if (g.key_left && (int)(now - g.key_left_t) > 150 && (int)(now - last_h) > 60) {
        if (!collides(g.piece, g.px-1, g.py)) g.px--;
        last_h = now;
    }
    if (g.key_right && (int)(now - g.key_right_t) > 150 && (int)(now - last_h) > 60) {
        if (!collides(g.piece, g.px+1, g.py)) g.px++;
        last_h = now;
    }

    /* gravity */
    int ms = g.key_down ? 60 : g.drop_ms;
    if ((int)(now - g.last_drop) >= ms) {
        g.last_drop = now;
        if (!collides(g.piece, g.px, g.py+1)) g.py++;
        else lock_and_spawn();
    }
}

static void draw_cell(int x, int y, Uint8 r, Uint8 gg, Uint8 b) {
    SDL_FRect rect = { (float)(x*CELL)+1, (float)(y*CELL)+1,
                       (float)CELL-2, (float)CELL-2 };
    SDL_SetRenderDrawColor(g.ren, r, gg, b, 255);
    SDL_RenderFillRect(g.ren, &rect);
    SDL_SetRenderDrawColor(g.ren, 255, 255, 255, 50);
    SDL_RenderLine(g.ren, rect.x, rect.y, rect.x+rect.w-1, rect.y);
    SDL_RenderLine(g.ren, rect.x, rect.y, rect.x, rect.y+rect.h-1);
}

static void draw_ghost(void) {
    int gy = g.py;
    while (!collides(g.piece, g.px, gy+1)) gy++;
    SDL_SetRenderDrawColor(g.ren, 60, 60, 80, 255);
    for (int i = 0; i < 4; i++) {
        int cx = g.px + g.piece[i][0];
        int cy = gy  + g.piece[i][1];
        if (cy < 0) continue;
        SDL_FRect r2 = { (float)(cx*CELL)+1, (float)(cy*CELL)+1,
                         (float)CELL-2, (float)CELL-2 };
        SDL_RenderFillRect(g.ren, &r2);
    }
}

static void render(void) {
    SDL_SetRenderDrawColor(g.ren, 0x08, 0x08, 0x12, 255);
    SDL_RenderClear(g.ren);

    /* board bg */
    SDL_SetRenderDrawColor(g.ren, 0x10, 0x10, 0x1C, 255);
    SDL_FRect bb = {0, 0, (float)BOARD_W, (float)H};
    SDL_RenderFillRect(g.ren, &bb);

    /* grid */
    SDL_SetRenderDrawColor(g.ren, 0x1A, 0x1A, 0x28, 255);
    for (int c = 1; c < COLS; c++)
        SDL_RenderLine(g.ren, (float)(c*CELL), 0, (float)(c*CELL), (float)H);
    for (int r = 1; r < ROWS; r++)
        SDL_RenderLine(g.ren, 0, (float)(r*CELL), (float)BOARD_W, (float)(r*CELL));

    /* board cells */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (g.board[r][c])
                draw_cell(c, r, COLORS[g.board[r][c]-1][0],
                                COLORS[g.board[r][c]-1][1],
                                COLORS[g.board[r][c]-1][2]);

    draw_ghost();

    /* active piece */
    for (int i = 0; i < 4; i++) {
        int cx = g.px + g.piece[i][0];
        int cy = g.py + g.piece[i][1];
        if (cy < 0) continue;
        draw_cell(cx, cy, COLORS[g.ptype][0], COLORS[g.ptype][1], COLORS[g.ptype][2]);
    }

    /* sidebar */
    SDL_SetRenderDrawColor(g.ren, 0x12, 0x12, 0x20, 255);
    SDL_FRect sb = {(float)BOARD_W, 0, (float)SIDEBAR, (float)H};
    SDL_RenderFillRect(g.ren, &sb);

    /* next piece preview box */
    SDL_SetRenderDrawColor(g.ren, 0x20, 0x20, 0x35, 255);
    SDL_FRect nb = {(float)BOARD_W+10, 10, (float)SIDEBAR-20, 80};
    SDL_RenderFillRect(g.ren, &nb);

    for (int i = 0; i < 4; i++) {
        int cx = g.next_piece[i][0];
        int cy = g.next_piece[i][1];
        SDL_FRect cell = { (float)(BOARD_W+20 + cx*20), (float)(20 + cy*20), 18, 18 };
        const Uint8 *col = COLORS[g.next_type];
        SDL_SetRenderDrawColor(g.ren, col[0], col[1], col[2], 255);
        SDL_RenderFillRect(g.ren, &cell);
    }

    /* level progress bar */
    int bar_h = (g.lines % 10) * (H - 100) / 10;
    SDL_FRect bar = {(float)BOARD_W+10, (float)(H-10-bar_h), (float)SIDEBAR-20, (float)bar_h};
    SDL_SetRenderDrawColor(g.ren, 0x60, 0xDD, 0x80, 200);
    SDL_RenderFillRect(g.ren, &bar);

    if (g.dead) {
        SDL_SetRenderDrawBlendMode(g.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 180);
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
    g.win = SDL_CreateWindow("Blocks", W, H, 0);
    g.ren = SDL_CreateRenderer(g.win, NULL);
    init_game();
    emscripten_set_main_loop(main_loop, 0, 1);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
