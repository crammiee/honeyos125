/*
 * HoneyOS VGA Text-Mode Driver
 *
 * Writes to the VGA text buffer at 0xB8000 (80×25 cells).
 * Each cell is two bytes: [attribute][character].
 * Supports scrolling, backspace, newline, and hardware cursor updates.
 * kprintf() handles %s, %d, %u, %x, %c without the standard library.
 */

#include "vga.h"

/* VGA CRT controller ports for moving the hardware cursor */
#define VGA_CTRL  0x3D4
#define VGA_DATA  0x3D5

static int     cur_row   = 0;
static int     cur_col   = 0;
static uint8_t cur_color = 0;

static volatile uint16_t *const vga_buf = (uint16_t *)VGA_ADDR;

static inline uint16_t make_cell(char c, uint8_t attr) {
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

/* Write cursor position to the CRT controller registers */
static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(cur_row * VGA_COLS + cur_col);
    /* outb via inline asm — ports.h is not included here to keep the
     * VGA driver self-contained at this stage of development */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)VGA_CTRL));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(pos >> 8)), "Nd"((uint16_t)VGA_DATA));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)VGA_CTRL));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)VGA_DATA));
}

static void scroll(void) {
    for (int r = 0; r < VGA_ROWS - 1; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_buf[(r + 1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + c] = make_cell(' ', cur_color);
    cur_row = VGA_ROWS - 1;
}

void vga_init(void) {
    vga_set_color(VGA_COLOR(VGA_WHITE, VGA_BLACK));
    vga_clear();
}

void vga_clear(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        vga_buf[i] = make_cell(' ', cur_color);
    cur_row = 0;
    cur_col = 0;
    update_hw_cursor();
}

void vga_set_color(uint8_t attr) {
    cur_color = attr;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cur_col = 0;
        cur_row++;
    } else if (c == '\r') {
        cur_col = 0;
    } else if (c == '\b') {
        if (cur_col > 0) cur_col--;
        else if (cur_row > 0) { cur_row--; cur_col = VGA_COLS - 1; }
        vga_buf[cur_row * VGA_COLS + cur_col] = make_cell(' ', cur_color);
    } else {
        vga_buf[cur_row * VGA_COLS + cur_col] = make_cell(c, cur_color);
        if (++cur_col >= VGA_COLS) { cur_col = 0; cur_row++; }
    }
    if (cur_row >= VGA_ROWS) scroll();
    update_hw_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}

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

/* ------------------------------------------------------------------
 * kprintf — minimal formatted print for kernel use
 * Pulls varargs off the stack manually (x86-32 cdecl, int-sized args).
 * ------------------------------------------------------------------ */

static void print_int(int32_t n) {
    if (n < 0) { vga_putchar('-'); n = -n; }
    if (n == 0) { vga_putchar('0'); return; }
    char buf[12]; int i = 0;
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (--i >= 0) vga_putchar(buf[i]);
}

static void print_uint(uint32_t n) {
    if (n == 0) { vga_putchar('0'); return; }
    char buf[11]; int i = 0;
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (--i >= 0) vga_putchar(buf[i]);
}

static void print_hex(uint32_t n) {
    const char *d = "0123456789abcdef";
    vga_puts("0x");
    for (int s = 28; s >= 0; s -= 4) vga_putchar(d[(n >> s) & 0xF]);
}

void kprintf(const char *fmt, ...) {
    uint32_t *argp = (uint32_t *)&fmt + 1;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { vga_putchar(*p); continue; }
        switch (*++p) {
        case 's': vga_puts((const char *)*argp++); break;
        case 'd': print_int((int32_t)*argp++);     break;
        case 'u': print_uint(*argp++);              break;
        case 'x': print_hex(*argp++);               break;
        case 'c': vga_putchar((char)*argp++);       break;
        case '%': vga_putchar('%');                  break;
        default:  vga_putchar('%'); vga_putchar(*p); break;
        }
    }
}
