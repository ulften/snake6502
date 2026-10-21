/* The keyboard: RIA keeps a bitmap of the pressed keys in XRAM */

#ifndef INPUT_H
#define INPUT_H

/* ---------- USB HID key codes ---------- */
#define KEY_A 0x04
#define KEY_D 0x07
#define KEY_ESC 0x29
#define KEY_SPACE 0x2C
#define KEY_P 0x13

/* Swap these two if you want A = right and D = left */
#define KEY_TURN_LEFT KEY_A
#define KEY_TURN_RIGHT KEY_D

extern unsigned char keys[32];

#define KEY_DOWN(code) ((keys[(code) >> 3] & (1 << ((code) & 7))) != 0)

/* Tells RIA where to put the key bitmap */
void input_init(void);

/* Copies the key bitmap from XRAM to keys[] */
void read_keys(void);

#endif
