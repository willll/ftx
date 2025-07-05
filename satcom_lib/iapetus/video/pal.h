/*  Copyright 2005-2007,2013 Theo Berkau

    This file is part of Iapetus.

    Iapetus is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Iapetus is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Iapetus; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef PAL_H
#define PAL_H

/* Color IDs in default palette */
#define COLOR_BLACK    0
#define COLOR_BLUE     1
#define COLOR_GREEN    2
#define COLOR_TEAL     3
#define COLOR_RED      4
#define COLOR_PURPLE   5
#define COLOR_MAROON   6
#define COLOR_SILVER   7
#define COLOR_GRAY     8
#define COLOR_AQUA     9
#define COLOR_LIME    10
#define COLOR_OLIVE   11
#define COLOR_FUCHSIA 12
#define COLOR_PINK    13
#define COLOR_YELLOW  14
#define COLOR_WHITE   15

// ???
#define COLOR_BLACK2  16

/* For all the "printf" functions, use this macro to define background and foreground colors.
 * Not using this macro will define the background color to 0 (= transparent color).
 */
#define COLOR_BG_FG(B,F) ((B)<<8|(F))

#endif
