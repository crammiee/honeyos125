#ifndef VGA_H
#define VGA_H

/*
 * VGA Text-Mode Interface
 *
 * The VGA text buffer lives at physical address 0xB8000 and holds an 80×25
 * grid of two-byte cells. Each cell is [attribute byte][character byte]:
 *   - High nibble of attribute = background color
 *   - Low  nibble of attribute = foreground color
 */

#include "../types.h"

#define VGA_COLS  80       /* characters per row */
#define VGA_ROWS  25       /* rows on screen */
#define VGA_ADDR  0xB8000  /* physical address of the VGA text buffer */

/* Build an attribute byte from foreground and background color constants */
#define VGA_COLOR(fg, bg)  ((uint8_t)((bg) << 4 | (fg)))

/* Standard 16-color VGA palette */
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

/* Initialise the VGA driver: set default color and clear the screen */
void vga_init(void);

/* Fill the screen with spaces using the current color */
void vga_clear(void);

/* Set the current foreground/background color for subsequent characters */
void vga_set_color(uint8_t attr);

/* Write one character, handling '\n', '\r', and '\b'; scroll if needed */
void vga_putchar(char c);

/* Write a null-terminated string character by character */
void vga_puts(const char *s);

/* Print the HoneyOS welcome banner with team names */
void vga_print_welcome(void);

/* Minimal kernel printf: supports %s %d %u %x %c %% */
void kprintf(const char *fmt, ...);

#endif /* VGA_H */
