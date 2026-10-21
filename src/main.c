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
 * The source files:
 *   main.c     the game: rules, the snake, apples and stones, the main loop
 *   gfx.c      tile map, sprites, palette and tile images (see gfx.h)
 *   grass.c    the grass and the wind in it
 *   hud.c      the text row with status and score
 *   sound.c    sound effects on the PSG
 *   input.c    the keyboard
 *   hiscore.c  the high score on the USB drive
 *   log.c      logging to the console
 *   xram.h     the XRAM map and XREG shortcuts
 */

#include <stdlib.h>
#include "gfx.h"
#include "grass.h"
#include "hiscore.h"
#include "hud.h"
#include "input.h"
#include "log.h"
#include "sound.h"

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
#define MAXLEN 1024
#define MASK (MAXLEN - 1)

/* ---------- state ---------- */
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

/* ---------- apples and stones ---------- */

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

/* Called once per frame, both on the title screen and in the game */
static void animate(void)
{
    static unsigned char wind_t;

    ++tick;
    sfx_update();
    if ((tick & 3) == 0)
        pulse(tick);
    if (++wind_t >= WIND_FRAMES)
    {
        wind_t = 0;
        wind();
    }
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
    show_score(score, best);
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
                show_score(score, best);
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
    input_init();
    sound_init();
    best = load_best();
    draw_board();
    status("SPACE = START");
    show_score(score, best);

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
                save_best(best);
                show_score(score, best);
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
