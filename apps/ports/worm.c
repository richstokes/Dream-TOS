/* Dreamcast GEM frontend for Jeffrey Armstrong's GEM Worm. GPL-3.0-or-later.
 * Retains the upstream movement, field renderer, food and score engines. */
#include "app.h"
#include "field.h"
#include "player.h"
#include "scores.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
static GRECT board = {0, 60, 640, 384};
static WPLAYER *player;
static int playing;
static void redraw(void)
{
    char s[80];
    app_clear(0);
    snprintf(s, sizeof(s), "Score: %d     %s", player->score,
             playing ? "Playing" : "Paused - press P to play");
    app_text(8, 45, s, 1);
    draw_field(vc[6], &board, NULL);
    app_status("Arrows/WASD:move P:pause N:new H:scores Esc:exit");
}
static void scores(void)
{
    app_clear(0);
    app_text(32, 62, "GEM Worm high scores (C:\\WORM.HI, lost at reset)", 1);
    for (int i = 0; i < 10; i++) {
        int n;
        const char *initials = get_score_at(i, &n);
        char s[64];
        snprintf(s, sizeof(s), "%2d   %.3s     %6d", i + 1, initials, n);
        app_text(160, 96 + i * 28, s, 1);
    }
    app_status("Press any key to return");
    app_key();
    redraw();
}
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!app_begin("GEM WORM - Jeffrey Armstrong / native SH-4"))
        return 1;
    atexit(app_end);
    srand(app_millis());
    player = init_player();
    field_init();
    update_field(player);
    food_init();
    load_scores(NULL);
    redraw();
    unsigned long last = app_millis();
    int direction = player->dir;
    for (;;) {
        int key = app_event(20, NULL, NULL, NULL), a = tolower(key & 255), d = -1, refresh = 0;
        if (a == 27)
            break;
        if (a == 'p' || a == ' ') {
            playing = !playing;
            refresh = 1;
            last = app_millis();
        }
        if (a == 'n') {
            reset_player(player);
            field_init();
            update_field(player);
            food_init();
            direction = player->dir;
            playing = 1;
            refresh = 1;
            last = app_millis();
        }
        if (a == 'h') {
            playing = 0;
            scores();
        }
        if (key == KEY_UP || a == 'w')
            d = WUP;
        else if (key == KEY_DOWN || a == 's')
            d = WDOWN;
        else if (key == KEY_LEFT || a == 'a')
            d = WLEFT;
        else if (key == KEY_RIGHT || a == 'd')
            d = WRIGHT;
        if (d >= 0 && (d ^ 1) != player->dir)
            direction = d;
        if (playing && app_millis() - last >= 300) {
            last = app_millis();
            player->dir = direction;
            int tx, ty;
            int alive = update_player(player, &tx, &ty);
            alive &= update_field(player);
            int f = food_check(vc[6], &board, player);
            if (f == FOODADDITION)
                player->score += 100;
            else if (f == FOODRESETCOUNT && player->score >= 10)
                player->score -= 10;
            if (!alive) {
                playing = 0;
                redraw();
                if (is_high_score(player->score)) {
                    char initials[4] = "YOU";
                    if (app_prompt("New high score! Your initials:", initials, sizeof(initials))) {
                        add_high_score(initials, player->score);
                        save_scores(NULL);
                    }
                } else
                    app_alert("Game over! Press P to try again.");
                reset_player(player);
                field_init();
                update_field(player);
                food_init();
                direction = player->dir;
            }
            refresh = 1;
        }
        if (refresh)
            redraw();
    }
    return 0;
}
