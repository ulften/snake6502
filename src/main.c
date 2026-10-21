/*
 * Snake for the Picocomputer 6502 - step 6: growing stones, game over, pause
 *
 * New in step 6:
 *  - The number of stones from apples that may stay on the board starts at
 *    STONE_START and grows by one every STONE_GROW apples, up to STONE_MAX.
 *  - When the snake dies the board shakes (x_pos in the tile config changes)
 *    and the snake flashes white (colors 5 and 7 in both palettes).
 *  - P pauses and resumes the game.
 *
 * Step 5: speed, high score and golden apple
 *
 * New in step 5:
 *  - The snake speeds up: a step takes STEP_START frames at first and one
 *    frame less every SPEEDUP_EVERY apples, down to STEP_MIN.
 *  - The high score is shown on the score row and saved in HISCORE_FILE on
 *    the USB drive.
 *  - The golden apple: after every BONUS_EVERY apples a golden apple worth
 *    BONUS_POINTS appears. It disappears after BONUS_FRAMES frames and
 *    blinks (via the palette) before that.
 *
 * Step 4: sound (PSG)
 *
 * New in step 4: the PSG sound generator (8 channels) is controlled through
 * 64 bytes in XRAM, just like the keyboard and the sprite list. Three sound
 * effects:
 *  - eating an apple: short square wave with rising pitch
 *  - a stone lands: a short burst of noise ("thud")
 *  - game over: sawtooth wave with falling pitch
 * A sound starts with gate = 1 and fades out when gate is set to 0.
 * sfx_update() runs every frame and handles pitch and gate.
 *
 * Step 3c: stones behind the tail
 *
 * New in 3c: every STONE_EVERY apples gives a stone that lands in the cell
 * the tail leaves. It is revealed as the tail sprite slides away. Only a
 * limited number of such stones stay on the board; when a new one arrives
 * the oldest turns back into grass.
 *
 * Step 3b: sprites for head and tail
 *
 * Changes compared to step 3:
 *  - The tail is also a sprite. When the tail leaves a cell the map is set
 *    to grass right away, but a body sprite slides from that cell to the
 *    next segment. That way both ends of the snake move smoothly (the middle
 *    stands still between steps, just like a real snake).
 *  - Stones (T_STONE) that the snake must avoid. NUM_STONES sets how many.
 *  - The score row is padded with spaces so that old digits don't linger.
 *
 * Step 3:
 *
 * New in step 3:
 *  - The snake's head is a sprite (mode 5, 4 bits, 8x8) that slides one
 *    pixel per frame between cells instead of jumping a whole cell at a time.
 *    The game logic still works in whole cells, but VGA draws the sprite
 *    layer on top of the tile map without the 6502 redrawing anything.
 *  - The sprite uses the same images as the head tiles (same format in XRAM)
 *    but its own palette where color 0 is transparent.
 *  - The head's cell in the map is T_OCC: a tile that looks like grass but
 *    counts as occupied. The apple stays in the map until the head has slid
 *    onto it.
 *
 * From step 2 (compared to step 1):
 *  - Wind in the grass: the board is split into 16 diagonal bands with their
 *    own tile numbers. A gust sweeps from band to band and the blades bend as
 *    it passes. Since the map only points at tile numbers, it is enough to
 *    rewrite the tile images of the bands that change; the map is never
 *    redrawn.
 *  - The walls glow and the apple glints: we change single colors in the
 *    palette (2 bytes per color). The whole screen changes color without
 *    redrawing the map.
 *
 * Controls:  A = turn left, D = turn right, P = pause, ESC = quit
 *
 * The graphics are two "planes" that VGA stacks on top of each other:
 *   plane 0: tile map (mode 2). 40x30 cells, 1 byte per cell = tile number.
 *            Tiles are 8x8 pixels, 4 bits per pixel (16 colors), 32 bytes each.
 *   plane 1: text (mode 1, 1-bit color) on the top row only, for the score.
 *
 * Everything lives in XRAM. VGA reads it directly every frame:
 *   0x1000  text row (40 bytes)
 *   0x1100  tile map (1200 bytes)
 *   0x2000  tile images (32 bytes per tile)
 *   0xFF00  keyboard (32 bytes)
 *   0xFF40  tile map config (16 bytes)
 *   0xFF50  text row config (16 bytes)
 *   0xFF80  palette (16 colors * 2 bytes)
 *   0xFFA0  sprite palette (as above, but color 0 transparent)
 *   0xFFC0  sprite list (2 sprites * 8 bytes: 0 = tail, 1 = head)
 *   0xFE00  PSG, the sound generator (8 channels * 8 bytes)
 */

