/* GEM Worm
 * Copyright (C) 2009 Jeffrey Armstrong
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
#ifndef __PLHEADERS
#define __PLHEADERS

typedef struct {
    char c_x;
    char c_y;
    void *next;
} WUNIT;

typedef struct {
    int length;
    WUNIT *head;
    char dir;
    int grow;
    int score;
    int status;
} WPLAYER;

#define WUP     0
#define WDOWN   1
#define WLEFT   2
#define WRIGHT  3

#define ALIVE   1
#define DEAD    0

#define STARTLENGTH 6

void move_player(WPLAYER *player, char dir);

int update_player(WPLAYER *player, int *tailx, int *taily);

WPLAYER *init_player(void);

void reset_player(WPLAYER *player);

#endif /* __PLHEADERS */
