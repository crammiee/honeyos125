/*
 * HoneyOS PS/2 Keyboard Driver
 *
 * Translates PS/2 set-1 scancodes to ASCII using polling (no interrupts).
 * keyboard_getchar() spins until a key-press event arrives, then maps
 * the scancode to ASCII through one of two lookup tables depending on
 * whether Shift is currently held. Key-release events (scancode | 0x80)
 * are discarded except for the Shift keys which update shift state.
 */

#include "keyboard.h"
#include "ports.h"

/* Tracks whether either Shift key is currently held down */
static uint8_t shift_pressed = 0;

/*
 * Scancode set-1 to ASCII lookup tables.
 * Index = scancode byte, value = ASCII character (0 = no mapping).
 * Two tables: normal (unshifted) and shifted.
 */
static const char sc_normal[128] = {
/*00*/  0,    0,   '1', '2', '3', '4', '5', '6',
/*08*/ '7', '8',  '9', '0', '-', '=', '\b','\t',
/*10*/ 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i',
/*18*/ 'o', 'p',  '[', ']', '\n', 0,  'a', 's',
/*20*/ 'd', 'f',  'g', 'h', 'j', 'k', 'l', ';',
/*28*/'\'', '`',   0, '\\', 'z', 'x', 'c', 'v',
/*30*/ 'b', 'n',  'm', ',', '.', '/',  0,  '*',
/*38*/  0,  ' ',   0,   0,   0,   0,   0,   0,
};

static const char sc_shift[128] = {
/*00*/  0,    0,   '!', '@', '#', '$', '%', '^',
/*08*/ '&', '*',  '(', ')', '_', '+', '\b','\t',
/*10*/ 'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I',
/*18*/ 'O', 'P',  '{', '}', '\n', 0,  'A', 'S',
/*20*/ 'D', 'F',  'G', 'H', 'J', 'K', 'L', ':',
/*28*/ '"', '~',   0,  '|', 'Z', 'X', 'C', 'V',
/*30*/ 'B', 'N',  'M', '<', '>', '?',  0,  '*',
/*38*/  0,  ' ',   0,   0,   0,   0,   0,   0,
};

/* Scancodes for left and right Shift press and release events */
#define SC_LSHIFT_PRESS    0x2A
#define SC_RSHIFT_PRESS    0x36
#define SC_LSHIFT_RELEASE  0xAA
#define SC_RSHIFT_RELEASE  0xB6

void keyboard_init(void) {
    /* Drain any stale bytes left in the PS/2 output buffer from boot */
    while (inb(KBD_STATUS_PORT) & 0x01)
        inb(KBD_DATA_PORT);
    shift_pressed = 0;
}

char keyboard_getchar(void) {
    while (1) {
        /* Poll status register until the output buffer full bit (bit 0) is set */
        while (!(inb(KBD_STATUS_PORT) & 0x01))
            ;

        uint8_t sc = inb(KBD_DATA_PORT); /* read the scancode */

        /* Track Shift key state — press sets the flag, release clears it */
        if (sc == SC_LSHIFT_PRESS  || sc == SC_RSHIFT_PRESS)  { shift_pressed = 1; continue; }
        if (sc == SC_LSHIFT_RELEASE|| sc == SC_RSHIFT_RELEASE) { shift_pressed = 0; continue; }

        /* Bit 7 set means key-release event — skip, we only care about presses */
        if (sc & 0x80) continue;

        /* Ignore scancodes beyond our lookup table range */
        if (sc >= 128) continue;

        /* Map scancode to ASCII using the appropriate table */
        char c = shift_pressed ? sc_shift[sc] : sc_normal[sc];

        /* Skip scancodes with no ASCII mapping (e.g. F-keys, Ctrl) */
        if (c == 0) continue;

        return c;
    }
}
