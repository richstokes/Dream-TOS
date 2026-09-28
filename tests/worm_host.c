#include "field.h"
#include "player.h"
#include "scores.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    WPLAYER *p = init_player();
    field_init();
    assert(update_field(p) == ALIVE);
    int x = p->head->c_x, y = p->head->c_y, tx, ty;
    assert(update_player(p, &tx, &ty) == ALIVE);
    assert(p->head->c_x == x - 1 && p->head->c_y == y);
    assert(update_field(p) == ALIVE);
    p->grow = 3;
    reset_player(p);
    assert(p->grow == 0);
    load_scores(NULL);
    assert(is_high_score(99999));
    add_high_score("DC!", 99999);
    save_scores(NULL);
    load_scores(NULL);
    int score;
    assert(!strcmp(get_score_at(0, &score), "DC!") && score == 99999);
    FILE *f = fopen("C:\\WORM.HI", "w");
    assert(f);
    fputs("ABC 42\n", f);
    fclose(f);
    load_scores(NULL);
    assert(!strcmp(get_score_at(0, &score), "ABC") && score == 42);
    get_score_at(9, &score);
    assert(score == 0);
    remove("C:\\WORM.HI");
    WUNIT *u = p->head;
    while (u) {
        WUNIT *next = u->next;
        free(u);
        u = next;
    }
    free(p);
    return 0;
}