#include <rp6502.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* XREG shortcuts, taken from the RIA and VGA documentation */
#define xreg_ria_keyboard(...) xreg(0, 0, 0, __VA_ARGS__)
#define xreg_vga_canvas(...) xreg(1, 0, 0, __VA_ARGS__)
#define xreg_vga_mode(...) xreg(1, 0, 1, __VA_ARGS__)
#define xreg_vga_mode5(...) xreg(1, 0, 1, 5, __VA_ARGS__)
#define xreg_ria_psg(...) xreg(0, 1, 0, __VA_ARGS__)

/* ---------- XRAM map ---------- */
#define TEXT_XRAM 0x1000
#define MAP_XRAM 0x1100
#define TILES_XRAM 0x2000
#define KEYS_XRAM 0xFF00
#define CFG_TILES 0xFF40
#define CFG_TEXT 0xFF50
#define PAL_XRAM 0xFF80
#define SPR_PAL 0xFFA0
#define SPR_CFG 0xFFC0
#define PSG_XRAM 0xFE00 /* 64 bytes, even address, doesn't cross a page boundary */

#define COLS 40
#define ROWS 30

/* mode 2: 4 bits per pixel, 8x8 tiles */
#define MODE2_4BPP 0x02
#define MODE2_8X8 0x00

/* mode 5: paletted sprites, 4 bits, 8x8 */
#define MODE5_4BPP 0x02
#define MODE5_8X8 0x00

/* PSG: waveforms (upper half of release_wave) and the gate bit */
#define PSG_WAVE_SQUARE 0x10
#define PSG_WAVE_SAWTOOTH 0x20
#define PSG_WAVE_NOISE 0x40
#define PSG_GATE 0x01

/* The PSG channels we use */
#define CH_EAT 0
#define CH_STONE 1
#define CH_OVER 2

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

/* ---------- grass and wind ---------- */
#define GRASS_LAYOUTS 2 /* two blade patterns so the grass doesn't look tiled */
#define GRASS_POSES 3   /* 0 = straight, 1 = tip bent, 2 = bent more */
#define GRASS_BANDS 16  /* diagonal bands; the wind moves one band per step */
#define WIND_FRAMES 8   /* frames per wind step (higher = calmer wind) */

/* ---------- USB HID key codes ---------- */
#define KEY_A 0x04
#define KEY_D 0x07
#define KEY_ESC 0x29
#define KEY_SPACE 0x2C
#define KEY_P 0x13

/* Swap these two if you want A = right and D = left */
#define KEY_TURN_LEFT KEY_A
#define KEY_TURN_RIGHT KEY_D

/* ---------- game settings ---------- */
#define NUM_STONES 8   /* stones on the board from the start */
#define STONE_START 4  /* this many stones from apples may stay at first */
#define STONE_GROW 5   /* one more stone may stay for every 5th apple */
#define STONE_MAX 20   /* but never more than this */
#define STONE_EVERY 1  /* one stone per this many apples */
#define STEP_START 8   /* frames per step at the start (lower = faster) */
#define STEP_MIN 4     /* top speed */
#define SPEEDUP_EVERY 4 /* one step faster every 4th apple */
#define BONUS_EVERY 5   /* a golden apple appears after every 5th apple */
#define BONUS_POINTS 5
#define BONUS_FRAMES 300 /* the golden apple stays for 5 seconds */
#define BONUS_BLINK 90   /* and blinks for the last 1.5 seconds */
#define HISCORE_FILE "snake.hi" /* the high score is saved on the USB drive */
#define MAXLEN 1024
#define MASK (MAXLEN - 1)

/* ---------- state ---------- */
static unsigned char keys[32];
static unsigned char sx[MAXLEN], sy[MAXLEN];
static unsigned head, tail;
static unsigned score;
static unsigned best;   /* the high score, read from and saved to HISCORE_FILE */
static unsigned apples; /* regular apples this round, controls the speed */
static unsigned char tick; /* counts frames, drives the animations */

/* the golden apple: bonus_t is frames left, 0 = no golden apple on the board */
static unsigned bonus_t;
static unsigned char bonus_x, bonus_y;

/* queue of the stones that apples have produced, oldest first (ring buffer) */
static unsigned char stq_x[STONE_MAX], stq_y[STONE_MAX];
static unsigned char stq_first, stq_count;

