/*
 * Snake för Picocomputer 6502 - steg 5: fart, rekord och guldäpple
 *
 * Nytt i steg 5:
 *  - Ormen blir snabbare: ett steg tar STEP_START bildrutor från början och
 *    en bildruta mindre var SPEEDUP_EVERY:e äpple, ner till STEP_MIN.
 *  - Rekordet visas på poängraden och sparas i HISCORE_FILE på USB-disken.
 *  - Guldäpplet: efter vart BONUS_EVERY:e äpple dyker det upp ett guldäpple
 *    värt BONUS_POINTS. Det försvinner efter BONUS_FRAMES bildrutor och
 *    blinkar (via paletten) innan dess.
 *
 * Steg 4: ljud (PSG)
 *
 * Nytt i steg 4: ljudgeneratorn PSG (8 kanaler) styrs genom 64 byte i XRAM,
 * precis som tangentbordet och spritelistan. Tre ljudeffekter:
 *  - äta äpple: kort fyrkantsvåg med stigande tonhöjd
 *  - sten landar: ett kort brus ("duns")
 *  - game over: sågtandsvåg med fallande tonhöjd
 * Ljuden startas med gate = 1 och klingar ut när gate sätts till 0.
 * sfx_update() körs varje bildruta och sköter tonhöjd och gate.
 *
 * Steg 3c: stenar bakom svansen
 *
 * Nytt i 3c: var STONE_EVERY:e äpple ger en sten som landar i cellen som
 * svansen lämnar. Den avslöjas när svansspriten glider bort. Högst STONE_MAX
 * sådana stenar finns kvar på planen; när en ny kommer blir den äldsta gräs.
 *
 * Steg 3b: sprites för huvud och svans
 *
 * Ändringar jämfört med steg 3:
 *  - Svansen är också en sprite. När svansen lämnar en cell sätts kartan till
 *    gräs direkt, men en kropps-sprite glider från den cellen till nästa
 *    segment. Då rör sig båda ändarna av ormen mjukt (mitten står stilla
 *    mellan stegen, precis som hos en riktig orm).
 *  - Stenar (T_STONE) som ormen måste undvika. Antal styrs av NUM_STONES.
 *  - Poängraden fylls med blanksteg så att gamla siffror inte blir kvar.
 *
 * Steg 3:
 *
 * Nytt i steg 3:
 *  - Ormens huvud är en sprite (mode 5, 4 bitar, 8x8) som glider en pixel per
 *    bildruta mellan cellerna, i stället för att hoppa en hel cell åt gången.
 *    Spelets logik går fortfarande i hela celler, men VGA ritar sprite-lagret
 *    ovanpå tile-kartan utan att 6502:an ritar om något.
 *  - Spriten använder samma bilder som huvud-tiles (samma format i XRAM) men
 *    en egen palett där färg 0 är genomskinlig.
 *  - Huvudets cell i kartan är T_OCC: en tile som ser ut som gräs men räknas
 *    som upptagen. Äpplet ligger kvar i kartan tills huvudet har glidit dit.
 *
 * Från steg 2 (jämfört med steg 1):
 *  - Vind i gräset: planen är delad i 16 diagonala band med egna tile-nummer.
 *    En vindpust sveper från band till band och stråna böjer sig när den
 *    passerar. Eftersom kartan bara pekar på tile-numret räcker det att skriva
 *    om tile-bilderna för de band som ändras, kartan ritas aldrig om.
 *  - Murarna glöder och äpplet blänker: vi ändrar enstaka färger i paletten
 *    (2 byte per färg). Hela skärmen byter färg utan att kartan ritas om.
 *
 * Styrning:  A = sväng vänster, D = sväng höger, ESC = avsluta
 *
 * Grafiken är nu två "plan" som VGA lägger ovanpå varandra:
 *   plan 0: tile map (mode 2). 40x30 celler, 1 byte per cell = tile-nummer.
 *           Tiles är 8x8 pixlar, 4 bitar per pixel (16 färger), 32 byte styck.
 *   plan 1: text (mode 1, 1 bit färg) bara på översta raden, för poängen.
 *
 * Allt ligger i XRAM. VGA läser det direkt varje bildruta:
 *   0x1000  textrad (40 byte)
 *   0x1100  tile map (1200 byte)
 *   0x2000  tile-bilder (32 byte per tile)
 *   0xFF00  tangentbord (32 byte)
 *   0xFF40  konfiguration för tile map (16 byte)
 *   0xFF50  konfiguration för textraden (16 byte)
 *   0xFF80  palett (16 färger * 2 byte)
 *   0xFFA0  spritepalett (som ovan, men färg 0 genomskinlig)
 *   0xFFC0  spritelista (2 sprites * 8 byte: 0 = svans, 1 = huvud)
 *   0xFE00  PSG, ljudgeneratorn (8 kanaler * 8 byte)
 */

