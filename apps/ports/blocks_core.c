/* Blocks game rules. GPL-2.0-or-later. Original code. */
#include "blocks_core.h"
#include "drives.h"
#include <stdio.h>
#include <string.h>

/* Rotation-0 cells (col,row) in each piece's SRS bounding box. */
static const int8_t base[BL_PIECES][8] = {
    {0, 1, 1, 1, 2, 1, 3, 1}, /* I (4x4) */
    {0, 0, 1, 0, 0, 1, 1, 1}, /* O (2x2) */
    {1, 0, 0, 1, 1, 1, 2, 1}, /* T */
    {1, 0, 2, 0, 0, 1, 1, 1}, /* S */
    {0, 0, 1, 0, 1, 1, 2, 1}, /* Z */
    {0, 0, 0, 1, 1, 1, 2, 1}, /* J */
    {2, 0, 0, 1, 1, 1, 2, 1}, /* L */
};
int blocks_box(int p)
{
    return p == BL_I ? 4 : p == BL_O ? 2 : 3;
}
const int8_t *blocks_cells(int p, int rot)
{
    static int8_t table[BL_PIECES][4][8];
    static int ready;
    if (!ready) {
        for (int i = 0; i < BL_PIECES; i++) {
            int n = blocks_box(i);
            memcpy(table[i][0], base[i], 8);
            for (int r = 1; r < 4; r++)
                for (int c = 0; c < 4; c++) {
                    /* clockwise: (col,row) -> (n-1-row, col) */
                    table[i][r][c * 2] = (int8_t)(n - 1 - table[i][r - 1][c * 2 + 1]);
                    table[i][r][c * 2 + 1] = table[i][r - 1][c * 2];
                }
        }
        ready = 1;
    }
    return table[p][rot & 3];
}