static const signed char dx[4] = {0, 1, 0, -1};
static const signed char dy[4] = {-1, 0, 1, 0};

#define KEY_DOWN(code) ((keys[(code) >> 3] & (1 << ((code) & 7))) != 0)

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

static const char *const ART_STONE[8] = {
    "111ee111", "11eeeef1", "1eeeeeff", "1eeeeeff",
    "1eeeeeff", "1feeeeff", "11ffff11", "11111111"};

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

/* ---------- Logging to the console (stderr), without printf ---------- */

static void log_str(const char *s)
{
    write(STDERR_FILENO, s, strlen(s));
}

/* Writes "ERROR: msg" on a line of its own */
static void log_error(const char *msg)
{
    log_str("ERROR: ");
    log_str(msg);
    log_str("\r\n");
}

/* Writes "label=n" on a line of its own, e.g. log_num("score", score) */
static void log_num(const char *label, unsigned n)
{
    char buf[6];
    unsigned char i = sizeof buf - 1;

    buf[i] = 0;
    do
    {
        buf[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    log_str(label);
    log_str("=");
    log_str(buf + i);
    log_str("\r\n");
}

/* ---------- XRAM helpers ---------- */

static void xram_write(unsigned addr, const unsigned char *src, unsigned n)
{
    RIA.addr0 = addr;
    RIA.step0 = 1;
    while (n--)
        RIA.rw0 = *src++;
}

static void put_tile(unsigned char x, unsigned char y, unsigned char t)
{
    RIA.addr0 = MAP_XRAM + (unsigned)y * COLS + x;
    RIA.step0 = 1;
    RIA.rw0 = t;
}

static unsigned char get_tile(unsigned char x, unsigned char y)
{
    RIA.addr0 = MAP_XRAM + (unsigned)y * COLS + x;
    RIA.step0 = 1;
    return RIA.rw0;
}

/* Places a sprite (idx 0 = tail, 1 = head). x and y are pixels on the
   canvas (0..319, 0..239).
   t is the tile number whose image is used as the sprite image (T_HEAD + direction).
   The sprite is a mode5_sprite_t: x, y, image pointer, palette pointer (8 bytes). */
static void sprite_set(unsigned char idx, unsigned x, unsigned y, unsigned char t)
{
    unsigned img = TILES_XRAM + (unsigned)t * 32;
    RIA.addr0 = SPR_CFG + (unsigned)idx * 8;
    RIA.step0 = 1;
    RIA.rw0 = x & 0xFF;
    RIA.rw0 = x >> 8;
    RIA.rw0 = y & 0xFF;
    RIA.rw0 = y >> 8;
    RIA.rw0 = img & 0xFF;
    RIA.rw0 = img >> 8;
    RIA.rw0 = SPR_PAL & 0xFF;
    RIA.rw0 = SPR_PAL >> 8;
}

static void read_keys(void)
{
    unsigned char i;
    RIA.addr0 = KEYS_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < 32; i++)
        keys[i] = RIA.rw0;
}

static void wait_vsync(void)
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
    set_color_at(PAL_XRAM, i, r, g, b);
}

