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

#ifdef __GNUC__

#include <gem.h>

#else

#include <portab.h>
#include <aes.h>
#include <vdi.h>

#endif

#include "util.h"
#include "field.h"
#include "player.h"

#ifdef __GEMLIB__
#define RED     G_RED
#define GREEN   G_GREEN
#define WHITE   G_WHITE
#define BLACK   G_BLACK
#endif

/* Defines the play field */
char field[CELLW][CELLH];

#define CELLBLANK       0
#define CELLPLAYER      1
#define CELLWALL        2
#define CELLFOOD        3

#define max(x,y)    x>y ? x : y
#define min(x,y)    x<y ? x : y

struct _food{
    WORD c_x;
    WORD c_y;
    int count;
} food;

void empty_field(char celltype)
{
char i,j;

    for(i=0;i<CELLW;i++) {
        field[i][0] = CELLWALL;
        field[i][CELLH-1] = CELLWALL;
    }
    for(j=0;j<CELLH;j++) {
        field[0][j] = CELLWALL;
        field[CELLW-1][j] = CELLWALL;
    }

    for(i=1;i<CELLW-1;i++) {
        for(j=1;j<CELLH-1;j++) {
            if(field[i][j] == celltype) 
                field[i][j] = CELLBLANK;
        }
    }
    
}

void food_reset()
{
    
    field[food.c_x][food.c_y] = CELLBLANK;
    food.c_x = rand() % CELLW;
    food.c_y = rand() % CELLH;
    while(field[food.c_x][food.c_y] != CELLBLANK) {
        food.c_x = rand() % CELLW;
        food.c_y = rand() % CELLH;
    }
    food.count = 0;
    field[food.c_x][food.c_y] = CELLFOOD;
}

void food_init()
{
    food.c_x = 10;
    food.c_y = 10;
    food_reset();
    field[food.c_x][food.c_y] = CELLFOOD;
}

int food_check(WORD h_vdi, GRECT *play, WPLAYER *player) 
{
int oldx,oldy;

    if(player->head->c_x == food.c_x && player->head->c_y == food.c_y) {
        player->grow += FOODADDITION;
        oldx = food.c_x; oldy = food.c_y;
        food_reset();
        incremental_draw(h_vdi, play, food.c_x, food.c_y);
        field[oldx][oldy] = CELLPLAYER;
        return FOODADDITION;
    }
    
    food.count++;
    if(food.count > FOODRESETCOUNT) {
        oldx = food.c_x; oldy = food.c_y;
        food_reset();
        incremental_draw(h_vdi, play, food.c_x, food.c_y);
        incremental_draw(h_vdi, play, oldx, oldy);
        return FOODRESETCOUNT;
    }
    return 0;
}

void field_init() 
{
char i,j;

    for(j=0;j<CELLH;j++) {
        field[0][j] = CELLWALL;
        field[CELLW-1][j] = CELLWALL;
    }

    for(i=1;i<CELLW-1;i++) {
        field[i][0] = CELLWALL;
        field[i][CELLH-1] = CELLWALL; 
        for(j=1;j<CELLH-2;j++)
            field[i][j] = CELLBLANK;
    }

}

int check_wall(WPLAYER *player) 
{
    return field[player->head->c_x][player->head->c_y] == CELLWALL ? DEAD : ALIVE;  
}

static void getcellrect(char cx, char cy, int xoffset, int yoffset, 
                 int cellwidth, int cellheight, GRECT *rc)
{
    rc->g_x = xoffset + cx*cellwidth;
    rc->g_y = yoffset + cy*cellheight;
    rc->g_w = cellwidth;
    rc->g_h = cellheight;
}

void drawcell(WORD h_vdi, int type, char cx, char cy, 
              int xoffset, int yoffset, int cellwidth, int cellheight)
{
WORD pts[4];
int vx,vy;

    vx = xoffset + cx*cellwidth;
    vy = yoffset + cy*cellheight;

    switch(type) {
        case CELLWALL:
            vsf_color(h_vdi, (WORD)RED);
            pts[0] = vx; pts[1] = vy;
            pts[2] = vx+cellwidth; pts[3] = vy+cellheight;
            v_bar(h_vdi,pts);
            break;
        case CELLBLANK:
            vsf_color(h_vdi, (WORD)WHITE);
            pts[0] = vx; pts[1] = vy;
            pts[2] = vx+cellwidth; pts[3] = vy+cellheight;
            v_bar(h_vdi,pts);
            break;
        case CELLPLAYER:
            vsf_color(h_vdi,(WORD)BLACK);
            v_ellipse(h_vdi,(WORD)(vx+cellwidth/2),
                     (WORD)(vy+cellheight/2),
                 (WORD)(max(cellwidth/2-1,1)),
                 (WORD)(max(cellheight/2-1,1)));
            break;
        case CELLFOOD:
            vsf_color(h_vdi,(WORD)GREEN);
            v_ellipse(h_vdi,(WORD)(vx+cellwidth/2),
                     (WORD)(vy+2*cellheight/3),
                 (WORD)(cellwidth/2),
                 (WORD)(cellheight/3));
            vsf_color(h_vdi,(WORD)BLACK);
            v_pieslice(h_vdi,(WORD)(vx+cellwidth/2),
                     (WORD)(vy+cellheight/2),
                 (WORD)(cellheight/2),
                 (WORD)(500),
                 (WORD)(1300));
            break;
    }
    
}

