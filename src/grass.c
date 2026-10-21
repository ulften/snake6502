/* The grass and the wind in it */

#include <string.h>
#include "gfx.h"
#include "grass.h"
#include "xram.h"

/* The grass is drawn in code as blades: column, top row and length in pixels.
   The tip is light (color 2) and the root dark (color 6). The column may be
   at most 6 since the tip bends one step to the right. */
typedef struct
{
    unsigned char col, row, len;
} blade_t;

static const blade_t BLADES[GRASS_LAYOUTS][4] = {
    {{1, 1, 3}, {2, 2, 2}, {5, 4, 3}, {6, 5, 2}},
    {{3, 0, 3}, {4, 1, 2}, {0, 4, 3}, {6, 6, 2}}};

/* A gust of wind: how much a band bends, step by step. The grass bends
   softly, stays bent for a while and then springs back. The zeros are the
   calm between gusts. */
static const unsigned char GUST[GRASS_BANDS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 1};

/* Pre-packed grass images (32 bytes each) and which pose each band shows */
static unsigned char grass_img[GRASS_LAYOUTS][GRASS_POSES][32];
static unsigned char band_pose[GRASS_BANDS];
static unsigned char wind_phase;

/* Draws a grass pattern in a given pose. The root stays put; in pose 1 the
   tip bends one step to the right, in pose 2 the pixel below the tip too. */
static void make_grass(unsigned char px[8][8], unsigned char layout, unsigned char pose)
{
    const blade_t *b = BLADES[layout];
    unsigned char k, i, root;

    memset(px, 1, 64);
    for (k = 0; k < 4; k++, b++)
    {
        root = b->len - 1;
        for (i = 0; i <= root; i++)
            px[b->row + i][b->col + (i < root && pose > i)] = (i == root) ? 6 : 2;
    }
}

void grass_init(void)
{
    unsigned char px[8][8];
    unsigned char t, p;

    for (t = 0; t < GRASS_LAYOUTS; t++)
        for (p = 0; p < GRASS_POSES; p++)
        {
            make_grass(px, t, p);
            pack_tile(grass_img[t][p], px);
        }

    /* T_OCC looks like straight grass (it is under the head sprite anyway) */
    xram_write(XRAM_TILES + T_OCC * 32, grass_img[0][0], 32);

    memset(band_pose, 0xFF, sizeof band_pose); /* forces wind() to write all bands */
    wind();
}

/* Only the bands whose pose changes are rewritten
   (4 bands * 2 patterns = 256 bytes per step). */
void wind(void)
{
    unsigned char b, p, l;

    for (b = 0; b < GRASS_BANDS; b++)
    {
        p = GUST[(unsigned char)(wind_phase - b) & (GRASS_BANDS - 1)];
        if (p == band_pose[b])
            continue;
        band_pose[b] = p;
        for (l = 0; l < GRASS_LAYOUTS; l++)
            xram_write(XRAM_TILES + (unsigned)(T_GRASS + l * GRASS_BANDS + b) * 32,
                       grass_img[l][p], 32);
    }
    ++wind_phase;
}

/* The band is the diagonal x + y, with a small notch here and there so the
   wind front isn't a straight line. */
unsigned char grass_tile(unsigned char x, unsigned char y)
{
    unsigned char layout = ((unsigned)x * 7 + (unsigned)y * 13) % 5 == 0;
    unsigned char band = x + y + (((unsigned)x * 11 + (unsigned)y * 3) % 3 == 0);
    return T_GRASS + layout * GRASS_BANDS + (band & (GRASS_BANDS - 1));
}

unsigned char is_grass(unsigned char t)
{
    return t >= T_GRASS && t < T_GRASS + GRASS_LAYOUTS * GRASS_BANDS;
}