static void load_palette(void)
{
    unsigned char i;
    for (i = 0; i < 16; i++)
        set_color(i, pal[i][0], pal[i][1], pal[i][2]);

    /* The sprite palette is a copy, but color 0 is transparent (alpha bit off) */
    for (i = 1; i < 16; i++)
        set_color_at(SPR_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
    /* Color 9 is the pupil in the head sprite. The head is never put in the
       map, so in the tile palette the same number can be gold for the golden apple. */
    set_color_at(SPR_PAL, 9, 10, 10, 10);
    RIA.addr0 = SPR_PAL;
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

/* Packs 8x8 pixels into 32 bytes: two pixels per byte, the left pixel in
   the upper half of the byte. Swap the order here if images look mirrored. */
static void pack_tile(unsigned char *dst, unsigned char px[8][8])
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
    xram_write(TILES_XRAM + (unsigned)id * 32, buf, 32);
}

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

/* The wind: one step of the gust sweeping diagonally across the board. Only
   the bands whose pose changes are rewritten (4 bands * 2 patterns = 256 bytes per step). */
static void wind(void)
{
    unsigned char b, p, l;

    for (b = 0; b < GRASS_BANDS; b++)
    {
        p = GUST[(unsigned char)(wind_phase - b) & (GRASS_BANDS - 1)];
        if (p == band_pose[b])
            continue;
        band_pose[b] = p;
        for (l = 0; l < GRASS_LAYOUTS; l++)
            xram_write(TILES_XRAM + (unsigned)(T_GRASS + l * GRASS_BANDS + b) * 32,
                       grass_img[l][p], 32);
    }
    ++wind_phase;
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
    unsigned char t, k, p;

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

    for (t = 0; t < GRASS_LAYOUTS; t++)
        for (p = 0; p < GRASS_POSES; p++)
        {
            make_grass(px, t, p);
            pack_tile(grass_img[t][p], px);
        }

    /* T_OCC looks like straight grass (it is under the head sprite anyway) */
    xram_write(TILES_XRAM + T_OCC * 32, grass_img[0][0], 32);

    memset(band_pose, 0xFF, sizeof band_pose); /* forces wind() to write all bands */
    wind();
}

/* ---------- video ---------- */

static void video_init(void)
{
    /* mode2_config_t: x_wrap, y_wrap, x_pos, y_pos, width, height in tiles,
       pointers to the map, the palette and the tile images */
    static const unsigned char cfg_tiles[16] = {
        0, 0,
        0, 0,
        0, 0,
        COLS, 0,
        ROWS, 0,
        MAP_XRAM & 0xFF, MAP_XRAM >> 8,
        PAL_XRAM & 0xFF, PAL_XRAM >> 8,
        TILES_XRAM & 0xFF, TILES_XRAM >> 8};
    /* mode1_config_t for a single text row. Palette and font 0xFFFF = built-in */
    static const unsigned char cfg_text[16] = {
        0, 0,
        0, 0,
        0, 0,
        COLS, 0,
        1, 0,
        TEXT_XRAM & 0xFF, TEXT_XRAM >> 8,
        0xFF, 0xFF,
        0xFF, 0xFF};
    unsigned char i;

    xreg_vga_canvas(1); /* 320x240 */

    load_palette();
    load_tiles();

    xram_write(CFG_TILES, cfg_tiles, 16);
    xram_write(CFG_TEXT, cfg_text, 16);

    sprite_set(0, 320, 240, T_BODY); /* outside the canvas = invisible */
    sprite_set(1, 320, 240, T_HEAD);

    /* blank spaces on the text row */
    RIA.addr0 = TEXT_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < COLS; i++)
        RIA.rw0 = ' ';

    /* plane 0: tile map over the whole canvas */
    xreg_vga_mode(2, MODE2_4BPP | MODE2_8X8, CFG_TILES, 0);
    /* plane 1: text on scanlines 0-7 (the top row) */
    xreg_vga_mode(1, 0, CFG_TEXT, 1, 0, 8);

    /* the sprite layer on top of plane 0: 2 sprites, 8x8, 4 bits */
    xreg_vga_mode5(MODE5_4BPP | MODE5_8X8, SPR_CFG, 2, 0);

    xreg_ria_keyboard(KEYS_XRAM);
}

/* The same position always gives the same tile. The band is the diagonal
   x + y, with a small notch here and there so the wind front isn't a straight line. */
static unsigned char grass_tile(unsigned char x, unsigned char y)
{
    unsigned char layout = ((unsigned)x * 7 + (unsigned)y * 13) % 5 == 0;
    unsigned char band = x + y + (((unsigned)x * 11 + (unsigned)y * 3) % 3 == 0);
    return T_GRASS + layout * GRASS_BANDS + (band & (GRASS_BANDS - 1));
}

static unsigned char is_grass(unsigned char t)
{
    return t >= T_GRASS && t < T_GRASS + GRASS_LAYOUTS * GRASS_BANDS;
}

