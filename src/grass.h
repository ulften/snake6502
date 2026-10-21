/* The grass and the wind in it
 *
 * The board is split into 16 diagonal bands with their own tile numbers. A
 * gust sweeps from band to band and the blades bend as it passes. Since the
 * map only points at tile numbers, it is enough to rewrite the tile images
 * of the bands that change; the map is never redrawn.
 */

#ifndef GRASS_H
#define GRASS_H

#define GRASS_LAYOUTS 2 /* two blade patterns so the grass doesn't look tiled */
#define GRASS_POSES 3   /* 0 = straight, 1 = tip bent, 2 = bent more */
#define GRASS_BANDS 16  /* diagonal bands; the wind moves one band per step */
#define WIND_FRAMES 8   /* frames per wind step (higher = calmer wind) */

/* Draws the grass images and writes all grass tiles (and T_OCC) to XRAM */
void grass_init(void);

/* The wind: one step of the gust sweeping diagonally across the board */
void wind(void);

/* The grass tile for a cell. The same position always gives the same tile. */
unsigned char grass_tile(unsigned char x, unsigned char y);

unsigned char is_grass(unsigned char t);

#endif
