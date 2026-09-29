/* Host test for the Blocks rules: collisions, SRS turns and kicks, bag,
 * line clears, scoring, lock delay, hold, game over and score files. */
#include "../apps/ports/blocks_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct blocks g;
static void put(int piece, int rot, int x, int y)
{
    g.piece = piece;
    g.rot = rot;
    g.x = x;
    g.y = y;
    g.lowest_y = y;
    g.lock_acc = g.grav_acc = 0;
    g.lock_resets = 0;
}
static void fill_row(int r, int gap)
{
    for (int c = 0; c < BL_W; c++)
        g.well[r][c] = c == gap ? 0 : 1;
}
int main(void)
{
    /* Every piece has 4 distinct cells in every rotation and stays inside its box. */
    for (int p = 0; p < BL_PIECES; p++)
        for (int r = 0; r < 4; r++) {
            const int8_t *c = blocks_cells(p, r);
            for (int i = 0; i < 4; i++) {
                assert(c[i * 2] >= 0 && c[i * 2] < blocks_box(p) && c[i * 2 + 1] >= 0 && c[i * 2 + 1] < blocks_box(p));
                for (int j = i + 1; j < 4; j++)
                    assert(c[i * 2] != c[j * 2] || c[i * 2 + 1] != c[j * 2 + 1]);
            }
        }
    /* Four clockwise turns return to the start for all pieces on an empty well. */
    blocks_new(&g, 1, 1);
    for (int p = 0; p < BL_PIECES; p++) {
        put(p, 0, 3, 5);
        for (int i = 0; i < 4; i++)
            assert(blocks_rotate(&g, 1) || p == BL_O);
        assert(g.rot == 0 && g.x == 3 && g.y == 5);
        assert(blocks_rotate(&g, -1) || p == BL_O);
    }

    /* 7-bag: every consecutive group of 7 pieces holds each piece once. */
    blocks_new(&g, 12345, 1);
    {
        int seq[7 * 6];
        for (int n = 0; n < 7 * 6; n++) {
            seq[n] = g.piece;
            assert(g.next >= 0 && g.next < BL_PIECES);
            memset(g.well, 0, sizeof(g.well));
            blocks_hard_drop(&g);
            assert(!g.over);
        }
        for (int b = 0; b < 6; b++) {
            int seen = 0;
            for (int i = 0; i < 7; i++)
                seen |= 1 << seq[b * 7 + i];
            assert(seen == 0x7f);
        }
    }

    /* Movement and wall collision. */
    blocks_new(&g, 7, 1);
    memset(g.well, 0, sizeof(g.well));
    put(BL_O, 0, 4, 0);
    while (blocks_move(&g, -1)) {}
    assert(g.x == 0 && !blocks_move(&g, -1));
    while (blocks_move(&g, 1)) {}
    assert(g.x == BL_W - 2);
    g.well[5][3] = 1;
    put(BL_O, 0, 0, 3);
    assert(blocks_fits(&g, BL_O, 0, 0, 3) && !blocks_fits(&g, BL_O, 0, 2, 4));
    assert(blocks_ghost_y(&g) == 18);
    /* pieces may not rest below the floor */
    assert(!blocks_fits(&g, BL_O, 0, 0, BL_H - 1) && blocks_fits(&g, BL_O, 0, 0, BL_H - 2));

    /* SRS: a vertical T against the left wall turns flat by kicking right. */
    memset(g.well, 0, sizeof(g.well));
    put(BL_T, 1, -1, 5); /* R state: cells at box cols 1,2 -> col 0 and 1 (col -1 unused) */
    assert(blocks_fits(&g, BL_T, 1, -1, 5));
    assert(blocks_rotate(&g, 1) && g.rot == 2 && g.x >= 0);
    /* Vertical I on the right wall turns flat and is kicked back inside. */
    put(BL_I, 1, 7, 5); /* vertical I in box col 2 = well col 9 */
    assert(blocks_rotate(&g, 1));
    assert(g.rot == 2 && g.x + 3 < BL_W && g.x >= 0);
    /* A fully blocked turn is refused and leaves the piece alone. */
    memset(g.well, 1, sizeof(g.well));
    for (int c = 3; c < 6; c++)
        g.well[5][c] = 0;
    g.well[4][4] = 0;
    put(BL_T, 0, 3, 4);
    assert(blocks_fits(&g, BL_T, 0, 3, 4));
    assert(!blocks_rotate(&g, 1) && g.rot == 0 && g.x == 3 && g.y == 4);
    assert(!blocks_rotate(&g, -1) && g.rot == 0 && g.x == 3 && g.y == 4);
    /* T-shaped kick: this slot only admits the T via an SRS offset. */
    memset(g.well, 0, sizeof(g.well));
    g.well[6][3] = g.well[6][5] = 1;
    put(BL_T, 0, 3, 4); /* cells (4,4),(3,5),(4,5),(5,5) */
    assert(blocks_rotate(&g, 1) && g.rot == 1);
    /* floor kick: I lying flat on the floor turns up using an upward kick */
    memset(g.well, 0, sizeof(g.well));
    put(BL_I, 0, 3, BL_H - 2); /* cells on row 19 */
    assert(blocks_grounded(&g));
    assert(blocks_rotate(&g, 1) && g.rot == 1);
    for (int i = 0; i < 4; i++) {
        const int8_t *c = blocks_cells(BL_I, 1);
        assert(g.y + c[i * 2 + 1] <= BL_H - 1);
    }

    /* Line clears and scoring. */
    static const int pts[5] = {0, 100, 300, 500, 800};
    for (int n = 1; n <= 4; n++) {
        blocks_new(&g, 99, 1);
        memset(g.well, 0, sizeof(g.well));
        for (int i = 0; i < n; i++)
            fill_row(BL_H - 1 - i, 9);
        g.well[BL_H - 5][0] = 5; /* leftover marker, must fall by n */
        put(BL_I, 1, 7, 16);     /* vertical I in col 9; cells rows 16..19 */
        g.score = 0;
        int ev = blocks_hard_drop(&g);
        assert(ev & BL_EV_LOCKED);
        assert((ev & BL_EV_CLEARED) && g.last_clear == n && g.lines == n);
        assert(g.score == pts[n]); /* hard drop distance 0, level 1 */
        assert(g.well[BL_H - 5 + n][0] == 5);
        for (int c = 1; c < BL_W - 1; c++)
            assert(!g.well[BL_H - 1][c]);
    }
    /* level ups every ten lines multiply the reward */
    blocks_new(&g, 3, 1);
    g.lines = 9;
    memset(g.well, 0, sizeof(g.well));
    fill_row(BL_H - 1, 9);
    put(BL_I, 1, 7, 16);
    blocks_hard_drop(&g);
    assert(g.lines == 10 && g.level == 2 && g.score == 100);
    assert(blocks_gravity_ms(2) < blocks_gravity_ms(1) && blocks_gravity_ms(99) == blocks_gravity_ms(15));
    memset(g.well, 0, sizeof(g.well));
    fill_row(BL_H - 1, 9);
    put(BL_I, 1, 7, 16);
    long before = g.score;
    blocks_hard_drop(&g);
    assert(g.score - before == 200);
    /* start level */
    blocks_new(&g, 3, 5);
    assert(g.level == 5);

    /* Soft and hard drop points. */
    blocks_new(&g, 42, 1);
    memset(g.well, 0, sizeof(g.well));
    put(BL_O, 0, 4, 0);
    assert(blocks_soft_drop(&g) && g.score == 1 && g.y == 1);
    int gy = blocks_ghost_y(&g);
    assert(gy == BL_H - 2);
    blocks_hard_drop(&g);
    assert(g.score == 1 + 2 * (gy - 1) && g.well[BL_H - 1][4] && g.well[BL_H - 2][5]);

    /* Gravity and lock delay. */
    blocks_new(&g, 42, 1);
    memset(g.well, 0, sizeof(g.well));
    put(BL_O, 0, 4, 0);
    assert(blocks_update(&g, 999) == 0 && g.y == 0);
    assert(blocks_update(&g, 1) == 0 && g.y == 1);
    put(BL_O, 0, 4, BL_H - 2);
    assert(blocks_update(&g, BL_LOCK_MS - 1) == 0);
    assert(blocks_move(&g, 1) && g.lock_acc == 0); /* a move restarts the delay */
    assert(blocks_update(&g, BL_LOCK_MS) & BL_EV_LOCKED);
    /* lock resets are limited so a piece cannot hover for ever */
    blocks_new(&g, 42, 1);
    memset(g.well, 0, sizeof(g.well));
    put(BL_O, 0, 4, BL_H - 2);
    for (int i = 0; i < BL_MAX_RESETS + 2; i++)
        blocks_move(&g, i & 1 ? 1 : -1);
    assert(g.lock_resets == BL_MAX_RESETS);
    g.lock_acc = BL_LOCK_MS - 1;
    blocks_move(&g, 1);
    assert(g.lock_acc == BL_LOCK_MS - 1);

    /* Hold: swaps once per piece and needs a fresh piece after locking. */
    blocks_new(&g, 5, 1);
    int first = g.piece, second = g.next;
    assert(blocks_hold(&g) && g.hold == first && g.piece == second && g.hold_used);
    assert(!blocks_hold(&g));
    blocks_hard_drop(&g);
    assert(!g.hold_used);
    int cur = g.piece;
    assert(blocks_hold(&g) && g.piece == first && g.hold == cur && g.y <= 0 && g.x >= 3);

    /* Game over when the stack reaches the spawn area. */
    blocks_new(&g, 8, 1);
    memset(g.well, 0, sizeof(g.well));
    for (int r = 2; r < BL_H; r++)
        fill_row(r, r & 1 ? 0 : 9);
    int ev = 0;
    for (int i = 0; i < 6 && !g.over; i++)
        ev |= blocks_hard_drop(&g);
    assert(g.over && (ev & BL_EV_OVER));
    assert(!blocks_move(&g, 1) && !blocks_rotate(&g, 1) && !blocks_soft_drop(&g) && !blocks_hold(&g) &&
           !blocks_update(&g, 5000) && !blocks_hard_drop(&g));
    blocks_new(&g, 8, 1);
    assert(!g.over && g.score == 0 && g.lines == 0);

    /* High scores: ranking, insertion, persistence, hand-edited files. */
    struct blocks_score t[BL_SCORES];
    blocks_scores_clear(t);
    assert(blocks_scores_qualify(t, 0) < 0 && blocks_scores_qualify(t, 10) == 0);
    blocks_scores_insert(t, 0, "AAA", 500);
    blocks_scores_insert(t, blocks_scores_qualify(t, 900), "BBBB", 900);
    blocks_scores_insert(t, blocks_scores_qualify(t, 700), "", 700);
    assert(t[0].score == 900 && !strcmp(t[0].name, "BBB") && t[1].score == 700 && !strcmp(t[1].name, "???") &&
           t[2].score == 500);
    for (int i = 0; i < 4; i++)
        blocks_scores_insert(t, blocks_scores_qualify(t, 1000 + i), "TOP", 1000 + i);
    assert(t[BL_SCORES - 1].score == 900 && blocks_scores_qualify(t, 100) < 0);
    const char *path = "BLOCKS_TEST.HI";
    assert(blocks_scores_save(t, path));
    struct blocks_score u[BL_SCORES];
    assert(blocks_scores_load(u, path));
    assert(!memcmp(t, u, sizeof(t)));
    FILE *f = fopen(path, "w");
    fputs("LOW 5\nHI! 9000\nbad line\n", f);
    fclose(f);
    assert(blocks_scores_load(u, path) && u[0].score == 9000 && u[1].score == 5 && !strcmp(u[2].name, "---"));
    remove(path);
    assert(!blocks_scores_load(u, path) && u[0].score == 0);
    char p[16];
    blocks_scores_path(p, sizeof(p));
    assert(!strcmp(p, "C:\\BLOCKS.HI")); /* no system-info API on the host */
    return 0;
}