static void draw_board(void)
{
    unsigned char x, y, t;

    RIA.addr0 = MAP_XRAM;
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

/* Text on row 0, columns 1..19 (at most 19 characters), the rest spaces.
   Columns 20..39 are the score. */
static void status(const char *s)
{
    unsigned char x = 1;
    RIA.addr0 = TEXT_XRAM + 1;
    RIA.step0 = 1;
    while (*s)
    {
        RIA.rw0 = *s++;
        x++;
    }
    while (x < 20)
    {
        RIA.rw0 = ' ';
        x++;
    }
}

/* Writes text wherever RIA.addr0 currently points */
static void put_str(const char *s)
{
    while (*s)
        RIA.rw0 = *s++;
}

/* Writes n as 4 digits, left-aligned and padded with spaces.
   Version without sprintf(), which isn't available in Picocomputer C17. */
static void put_num4(unsigned n)
{
    static const unsigned powers[4] = {1000, 100, 10, 1};
    unsigned char i, d, started = 0, col = 0;

    if (n > 9999)
        n = 9999;
    for (i = 0; i < 4; i++)
    {
        d = 0;
        while (n >= powers[i])
        {
            n -= powers[i];
            ++d;
        }
        if (d || started || i == 3) /* no leading zeros */
        {
            RIA.rw0 = '0' + d;
            started = 1;
            ++col;
        }
    }
    for (; col < 4; col++)
        RIA.rw0 = ' '; /* clears old digits */
}

/* "SCORE 1234 BEST 1234" in columns 20..39 */
static void show_score(void)
{
    RIA.addr0 = TEXT_XRAM + 20;
    RIA.step0 = 1;
    put_str("SCORE ");
    put_num4(score);
    put_str(" BEST ");
    put_num4(best);
}

/* ---------- the high score on the USB drive ---------- */

static void load_best(void)
{
    unsigned char buf[2];
    int fd = open(HISCORE_FILE, O_RDONLY);
    if (fd < 0)
        return; /* no file yet, so the high score is 0 */
    if (read(fd, buf, 2) == 2)
        best = buf[0] | ((unsigned)buf[1] << 8);
    close(fd);
}

static void save_best(void)
 {
    unsigned char buf[2];
    int fd = open(HISCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        log_error("could not open " HISCORE_FILE);
        return;
    }
    buf[0] = best & 0xFF;
    buf[1] = best >> 8;
    if (write(fd, buf, 2) != 2)
        log_error("could not write " HISCORE_FILE);
    close(fd);
}

#if 0
static void show_score(void)
{
    char buf[12];
    unsigned char i;
    sprintf(buf, "SCORE %u", score);
    RIA.addr0 = TEXT_XRAM + 30;
    RIA.step0 = 1;
    for (i = 0; buf[i]; i++)
        RIA.rw0 = buf[i];
    for (; i < 10; i++) /* pad to column 39 so old digits disappear */
        RIA.rw0 = ' ';
}
#endif

/* Picks a random grass cell on the board */
static void random_grass(unsigned char *x, unsigned char *y)
{
    do
    {
        *x = 1 + rand() % 38; /* column 1..38 */
        *y = 2 + rand() % 27; /* row 2..28    */
    } while (!is_grass(get_tile(*x, *y)));
}

static void place_food(void)
{
    unsigned char fx, fy;
    random_grass(&fx, &fy);
    put_tile(fx, fy, T_FOOD);
}

/* The gold color: when it blinks off it shows the grass color instead */
static void bonus_color(unsigned char on)
{
    if (on)
        set_color(9, pal[9][0], pal[9][1], pal[9][2]);
    else
        set_color(9, pal[1][0], pal[1][1], pal[1][2]);
}

static void place_bonus(void)
{
    random_grass(&bonus_x, &bonus_y);
    put_tile(bonus_x, bonus_y, T_BONUS);
    bonus_t = BONUS_FRAMES;
    bonus_color(1);
}

/* Called every frame during play: the golden apple blinks and disappears */
static void bonus_update(void)
{
    if (!bonus_t)
        return;
    if (--bonus_t == 0)
    {
        if (get_tile(bonus_x, bonus_y) == T_BONUS)
            put_tile(bonus_x, bonus_y, grass_tile(bonus_x, bonus_y));
        bonus_color(1);
    }
    else if (bonus_t <= BONUS_BLINK && (bonus_t & 7) == 0)
        bonus_color(bonus_t & 8);
}

/* Scatters stones on free grass, but not on the snake's starting stretch
   (rows 14-16, columns 16-32) so you don't die right away. */
static void place_stones(void)
{
    unsigned char i, fx, fy;
    for (i = 0; i < NUM_STONES; i++)
    {
        do
        {
            fx = 1 + rand() % 38;
            fy = 2 + rand() % 27;
        } while (!is_grass(get_tile(fx, fy)) ||
                 (fy >= 14 && fy <= 16 && fx >= 16 && fx <= 32));
        put_tile(fx, fy, T_STONE);
    }
}

/* ---------- sound (PSG) ---------- */

/* Each PSG channel is 8 bytes in XRAM:
   frequency (Hz * 3, 2 bytes), duty, attack, decay, release + waveform,
   pan + gate, and a reserved byte. */

static unsigned char sfx_eat_t, sfx_stone_t, sfx_over_t; /* frames left */
static unsigned sfx_eat_hz; /* starting pitch of the apple sound */

static void sound_init(void)
{
    unsigned char i;
    RIA.addr0 = PSG_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < 64; i++)
        RIA.rw0 = 0; /* all channels silent (gate = 0) */
    xreg_ria_psg(PSG_XRAM);
}