#include <rp6502.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* XREG-genvägar, hämtade från RIA- och VGA-dokumentationen */
#define xreg_ria_keyboard(...) xreg(0, 0, 0, __VA_ARGS__)
#define xreg_vga_canvas(...) xreg(1, 0, 0, __VA_ARGS__)
#define xreg_vga_mode(...) xreg(1, 0, 1, __VA_ARGS__)
#define xreg_vga_mode5(...) xreg(1, 0, 1, 5, __VA_ARGS__)
#define xreg_ria_psg(...) xreg(0, 1, 0, __VA_ARGS__)

/* ---------- XRAM-karta ---------- */
#define TEXT_XRAM 0x1000
#define MAP_XRAM 0x1100
#define TILES_XRAM 0x2000
#define KEYS_XRAM 0xFF00
#define CFG_TILES 0xFF40
#define CFG_TEXT 0xFF50
#define PAL_XRAM 0xFF80
#define SPR_PAL 0xFFA0
#define SPR_CFG 0xFFC0
#define PSG_XRAM 0xFE00 /* 64 byte, jämn adress, korsar ingen sidgräns */

#define COLS 40
#define ROWS 30

/* mode 2: 4 bitar per pixel, 8x8-tiles */
#define MODE2_4BPP 0x02
#define MODE2_8X8 0x00

/* mode 5: sprites med palett, 4 bitar, 8x8 */
#define MODE5_4BPP 0x02
#define MODE5_8X8 0x00

/* PSG: vågformer (övre halvan av release_wave) och gate-biten */
#define PSG_WAVE_SQUARE 0x10
#define PSG_WAVE_SAWTOOTH 0x20
#define PSG_WAVE_NOISE 0x40
#define PSG_GATE 0x01

/* PSG-kanalerna vi använder */
#define CH_EAT 0
#define CH_STONE 1
#define CH_OVER 2

/* ---------- tile-nummer ---------- */
#define T_HUD 0
#define T_WALL 1
#define T_BODY 2
#define T_FOOD 3
#define T_HEAD 4 /* 4=upp 5=höger 6=ner 7=vänster (samma ordning som dir) */
#define T_OCC 8  /* ser ut som gräs, markerar cellen där huvudet är */
#define T_STONE 9
#define T_BONUS 10 /* guldäpplet */
#define T_GRASS 16 /* 16..47: gräs, GRASS_LAYOUTS * GRASS_BANDS stycken */

/* ---------- gräs och vind ---------- */
#define GRASS_LAYOUTS 2 /* två olika strå-mönster så att gräset inte ser rutigt ut */
#define GRASS_POSES 3   /* 0 = rakt, 1 = spetsen böjd, 2 = mer böjt */
#define GRASS_BANDS 16  /* diagonala band; vinden flyttar sig ett band per steg */
#define WIND_FRAMES 8   /* bildrutor per vindsteg (större = lugnare vind) */

