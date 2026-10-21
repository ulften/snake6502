/* Sound effects on the PSG sound generator (8 channels in XRAM) */

#ifndef SOUND_H
#define SOUND_H

void sound_init(void);
void sound_off(void);

/* 440 Hz for regular apples, an octave up for the golden apple */
void sfx_eat(unsigned hz);
void sfx_stone(void);
void sfx_over(void);

/* Called every frame: slides the pitch and closes the gate when time is up */
void sfx_update(void);

#endif
