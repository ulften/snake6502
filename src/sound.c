/* Sound effects on the PSG sound generator
 *
 * Each PSG channel is 8 bytes in XRAM:
 * frequency (Hz * 3, 2 bytes), duty, attack, decay, release + waveform,
 * pan + gate, and a reserved byte.
 *
 * A sound starts with gate = 1 and fades out when gate is set to 0.
 */

#include <rp6502.h>
#include "sound.h"
#include "xram.h"

/* The PSG channels we use */
#define CH_EAT 0
#define CH_STONE 1
#define CH_OVER 2

static unsigned char sfx_eat_t, sfx_stone_t, sfx_over_t; /* frames left */
static unsigned sfx_eat_hz; /* starting pitch of the apple sound */

void sound_init(void)
{
    unsigned char i;
    RIA.addr0 = XRAM_PSG;
    RIA.step0 = 1;
    for (i = 0; i < 64; i++)
        RIA.rw0 = 0; /* all channels silent (gate = 0) */
    xreg_ria_psg(XRAM_PSG);
}

void sound_off(void)
{
    xreg_ria_psg(0xFFFF); /* turn off the PSG */
}

/* Starts a note. Gate = 1 starts attack and decay. */
static void psg_note(unsigned char ch, unsigned hz, unsigned char attack,
                     unsigned char decay, unsigned char wave_release)
{
    unsigned f = hz * 3;
    RIA.addr0 = XRAM_PSG + (unsigned)ch * 8;
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
    RIA.addr0 = XRAM_PSG + (unsigned)ch * 8;
    RIA.step0 = 1;
    RIA.rw0 = f & 0xFF;
    RIA.rw0 = f >> 8;
}

static void psg_release(unsigned char ch)
{
    RIA.addr0 = XRAM_PSG + (unsigned)ch * 8 + 6;
    RIA.step0 = 1;
    RIA.rw0 = 0; /* gate off: the note fades out (release) */
}

void sfx_eat(unsigned hz)
{
    sfx_eat_hz = hz;
    psg_note(CH_EAT, hz, 0x00, 0x30, PSG_WAVE_SQUARE | 3);
    sfx_eat_t = 8;
}

void sfx_stone(void)
{
    psg_note(CH_STONE, 200, 0x00, 0x40, PSG_WAVE_NOISE | 4);
    sfx_stone_t = 6;
}

void sfx_over(void)
{
    psg_note(CH_OVER, 330, 0x00, 0x20, PSG_WAVE_SAWTOOTH | 6);
    sfx_over_t = 45;
}

void sfx_update(void)
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
