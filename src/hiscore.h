/* The high score on the USB drive */

#ifndef HISCORE_H
#define HISCORE_H

#define HISCORE_FILE "snake.hi"

/* Returns the saved high score, or 0 if there is no file yet */
unsigned load_best(void);

void save_best(unsigned best);

#endif
