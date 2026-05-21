/*
 * HoneyOS Shell (Stage 4: keyboard + basic commands)
 *
 * REPL: print prompt → read line → tokenise → dispatch.
 * Only 'help' and 'shutdown' are wired up at this stage.
 * File system commands (ls, mkdir, cat, …) are added in Stage 5.
 */

#include "shell.h"
#include "../screen/vga.h"
#include "../io/keyboard.h"
#include "../io/ports.h"

/* -----------------------------------------------------------------------
 * String helpers
 * --------------------------------------------------------------------- */

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* -----------------------------------------------------------------------
 * Line reader: echoes characters, handles backspace, stops on Enter
 * --------------------------------------------------------------------- */

static void read_line(char *buf, int len) {
    int i = 0;
    while (1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') { buf[i] = '\0'; vga_putchar('\n'); return; }
        if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } continue; }
        if (i < len - 1) { buf[i++] = c; vga_putchar(c); }
    }
}

/* -----------------------------------------------------------------------
 * Tokeniser: splits line in-place, fills argv[], returns argc
 * --------------------------------------------------------------------- */

static int tokenise(char *line, char **argv, int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0';
    }
    return argc;
}

/* -----------------------------------------------------------------------
 * Command handlers
 * --------------------------------------------------------------------- */

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Available commands:\n");
    vga_puts("  help            - show this message\n");
    vga_puts("  shutdown        - halt the OS\n");
}

static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Shutting down HoneyOS... Goodbye!\n");
    /* QEMU ACPI power-off: PM1a_CNT at 0x604, SLP_EN = bit 13 */
    outw(0x604, 0x2000);
    __asm__ volatile ("cli; hlt");
    while (1);
}

/* -----------------------------------------------------------------------
 * Dispatch table
 * --------------------------------------------------------------------- */

typedef struct { const char *name; void (*fn)(int, char **); } cmd_t;

static const cmd_t commands[] = {
    { "help",     cmd_help     },
    { "shutdown", cmd_shutdown },
    { NULL,       NULL         },
};

/* -----------------------------------------------------------------------
 * shell_run — called from the kernel main loop each iteration
 * --------------------------------------------------------------------- */

void shell_run(void) {
    static char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARGC_MAX];

    vga_set_color(VGA_COLOR(VGA_LCYAN, VGA_BLACK));
    vga_puts("honey> ");
    vga_set_color(VGA_COLOR(VGA_WHITE, VGA_BLACK));

    read_line(line, SHELL_LINE_MAX);
    if (line[0] == '\0') return;

    int argc = tokenise(line, argv, SHELL_ARGC_MAX);
    if (argc == 0) return;

    for (int i = 0; commands[i].name != NULL; i++) {
        if (str_eq(argv[0], commands[i].name)) {
            commands[i].fn(argc, argv);
            return;
        }
    }

    vga_puts("Unknown command: ");
    vga_puts(argv[0]);
    vga_puts("\nType 'help' for a list of commands.\n");
}
