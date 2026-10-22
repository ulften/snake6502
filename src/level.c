/* Levels: each level has its own wall layout inside the board
 *
 * The inside of the board is columns 1..38 and rows 2..28. The snake starts
 * on row 15 at columns 18-20 heading right, so no layout may put a wall on
 * rows 14-16 between columns 16 and 32.
 *
 *   0  open       just the outer walls
 *   1  bars       two long bars, above and below the snake
 *   2  box        a box around the middle with a gap in each side
 *   3  pillars    a grid of 2x2 pillars
 */

#include "gfx.h"
#include "level.h"

/* Fills the rectangle (x0,y0)-(x1,y1), corners included, with wall */
static void wall_rect(unsigned char x0, unsigned char y0,
                      unsigned char x1, unsigned char y1)
{
    unsigned char x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            put_tile(x, y, T_WALL);
}

void draw_level(unsigned char n)
{
    unsigned char x, y;

    draw_board();
    switch (n % NUM_LAYOUTS)
    {
    case 1: /* bars */
        wall_rect(8, 7, 31, 7);
        wall_rect(8, 23, 31, 23);
        break;
    case 2: /* box: columns 6..33, rows 6..24, gaps in the middle of each side */
        wall_rect(6, 6, 16, 6);
        wall_rect(23, 6, 33, 6);
        wall_rect(6, 24, 16, 24);
        wall_rect(23, 24, 33, 24);
        wall_rect(6, 7, 6, 12);
        wall_rect(6, 18, 6, 23);
        wall_rect(33, 7, 33, 12);
        wall_rect(33, 18, 33, 23);
        break;
    case 3: /* pillars: 5 columns x 4 rows, none on rows 14-16 */
        for (y = 5; y <= 19; y += 14)
        {
            for (x = 5; x <= 33; x += 7)
            {
                wall_rect(x, y, x + 1, y + 1);
                wall_rect(x, y + 5, x + 1, y + 6);
            }
        }
        break;
    }
}
