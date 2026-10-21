/* Graphics: tile map, sprites, palette and tile images
 *
 * The graphics are two "planes" that VGA stacks on top of each other:
 *   plane 0: tile map (mode 2). 40x30 cells, 1 byte per cell = tile number.
 *            Tiles are 8x8 pixels, 4 bits per pixel (16 colors), 32 bytes each.
 *   plane 1: text (mode 1, 1-bit color) on the top row only, for the score.
 * On top of plane 0 is a sprite layer (mode 5) with the snake's head and tail.
 */

#ifndef GFX_H
#define GFX_H

#define COLS 40
#define ROWS 30

/* ---------- tile numbers ---------- */
#define T_HUD 0
#define T_WALL 1
#define T_BODY 2
#define T_FOOD 3
#define T_HEAD 4 /* 4=up 5=right 6=down 7=left (same order as dir) */
#define T_OCC 8  /* looks like grass, marks the cell where the head is */
#define T_STONE 9
#define T_BONUS 10 /* the golden apple */
#define T_GRASS 16 /* 16..47: grass, GRASS_LAYOUTS * GRASS_BANDS of them */

/* Sets up the canvas, palette, tile images and the planes */
void video_init(void);

void wait_vsync(void);

void xram_write(unsigned addr, const unsigned char *src, unsigned n);

void put_tile(unsigned char x, unsigned char y, unsigned char t);
unsigned char get_tile(unsigned char x, unsigned char y);

/* Walls around the board, grass inside and a black HUD row on top */
void draw_board(void);

/* Places a sprite (idx 0 = tail, 1 = head). x and y are pixels on the
   canvas (0..319, 0..239).
   t is the tile number whose image is used as the sprite image (T_HEAD + direction). */
void sprite_set(unsigned char idx, unsigned x, unsigned y, unsigned char t);

/* Packs 8x8 pixels into 32 bytes: two pixels per byte, the left pixel in
   the upper half of the byte. */
void pack_tile(unsigned char *dst, unsigned char px[8][8]);

/* ---------- palette effects ---------- */

/* Palette pulse: the mortar (color 4) and the apple glint (color 13) */
void pulse(unsigned char tick);

/* The gold color: when it blinks off it shows the grass color instead */
void bonus_color(unsigned char on);

/* flash = 1 makes the snake white, 0 restores it */
void snake_color(unsigned char flash);

/* Moves the tile plane sideways (x_pos in the mode 2 config) */
void shake(signed char s);

#endif
