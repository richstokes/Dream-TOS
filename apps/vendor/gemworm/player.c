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

#include "player.h"
#include "field.h"

void move_player(WPLAYER *player, char dir) 
{
    player->dir = dir;
}

void swap(char *i, char *j)
{
char s;

    s = *i;
    *i = *j;
    *j = s;
}

void grow(WPLAYER *player, int count)
{
    player->grow += count;
}

int update_player(WPLAYER *player, int *tailx, int *taily)
{
WUNIT *walker;
char lastx, lasty;
int alive;

    walker = player->head;
    lastx = walker->c_x;
    lasty = walker->c_y;

    switch(player->dir) {
        case WUP:
            lasty--;
            break;
        case WDOWN:
            lasty++;
            break;
        case WLEFT:
            lastx--;
            break;
        case WRIGHT:
            lastx++;
            break;
    }

    while(walker != NULL) {
        swap(&lastx, &walker->c_x);
        swap(&lasty, &walker->c_y);
        
        if(walker->next == NULL && player->grow > 0) {
            walker->next =(WUNIT *)malloc(sizeof(WUNIT));
            ((WUNIT *)walker->next)->next = NULL;
            ((WUNIT *)walker->next)->c_x = lastx;
            ((WUNIT *)walker->next)->c_y = lasty;
            player->grow--;
        }
        
        walker = (WUNIT *)walker->next;
    }
    
    *tailx = lastx;
    *taily = lasty;
    
    walker = (WUNIT *)player->head->next;
    alive = ALIVE;
    while(walker != NULL) {
        if(walker->c_x == player->head->c_x && walker->c_y == player->head->c_y) {
            alive = DEAD;
            player->status = DEAD;
            break;
        }
        walker = (WUNIT *)walker->next;
    }
    return alive;
}

WUNIT *reset_body(WUNIT *head)
{
WUNIT *walker;
WUNIT *tmp;
int i;
char lastx;

    walker = head;
    while(walker != NULL) {
        tmp = walker;
        walker = (WUNIT *)walker->next;
        tmp->next = NULL;
        free(tmp);
    }

    walker = (WUNIT *)malloc(sizeof(WUNIT));
    walker->c_x = CELLW/3*2;
    walker->c_y = CELLH/2;
    tmp = walker;
    for(i=1;i<STARTLENGTH;i++) {
        lastx = walker->c_x;
        walker->next = (WUNIT *)malloc(sizeof(WUNIT));
        walker = (WUNIT *)walker->next;
        walker->c_y = CELLH/2;
        walker->c_x = lastx+1;
        walker->next = NULL;
    }

    return tmp;
}


void reset_player(WPLAYER *player)
{
    player->length = STARTLENGTH;
    player->dir = WLEFT;
    player->head = reset_body(player->head);
    player->status = ALIVE;
    player->score = 0;
}

WPLAYER *init_player()
{
WPLAYER *player;

    player = (WPLAYER *)malloc(sizeof(WPLAYER));
    player->head = NULL;
    reset_player(player);
    player->grow = 0;
    player->score = 0;
    player->status = ALIVE;
    return player;
}