/* ---------- USB HID-tangentkoder ---------- */
#define KEY_A 0x04
#define KEY_D 0x07
#define KEY_ESC 0x29
#define KEY_SPACE 0x2C

/* Byt plats på dessa två om du vill ha A = höger och D = vänster */
#define KEY_TURN_LEFT KEY_A
#define KEY_TURN_RIGHT KEY_D

/* ---------- spelinställningar ---------- */
#define NUM_STONES 8   /* stenar som finns från början */
#define STONE_MAX 6    /* högst så många stenar från äpplen åt gången */
#define STONE_EVERY 1  /* en sten per så här många äpplen */
#define STEP_START 8   /* bildrutor per steg i början (lägre = snabbare) */
#define STEP_MIN 4     /* snabbaste farten */
#define SPEEDUP_EVERY 4 /* ett steg snabbare var 4:e äpple */
#define BONUS_EVERY 5   /* ett guldäpple dyker upp efter vart 5:e äpple */
#define BONUS_POINTS 5
#define BONUS_FRAMES 300 /* guldäpplet ligger kvar i 5 sekunder */
#define BONUS_BLINK 90   /* och blinkar de sista 1,5 sekunderna */
#define HISCORE_FILE "snake.hi" /* rekordet sparas på USB-disken */
#define MAXLEN 1024
#define MASK (MAXLEN - 1)

/* ---------- tillstånd ---------- */
static unsigned char keys[32];
static unsigned char sx[MAXLEN], sy[MAXLEN];
static unsigned head, tail;
static unsigned score;
static unsigned best;   /* rekordet, läses från och sparas till HISCORE_FILE */
static unsigned apples; /* vanliga äpplen i den här rundan, styr farten */
static unsigned char tick; /* räknar bildrutor, styr animationerna */

/* guldäpplet: bonus_t är bildrutor kvar, 0 = inget guldäpple på planen */
static unsigned bonus_t;
static unsigned char bonus_x, bonus_y;

/* kö med de stenar som äpplen gett, äldst först (ringbuffert) */
static unsigned char stq_x[STONE_MAX], stq_y[STONE_MAX];
static unsigned char stq_first, stq_count;

static const signed char dx[4] = {0, 1, 0, -1};
static const signed char dy[4] = {-1, 0, 1, 0};

#define KEY_DOWN(code) ((keys[(code) >> 3] & (1 << ((code) & 7))) != 0)

/* ---------- palett: 16 färger (R, G, B, 0-255) ---------- */
static const unsigned char pal[16][3] = {
    {0, 0, 0},       /* 0  svart (HUD)          */
    {36, 110, 44},   /* 1  gräs                 */
    {62, 156, 62},   /* 2  grässtrå             */
    {70, 70, 110},   /* 3  mur, mörk            */
    {120, 120, 170}, /* 4  mur, ljus (murbruk)  */
    {40, 190, 70},   /* 5  orm, kropp           */
    {15, 100, 35},   /* 6  orm, kant + strårot  */
    {120, 235, 100}, /* 7  orm, huvud/glans     */
    {255, 255, 255}, /* 8  vitt (ögon)          */
    {235, 185, 40},  /* 9  guld (pupill i spritepaletten, se load_palette) */
    {225, 40, 40},   /* 10 äpple, rött          */
    {140, 15, 25},   /* 11 äpple, mörkrött      */
    {120, 80, 30},   /* 12 skaft, brunt         */
    {255, 255, 255}, /* 13 äpplets glans (pulserar) */
    {160, 160, 165}, /* 14 sten, ljus          */
    {90, 90, 100}};  /* 15 sten, mörk          */

/* ---------- tile-bilder som text: en siffra/bokstav per pixel ----------
   0-9 och a-f är index i paletten ovan. */
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

/* Huvudet ritas bara en gång (uppåt) och roteras sedan i koden */
static const char *const ART_HEAD_UP[8] = {
    "66666666", "67977976", "67877876", "67777776",
    "67777776", "67777776", "67777776", "66666666"};

