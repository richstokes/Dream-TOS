#define app_main guest_app_main
#include "../apps/ports/puzzle.c"
#include <assert.h>
int main(void)
{
    fe.me = midend_new(&fe, &thegame, &drawapi, &fe);
    midend_new_game(fe.me);
    int w = 620, h = 372;
    midend_size(fe.me, &w, &h, true, 1.0);
    midend_force_redraw(fe.me);
    midend_process_key(fe.me, 0, 0, CURSOR_RIGHT);
    midend_process_key(fe.me, 0, 0, CURSOR_SELECT);
    midend_timer(fe.me, 1.0f);
    midend_redraw(fe.me);
    FILE *f = tmpfile();
    assert(f);
    struct stream s = {f, 0};
    midend_serialise(fe.me, save_cb, &s);
    assert(!s.failed);
    char *id = midend_get_game_id(fe.me);
    midend_new_game(fe.me);
    rewind(f);
    assert(!midend_deserialise(fe.me, load_cb, f));
    fclose(f);
    char *loaded = midend_get_game_id(fe.me);
    assert(!strcmp(id, loaded));
    sfree(id);
    sfree(loaded);
    midend_process_key(fe.me, 0, 0, UI_UNDO);
    midend_timer(fe.me, 1.0f);
    midend_redraw(fe.me);
    midend_free(fe.me);
    return 0;
}
