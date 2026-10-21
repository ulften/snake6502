/* RP6502 XRAM map and XREG shortcuts
 *
 * Everything lives in XRAM. VGA reads it directly every frame:
 *   0x1000  text row (40 bytes)
 *   0x1100  tile map (1200 bytes)
 *   0x2000  tile images (32 bytes per tile)
 *   0xFE00  PSG, the sound generator (8 channels * 8 bytes)
 *   0xFF00  keyboard (32 bytes)
 *   0xFF40  tile map config (16 bytes)
 *   0xFF50  text row config (16 bytes)
 *   0xFF80  palette (16 colors * 2 bytes)
 *   0xFFA0  sprite palette (as above, but color 0 transparent)
 *   0xFFC0  sprite list (2 sprites * 8 bytes: 0 = tail, 1 = head)
 *
 * CMakeLists.txt reads the XRAM_* names with rp6502_map(), so they can also
 * be used to place assets in XRAM.
 */

#ifndef XRAM_H
#define XRAM_H

/* ---------- XRAM map ---------- */
#define XRAM_TEXT 0x1000
#define XRAM_MAP 0x1100
#define XRAM_TILES 0x2000
#define XRAM_PSG 0xFE00 /* 64 bytes, even address, doesn't cross a page boundary */
#define XRAM_KEYS 0xFF00
#define XRAM_CFG_TILES 0xFF40
#define XRAM_CFG_TEXT 0xFF50
#define XRAM_PAL 0xFF80
#define XRAM_SPR_PAL 0xFFA0
#define XRAM_SPR_CFG 0xFFC0

/* XREG shortcuts, taken from the RIA and VGA documentation */
#define xreg_ria_keyboard(...) xreg(0, 0, 0, __VA_ARGS__)
#define xreg_vga_canvas(...) xreg(1, 0, 0, __VA_ARGS__)
#define xreg_vga_mode(...) xreg(1, 0, 1, __VA_ARGS__)
#define xreg_vga_mode5(...) xreg(1, 0, 1, 5, __VA_ARGS__)
#define xreg_ria_psg(...) xreg(0, 1, 0, __VA_ARGS__)

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

#endif
