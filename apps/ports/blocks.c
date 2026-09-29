/* Blocks: a falling-block puzzle game for the native GEM toolkit.
 * Original code, GPL-2.0-or-later. Rules live in blocks_core.c; this file
 * only draws, reads the keyboard / controller and stores high scores. */
#include "app.h"
#include "blocks_core.h"
#include "drives.h"
#include "dreamcast/control.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CELL 20
#define WELL_X 220
#define WELL_Y 36
#define MINI 16

/* Palette indices: piece colour = piece + 2. */
enum { C_BG = 0, C_TEXT = 1, C_SHADOW = 9, C_GRID = 10, C_PANEL = 11, C_LIGHT = 12, C_GHOST = 13, C_GOLD = 14, C_RED = 15 };
static const short pal[16][3] = {
    {0, 0, 0},       {1000, 1000, 1000}, {0, 850, 900},     {950, 900, 0},   {650, 200, 800}, {150, 750, 200},
    {900, 150, 150}, {150, 250, 900},    {950, 550, 100},   {200, 200, 260}, {130, 130, 180}, {60, 60, 140},
    {880, 880, 920}, {600, 600, 700},    {1000, 800, 200},  {1000, 250, 250}};

static struct blocks g;
static struct blocks_score scores[BL_SCORES];
static char score_path[16];
static uint8_t shown[BL_H][BL_W];
static int shown_next = -2, shown_hold = -2, shown_hold_dim = -1;
static long shown_score = -1;
static int shown_level = -1, shown_lines = -1;

/* Controller support via the optional input_snapshot extension. */
static int pad_ok(void)
{
    return dc_os && dc_os->size >= offsetof(struct dc_native_api, input_snapshot) + sizeof(dc_os->input_snapshot) &&
           dc_os->input_snapshot;
}
static uint32_t pad_read(void)
{
    struct dc_input_snapshot s;
    if (!pad_ok() || dc_os->input_snapshot(&s, sizeof(s)) != (long)sizeof(s) || !(s.present & DC_INPUT_CONTROLLER))
        return 0;
    return s.controller_buttons;
}
#define PAD_B (1u << 1)
#define PAD_A (1u << 2)
#define PAD_START (1u << 3)
#define PAD_UP (1u << 4)
#define PAD_DOWN (1u << 5)
#define PAD_LEFT (1u << 6)
#define PAD_RIGHT (1u << 7)
#define PAD_Y (1u << 9)
#define PAD_X (1u << 10)

