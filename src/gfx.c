/* Graphics: tile map, sprites, palette and tile images */

#include <rp6502.h>
#include "gfx.h"
#include "grass.h"
#include "xram.h"

/* ---------- palette: 16 colors (R, G, B, 0-255) ---------- */
static const unsigned char pal[16][3] = {
    {0, 0, 0},       /* 0  black (HUD)          */
    {36, 110, 44},   /* 1  grass                */
    {62, 156, 62},   /* 2  grass blade          */
    {70, 70, 110},   /* 3  wall, dark           */
    {120, 120, 170}, /* 4  wall, light (mortar) */
    {40, 190, 70},   /* 5  snake, body          */
    {15, 100, 35},   /* 6  snake, edge + blade root */
    {120, 235, 100}, /* 7  snake, head/shine    */
    {255, 255, 255}, /* 8  white (eyes)         */
    {235, 185, 40},  /* 9  gold (pupil in the sprite palette, see load_palette) */
    {225, 40, 40},   /* 10 apple, red           */
    {140, 15, 25},   /* 11 apple, dark red      */
    {120, 80, 30},   /* 12 stem, brown          */
    {255, 255, 255}, /* 13 apple glint (pulses) */
    {160, 160, 165}, /* 14 stone, light         */
    {90, 90, 100}};  /* 15 stone, dark          */

/* ---------- tile images as text: one digit/letter per pixel ----------
   0-9 and a-f are indexes into the palette above. */
static const char *const ART[T_HEAD][8] = {
    /* T_HUD */
    {"00000000", "00000000", "00000000", "00000000",
     "00000000", "00000000", "00000000", "00000000"},
    /* T_WALL */
    {"44444444", "33343333", "33343333", "44444444",
     "33333433", "33333433", "44444444", "33343333"},
    /* T_BODY */
    {"66666666", "65555556", "65755556", "65555556",
     "65555556", "65555756", "65555556", "66666666"},
    /* T_FOOD */
    {"111c1111", "11aaaa11", "1aadaaa1", "1aaaaaa1",
     "1aaaaaa1", "1baaaab1", "11bbbb11", "11111111"}};

/* The head is drawn only once (facing up) and then rotated in code */
static const char *const ART_HEAD_UP[8] = {
    "66666666", "67977976", "67877876", "67777776",
    "67777776", "67777776", "67777776", "66666666"};

static const char *const ART_BONUS[8] = {
    "111c1111", "11999911", "199d9991", "19999991",
    "19999991", "1c9999c1", "11cccc11", "11111111"};

/* Rows of round leaf clumps in the light grass green, lighter than the
   grass so the hedge stands out, with black gaps between them. Colors 0, 2
   and 6 never change, so the hedge stays calm while the outer walls glow.
   No border, so hedge tiles grow together. */
static const char *const ART_HEDGE[8] = {
    "62266226", "22222222", "22262226", "06600660",
    "26622662", "22222222", "26222622", "60066006"};

static const char *const ART_STONE[8] = {
    "111ee111", "11eeeef1", "1eeeeeff", "1eeeeeff",
    "1eeeeeff", "1feeeeff", "11ffff11", "11111111"};

/* ---------- XRAM helpers ---------- */

void xram_write(unsigned addr, const unsigned char *src, unsigned n)
{
    RIA.addr0 = addr;
    RIA.step0 = 1;
    while (n--)
        RIA.rw0 = *src++;
}

void put_tile(unsigned char x, unsigned char y, unsigned char t)
{
    RIA.addr0 = XRAM_MAP + (unsigned)y * COLS + x;
    RIA.step0 = 1;
    RIA.rw0 = t;
}

unsigned char get_tile(unsigned char x, unsigned char y)
{
    RIA.addr0 = XRAM_MAP + (unsigned)y * COLS + x;
    RIA.step0 = 1;
    return RIA.rw0;
}

