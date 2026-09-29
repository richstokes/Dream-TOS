#include "field.h"
#include "player.h"
#include "scores.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "drives.h"
char *get_hi_score_filepath(const char *);
static long fake_info(void *buffer, uint32_t bytes)
{
    struct dc_system_info *i = buffer;
    if (bytes != sizeof(*i)) return -64;
    memset(i, 0, sizeof(*i));
    i->version = DC_SYSTEM_INFO_VERSION;
    i->drive_mask = 4 | 8 | 16;
    i->readonly_mask = 8;
    i->volatile_mask = 4;
    return sizeof(*i);
}
static struct dc_native_api fake_api = {.version = 1, .size = sizeof(fake_api), .system_info = fake_info};
int main(void)
{
    /* High scores follow the SD card when one is mounted, else the RAM disk. */
    assert(dc_storage_drive() == 'C');
    dc_os = &fake_api;
    assert(dc_storage_drive() == 'E' && dc_drive_state('C') == DC_DRIVE_VOLATILE && !dc_drive_state('D'));
    assert(!strcmp(dc_drive_note('C'), " (lost at reset)") && !*dc_drive_note('E'));
    {
        char *path = get_hi_score_filepath(NULL);
        assert(!strcmp(path, "E:\\WORM.HI"));
        free(path);
        load_scores(NULL);
        add_high_score("SD!", 77777);
        save_scores(NULL);
        FILE *sd = fopen("E:\\WORM.HI", "r");
        assert(sd);
        fclose(sd);
        remove("E:\\WORM.HI");
    }
    dc_os = NULL;
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