static void cell(int px, int py, int size, int colour)
{
    app_box(px, py, size, size, colour);
    app_box(px, py, size, 2, C_LIGHT);
    app_box(px, py, 2, size, C_LIGHT);
    app_box(px, py + size - 2, size, 2, C_SHADOW);
    app_box(px + size - 2, py, 2, size, C_SHADOW);
}
static void empty_cell(int px, int py)
{
    app_box(px, py, CELL, CELL, C_BG);
    app_box(px + CELL - 1, py + CELL - 1, 1, 1, C_GRID);
}
static void ghost_cell(int px, int py)
{
    app_box(px, py, CELL, CELL, C_BG);
    app_box(px + 2, py + 2, CELL - 4, 2, C_GHOST);
    app_box(px + 2, py + CELL - 4, CELL - 4, 2, C_GHOST);
    app_box(px + 2, py + 2, 2, CELL - 4, C_GHOST);
    app_box(px + CELL - 4, py + 2, 2, CELL - 4, C_GHOST);
}
static void draw_cell_code(int col, int row, int code)
{
    int px = WELL_X + col * CELL, py = WELL_Y + row * CELL;
    if (!code)
        empty_cell(px, py);
    else if (code & 0x10)
        ghost_cell(px, py);
    else
        cell(px, py, CELL, code + 1);
}
static void draw_well(int force)
{
    uint8_t now[BL_H][BL_W];
    memcpy(now, g.well, sizeof(now));
    if (!g.over) {
        int gy = blocks_ghost_y(&g);
        const int8_t *c = blocks_cells(g.piece, g.rot);
        for (int i = 0; i < 4; i++) {
            int cx = g.x + c[i * 2], cy = gy + c[i * 2 + 1];
            if (cy >= 0 && !now[cy][cx])
                now[cy][cx] = (uint8_t)(0x10 | (g.piece + 1));
        }
        for (int i = 0; i < 4; i++) {
            int cx = g.x + c[i * 2], cy = g.y + c[i * 2 + 1];
            if (cy >= 0)
                now[cy][cx] = (uint8_t)(g.piece + 1);
        }
    }
    for (int r = 0; r < BL_H; r++)
        for (int c = 0; c < BL_W; c++)
            if (force || now[r][c] != shown[r][c]) {
                draw_cell_code(c, r, now[r][c]);
                shown[r][c] = now[r][c];
            }
}
static void invalidate(void)
{
    memset(shown, 0xff, sizeof(shown));
    shown_next = shown_hold = -2;
    shown_hold_dim = -1;
    shown_score = -1;
    shown_level = shown_lines = -1;
}
/* A piece centred in a 96x72 preview box at (x,y). */
static void preview(int x, int y, int piece, int dim)
{
    app_box(x, y, 96, 72, C_PANEL);
    if (piece < 0)
        return;
    const int8_t *c = blocks_cells(piece, 0);
    int minc = 9, maxc = -1, minr = 9, maxr = -1;
    for (int i = 0; i < 4; i++) {
        if (c[i * 2] < minc) minc = c[i * 2];
        if (c[i * 2] > maxc) maxc = c[i * 2];
        if (c[i * 2 + 1] < minr) minr = c[i * 2 + 1];
        if (c[i * 2 + 1] > maxr) maxr = c[i * 2 + 1];
    }
    int ox = x + (96 - (maxc - minc + 1) * MINI) / 2, oy = y + (72 - (maxr - minr + 1) * MINI) / 2;
    for (int i = 0; i < 4; i++)
        if (dim)
            app_box(ox + (c[i * 2] - minc) * MINI, oy + (c[i * 2 + 1] - minr) * MINI, MINI, MINI, C_GRID);
        else
            cell(ox + (c[i * 2] - minc) * MINI, oy + (c[i * 2 + 1] - minr) * MINI, MINI, piece + 2);
}
static void draw_panels(void)
{
    char s[40];
    if (shown_hold != g.hold || shown_hold_dim != g.hold_used) {
        preview(60, 66, g.hold, g.hold_used);
        shown_hold = g.hold;
        shown_hold_dim = g.hold_used;
    }
    if (shown_next != g.next) {
        preview(444, 66, g.next, 0);
        shown_next = g.next;
    }
    if (shown_score != g.score || shown_level != g.level || shown_lines != g.lines) {
        app_box(40, 160, 150, 90, C_BG);
        snprintf(s, sizeof(s), "Score %ld", g.score);
        app_text(44, 176, s, C_TEXT);
        snprintf(s, sizeof(s), "Level %d", g.level);
        app_text(44, 208, s, C_TEXT);
        snprintf(s, sizeof(s), "Lines %d", g.lines);
        app_text(44, 240, s, C_TEXT);
        shown_score = g.score;
        shown_level = g.level;
        shown_lines = g.lines;
    }
}
static void draw_scores(int x, int y)
{
    char s[40];
    app_text(x, y, "BEST", C_GOLD);
    for (int i = 0; i < BL_SCORES; i++) {
        snprintf(s, sizeof(s), "%d. %s %7ld", i + 1, scores[i].name, scores[i].score);
        app_text(x, y + 26 + i * 22, s, C_TEXT);
    }
}
static void draw_frame(void)
{
    app_clear(C_BG);
    app_box(WELL_X - 4, WELL_Y - 4, BL_W * CELL + 8, BL_H * CELL + 8, C_LIGHT);
    app_box(WELL_X - 2, WELL_Y - 2, BL_W * CELL + 4, BL_H * CELL + 4, C_SHADOW);
    app_text(80, 52, "HOLD", C_GOLD);
    app_text(468, 52, "NEXT", C_GOLD);
    draw_scores(444, 176);
    app_text(444, 330, "P pause", C_GRID);
    app_text(444, 352, "Esc menu", C_GRID);
    app_status("Arrows:move Up/X:turn Z:turn back Space:drop C:hold P:pause Esc:menu");
    invalidate();
}
static void redraw_all(void)
{
    draw_frame();
    draw_well(1);
    draw_panels();
}
static void banner(const char *a, const char *b)
{
    int x = WELL_X + 8, y = WELL_Y + 150;
    app_box(x, y, BL_W * CELL - 16, 84, C_PANEL);
    app_box(x, y, BL_W * CELL - 16, 2, C_LIGHT);
    app_box(x, y + 82, BL_W * CELL - 16, 2, C_SHADOW);
    app_text(x + (BL_W * CELL - 16 - 8 * (int)strlen(a)) / 2, y + 34, a, C_GOLD);
    if (b)
        app_text(x + (BL_W * CELL - 16 - 8 * (int)strlen(b)) / 2, y + 62, b, C_TEXT);
}
static void save_scores(void)
{
    blocks_scores_path(score_path, sizeof(score_path));
    if (!blocks_scores_save(scores, score_path)) {
        /* fall back to the RAM disk so a full or removed card does not lose the score */
        snprintf(score_path, sizeof(score_path), "C:\\BLOCKS.HI");
        blocks_scores_save(scores, score_path);
    }
}
static void load_scores(void)
{
    blocks_scores_path(score_path, sizeof(score_path));
    if (!blocks_scores_load(scores, score_path) && score_path[0] != 'C')
        blocks_scores_load(scores, "C:\\BLOCKS.HI");
}

