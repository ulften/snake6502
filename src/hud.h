/* The text row at the top of the screen (plane 1) */

#ifndef HUD_H
#define HUD_H

/* Text on row 0, columns 1..19 (at most 19 characters), the rest spaces.
   Columns 20..39 are the score. */
void status(const char *s);

/* "SCORE 1234 BEST 1234" in columns 20..39 */
void show_score(unsigned score, unsigned best);

#endif
