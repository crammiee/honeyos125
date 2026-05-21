#ifndef VGA_H
#define VGA_H

#include "../types.h"

#define VGA_COLS  80
#define VGA_ROWS  25
#define VGA_ADDR  0xB8000

/* Colour attribute: high nibble = background, low nibble = foreground */
#define VGA_COLOR(fg, bg)  ((uint8_t)((bg) << 4 | (fg)))

typedef enum {
    VGA_BLACK   = 0,
    VGA_BLUE    = 1,
    VGA_GREEN   = 2,
    VGA_CYAN    = 3,
    VGA_RED     = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN   = 6,
    VGA_LGRAY   = 7,
    VGA_DGRAY   = 8,
    VGA_LBLUE   = 9,
    VGA_LGREEN  = 10,
    VGA_LCYAN   = 11,
    VGA_LRED    = 12,
    VGA_LMAG    = 13,
    VGA_YELLOW  = 14,
    VGA_WHITE   = 15,
} vga_color_t;

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t attr);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_print_welcome(void);

/* Minimal formatted print: %s %d %u %x %c */
void kprintf(const char *fmt, ...);

#endif /* VGA_H */
