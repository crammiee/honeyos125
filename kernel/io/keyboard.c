/*
 * HoneyOS PS/2 Keyboard Driver
 *
 * Translates PS/2 set-1 scancodes to ASCII using polling (no interrupts).
 * keyboard_getchar() spins until a key-press arrives then returns the
 * ASCII value. Shift state is tracked for uppercase and symbols.
 */

#include "keyboard.h"
#include "ports.h"

static uint8_t shift_pressed = 0;

/* Unshifted scancode set-1 → ASCII (index = scancode, 0 = ignore) */
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

/* Shifted scancode set-1 → ASCII */
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

#define SC_LSHIFT_PRESS    0x2A
#define SC_RSHIFT_PRESS    0x36
#define SC_LSHIFT_RELEASE  0xAA
#define SC_RSHIFT_RELEASE  0xB6

void keyboard_init(void) {
    /* Flush any stale bytes sitting in the controller buffer */
    while (inb(KBD_STATUS_PORT) & 0x01)
        inb(KBD_DATA_PORT);
    shift_pressed = 0;
}

char keyboard_getchar(void) {
    while (1) {
        /* Wait for output buffer full (bit 0 of status register) */
        while (!(inb(KBD_STATUS_PORT) & 0x01))
            ;

        uint8_t sc = inb(KBD_DATA_PORT);

        if (sc == SC_LSHIFT_PRESS  || sc == SC_RSHIFT_PRESS)  { shift_pressed = 1; continue; }
        if (sc == SC_LSHIFT_RELEASE|| sc == SC_RSHIFT_RELEASE) { shift_pressed = 0; continue; }
        if (sc & 0x80) continue;   /* key-release event */
        if (sc >= 128) continue;   /* out of table range */

        char c = shift_pressed ? sc_shift[sc] : sc_normal[sc];
        if (c == 0) continue;      /* non-printable */
        return c;
    }
}