/* The sprite is a mode5_sprite_t: x, y, image pointer, palette pointer (8 bytes). */
void sprite_set(unsigned char idx, unsigned x, unsigned y, unsigned char t)
{
    unsigned img = XRAM_TILES + (unsigned)t * 32;
    RIA.addr0 = XRAM_SPR_CFG + (unsigned)idx * 8;
    RIA.step0 = 1;
    RIA.rw0 = x & 0xFF;
    RIA.rw0 = x >> 8;
    RIA.rw0 = y & 0xFF;
    RIA.rw0 = y >> 8;
    RIA.rw0 = img & 0xFF;
    RIA.rw0 = img >> 8;
    RIA.rw0 = XRAM_SPR_PAL & 0xFF;
    RIA.rw0 = XRAM_SPR_PAL >> 8;
}

void wait_vsync(void)
{
    unsigned char v = RIA.vsync;
    while (v == RIA.vsync)
        ;
}

/* ---------- tiles and palette to XRAM ---------- */

/* Writes a color into a palette (RGB555 + alpha bit 0x20 that makes the color visible) */
static void set_color_at(unsigned base, unsigned char i,
                         unsigned char r, unsigned char g, unsigned char b)
{
    unsigned c = ((unsigned)(b >> 3) << 11) |
                 ((unsigned)(g >> 3) << 6) |
                 (unsigned)(r >> 3) | 0x20;
    RIA.addr0 = base + (unsigned)i * 2;
    RIA.step0 = 1;
    RIA.rw0 = c & 0xFF; /* low byte first */
    RIA.rw0 = c >> 8;
}

static void set_color(unsigned char i, unsigned char r, unsigned char g, unsigned char b)
{
    set_color_at(XRAM_PAL, i, r, g, b);
}

static void load_palette(void)
{
    unsigned char i;
    for (i = 0; i < 16; i++)
        set_color(i, pal[i][0], pal[i][1], pal[i][2]);

    /* The sprite palette is a copy, but color 0 is transparent (alpha bit off) */
    for (i = 1; i < 16; i++)
        set_color_at(XRAM_SPR_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
    /* Color 9 is the pupil in the head sprite. The head is never put in the
       map, so in the tile palette the same number can be gold for the golden apple. */
    set_color_at(XRAM_SPR_PAL, 9, 10, 10, 10);
    RIA.addr0 = XRAM_SPR_PAL;
    RIA.step0 = 1;
    RIA.rw0 = 0;
    RIA.rw0 = 0;
}

static void load_art(unsigned char px[8][8], const char *const *art)
{
    unsigned char r, c;
    char ch;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
        {
            ch = art[r][c];
            px[r][c] = (ch <= '9') ? (ch - '0') : (ch - 'a' + 10);
        }
}

/* Swap the order here if images look mirrored. */
void pack_tile(unsigned char *dst, unsigned char px[8][8])
{
    unsigned char r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c += 2)
            *dst++ = (px[r][c] << 4) | px[r][c + 1];
}

static void store_tile(unsigned char id, unsigned char px[8][8])
{
    unsigned char buf[32];
    pack_tile(buf, px);
    xram_write(XRAM_TILES + (unsigned)id * 32, buf, 32);
}

static void rotate_cw(unsigned char px[8][8])
{
    unsigned char t[8][8];
    unsigned char r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            t[r][c] = px[7 - c][r];
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            px[r][c] = t[r][c];
}

static void load_tiles(void)
{
    unsigned char px[8][8];
    unsigned char t, k;

    for (t = 0; t < T_HEAD; t++)
    {
        load_art(px, ART[t]);
        store_tile(t, px);
    }

    load_art(px, ART_HEAD_UP);
    for (k = 0; k < 4; k++)
    {
        store_tile(T_HEAD + k, px);
        rotate_cw(px); /* up -> right -> down -> left */
    }

    load_art(px, ART_STONE);
    store_tile(T_STONE, px);

    load_art(px, ART_BONUS);
    store_tile(T_BONUS, px);

    load_art(px, ART_HEDGE);
    store_tile(T_HEDGE, px);

    grass_init();
}

/* ---------- video ---------- */

