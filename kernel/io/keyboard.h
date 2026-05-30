#ifndef KEYBOARD_H
#define KEYBOARD_H

/*
 * PS/2 Keyboard Driver Interface
 *
 * Port 0x60 — data register: read a scancode after the status register
 *             signals that the output buffer is full.
 * Port 0x64 — status/command register: bit 0 (OBF) goes high when data
 *             is ready to be read from port 0x60.
 */

#define KBD_DATA_PORT    0x60  /* scancode / data read port */
#define KBD_STATUS_PORT  0x64  /* status register (read) / command port (write) */

/* Flush the controller buffer and reset shift state */
void keyboard_init(void);

/* Spin-wait until a printable ASCII character arrives, then return it */
char keyboard_getchar(void);

#endif /* KEYBOARD_H */