static const char *const ART_BONUS[8] = {
    "111c1111", "11999911", "199d9991", "19999991",
    "19999991", "1c9999c1", "11cccc11", "11111111"};

static const char *const ART_STONE[8] = {
    "111ee111", "11eeeef1", "1eeeeeff", "1eeeeeff",
    "1eeeeeff", "1feeeeff", "11ffff11", "11111111"};

/* Gräset ritas i koden som strån: kolumn, översta rad och längd i pixlar.
   Spetsen är ljus (färg 2) och roten mörk (färg 6). Kolumnen får vara högst 6
   eftersom spetsen böjs ett steg åt höger. */
typedef struct
{
    unsigned char col, row, len;
} blade_t;

static const blade_t BLADES[GRASS_LAYOUTS][4] = {
    {{1, 1, 3}, {2, 2, 2}, {5, 4, 3}, {6, 5, 2}},
    {{3, 0, 3}, {4, 1, 2}, {0, 4, 3}, {6, 6, 2}}};

/* En vindpust: hur mycket ett band böjer sig, steg för steg. Gräset böjs
   mjukt, står böjt en stund och fjädrar sedan tillbaka. Nollorna är
   lugnet mellan pustarna. */
static const unsigned char GUST[GRASS_BANDS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 1};

/* Färdigpackade gräsbilder (32 byte styck) och vilken pose varje band visar */
static unsigned char grass_img[GRASS_LAYOUTS][GRASS_POSES][32];
static unsigned char band_pose[GRASS_BANDS];
static unsigned char wind_phase;

/* ---------- Loggning till konsolen (stderr), utan printf ---------- */

static void log_str(const char *s)
{
    write(STDERR_FILENO, s, strlen(s));
}

/* Skriver "ERROR: msg" på en egen rad */
static void log_error(const char *msg)
{
    log_str("ERROR: ");
    log_str(msg);
    log_str("\r\n");
}

/* Skriver "label=n" på en egen rad, t.ex. log_num("score", score) */
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

/* ---------- XRAM-hjälpfunktioner ---------- */

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

/* Placerar en sprite (idx 0 = svans, 1 = huvud). x och y är pixlar på
   canvasen (0..319, 0..239).
   t är tile-numret vars bild används som sprite-bild (T_HEAD + riktning).
   Spriten är ett mode5_sprite_t: x, y, bildpekare, palettpekare (8 byte). */
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

/* ---------- tiles och palett till XRAM ---------- */

/* Skriver en färg i en palett (RGB555 + alpha-bit 0x20 som gör färgen synlig) */
static void set_color_at(unsigned base, unsigned char i,
                         unsigned char r, unsigned char g, unsigned char b)
{
    unsigned c = ((unsigned)(b >> 3) << 11) |
                 ((unsigned)(g >> 3) << 6) |
                 (unsigned)(r >> 3) | 0x20;
    RIA.addr0 = base + (unsigned)i * 2;
    RIA.step0 = 1;
    RIA.rw0 = c & 0xFF; /* låg byte först */
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

    /* Spritepaletten är en kopia, men färg 0 är genomskinlig (alpha-bit av) */
    for (i = 1; i < 16; i++)
        set_color_at(SPR_PAL, i, pal[i][0], pal[i][1], pal[i][2]);
    /* Färg 9 är pupillen i huvudspriten. Huvudet läggs aldrig i kartan, så
       i tile-paletten kan samma nummer vara guld till guldäpplet. */
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

/* Packar 8x8 pixlar till 32 byte: två pixlar per byte, vänstra pixeln i
   övre halvan av byten. Byt ordning här om bilderna ser spegelvända ut. */
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

/* Ritar ett gräsmönster i en viss pose. Roten står still; vid pose 1 böjs
   spetsen ett steg åt höger, vid pose 2 även pixeln under spetsen. */
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

/* Vinden: ett steg av pusten som sveper diagonalt över planen. Bara de band
   vars pose ändras skrivs om (4 band * 2 mönster = 256 byte per steg). */
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
        rotate_cw(px); /* upp -> höger -> ner -> vänster */
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

    /* T_OCC ser ut som rakt gräs (den ligger ändå under huvudspriten) */
    xram_write(TILES_XRAM + T_OCC * 32, grass_img[0][0], 32);

    memset(band_pose, 0xFF, sizeof band_pose); /* tvingar wind() att skriva alla band */
    wind();
}

