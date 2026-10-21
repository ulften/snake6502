/* The text row at the top of the screen (plane 1) */

#include <rp6502.h>
#include "hud.h"
#include "xram.h"

void status(const char *s)
{
    unsigned char x = 1;
    RIA.addr0 = XRAM_TEXT + 1;
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

void show_score(unsigned score, unsigned best)
{
    RIA.addr0 = XRAM_TEXT + 20;
    RIA.step0 = 1;
    put_str("SCORE ");
    put_num4(score);
    put_str(" BEST ");
    put_num4(best);
}
