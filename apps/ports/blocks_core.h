/* Blocks: falling-block game rules, independent of drawing. GPL-2.0-or-later.
 * 10x20 well, 7 tetrominoes, SRS-style rotation and kicks, 7-bag randomiser,
 * hold, ghost, lock delay, scoring, levels and a top-5 score table. */
#ifndef BLOCKS_CORE_H
#define BLOCKS_CORE_H
#include <stddef.h>
#include <stdint.h>

#define BL_W 10
#define BL_H 20
#define BL_PIECES 7
enum { BL_I, BL_O, BL_T, BL_S, BL_Z, BL_J, BL_L };
#define BL_LOCK_MS 500
#define BL_MAX_RESETS 15
#define BL_SCORES 5
#define BL_EV_LOCKED 1
#define BL_EV_CLEARED 2
#define BL_EV_OVER 4

struct blocks {
    uint8_t well[BL_H][BL_W]; /* 0 empty, else piece + 1 */
    int piece, rot, x, y;     /* active piece; x,y = top-left of its box */
    int next, hold;           /* hold is -1 when empty */
    int hold_used;
    uint8_t bag[BL_PIECES];
    int bag_pos;
    uint32_t rng;
    long score;
    int lines, level, start_level, last_clear;
    int over;
    unsigned grav_acc, lock_acc;
    int lock_resets, lowest_y;
};

struct blocks_score {
    char name[4];
    long score;
};

void blocks_new(struct blocks *g, uint32_t seed, int start_level);
/* Cells of a piece at a rotation as (col,row) offsets from the box corner. */
const int8_t *blocks_cells(int piece, int rot); /* 8 values: c0,r0,c1,r1,... */
int blocks_box(int piece);
int blocks_fits(const struct blocks *g, int piece, int rot, int x, int y);
int blocks_grounded(const struct blocks *g);
int blocks_ghost_y(const struct blocks *g);
unsigned blocks_gravity_ms(int level);
int blocks_move(struct blocks *g, int dx);
int blocks_rotate(struct blocks *g, int dir); /* +1 clockwise, -1 counter */
int blocks_soft_drop(struct blocks *g);       /* one row, 1 point */
int blocks_hard_drop(struct blocks *g);       /* returns BL_EV_* flags */
int blocks_hold(struct blocks *g);
int blocks_update(struct blocks *g, unsigned dt_ms); /* BL_EV_* flags */

int blocks_scores_qualify(const struct blocks_score *t, long score); /* rank or -1 */
void blocks_scores_insert(struct blocks_score *t, int rank, const char *name, long score);
void blocks_scores_clear(struct blocks_score *t);
int blocks_scores_load(struct blocks_score *t, const char *path);
int blocks_scores_save(const struct blocks_score *t, const char *path);
void blocks_scores_path(char *buf, size_t size); /* "<storage drive>:\BLOCKS.HI" */
#endif
