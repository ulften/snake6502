/* Levels: each level has its own wall layout inside the board */

#ifndef LEVEL_H
#define LEVEL_H

#define NUM_LAYOUTS 4 /* after the last layout it starts over from the first */

/* Draws the board for level n (0 = the first level): the outer walls and
   grass from draw_board(), then the level's own walls on top. Every layout
   keeps the snake's starting stretch (rows 14-16, columns 16-32) free. */
void draw_level(unsigned char n);

#endif