/* SRS kick offsets (x right, y UP as published); row = from*2 + (ccw ? 1 : 0). */
static const int8_t kicks_jlstz[8][5][2] = {
    /* 0>1 */ {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    /* 0>3 */ {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    /* 1>2 */ {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    /* 1>0 */ {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    /* 2>3 */ {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    /* 2>1 */ {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    /* 3>0 */ {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    /* 3>2 */ {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
};
static const int8_t kicks_i[8][5][2] = {
    /* 0>1 */ {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
    /* 0>3 */ {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
    /* 1>2 */ {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
    /* 1>0 */ {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    /* 2>3 */ {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    /* 2>1 */ {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
    /* 3>0 */ {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
    /* 3>2 */ {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
};

static uint32_t rnd(struct blocks *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}
static int draw_piece(struct blocks *g)
{
    if (g->bag_pos >= BL_PIECES) {
        for (int i = 0; i < BL_PIECES; i++)
            g->bag[i] = (uint8_t)i;
        for (int i = BL_PIECES - 1; i > 0; i--) {
            int j = (int)(rnd(g) % (uint32_t)(i + 1));
            uint8_t t = g->bag[i];
            g->bag[i] = g->bag[j];
            g->bag[j] = t;
        }
        g->bag_pos = 0;
    }
    return g->bag[g->bag_pos++];
}
int blocks_fits(const struct blocks *g, int p, int rot, int x, int y)
{
    const int8_t *c = blocks_cells(p, rot);
    for (int i = 0; i < 4; i++) {
        int cx = x + c[i * 2], cy = y + c[i * 2 + 1];
        if (cx < 0 || cx >= BL_W || cy >= BL_H)
            return 0;
        if (cy >= 0 && g->well[cy][cx])
            return 0;
    }
    return 1;
}
int blocks_grounded(const struct blocks *g)
{
    return !blocks_fits(g, g->piece, g->rot, g->x, g->y + 1);
}
int blocks_ghost_y(const struct blocks *g)
{
    int y = g->y;
    while (blocks_fits(g, g->piece, g->rot, g->x, y + 1))
        y++;
    return y;
}
unsigned blocks_gravity_ms(int level)
{
    static const unsigned ms[] = {1000, 793, 618, 473, 355, 262, 190, 135, 94, 64, 43, 28, 18, 11, 7};
    if (level < 1)
        level = 1;
    if (level > 15)
        level = 15;
    return ms[level - 1];
}
static int spawn(struct blocks *g, int p)
{
    g->piece = p;
    g->rot = 0;
    g->x = (BL_W - blocks_box(p)) / 2;
    g->y = p == BL_I ? -1 : 0;
    g->grav_acc = g->lock_acc = 0;
    g->lock_resets = 0;
    g->lowest_y = g->y;
    if (!blocks_fits(g, p, 0, g->x, g->y)) {
        g->over = 1;
        return 0;
    }
    return 1;
}
void blocks_new(struct blocks *g, uint32_t seed, int start_level)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x9e3779b9u;
    g->bag_pos = BL_PIECES;
    g->hold = -1;
    g->start_level = g->level = start_level < 1 ? 1 : start_level > 15 ? 15 : start_level;
    int p = draw_piece(g);
    g->next = draw_piece(g);
    spawn(g, p);
}
/* A successful move or turn restarts the lock delay a limited number of times. */
static void moved(struct blocks *g)
{
    if (g->y > g->lowest_y) {
        g->lowest_y = g->y;
        g->lock_resets = 0;
    }
    if (blocks_grounded(g) && g->lock_resets < BL_MAX_RESETS) {
        g->lock_acc = 0;
        g->lock_resets++;
    }
}
int blocks_move(struct blocks *g, int dx)
{
    if (g->over || !blocks_fits(g, g->piece, g->rot, g->x + dx, g->y))
        return 0;
    g->x += dx;
    moved(g);
    return 1;
}
int blocks_rotate(struct blocks *g, int dir)
{
    if (g->over || g->piece == BL_O)
        return 0;
    int to = (g->rot + (dir > 0 ? 1 : 3)) & 3;
    const int8_t(*k)[2] = (g->piece == BL_I ? kicks_i : kicks_jlstz)[g->rot * 2 + (dir < 0)];
    for (int i = 0; i < 5; i++) {
        int nx = g->x + k[i][0], ny = g->y - k[i][1];
        if (blocks_fits(g, g->piece, to, nx, ny)) {
            g->rot = to;
            g->x = nx;
            g->y = ny;
            moved(g);
            return 1;
        }
    }
    return 0;
}
static int lock_piece(struct blocks *g)
{
    const int8_t *c = blocks_cells(g->piece, g->rot);
    int ev = BL_EV_LOCKED, above = 0;
    for (int i = 0; i < 4; i++) {
        int cx = g->x + c[i * 2], cy = g->y + c[i * 2 + 1];
        if (cy < 0)
            above = 1;
        else
            g->well[cy][cx] = (uint8_t)(g->piece + 1);
    }
    int n = 0;
    for (int r = BL_H - 1; r >= 0;) {
        int full = 1;
        for (int x = 0; x < BL_W; x++)
            full &= g->well[r][x] != 0;
        if (!full) {
            r--;
            continue;
        }
        memmove(g->well[1], g->well[0], (size_t)r * BL_W);
        memset(g->well[0], 0, BL_W);
        n++;
    }
    static const int pts[5] = {0, 100, 300, 500, 800};
    g->last_clear = n;
    if (n) {
        g->score += (long)pts[n] * g->level;
        g->lines += n;
        g->level = g->start_level + g->lines / 10;
        if (g->level > 99)
            g->level = 99;
        ev |= BL_EV_CLEARED;
    }
    g->hold_used = 0;
    int p = g->next;
    g->next = draw_piece(g);
    if (above || !spawn(g, p)) {
        g->over = 1;
        ev |= BL_EV_OVER;
    }
    return ev;
}
int blocks_soft_drop(struct blocks *g)
{
    if (g->over || !blocks_fits(g, g->piece, g->rot, g->x, g->y + 1))
        return 0;
    g->y++;
    g->score++;
    g->grav_acc = 0;
    moved(g);
    return 1;
}
int blocks_hard_drop(struct blocks *g)
{
    if (g->over)
        return 0;
    int gy = blocks_ghost_y(g);
    g->score += 2L * (gy - g->y);
    g->y = gy;
    return lock_piece(g);
}
int blocks_hold(struct blocks *g)
{
    if (g->over || g->hold_used)
        return 0;
    int p = g->piece;
    if (g->hold < 0) {
        g->hold = p;
        p = g->next;
        g->next = draw_piece(g);
    } else {
        int t = g->hold;
        g->hold = p;
        p = t;
    }
    spawn(g, p);
    g->hold_used = 1;
    return 1;
}
int blocks_update(struct blocks *g, unsigned dt)
{
    if (g->over)
        return 0;
    if (blocks_grounded(g)) {
        g->lock_acc += dt;
        if (g->lock_acc >= BL_LOCK_MS)
            return lock_piece(g);
        return 0;
    }
    g->grav_acc += dt;
    unsigned ms = blocks_gravity_ms(g->level);
    while (g->grav_acc >= ms && !blocks_grounded(g)) {
        g->grav_acc -= ms;
        g->y++;
        if (g->y > g->lowest_y) {
            g->lowest_y = g->y;
            g->lock_resets = 0;
        }
        if (blocks_grounded(g))
            g->lock_acc = 0;
    }
    return 0;
}

int blocks_scores_qualify(const struct blocks_score *t, long score)
{
    if (score <= 0)
        return -1;
    for (int i = 0; i < BL_SCORES; i++)
        if (score > t[i].score)
            return i;
    return -1;
}
void blocks_scores_insert(struct blocks_score *t, int rank, const char *name, long score)
{
    if (rank < 0 || rank >= BL_SCORES)
        return;
    for (int i = BL_SCORES - 1; i > rank; i--)
        t[i] = t[i - 1];
    snprintf(t[rank].name, sizeof(t[rank].name), "%.3s", name && *name ? name : "???");
    t[rank].score = score;
}
void blocks_scores_clear(struct blocks_score *t)
{
    memset(t, 0, sizeof(*t) * BL_SCORES);
    for (int i = 0; i < BL_SCORES; i++)
        strcpy(t[i].name, "---");
}
int blocks_scores_load(struct blocks_score *t, const char *path)
{
    blocks_scores_clear(t);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[64];
    int n = 0;
    while (n < BL_SCORES && fgets(line, sizeof(line), f)) {
        char name[8];
        long v;
        if (sscanf(line, "%3s %ld", name, &v) == 2 && v >= 0) {
            snprintf(t[n].name, sizeof(t[n].name), "%.3s", name);
            t[n++].score = v;
        }
    }
    fclose(f);
    /* keep sorted even if the file was edited by hand */
    for (int i = 1; i < BL_SCORES; i++)
        for (int j = i; j > 0 && t[j].score > t[j - 1].score; j--) {
            struct blocks_score s = t[j];
            t[j] = t[j - 1];
            t[j - 1] = s;
        }
    return 1;
}
int blocks_scores_save(const struct blocks_score *t, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return 0;
    for (int i = 0; i < BL_SCORES; i++)
        if (t[i].score > 0)
            fprintf(f, "%.3s %ld\n", t[i].name, t[i].score);
    return fclose(f) == 0;
}
void blocks_scores_path(char *buf, size_t size)
{
    snprintf(buf, size, "%c:\\BLOCKS.HI", dc_storage_drive());
}