/* ---------- video ---------- */

static void video_init(void)
{
    /* mode2_config_t: x_wrap, y_wrap, x_pos, y_pos, bredd, höjd i tiles,
       pekare till kartan, till paletten och till tile-bilderna */
    static const unsigned char cfg_tiles[16] = {
        0, 0,
        0, 0,
        0, 0,
        COLS, 0,
        ROWS, 0,
        MAP_XRAM & 0xFF, MAP_XRAM >> 8,
        PAL_XRAM & 0xFF, PAL_XRAM >> 8,
        TILES_XRAM & 0xFF, TILES_XRAM >> 8};
    /* mode1_config_t för en enda textrad. Palett och font 0xFFFF = inbyggda */
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

    sprite_set(0, 320, 240, T_BODY); /* utanför canvasen = osynlig */
    sprite_set(1, 320, 240, T_HEAD);

    /* tomma blanksteg på textraden */
    RIA.addr0 = TEXT_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < COLS; i++)
        RIA.rw0 = ' ';

    /* plan 0: tile map över hela canvasen */
    xreg_vga_mode(2, MODE2_4BPP | MODE2_8X8, CFG_TILES, 0);
    /* plan 1: text på scanline 0-7 (översta raden) */
    xreg_vga_mode(1, 0, CFG_TEXT, 1, 0, 8);

    /* sprite-lagret ovanpå plan 0: 2 sprites, 8x8, 4 bitar */
    xreg_vga_mode5(MODE5_4BPP | MODE5_8X8, SPR_CFG, 2, 0);

    xreg_ria_keyboard(KEYS_XRAM);
}

/* Samma position ger alltid samma tile. Bandet är diagonalen x + y, med ett
   litet hack här och där så att vindfronten inte blir en rak linje. */
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

/* Text på rad 0, kolumn 1..19 (högst 19 tecken), resten blanksteg.
   Kolumn 20..39 är poängen. */
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

/* Skriver text där RIA.addr0 pekar just nu */
static void put_str(const char *s)
{
    while (*s)
        RIA.rw0 = *s++;
}

/* Skriver n med 4 siffror vänsterställt, utfyllt med blanksteg.
   Version utan sprintf() som inte finns i Picocomputer C17. */
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
        if (d || started || i == 3) /* inga inledande nollor */
        {
            RIA.rw0 = '0' + d;
            started = 1;
            ++col;
        }
    }
    for (; col < 4; col++)
        RIA.rw0 = ' '; /* rensar gamla siffror */
}

/* "SCORE 1234 BEST 1234" i kolumn 20..39 */
static void show_score(void)
{
    RIA.addr0 = TEXT_XRAM + 20;
    RIA.step0 = 1;
    put_str("SCORE ");
    put_num4(score);
    put_str(" BEST ");
    put_num4(best);
}

/* ---------- rekordet på USB-disken ---------- */

static void load_best(void)
{
    unsigned char buf[2];
    int fd = open(HISCORE_FILE, O_RDONLY);
    if (fd < 0)
        return; /* ingen fil än, då är rekordet 0 */
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
    for (; i < 10; i++) /* fyll ut till kolumn 39 så gamla siffror försvinner */
        RIA.rw0 = ' ';
}
#endif

