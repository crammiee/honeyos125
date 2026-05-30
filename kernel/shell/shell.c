/*
 * HoneyOS Shell (Stage 5: full command set with file system)
 *
 * REPL: print prompt → read line → tokenise → dispatch.
 * All file and directory commands are wired up in this stage.
 */

#include "shell.h"
#include "../screen/vga.h"
#include "../io/keyboard.h"
#include "../io/ports.h"
#include "../fs/fat.h"
#include "../fs/file.h"
#include "../fs/dir.h"

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void read_line(char *buf, int len) {
    int i = 0;
    while (1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') { buf[i] = '\0'; vga_putchar('\n'); return; }
        if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } continue; }
        if (i < len - 1) { buf[i++] = c; vga_putchar(c); }
    }
}

static int tokenise(char *line, char **argv, int max) {
    int argc = 0; char *p = line;
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
    vga_puts("  ls              - list current directory\n");
    vga_puts("  mkdir <name>    - create a directory\n");
    vga_puts("  cd <name>       - change directory (.. to go up)\n");
    vga_puts("  rm <name>       - delete a file or directory\n");
    vga_puts("  cat <file>      - print file contents\n");
    vga_puts("  write <file>    - create/overwrite file (end input with '.')\n");
    vga_puts("  edit <file>     - rewrite an existing file\n");
    vga_puts("  shutdown        - halt the OS\n");
}

static void cmd_ls    (int argc, char **argv) { (void)argc; (void)argv; dir_list(); }
static void cmd_mkdir (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: mkdir <name>\n"); return; } dir_create(argv[1]); }
static void cmd_cd    (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cd <name>\n");    return; } dir_change(argv[1]); }
static void cmd_rm    (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: rm <name>\n");    return; } file_delete(argv[1]); }
static void cmd_cat   (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cat <file>\n");   return; } file_read(argv[1]); }
static void cmd_write (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: write <file>\n"); return; } file_write(argv[1]); }
static void cmd_edit  (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: edit <file>\n");  return; } file_edit(argv[1]); }

static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Shutting down HoneyOS... Goodbye!\n");
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
    { "ls",       cmd_ls       },
    { "mkdir",    cmd_mkdir    },
    { "cd",       cmd_cd       },
    { "rm",       cmd_rm       },
    { "cat",      cmd_cat      },
    { "write",    cmd_write    },
    { "edit",     cmd_edit     },
    { "shutdown", cmd_shutdown },
    { NULL,       NULL         },
};

/* -----------------------------------------------------------------------
 * shell_run
 * --------------------------------------------------------------------- */

void shell_run(void) {
    static char line[SHELL_LINE_MAX];
    char *argv[SHELL_ARGC_MAX];

    vga_set_color(VGA_COLOR(VGA_LCYAN, VGA_BLACK));
    vga_puts(cwd_path);
    vga_puts("> ");
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
