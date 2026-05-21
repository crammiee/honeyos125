#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

void keyboard_init(void);

/* Block until a printable ASCII character is available */
char keyboard_getchar(void);

#endif /* KEYBOARD_H */