/* Slumpar fram en gräsruta på planen */
static void random_grass(unsigned char *x, unsigned char *y)
{
    do
    {
        *x = 1 + rand() % 38; /* kolumn 1..38 */
        *y = 2 + rand() % 27; /* rad 2..28    */
    } while (!is_grass(get_tile(*x, *y)));
}

static void place_food(void)
{
    unsigned char fx, fy;
    random_grass(&fx, &fy);
    put_tile(fx, fy, T_FOOD);
}

/* Guldfärgen: blank när den blinkar av visar den gräsets färg */
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

/* Anropas varje bildruta i spelet: guldäpplet blinkar och försvinner */
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

/* Slumpar ut stenar på ledigt gräs, men inte på ormens startsträcka
   (rad 14-16, kolumn 16-32) så att man inte dör direkt. */
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

/* ---------- ljud (PSG) ---------- */

/* Varje PSG-kanal är 8 byte i XRAM:
   frekvens (Hz * 3, 2 byte), duty, attack, decay, release + vågform,
   pan + gate, och en reserverad byte. */

static unsigned char sfx_eat_t, sfx_stone_t, sfx_over_t; /* bildrutor kvar */
static unsigned sfx_eat_hz; /* starttonen för äppelljudet */

static void sound_init(void)
{
    unsigned char i;
    RIA.addr0 = PSG_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < 64; i++)
        RIA.rw0 = 0; /* alla kanaler tysta (gate = 0) */
    xreg_ria_psg(PSG_XRAM);
}

static void sound_off(void)
{
    xreg_ria_psg(0xFFFF); /* stäng av PSG */
}

/* Startar en ton. Gate = 1 startar attack och decay. */
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
    RIA.rw0 = PSG_GATE; /* pan i mitten, gate på */
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
    RIA.rw0 = 0; /* gate av: tonen klingar ut (release) */
}

/* 440 Hz för vanliga äpplen, en oktav upp för guldäpplet */
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

/* Anropas varje bildruta: glider tonhöjden och stänger gate när tiden gått */
static void sfx_update(void)
{
    if (sfx_eat_t)
    {
        if (--sfx_eat_t == 0)
            psg_release(CH_EAT);
        else
            psg_freq(CH_EAT, sfx_eat_hz + (8 - sfx_eat_t) * (sfx_eat_hz >> 3)); /* stiger */
    }
    if (sfx_stone_t && --sfx_stone_t == 0)
        psg_release(CH_STONE);
    if (sfx_over_t)
    {
        if (--sfx_over_t == 0)
            psg_release(CH_OVER);
        else
            psg_freq(CH_OVER, 330 - (45 - sfx_over_t) * 6); /* faller */
    }
}

