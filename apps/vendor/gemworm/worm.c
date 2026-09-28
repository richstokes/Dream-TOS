/* GEM Worm
 * Copyright (C) 2009, 2020 Jeffrey Armstrong
 * http://jeff.rainbow-100.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * A full version of the license terms is available in LICENSE.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __GNUC__

#include <gem.h>

#define HIWORD(x)  ((int16_t)((uint32_t)x >> 16))
#define LOWORD(x)  ((int16_t)((uint32_t)x & 0xFFFF)) 

#else

#include <portab.h>
#include <aes.h>
#include <vdi.h>

#endif /* __GNUC__ */

/* The proper resource header is dependent upon system */
#include "wormst.h"

#include "util.h"
#include "field.h"
#include "player.h"
#include "scores.h"

#ifndef BORDER
#define BORDER 0x8000
#endif

WORD    app_wflags = NAME | CLOSER | MOVER | SIZER;
WORD    app_accid;
WORD    app_appid;

WORD    app_wh = -1;    /* Border window handle */
WORD    app_vh;         /* VDI handle */

char    *app_title = "Worm";
GRECT   app_wdw;            /* xywh of working area */

OBJECT *app_menu;
OBJECT *about_box;
OBJECT *scores_box;
OBJECT *newscore_box;

#define RCS_FILE    "wormst.rsc"

#define MOVEDELAY   300

#define STARTWIDTH  640
#define STARTHEIGHT 480

#ifdef __GEMLIB__
#define WHITE   G_WHITE
#define BLACK   G_BLACK
#endif