enum { A_NONE, A_START, A_EXIT, A_LEVEL_UP, A_LEVEL_DOWN };
static uint32_t pad_prev;
/* Common menu/overlay input: returns an A_* action or A_NONE. */
static int menu_input(int wait_ms)
{
    int key = app_event(wait_ms, NULL, NULL, NULL), a = tolower(key & 255);
    uint32_t pad = pad_read(), edge = pad & ~pad_prev;
    pad_prev = pad;
    if (a == 27 || (edge & PAD_B))
        return A_EXIT;
    if (a == 13 || a == ' ' || (edge & (PAD_A | PAD_START)))
        return A_START;
    if (key == KEY_UP || key == KEY_RIGHT || a == '+' || a == '=' || (edge & (PAD_UP | PAD_RIGHT)))
        return A_LEVEL_UP;
    if (key == KEY_DOWN || key == KEY_LEFT || a == '-' || (edge & (PAD_DOWN | PAD_LEFT)))
        return A_LEVEL_DOWN;
    return A_NONE;
}
static void title_screen_draw(int level, long last)
{
    char s[64];
    app_clear(C_BG);
    for (int i = 0; i < BL_PIECES; i++) {
        const int8_t *c = blocks_cells(i, 0);
        int ox = 60 + i * 80;
        for (int k = 0; k < 4; k++)
            cell(ox + c[k * 2] * 16, 56 + c[k * 2 + 1] * 16, 16, i + 2);
    }
    app_text(272, 150, "B L O C K S", C_GOLD);
    app_text(60, 190, "Clear lines by filling rows of the 10x20 well.", C_TEXT);
    app_text(60, 220, "Keyboard: arrows move, Up/X turn right, Z turn left,", C_TEXT);
    app_text(60, 240, "  Down soft drop, Space hard drop, C hold, P pause.", C_TEXT);
    app_text(60, 270, "Controller: D-pad move/soft drop, Up hard drop,", C_TEXT);
    app_text(60, 290, "  A turn right, B turn left, X hold, Start pause.", C_TEXT);
    snprintf(s, sizeof(s), "Start level: %d  (Up/Down to change)", level);
    app_text(60, 330, s, C_TEXT);
    snprintf(s, sizeof(s), "Scores: %s", score_path);
    app_text(60, 360, s, C_GRID);
    if (last > 0) {
        snprintf(s, sizeof(s), "Last score: %ld", last);
        app_text(60, 385, s, C_TEXT);
    }
    for (int i = 0; i < BL_SCORES; i++) {
        snprintf(s, sizeof(s), "%d. %s %7ld", i + 1, scores[i].name, scores[i].score);
        app_text(440, 330 + i * 22, s, C_TEXT);
    }
    app_text(440, 310, "BEST", C_GOLD);
    app_status("Return/Space/A/Start: play   Esc/B: quit");
}
/* Returns start level, or 0 to quit. */
static int title_screen(int level, long last)
{
    title_screen_draw(level, last);
    pad_prev = pad_read();
    for (;;) {
        switch (menu_input(30)) {
        case A_START:
            return level;
        case A_EXIT:
            return 0;
        case A_LEVEL_UP:
            if (level < 15)
                level++;
            title_screen_draw(level, last);
            break;
        case A_LEVEL_DOWN:
            if (level > 1)
                level--;
            title_screen_draw(level, last);
            break;
        }
    }
}