static void sound_off(void)
{
    xreg_ria_psg(0xFFFF); /* turn off the PSG */
}

/* Starts a note. Gate = 1 starts attack and decay. */
static void psg_note(unsigned char ch, unsigned hz, unsigned char attack,
                     unsigned char decay, unsigned char wave_release)
{
    unsigned f = hz * 3;
    RIA.addr0 = PSG_XRAM + (unsigned)ch * 8;
    RIA.step0 = 1;
    RIA.rw0 = f & 0xFF;
    RIA.rw0 = f >> 8;
    RIA.rw0 = 128; /* duty 50 % */
    RIA.rw0 = attack;
    RIA.rw0 = decay;
    RIA.rw0 = wave_release;
    RIA.rw0 = PSG_GATE; /* pan in the middle, gate on */
}

static void psg_freq(unsigned char ch, unsigned hz)
{
    unsigned f = hz * 3;
    RIA.addr0 = PSG_XRAM + (unsigned)ch * 8;
    RIA.step0 = 1;
    RIA.rw0 = f & 0xFF;
    RIA.rw0 = f >> 8;
}

static void psg_release(unsigned char ch)
{
    RIA.addr0 = PSG_XRAM + (unsigned)ch * 8 + 6;
    RIA.step0 = 1;
    RIA.rw0 = 0; /* gate off: the note fades out (release) */
}

/* 440 Hz for regular apples, an octave up for the golden apple */
static void sfx_eat(unsigned hz)
{
    sfx_eat_hz = hz;
    psg_note(CH_EAT, hz, 0x00, 0x30, PSG_WAVE_SQUARE | 3);
    sfx_eat_t = 8;
}

static void sfx_stone(void)
{
    psg_note(CH_STONE, 200, 0x00, 0x40, PSG_WAVE_NOISE | 4);
    sfx_stone_t = 6;
}

static void sfx_over(void)
{
    psg_note(CH_OVER, 330, 0x00, 0x20, PSG_WAVE_SAWTOOTH | 6);
    sfx_over_t = 45;
}

/* Called every frame: slides the pitch and closes the gate when time is up */
static void sfx_update(void)
{
    if (sfx_eat_t)
    {
        if (--sfx_eat_t == 0)
            psg_release(CH_EAT);
        else
            psg_freq(CH_EAT, sfx_eat_hz + (8 - sfx_eat_t) * (sfx_eat_hz >> 3)); /* rising */
    }
    if (sfx_stone_t && --sfx_stone_t == 0)
        psg_release(CH_STONE);
    if (sfx_over_t)
    {
        if (--sfx_over_t == 0)
            psg_release(CH_OVER);
        else
            psg_freq(CH_OVER, 330 - (45 - sfx_over_t) * 6); /* falling */
    }
}

/* Puts a stone in cell (x,y). If the queue is full the oldest stone turns
   into grass. The queue may grow longer the more apples the snake has eaten. */
static void drop_stone(unsigned char x, unsigned char y)
{
    unsigned char i;
    unsigned limit = STONE_START + apples / STONE_GROW;

    if (limit > STONE_MAX)
        limit = STONE_MAX;
    while (stq_count >= limit)
    {
        put_tile(stq_x[stq_first], stq_y[stq_first],
                 grass_tile(stq_x[stq_first], stq_y[stq_first]));
        stq_first = (stq_first + 1) % STONE_MAX;
        --stq_count;
    }
    i = (stq_first + stq_count) % STONE_MAX;
    stq_x[i] = x;
    stq_y[i] = y;
    ++stq_count;
    put_tile(x, y, T_STONE);
    sfx_stone();
}

/* ---------- animation ---------- */

/* Palette pulse: the mortar (color 4) and the apple glint (color 13) */
static void pulse(void)
{
    unsigned char p = (tick >> 2) & 15; /* 0..15 */
    if (p > 7)
        p = 15 - p; /* triangle wave 0..7..0 */

    set_color(4, 120 + p * 16, 120 + p * 16, 170 + p * 10);
    set_color(13, 255, 255 - p * 20, 255 - p * 20);
}