void hndl_about()
{
GRECT box,origin;

    /* ob_xywh(app_menu, MDESK, &origin); */

    form_center(about_box, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    objc_draw(about_box,0,2,box.g_x, box.g_y, box.g_w, box.g_h);
    form_do(about_box,0);
    form_dial(FMD_FINISH,0,0,0,0,box.g_x, box.g_y, box.g_w, box.g_h);

    return;
}

void hndl_high_score(int score) 
{
int res;
GRECT box,origin;

TEDINFO *eted;

    form_center(newscore_box, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    objc_draw(newscore_box,0,2,box.g_x, box.g_y, box.g_w, box.g_h);
    res = form_do(newscore_box,0);
    
    if(res == BOK) {
        /* save! */
        eted = (TEDINFO *)(newscore_box[TINITIALS].ob_spec.index);
        add_high_score((char *)eted->te_ptext, score);
    }
    
    form_dial(FMD_FINISH,0,0,0,0,box.g_x, box.g_y, box.g_w, box.g_h);
    return;
}

short score_positions[] = {THI1, THI2, THI3, THI4, THI5, THI6, THI7, THI8, THI9, THI10};

void hndl_scores()
{
GRECT box,origin;
int i;
const char *initials;
int score;
static char entry[10][18];
char numtext[10];

    /* ob_xywh(app_menu, MDESK, &origin); */

    form_center(scores_box, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    objc_draw(scores_box,0,2,box.g_x, box.g_y, box.g_w, box.g_h);
    
    for(i=0;i<10;i++) {
        initials = get_score_at(i, &score);
        strcpy(entry[i], initials);
        
        /* Unsafe, but what the hell...  AHCC doesn't have snprintf */
        sprintf(numtext, "%d", score);

        while(strlen(entry[i]) < 17 - strlen(numtext)) strcat(entry[i], ".");
        strcat(entry[i], numtext);
        
        set_resource_string(scores_box, score_positions[i], entry[i]);
    }
    
    form_do(scores_box,0);
    form_dial(FMD_FINISH,0,0,0,0,box.g_x, box.g_y, box.g_w, box.g_h);

    return;

}

int open_window(int new)
{
    graf_mouse(M_OFF,NULL);
    wind_update(BEG_UPDATE);

    app_wh = wind_create(app_wflags, 0, 0, 200, 200);
    if(app_wh<=0)
        return -1;

#ifdef MGEMLIB
    wind_set(app_wh, WF_NAME, HIWORD(app_title), LOWORD(app_title), 0, 0);
#else
    wind_set(app_wh, WF_NAME, app_title);
#endif

    if(new == 1) {
        /* Get the desktop */
        wind_get(0, WF_WXYWH, &app_wdw.g_x,
                              &app_wdw.g_y,
                              &app_wdw.g_w,
                              &app_wdw.g_h);
    
        app_wdw.g_w = min(STARTWIDTH,app_wdw.g_w);
        app_wdw.g_h = min(STARTHEIGHT,app_wdw.g_h);
    }

    wind_open(app_wh, app_wdw.g_x, app_wdw.g_y, app_wdw.g_w, app_wdw.g_h);
    wind_get(app_wh, WF_WXYWH, &app_wdw.g_x,
                               &app_wdw.g_y,
                               &app_wdw.g_w,
                               &app_wdw.g_h);


    wind_update(END_UPDATE);
    graf_mouse(M_ON,NULL);
    return 1;
}

int init_app()
{
WORD work_in[11];
WORD work_out[57];

WORD ww;

WORD i;
WORD txtwidth,txtheight;

    app_accid = appl_init();

    /* VDI handle... */
    app_vh = graf_handle(&txtwidth,&txtheight,&i,&i);
    for(i=0;i<10;i++) work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in,&app_vh,work_out);

    /* RSC load */
    if(rsrc_load(RCS_FILE) == 0) {
        form_alert(1,"[3][The resource file was not found][Exit]");
        return -1;
    }

    wind_update(BEG_UPDATE);
    rsrc_gaddr(R_TREE,RCMENU,&app_menu);
    menu_bar(app_menu,1);
    wind_update(END_UPDATE);

    rsrc_gaddr(R_TREE,ABOUT,&about_box);
    rsrc_gaddr(R_TREE,HISCORES,&scores_box);
    rsrc_gaddr(R_TREE,NEWSCORE,&newscore_box);

    /* Make the application name nicer on Atari GEM */
    menu_register(app_accid, "Worm!");

    return open_window(1);
}

int get_score_height()
{
WORD attrib[10];
    
    vqt_attributes(app_vh, attrib);
    return attrib[9];
}

void do_redraw_score(WPLAYER *player, GRECT *update, int playing)
{
WORD pts[4];
char score_str[128];

    vsf_interior(app_vh, 1);

    /* Fill background */
    vsf_color(app_vh, (WORD)WHITE);
    pts[0] = update->g_x; pts[1] = update->g_y;
    pts[2] = update->g_x+update->g_w; pts[3] = update->g_y+update->g_h;
    v_bar(app_vh, pts);

    /* Text */
    vst_color(app_vh, (WORD)BLACK);
    vst_point(app_vh, (WORD)6, &pts[0], &pts[1], &pts[2], &pts[3]);
    v_gtext(app_vh, update->g_x + update->g_w - 20*pts[2],
                    update->g_y + update->g_h/2 - 1,
                    "W,A,S,D to Move");
    v_gtext(app_vh, update->g_x + update->g_w - 20*pts[2],
                    update->g_y + update->g_h - 2,
                    playing == 1 ? "P to Pause" : "P to Play");
    
    sprintf(score_str, "Score: %d", player->score);
    vst_point(app_vh, (WORD)12, &pts[0], &pts[1], &pts[2], &pts[3]);
    v_gtext(app_vh, update->g_x+5, update->g_y + update->g_h - 2, score_str);
    
}

void force_redraw_score(WPLAYER *player, int playing)
{
GRECT score;

    graf_mouse(M_OFF,NULL);
    wind_update(BEG_UPDATE);

    wind_get(app_wh, WF_WXYWH, &score.g_x, &score.g_y, &score.g_w, &score.g_h);
    score.g_h = get_score_height();

    do_redraw_score(player, &score, playing);
    
    wind_update(END_UPDATE);
    graf_mouse(M_ON,NULL);
}

void window_to_field_rect(GRECT *rc)
{
int sh;

    sh = get_score_height() + 1;
    rc->g_y += sh;
    rc->g_h -= sh;
}
    
#define PRINTRC(n,r)  printf("%s -> x=%d y=%d w=%d h=%d\n", n, r.g_x, r.g_y, r.g_w, r.g_h)

/* Not necessary yet... */
void do_redraw(WPLAYER *player, int playing, GRECT *dirty)
{
GRECT box, me, field, score, rc;
int updated_score;
WORD pts[4];

#ifdef DEBUG
    printf("REDRAW!\n");
#endif

    graf_mouse(M_OFF,NULL);
    wind_update(BEG_UPDATE);

    wind_get(app_wh, WF_WXYWH, &me.g_x, &me.g_y, &me.g_w, &me.g_h);
    
    memcpy(&field, &me, sizeof(GRECT));
    memcpy(&score, &me, sizeof(GRECT));

    score.g_h = get_score_height();
    window_to_field_rect(&field);

    wind_get(app_wh, WF_FIRSTXYWH, &box.g_x,
                                   &box.g_y,
                                   &box.g_w,
                                   &box.g_h);

    updated_score = 0;
    while(box.g_w || box.g_h) {

#ifdef DEBUG
        PRINTRC("box", box);
        PRINTRC("field", field);
#endif

        /* See if this rect intersects the reported dirty */
        if(dirty == NULL || rc_intersect(dirty, &box)) {

            memcpy(&rc, &field, sizeof(GRECT));
            if(rc_intersect(&box, &rc)) {
#ifdef DEBUG
                printf("Field draw\n");
#endif
                pts[0] = box.g_x;
                pts[1] = box.g_y;
                pts[2] = box.g_w+box.g_x-1;
                pts[3] = box.g_h+box.g_y-1;
                vs_clip(app_vh, 1, pts);
                draw_field(app_vh, &field, &rc);
            }
    
#ifdef DEBUG        
            PRINTRC("box", box);
            PRINTRC("score", score);
#endif
    
            memcpy(&rc, &score, sizeof(GRECT));
            if(rc_intersect(&box, &rc)) {
#ifdef DEBUG
                printf("Score draw\n");
#endif
                pts[0] = box.g_x;
                pts[1] = box.g_y;
                pts[2] = box.g_w+box.g_x-1;
                pts[3] = box.g_h+box.g_y-1;
                vs_clip(app_vh, 1, pts);
                do_redraw_score(player, &score, playing);
                /* updated_score = 1; */
            }

        }
        
        wind_get(app_wh, WF_NEXTXYWH, &box.g_x,
                                     &box.g_y,
                                     &box.g_w,
                                     &box.g_h);
    }

    /* Turn off clipping */
    vs_clip(app_vh, 0, pts);
    wind_update(END_UPDATE);
    graf_mouse(M_ON,NULL);
}

void do_window_change(GRECT *rect)
{
    wind_set(app_wh, WF_CXYWH, rect->g_x, rect->g_y, rect->g_w, rect->g_h);
    wind_get(app_wh, WF_WXYWH, &app_wdw.g_x, &app_wdw.g_y, &app_wdw.g_w, &app_wdw.g_h);
}

void hndl_keys(WORD key, WPLAYER *player, int *playing)
{
char asc;

    asc = key & 127;
    
    if(*playing == 1) {
        switch(asc) {
            case 'w':
            case 'W':
                player->dir = WUP;
                break;
            case 's':
            case 'S':
                player->dir = WDOWN;
                break;
            case 'a':
            case 'A':
                player->dir = WLEFT;
                break;
            case 'd':
            case 'D':
                player->dir = WRIGHT;
                break;
            case 'p':
            case 'P':
                *playing = 0;
                break;
        }
    } else {
        if(asc == 'p' || asc == 'P')
            *playing = 1;
    } 
       

}

int main(int argc, char *argv[])
{
WORD ret;
WORD msg[8];
int done;
WORD top;
WORD dummy;

WORD special_keys;
WORD key;

WPLAYER *player;
GRECT field;

int playing = 0;
int last_playing;
int palive, falive;

int tailx, taily;

#ifdef __GEMLIB__
EVMULT_IN evin;
EVMULT_OUT evout;
#endif

    srand(time(NULL));

    if(init_app() > 0) {
        done = 0;

#ifdef DEBUG
        printf("AppID: %d\n",app_accid);
#endif
        player = init_player();
        field_init();
        food_init();
        
        /* High scores */
        load_scores(argv[0]);

#ifdef DEBUG
        printf("Game init complete\n");
#endif

#ifdef __GEMLIB__
        memset(&evin, 0, sizeof(EVMULT_IN));
        evin.emi_flags = MU_KEYBD | MU_MESAG | MU_TIMER;
        evin.emi_tlow = (int16_t)MOVEDELAY;
#endif

        /* Set up the "field" rectangle */
        memcpy(&field, &app_wdw, sizeof(GRECT));
        window_to_field_rect(&field);
        
        menu_ienable(app_menu, MPAUSE, 0);

        msg[0] = AC_OPEN;
        msg[4] = app_accid;
        ret = MU_MESAG;
        done = -2;
        while(done <=0) {

            if(done == -2)
                done = -1;
            else {
#ifdef __GEMLIB__
                ret = evnt_multi_fast(&evin, msg, &evout);
                key = evout.emo_kreturn;
#else
                ret = evnt_multi(MU_KEYBD | MU_MESAG | MU_TIMER, /* Accept keypresses, messages, and timers */
                         0, 0, 0,                                            /* Mouse clicks, which buttons, and states */
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0,               /* Mouse movement info */
                         msg,                                                    /* 16-byte message array */
                         (WORD)MOVEDELAY, 0,                                 /* The low and high word of the timer in ms */
                         &dummy, &dummy,                                     /* Mouse position @ mouse event */
                         &dummy,                                                 /* Mouse button status */
                         &special_keys,                                  /* "Special" key statuses */
                         &key,                                               /* Key pressed scancode */
                         &dummy);                                            /* # of mouse button presses */
#endif
            }
#ifdef DEBUG
            printf("maybe MSG: %d\n",msg[0]);
#endif
            /* Key presses */
            if((ret & MU_KEYBD)) {
                last_playing = playing;
                hndl_keys(key, player, &playing);
                if(last_playing != playing) {
                    force_redraw_score(player, playing);
                    menu_text(app_menu, MPAUSE, playing == 1 ? "  Pause Game" :"  Resume Game");
                    menu_ienable(app_menu, MPAUSE, 1);
                }                
            } 

            /* Timer - update game */
            if((ret & MU_TIMER) && playing > 0) {
                wind_update(BEG_UPDATE);
                palive = update_player(player, &tailx, &taily);
                falive = update_field(player);

                switch(food_check(app_vh, &field, player)) {
                    case FOODADDITION:
                        player->score += 100;
                        force_redraw_score(player, playing);
                        break;
                    case FOODRESETCOUNT:
                        player->score -= 10;
                        if(player->score < 0) player->score = 0;
                        force_redraw_score(player, playing);
                        break;
                }

                /* form_alert(1,"[1][Update][Ok]"); */
                /* draw_field(app_vh, &app_wdw); */
                incremental_draw(app_vh, &field, player->head->c_x, player->head->c_y);
                incremental_draw(app_vh, &field, tailx, taily);

                wind_update(END_UPDATE);

                if(palive == DEAD || falive == DEAD) {
                    if(is_high_score(player->score)) { 
                        hndl_high_score(player->score);
                        save_scores(argv[0]);
                    } else
                        form_alert(1,"[3][You've Died!|Good try, though...][Reset]");
                    
                    reset_player(player);
                    playing = 0;
                    menu_ienable(app_menu, MPAUSE, 0);
                    update_field(player);
                    do_redraw(player, playing, NULL);
                }
            } 

            /* Messages (menu, resizes, etc.) */
            if(ret & MU_MESAG) {
#ifdef DEBUG
                printf("MSG: %d\n",msg[0]);
#endif
                switch(msg[0]) {
                    case AC_OPEN:
                        if(msg[4] != app_accid)
                            break;
                        if(app_wh > 0) {
                            wind_get(0,WF_TOP, &top, &dummy, &dummy, &dummy);
                            if(top != app_wh) {
                                wind_set(app_wh, WF_TOP, app_wh, 0, 0, 0);
                                wind_set(app_wh, WF_CXYWH, 30,30,100,100);
                            }
                        } else
                            open_window(0);
                        break;

                    case AC_CLOSE:
                        if(msg[3] == app_accid)
                            app_wh = -1;
                        break;

                    case WM_CLOSED:
                        if(msg[3] != app_wh || app_wh < 0)
                            break;
                        graf_mouse(M_OFF,NULL);
                        wind_close(app_wh);
                        wind_delete(app_wh);
                        graf_mouse(M_ON,NULL);
                        app_wh = -1;
                        done = 1;
                        break;

                    case WM_TOPPED:
                        wind_set(app_wh,WF_TOP,app_wh,0,0,0);
                        break;

                    case WM_MOVED:
                    case WM_SIZED:
                        do_window_change((GRECT *)&msg[4]);
                        memcpy(&field, &app_wdw, sizeof(GRECT));
                        window_to_field_rect(&field);
                        /*break;*/
                        /* Fall through */
                        
                    case WM_REDRAW:
                        update_field(player);
                        do_redraw(player, playing, (GRECT *)&msg[4]);
                        break;

                    case MN_SELECTED:
                        switch(msg[4]) {
                            case MQUIT:
                                msg[0] = WM_CLOSED;
                                msg[3] = app_wh;
                                appl_write(app_accid,sizeof(msg),msg);
                                break;
                            case MNEW:
                                player->score = 0;
                                playing = 1;
                                force_redraw_score(player, playing);
                                update_field(player);
                                draw_field(app_vh, &field, NULL);
                                menu_ienable(app_menu, MPAUSE, 1);
                                break;
                            case MPAUSE:
                                if(playing == 1) {
                                    playing = 0;
                                    menu_text(app_menu, MPAUSE, "  Resume Game");
                                } else {
                                    playing = 1;
                                    menu_text(app_menu, MPAUSE, "  Pause Game");
                                }
                                force_redraw_score(player, playing);
                                break;
                            case MSCORES:
                                hndl_scores();
                                break;
#ifdef MABOUT                                
                            case MABOUT:
                                hndl_about();
                                break;
#endif
                        }
                        menu_tnormal(app_menu, msg[3], 1);
                        break;

                } /* switch */

            } /* if */

            if(app_wh <= 0 )
                continue;

            /* done = 0; */

        } /* while */

        v_clsvwk(app_vh);
        /* wind_close(app_wh); */
        /* wind_delete(app_wh); */
        appl_exit();

    }

    return 0;
}
