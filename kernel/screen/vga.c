/*
 * HoneyOS VGA Text-Mode Driver
 *
 * Writes directly to the VGA text buffer at physical address 0xB8000.
 * The buffer is an 80×25 grid of two-byte cells: [attribute][character].
 * Supports scrolling, backspace, newline, color changes, and hardware
 * cursor updates via the CRT controller ports 0x3D4 / 0x3D5.
 *
 * kprintf() provides minimal formatted output (%s, %d, %u, %x, %c, %%)
 * without the standard library, pulling varargs off the stack manually
 * using the x86-32 cdecl calling convention.
 */

#include "vga.h"

/* VGA CRT controller I/O ports for moving the hardware cursor */
#define VGA_CTRL  0x3D4   /* index register — selects which register to access */
#define VGA_DATA  0x3D5   /* data register  — reads or writes the selected register */

/* Current cursor position and active color attribute */
static int     cur_row   = 0;
static int     cur_col   = 0;
static uint8_t cur_color = 0;

/* Pointer to the VGA text buffer at 0xB8000 */
static volatile uint16_t *const vga_buf = (uint16_t *)VGA_ADDR;

/* Build one VGA text cell from a character and its color attribute */
static inline uint16_t make_cell(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

/*
 * Write the current cursor row/column to the CRT controller registers
 * so the hardware blinking cursor appears at the correct position.
 * Register 0x0E holds the high byte, 0x0F the low byte of the cursor offset.
 */
static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(cur_row * VGA_COLS + cur_col);
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)VGA_CTRL));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(pos >> 8)), "Nd"((uint16_t)VGA_DATA));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)VGA_CTRL));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)VGA_DATA));
}

/*
 * Scroll the screen up by one row.
 * Copies each row to the row above, then clears the last row with spaces.
 * Sets cur_row to the last row so the next character appears there.
 */
static void scroll(void) {
    for (int r = 0; r < VGA_ROWS - 1; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_buf[(r + 1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + c] = make_cell(' ', cur_color);
    cur_row = VGA_ROWS - 1;
}

/* Set default color and clear the screen */
void vga_init(void) {
    vga_set_color(VGA_COLOR(VGA_WHITE, VGA_BLACK));
    vga_clear();
}

/* Fill every cell with a space in the current color and reset the cursor */
void vga_clear(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        vga_buf[i] = make_cell(' ', cur_color);
    cur_row = 0;
    cur_col = 0;
    update_hw_cursor();
}

/* Update the active color attribute for all subsequent output */
void vga_set_color(uint8_t attr) {
    cur_color = attr;
}

/*
 * Write one character to the VGA buffer at the current cursor position.
 * Special characters:
 *   '\n' — move to column 0 of the next row
 *   '\r' — move to column 0 of the current row
 *   '\b' — move back one column and erase the character there
 * After writing, scroll if the cursor has gone past the last row.
 */
void vga_putchar(char c) {
    if (c == '\n') {
        cur_col = 0;
        cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c == '\b') {
        /* Move cursor back; if at column 0, wrap to the previous row */
        if (cur_col > 0) cur_col--;
        else if (cur_row > 0) { cur_row--; cur_col = VGA_COLS - 1; }
        vga_buf[cur_row * VGA_COLS + cur_col] = make_cell(' ', cur_color);
    } else {
        vga_buf[cur_row * VGA_COLS + cur_col] = make_cell(c, cur_color);
        /* Advance cursor; wrap to next row if the line is full */
        if (++cur_col >= VGA_COLS) { cur_col = 0; cur_row++; }
    }
    if (cur_row >= VGA_ROWS) scroll();
    update_hw_cursor();
}

/* Write a null-terminated string to the screen */
void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}

/*
 * Draw the HoneyOS welcome banner.
 * Saves and restores the current color so the caller's color is unaffected.
 */
void vga_print_welcome(void) {
    uint8_t saved = cur_color;

    vga_set_color(VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    vga_puts("============================================================\n");
    vga_puts("                     Welcome to HoneyOS                    \n");
    vga_puts("          CMSC 125 Operating Systems Project                \n");
    vga_puts("               Alcaraz  |  Canete  |  Putalan               \n");
    vga_puts("============================================================\n");

    vga_set_color(VGA_COLOR(VGA_LGRAY, VGA_BLACK));
    vga_puts("Type 'help' for a list of commands.\n\n");

    vga_set_color(saved);
}

/* -----------------------------------------------------------------------
 * kprintf — minimal formatted kernel print
 *
 * Supports: %s (string), %d (signed int), %u (unsigned int),
 *           %x (hex with "0x" prefix), %c (char), %% (literal %).
 *
 * Pulls arguments off the stack manually using the x86-32 cdecl convention:
 * arguments are pushed right-to-left and the first argument after 'fmt'
 * sits at &fmt + 1 (one uint32_t past the format string pointer).
 * --------------------------------------------------------------------- */

/* Print a signed 32-bit integer in decimal */
static void print_int(int32_t n) {
    if (n < 0) { vga_putchar('-'); n = -n; }
    if (n == 0) { vga_putchar('0'); return; }
    char buf[12]; int i = 0;
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (--i >= 0) vga_putchar(buf[i]);
}

/* Print an unsigned 32-bit integer in decimal */
static void print_uint(uint32_t n) {
    if (n == 0) { vga_putchar('0'); return; }
    char buf[11]; int i = 0;
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (--i >= 0) vga_putchar(buf[i]);
}

/* Print an unsigned 32-bit integer in hexadecimal with "0x" prefix */
static void print_hex(uint32_t n) {
    const char *d = "0123456789abcdef";
    vga_puts("0x");
    for (int s = 28; s >= 0; s -= 4) vga_putchar(d[(n >> s) & 0xF]);
}

void kprintf(const char *fmt, ...) {
    /* In x86-32 cdecl, variadic args start one slot after the last named param */
    uint32_t *argp = (uint32_t *)&fmt + 1;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { vga_putchar(*p); continue; }
        switch (*++p) {
        case 's': vga_puts((const char *)*argp++); break;  /* string */
        case 'd': print_int((int32_t)*argp++);     break;  /* signed decimal */
        case 'u': print_uint(*argp++);              break;  /* unsigned decimal */
        case 'x': print_hex(*argp++);               break;  /* hexadecimal */
        case 'c': vga_putchar((char)*argp++);       break;  /* character */
        case '%': vga_putchar('%');                  break;  /* literal % */
        default:  vga_putchar('%'); vga_putchar(*p); break; /* unknown — pass through */
        }
    }
}