/* Called once per frame, both on the title screen and in the game */
static void animate(void)
{
    static unsigned char wind_t;

    ++tick;
    sfx_update();
    if ((tick & 3) == 0)
        pulse();
    if (++wind_t >= WIND_FRAMES)
    {
        wind_t = 0;
        wind();
    }
}

/* The snake's colors (5 = body, 7 = head/shine) in both palettes.
   flash = 1 makes the snake white, 0 restores it. Color 6 is left alone, it is the blade root. */
static void snake_color(unsigned char flash)
{
    unsigned char i;
    for (i = 5; i <= 7; i += 2)
    {
        if (flash)
        {
            set_color_at(PAL_XRAM, i, 255, 255, 255);
            set_color_at(SPR_PAL, i, 255, 255, 255);
        }
        else
        {
            set_color_at(PAL_XRAM, i, pal[i][0], pal[i][1], pal[i][2]);
            set_color_at(SPR_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
        }
    }
}

/* Moves the tile plane sideways (x_pos in the mode 2 config) */
static void shake(signed char s)
{
    RIA.addr0 = CFG_TILES + 2;
    RIA.step0 = 1;
    RIA.rw0 = (unsigned char)s;
    RIA.rw0 = s < 0 ? 0xFF : 0x00;
}

/* When the snake dies: the board shakes less and less and the snake blinks for a while */
static void game_over_fx(void)
{
    unsigned char i, amp;

    for (i = 0; i < 48; i++)
    {
        wait_vsync();
        animate();
        snake_color((i >> 2) & 1);
        amp = i < 16 ? 4 - (i >> 2) : 0; /* 4, 3, 2, 1, then still */
        shake((i & 1) ? amp : -(signed char)amp);
    }
    snake_color(0);
    shake(0);
}

/* Pauses until P is pressed again. Returns 0 if the player pressed ESC.
   The background keeps moving but the golden apple's timer stands still. */
static unsigned char pause_game(void)
{
    unsigned char p, prev_p = 1; /* P is held down when we get here */

    status("PAUSED  P = GO ON");
    for (;;)
    {
        wait_vsync();
        animate();
        read_keys();
        if (KEY_DOWN(KEY_ESC))
            return 0;
        p = KEY_DOWN(KEY_P);
        if (p && !prev_p)
            break;
        prev_p = p;
    }
    status("A/D TURN  P PAUSE");
    return 1;
}

/* ---------- the game ---------- */

/* Returns 0 if the player pressed ESC, 1 if the snake died */
static unsigned char play(void)
{
    unsigned char dir = 1; /* start heading right */
    unsigned char vdir = 1;
    unsigned char turn = 0;
    unsigned char frames = 0;
    unsigned char started = 0; /* the head only starts sliding after the first step */
    unsigned char a, d, p, prev_a, prev_d, prev_p;
    unsigned char hx, hy, nx, ny, t;
    unsigned char vx = 20, vy = 15; /* the cell the head sprite slides from */
    unsigned char tx = 18, ty = 15; /* the cell the tail sprite slides from */
    signed char tdx = 0, tdy = 0;   /* the tail's direction (0 = standing still) */
    unsigned char off;
    unsigned char pending = 0; /* stones waiting to land behind the tail */
    unsigned char step = STEP_START; /* frames per step, decreases as speed goes up */

    draw_board();
    status("A/D TURN  P PAUSE");

    head = tail = 0;
    sx[0] = 18; sy[0] = 15;
    sx[1] = 19; sy[1] = 15;
    sx[2] = 20; sy[2] = 15;
    head = 3;
    put_tile(18, 15, T_BODY);
    put_tile(19, 15, T_BODY);
    put_tile(20, 15, T_OCC); /* the head's cell: looks like grass but is occupied */
    sprite_set(0, 18 * 8, 15 * 8, T_BODY);
    sprite_set(1, 20 * 8, 15 * 8, T_HEAD + dir);

    score = 0;
    apples = 0;
    show_score();
    bonus_t = 0;
    bonus_color(1);
    stq_first = stq_count = 0;
    place_stones();
    place_food();

    read_keys();
    prev_a = KEY_DOWN(KEY_TURN_LEFT);
    prev_d = KEY_DOWN(KEY_TURN_RIGHT);
    prev_p = KEY_DOWN(KEY_P);

    for (;;)
    {
        wait_vsync();
        animate();
        bonus_update();
        read_keys();

        if (KEY_DOWN(KEY_ESC))
            return 0;

        p = KEY_DOWN(KEY_P);
        if (p && !prev_p && !pause_game())
            return 0;
        prev_p = p;

        a = KEY_DOWN(KEY_TURN_LEFT);
        d = KEY_DOWN(KEY_TURN_RIGHT);
        if (a && !prev_a && !turn)
            turn = 3; /* dir + 3 = left turn */
        if (d && !prev_d && !turn)
            turn = 1; /* dir + 1 = right turn */
        prev_a = a;
        prev_d = d;

        /* The game logic works in whole cells, one step every step:th frame */
        if (++frames >= step)
        {
            frames = 0;

            dir = (dir + turn) & 3;
            turn = 0;

            hx = sx[(head - 1) & MASK];
            hy = sy[(head - 1) & MASK];
            nx = hx + dx[dir];
            ny = hy + dy[dir];

            /* both sprites have finished sliding; turn the head to the new direction */
            sprite_set(0, (unsigned)(tx + tdx) * 8, (unsigned)(ty + tdy) * 8, T_BODY);
            sprite_set(1, (unsigned)hx * 8, (unsigned)hy * 8, T_HEAD + dir);

            t = get_tile(nx, ny); /* what is in the cell in front of the head? */
            if (t == T_WALL || t == T_BODY || t == T_OCC || t == T_STONE)
            {
                sfx_over();
                game_over_fx();
                return 1;
            }

            put_tile(hx, hy, T_BODY); /* the old head cell becomes body */
            sx[head] = nx;
            sy[head] = ny;
            head = (head + 1) & MASK;

            if (t == T_FOOD || t == T_BONUS)
            {
                /* The apple stays in the map until the head sprite has slid onto it.
                   The snake grows: the tail stands still. */
                if (t == T_FOOD)
                {
                    ++score;
                    ++apples;
                    sfx_eat(440);
                    if (apples % STONE_EVERY == 0)
                        ++pending; /* lands the next time the tail moves */
                    if (apples % SPEEDUP_EVERY == 0 && step > STEP_MIN)
                        --step; /* faster! */
                    place_food();
                    if (apples % BONUS_EVERY == 0 && !bonus_t)
                        place_bonus();
                }
                else
                {
                    score += BONUS_POINTS;
                    sfx_eat(880);
                    bonus_t = 0;
                    bonus_color(1);
                }
                show_score();
                //log_num("score", score);
                tx = sx[tail];
                ty = sy[tail];
                tdx = 0;
                tdy = 0;
            }
            else
            {
                put_tile(nx, ny, T_OCC); /* the head's new cell */

                /* The tail leaves its cell: the map turns to grass right away,
                   but the tail sprite slides from the cell towards the next segment. */
                tx = sx[tail];
                ty = sy[tail];
                put_tile(tx, ty, grass_tile(tx, ty));
                if (pending)
                {
                    drop_stone(tx, ty); /* shows up as the tail sprite slides away */
                    --pending;
                }
                tail = (tail + 1) & MASK;
                tdx = (signed char)((int)sx[tail] - (int)tx);
                tdy = (signed char)((int)sy[tail] - (int)ty);
            }

            vx = hx;
            vy = hy;
            vdir = dir;
            started = 1;
        }

        /* Every frame: move the sprites a bit towards their next cells */
        off = (unsigned char)((unsigned)frames * 8 / step);
        sprite_set(0, (unsigned)(tx * 8 + tdx * off),
                   (unsigned)(ty * 8 + tdy * off), T_BODY);
        if (!started)
            off = 0;
        sprite_set(1, (unsigned)(vx * 8 + dx[vdir] * off),
                   (unsigned)(vy * 8 + dy[vdir] * off), T_HEAD + vdir);
    }
}

static unsigned wait_for_space(void)
{
    unsigned n = 1;
    for (;;)
    {
        wait_vsync();
        animate();
        read_keys();
        if (KEY_DOWN(KEY_ESC))
            return 0;
        if (KEY_DOWN(KEY_SPACE))
            return n;
        ++n;
    }
}

int main(void)
{
    unsigned seed;

    unsigned char alive;

    video_init();
    sound_init();
    load_best();
    draw_board();
    status("SPACE = START");
    show_score();

    seed = wait_for_space();
    if (seed)
    {
        srand(seed);
        for (;;)
        {
            alive = play();
            if (score > best)
            {
                best = score;
                save_best();
                show_score();
                status("NEW BEST!  SPACE");
            }
            else
                status("GAME OVER  SPACE");
            if (!alive || !wait_for_space())
                break;
        }
    }

    sound_off(); /* silence the PSG before leaving the game */
    return 0;
}