void incremental_draw(WORD h_vdi, GRECT *play, int x, int y)
{
int cellwidth, cellheight;

WUNIT *walker;

    cellwidth = max(1,play->g_w/CELLW);
    cellheight = max(1,play->g_h/CELLH);

    drawcell(h_vdi, field[x][y], 
             x, y, 
             play->g_x, play->g_y,
             cellwidth, cellheight);
}

void draw_field(WORD h_vdi, GRECT *play, GRECT *dirty)
{
char i,j;
WORD pts[4];
int cellwidth, cellheight;
GRECT crc;

#ifdef VDEBUG
    printf("%d: %d %d %d %d\n",h_vdi,play->g_x,play->g_y,play->g_w,play->g_h);
#endif

    vsf_interior(h_vdi, 1);

    /* Fill background */
    vsf_color(h_vdi, (WORD)WHITE);
    
    if(dirty == NULL) {
        pts[0] = play->g_x; pts[1] = play->g_y;
        pts[2] = play->g_x+play->g_w; pts[3] = play->g_y+play->g_h;
    } else {
        pts[0] = dirty->g_x; pts[1] = dirty->g_y;
        pts[2] = dirty->g_x+dirty->g_w; pts[3] = dirty->g_y+dirty->g_h;
    }
    v_bar(h_vdi,pts);

    cellwidth = max(1,play->g_w/CELLW);
    cellheight = max(1,play->g_h/CELLH);
    
    /* Draw all cells */
    for(i=0;i<CELLW;i++) {
        for(j=0;j<CELLH;j++) {
            if(field[i][j] != CELLBLANK) {
                if(dirty == NULL)
                    drawcell(h_vdi, field[i][j], i, j, 
                             play->g_x, play->g_y,
                             cellwidth, cellheight);
                else {
                    getcellrect(i, j, 
                                play->g_x, play->g_y,
                                cellwidth, cellheight,
                                &crc);
                                
                    /* Only draw if inside the dirty */
                    if(rc_intersect(dirty, &crc)) {
                        drawcell(h_vdi, field[i][j], i, j, 
                                 play->g_x, play->g_y,
                                 cellwidth, cellheight);

                    }
                }
            }
        }
    }

    /* Rounding can cause some blank space, so... */
    crc.g_x = cellwidth*CELLW;
    crc.g_y = play->g_y;
    crc.g_w = play->g_w - crc.g_x;
    crc.g_h = play->g_h;
    crc.g_x += play->g_x;
    if(crc.g_w > 0 && (dirty == NULL || rc_intersect(dirty, &crc))) {
        vsf_color(h_vdi, (WORD)RED);
        pts[0] = crc.g_x; pts[1] = crc.g_y;
        pts[2] = crc.g_x+crc.g_w; pts[3] = crc.g_y+crc.g_h;
        v_bar(h_vdi,pts);
    }
    
    crc.g_x = play->g_x;
    crc.g_y = cellheight*CELLH;
    crc.g_w = play->g_w;
    crc.g_h = play->g_h - crc.g_y;
    crc.g_y += play->g_y;
    if(crc.g_h > 0 && (dirty == NULL || rc_intersect(dirty, &crc))) {
        vsf_color(h_vdi, (WORD)RED);
        pts[0] = crc.g_x; pts[1] = crc.g_y;
        pts[2] = crc.g_x+crc.g_w; pts[3] = crc.g_y+crc.g_h;
        v_bar(h_vdi,pts);
    } 

    /* Done! */

}


int update_field(WPLAYER *player) 
{
WUNIT *walker;
int da;
    
    /* Check the wall now */
    da = check_wall(player);
    
    /* First empty the player from the field */
    empty_field(CELLPLAYER);
    
    /* Now reinsert all player values */
    walker = player->head;
    while(walker != NULL) {
        field[walker->c_x][walker->c_y] = CELLPLAYER;
        walker = (WUNIT *)walker->next;
    }
    return da;
}