/* Lägger en sten i cellen (x,y). Är kön full blir den äldsta stenen gräs. */
static void drop_stone(unsigned char x, unsigned char y)
{
    unsigned char i;

    if (stq_count == STONE_MAX)
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

/* Palettpuls: murbruket (färg 4) och äpplets glans (färg 13) */
static void pulse(void)
{
    unsigned char p = (tick >> 2) & 15; /* 0..15 */
    if (p > 7)
        p = 15 - p; /* triangelvåg 0..7..0 */

    set_color(4, 120 + p * 16, 120 + p * 16, 170 + p * 10);
    set_color(13, 255, 255 - p * 20, 255 - p * 20);
}

/* Anropas en gång per bildruta, både på titelskärmen och i spelet */
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

/* ---------- spelet ---------- */

/* Returnerar 0 om spelaren tryckte ESC, 1 om ormen dog */
static unsigned char play(void)
{
    unsigned char dir = 1; /* börja åt höger */
    unsigned char vdir = 1;
    unsigned char turn = 0;
    unsigned char frames = 0;
    unsigned char started = 0; /* huvudet glider först efter första steget */
    unsigned char a, d, prev_a, prev_d;
    unsigned char hx, hy, nx, ny, t;
    unsigned char vx = 20, vy = 15; /* cellen huvudspriten glider ifrån */
    unsigned char tx = 18, ty = 15; /* cellen svansspriten glider ifrån */
    signed char tdx = 0, tdy = 0;   /* svansens riktning (0 = står still) */
    unsigned char off;
    unsigned char pending = 0; /* stenar som ska landa bakom svansen */
    unsigned char step = STEP_START; /* bildrutor per steg, minskar med farten */

    draw_board();
    status("A/D TURN  ESC QUIT");

    head = tail = 0;
    sx[0] = 18; sy[0] = 15;
    sx[1] = 19; sy[1] = 15;
    sx[2] = 20; sy[2] = 15;
    head = 3;
    put_tile(18, 15, T_BODY);
    put_tile(19, 15, T_BODY);
    put_tile(20, 15, T_OCC); /* huvudets cell: ser ut som gräs men är upptagen */
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

    for (;;)
    {
        wait_vsync();
        animate();
        bonus_update();
        read_keys();

        if (KEY_DOWN(KEY_ESC))
            return 0;

        a = KEY_DOWN(KEY_TURN_LEFT);
        d = KEY_DOWN(KEY_TURN_RIGHT);
        if (a && !prev_a && !turn)
            turn = 3; /* dir + 3 = vänstersväng */
        if (d && !prev_d && !turn)
            turn = 1; /* dir + 1 = högersväng   */
        prev_a = a;
        prev_d = d;

        /* Spelets logik går i hela celler, ett steg var step:e bildruta */
        if (++frames >= step)
        {
            frames = 0;

            dir = (dir + turn) & 3;
            turn = 0;

            hx = sx[(head - 1) & MASK];
            hy = sy[(head - 1) & MASK];
            nx = hx + dx[dir];
            ny = hy + dy[dir];

            /* båda spritarna har nu glidit klart; vänd huvudet åt nya hållet */
            sprite_set(0, (unsigned)(tx + tdx) * 8, (unsigned)(ty + tdy) * 8, T_BODY);
            sprite_set(1, (unsigned)hx * 8, (unsigned)hy * 8, T_HEAD + dir);

            t = get_tile(nx, ny); /* vad finns i cellen framför huvudet? */
            if (t == T_WALL || t == T_BODY || t == T_OCC || t == T_STONE)
            {
                sfx_over();
                return 1;
            }

            put_tile(hx, hy, T_BODY); /* gamla huvudcellen blir kropp */
            sx[head] = nx;
            sy[head] = ny;
            head = (head + 1) & MASK;

            if (t == T_FOOD || t == T_BONUS)
            {
                /* Äpplet ligger kvar i kartan tills huvudspriten har glidit dit.
                   Ormen växer: svansen står still. */
                if (t == T_FOOD)
                {
                    ++score;
                    ++apples;
                    sfx_eat(440);
                    if (apples % STONE_EVERY == 0)
                        ++pending; /* landar när svansen nästa gång flyttar sig */
                    if (apples % SPEEDUP_EVERY == 0 && step > STEP_MIN)
                        --step; /* snabbare! */
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
                log_num("score", score);
                tx = sx[tail];
                ty = sy[tail];
                tdx = 0;
                tdy = 0;
            }
            else
            {
                put_tile(nx, ny, T_OCC); /* huvudets nya cell */

                /* Svansen lämnar sin cell: kartan blir gräs direkt, men
                   svansspriten glider från cellen mot nästa segment. */
                tx = sx[tail];
                ty = sy[tail];
                put_tile(tx, ty, grass_tile(tx, ty));
                if (pending)
                {
                    drop_stone(tx, ty); /* syns när svansspriten glider bort */
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

        /* Varje bildruta: flytta spritarna en bit mot sina nästa celler */
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

    sound_off(); /* tysta PSG:n innan vi lämnar spelet */
    return 0;
}
