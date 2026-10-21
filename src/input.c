/* The keyboard: RIA keeps a bitmap of the pressed keys in XRAM */

#include <rp6502.h>
#include "input.h"
#include "xram.h"

unsigned char keys[32];

void input_init(void)
{
    xreg_ria_keyboard(XRAM_KEYS);
}

void read_keys(void)
{
    unsigned char i;
    RIA.addr0 = XRAM_KEYS;
    RIA.step0 = 1;
    for (i = 0; i < 32; i++)
        keys[i] = RIA.rw0;
}