void video_init(void)
{
    /* mode2_config_t: x_wrap, y_wrap, x_pos, y_pos, width, height in tiles,
       pointers to the map, the palette and the tile images */
    static const unsigned char cfg_tiles[16] = {
        0, 0,
        0, 0,
        0, 0,
        COLS, 0,
        ROWS, 0,
        XRAM_MAP & 0xFF, XRAM_MAP >> 8,
        XRAM_PAL & 0xFF, XRAM_PAL >> 8,
        XRAM_TILES & 0xFF, XRAM_TILES >> 8};
    /* mode1_config_t for a single text row. Palette and font 0xFFFF = built-in */
    static const unsigned char cfg_text[16] = {
        0, 0,
        0, 0,
        0, 0,
        COLS, 0,
        1, 0,
        XRAM_TEXT & 0xFF, XRAM_TEXT >> 8,
        0xFF, 0xFF,
        0xFF, 0xFF};
    unsigned char i;

    xreg_vga_canvas(1); /* 320x240 */

    load_palette();
    load_tiles();

    xram_write(XRAM_CFG_TILES, cfg_tiles, 16);
    xram_write(XRAM_CFG_TEXT, cfg_text, 16);

    sprite_set(0, 320, 240, T_BODY); /* outside the canvas = invisible */
    sprite_set(1, 320, 240, T_HEAD);

    /* blank spaces on the text row */
    RIA.addr0 = XRAM_TEXT;
    RIA.step0 = 1;
    for (i = 0; i < COLS; i++)
        RIA.rw0 = ' ';

    /* plane 0: tile map over the whole canvas */
    xreg_vga_mode(2, MODE2_4BPP | MODE2_8X8, XRAM_CFG_TILES, 0);
    /* plane 1: text on scanlines 0-7 (the top row) */
    xreg_vga_mode(1, 0, XRAM_CFG_TEXT, 1, 0, 8);

    /* the sprite layer on top of plane 0: 2 sprites, 8x8, 4 bits */
    xreg_vga_mode5(MODE5_4BPP | MODE5_8X8, XRAM_SPR_CFG, 2, 0);
}

void draw_board(void)
{
    unsigned char x, y, t;

    RIA.addr0 = XRAM_MAP;
    RIA.step0 = 1;
    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++)
        {
            if (y == 0)
                t = T_HUD;
            else if (y == 1 || y == ROWS - 1 || x == 0 || x == COLS - 1)
                t = T_WALL;
            else
                t = grass_tile(x, y);
            RIA.rw0 = t;
        }
}

/* ---------- palette effects ---------- */

void pulse(unsigned char tick)
{
    unsigned char p = (tick >> 2) & 15; /* 0..15, around once a second */
    unsigned char w = (tick >> 3) & 15; /* 0..15, half as fast for the walls */
    if (p > 7)
        p = 15 - p; /* triangle wave 0..7..0 */
    if (w > 7)
        w = 15 - w;

    set_color(4, 130 + w * 8, 130 + w * 8, 175 + w * 5); /* a calm glow */
    set_color(13, 255, 255 - p * 20, 255 - p * 20);
}

void bonus_color(unsigned char on)
{
    if (on)
        set_color(9, pal[9][0], pal[9][1], pal[9][2]);
    else
        set_color(9, pal[1][0], pal[1][1], pal[1][2]);
}

/* The snake's colors (5 = body, 7 = head/shine) in both palettes.
   Color 6 is left alone, it is the blade root. */
void snake_color(unsigned char flash)
{
    unsigned char i;
    for (i = 5; i <= 7; i += 2)
    {
        if (flash)
        {
            set_color_at(XRAM_PAL, i, 255, 255, 255);
            set_color_at(XRAM_SPR_PAL, i, 255, 255, 255);
        }
        else
        {
            set_color_at(XRAM_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
            set_color_at(XRAM_SPR_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
        }
    }
}

void shake(signed char s)
{
    RIA.addr0 = XRAM_CFG_TILES + 2;
    RIA.step0 = 1;
    RIA.rw0 = (unsigned char)s;
    RIA.rw0 = s < 0 ? 0xFF : 0x00;
}
