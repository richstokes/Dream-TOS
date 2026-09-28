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

#ifndef __FIELD_HEADERS
#define __FIELD_HEADERS

#define CELLW	40
#define CELLH	24

#ifdef __GNUC__
#include <stdint.h>

#ifndef WORD
#define WORD uint16_t
#endif

#include <gem.h>

#else

#include <portab.h>
#include <aes.h>

#endif

#include "player.h"

/* Initializes the play field */
void field_init(void);

/* Checks if the player died due to wall collision */
int check_wall(WPLAYER *player);

void draw_field(WORD h_vdi, GRECT *play, GRECT *dirty);

int update_field(WPLAYER *player);

void food_init(void);
int food_check(WORD h_vdi, GRECT *play, WPLAYER *player);

void incremental_draw(WORD h_vdi, GRECT *play, int x, int y);

#define FOODADDITION    5
#define FOODRESETCOUNT  30

#endif /* __FIELD_HEADERS */