/* Play one game. Returns 1 if it ended in game over, 0 if abandoned. */
static int play(int level)
{
    blocks_new(&g, (uint32_t)app_millis() * 2654435761u + 1, level);
    redraw_all();
    unsigned long last = app_millis();
    int paused = 0;
    pad_prev = pad_read();
    unsigned long rep_at[3] = {0, 0, 0}; /* left, right, down held-button repeat */
    for (;;) {
        int key = app_event(6, NULL, NULL, NULL), a = tolower(key & 255);
        unsigned long now = app_millis();
        uint32_t pad = pad_read(), edge = pad & ~pad_prev;
        pad_prev = pad;
        unsigned dt = (unsigned)(now - last);
        last = now;
        if (dt > 100)
            dt = 100;
        int ev = 0, changed = 0;
        if (a == 27 || (paused && (edge & PAD_B)))
            return 0;
        if (a == 'p' || (edge & PAD_START) || (paused && a == ' ')) {
            paused = !paused;
            if (paused)
                banner("PAUSED", "P to resume");
            else {
                invalidate();
                draw_well(1);
                draw_panels();
            }
            continue;
        }
        if (paused)
            continue;
        if (key == KEY_LEFT)
            changed |= blocks_move(&g, -1);
        else if (key == KEY_RIGHT)
            changed |= blocks_move(&g, 1);
        else if (key == KEY_DOWN)
            changed |= blocks_soft_drop(&g);
        else if (key == KEY_UP || a == 'x')
            changed |= blocks_rotate(&g, 1);
        else if (a == 'z')
            changed |= blocks_rotate(&g, -1);
        else if (a == ' ')
            ev |= blocks_hard_drop(&g);
        else if (a == 'c' || a == 'h')
            changed |= blocks_hold(&g);
        /* Controller: edges act at once, held directions auto-repeat. */
        static const uint32_t dirs[3] = {PAD_LEFT, PAD_RIGHT, PAD_DOWN};
        for (int i = 0; i < 3; i++) {
            if (edge & dirs[i])
                rep_at[i] = now + 170;
            else if ((pad & dirs[i]) && now >= rep_at[i])
                edge |= dirs[i], rep_at[i] = now + (i == 2 ? 40 : 55);
        }
        if (edge & PAD_LEFT)
            changed |= blocks_move(&g, -1);
        if (edge & PAD_RIGHT)
            changed |= blocks_move(&g, 1);
        if (edge & PAD_DOWN)
            changed |= blocks_soft_drop(&g);
        if (edge & (PAD_A))
            changed |= blocks_rotate(&g, 1);
        if (edge & PAD_B)
            changed |= blocks_rotate(&g, -1);
        if (edge & PAD_X)
            changed |= blocks_hold(&g);
        if (edge & (PAD_UP | PAD_Y))
            ev |= blocks_hard_drop(&g);
        ev |= blocks_update(&g, dt);
        if (changed || ev) {
            draw_well(0);
            draw_panels();
        } else
            draw_well(0);
        if (ev & BL_EV_OVER)
            return 1;
    }
}

int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!app_begin("BLOCKS - native SH-4"))
        return 1;
    atexit(app_end);
    for (int i = 0; i < 16; i++)
        app_palette(i, pal[i][0], pal[i][1], pal[i][2]);
    load_scores();
    int level = 1;
    long last_score = 0;
    for (;;) {
        level = title_screen(level, last_score);
        if (!level)
            break;
        int over = play(level);
        last_score = 0;
        if (!over)
            continue;
        last_score = g.score;
        draw_well(1);
        banner("GAME OVER", NULL);
        unsigned long t = app_millis();
        while (app_millis() - t < 1500)
            app_event(50, NULL, NULL, NULL);
        int rank = blocks_scores_qualify(scores, g.score);
        if (rank >= 0) {
            char who[4] = "YOU";
            char label[48];
            snprintf(label, sizeof(label), "New high score %ld! Your initials:", g.score);
            if (!app_prompt(label, who, sizeof(who)))
                who[0] = 0;
            blocks_scores_insert(scores, rank, who, g.score);
            save_scores();
        }
    }
    return 0;
}
