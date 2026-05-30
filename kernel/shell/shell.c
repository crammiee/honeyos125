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
    vga_puts("  clear           - clear the screen\n");
    vga_puts("  tree            - show full directory tree\n");
    vga_puts("  ls              - list current directory\n");
    vga_puts("  mkdir <name>    - create a directory\n");
    vga_puts("  cd <name>       - change directory (.. to go up)\n");
    vga_puts("  rm <name>       - delete a file or empty directory\n");
    vga_puts("  rm -r <name>    - recursively delete a directory and its contents\n");
    vga_puts("  cat <file>      - print file contents\n");
    vga_puts("  write <file>    - create/overwrite file (end input with '.')\n");
    vga_puts("  edit <file>     - rewrite an existing file\n");
    vga_puts("  fat             - display file allocation table\n");
    vga_puts("  shutdown        - halt the OS\n");
}

static void cmd_clear (int argc, char **argv) { (void)argc; (void)argv; vga_clear(); vga_puts("Screen cleared.\n"); }

static void tree_print_name(const dir_entry_t *e) {
    for (int k = 0; k < 8 && e->name[k] != ' '; k++) vga_putchar(e->name[k]);
    if (e->attr == ATTR_FILE && e->ext[0] != ' ') {
        vga_putchar('.');
        for (int k = 0; k < 3 && e->ext[k] != ' '; k++) vga_putchar(e->ext[k]);
    }
}

static void tree_recurse(uint16_t dir_sector, int depth) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec; sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;
            if (e[i].attr != ATTR_FILE && e[i].attr != ATTR_DIR) continue;
            for (int d = 0; d < depth; d++) vga_puts("  ");
            vga_puts(e[i].attr == ATTR_DIR ? "+ " : "- ");
            tree_print_name(&e[i]);
            vga_putchar('\n');
            if (e[i].attr == ATTR_DIR)
                tree_recurse(block_to_sector(e[i].first_block), depth + 1);
        }
    }
}

static void cmd_tree(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("root\n");
    tree_recurse(ROOT_DIR_SECTOR, 1);
}
static void cmd_ls    (int argc, char **argv) { (void)argc; (void)argv; dir_list(); }
static void cmd_mkdir (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: mkdir <name>\n"); return; } dir_create(argv[1]); }
static void cmd_cd    (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cd <name>\n");    return; } dir_change(argv[1]); }
static void shell_memset(void *dst, uint8_t val, int n) {
    uint8_t *p = (uint8_t *)dst; while (n--) *p++ = val;
}

static void delete_dir_contents(uint16_t dir_sector) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec; sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        int modified = 0;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;
            if (e[i].attr == ATTR_DIR) {
                delete_dir_contents(block_to_sector(e[i].first_block));
                fat_free_chain(e[i].first_block);
            } else {
                fat_free_chain(e[i].first_block);
            }
            shell_memset(&e[i], 0, sizeof(dir_entry_t));
            modified = 1;
        }
        if (modified) sector_write(sec, buf);
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { vga_puts("Usage: rm [-r] <name>\n"); return; }
    int recursive = (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'r');
    const char *name = recursive ? argv[2] : argv[1];

    if (recursive) {
        dir_entry_t *e = dir_find(name, cwd_sector);
        if (!e) { vga_puts("rm: not found: "); vga_puts(name); vga_putchar('\n'); return; }
        if (e->attr != ATTR_DIR) { file_delete(name); return; }
        uint16_t blk = e->first_block;
        shell_memset(e, 0, sizeof(dir_entry_t));
        dir_flush();
        delete_dir_contents(block_to_sector(blk));
        fat_free_chain(blk);
        kprintf("Deleted '%s' and all contents.\n", name);
    } else {
        file_delete(name);
    }
}
static void cmd_cat   (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cat <file>\n");   return; } file_read(argv[1]); }
static void cmd_write (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: write <file>\n"); return; } file_write(argv[1]); }
static void cmd_edit  (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: edit <file>\n");  return; } file_edit(argv[1]); }

/* Recursively scan a directory, marking block owners. */
static void scan_dir(uint16_t dir_sector, char owners[TOTAL_BLOCKS][13]) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec;
         sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr != ATTR_FILE && e[i].attr != ATTR_DIR) continue;
            if (e[i].name[0] == '.') continue;
            char name[13]; int n = 0;
            for (int k = 0; k < 8 && e[i].name[k] != ' '; k++) name[n++] = e[i].name[k];
            if (e[i].attr == ATTR_FILE && e[i].ext[0] != ' ') {
                name[n++] = '.';
                for (int k = 0; k < 3 && e[i].ext[k] != ' '; k++) name[n++] = e[i].ext[k];
            }
            name[n] = '\0';
            uint16_t blk = e[i].first_block;
            while (blk != FAT_EOC && blk != FAT_FREE && blk < TOTAL_BLOCKS) {
                for (int k = 0; k <= n; k++) owners[blk][k] = name[k];
                blk = fat_next(blk);
            }
            if (e[i].attr == ATTR_DIR)
                scan_dir(block_to_sector(e[i].first_block), owners);
        }
    }
}

static void build_owner_map(char owners[TOTAL_BLOCKS][13]) {
    for (int i = 0; i < TOTAL_BLOCKS; i++) owners[i][0] = '\0';
    scan_dir(ROOT_DIR_SECTOR, owners);
}

static void print_fat_row(uint16_t block, const char *owner) {
    if (block < 10) vga_puts("    "); else vga_puts("   ");
    kprintf("%u", (uint32_t)block);
    vga_puts("  ");
    uint16_t entry = fat_next(block);
    if (block == 0)              vga_puts("[SYS   ]");
    else if (entry == FAT_FREE)  vga_puts("[FREE  ]");
    else if (entry == FAT_EOC)   vga_puts("[END   ]");
    else {
        vga_puts("[->  ");
        kprintf("%u", (uint32_t)entry);
        if (entry < 10) vga_puts("  ]"); else vga_puts(" ]");
    }
    if (owner && owner[0]) { vga_puts("  "); vga_puts(owner); }
    vga_putchar('\n');
}

static void cmd_fat(int argc, char **argv) {
    (void)argc; (void)argv;
    static char owners[TOTAL_BLOCKS][13];
    build_owner_map(owners);

    vga_puts("FAT Table (32 blocks x 128 KB)\n");
    vga_puts("Block  Status     Owner\n");
    vga_puts("-----  ---------  ------------\n");

    for (uint16_t i = 0; i < TOTAL_BLOCKS; i++) {
        if (i == 20) {
            vga_puts("--- Press any key for more ---\n");
            keyboard_getchar();
        }
        print_fat_row(i, owners[i]);
    }

    vga_puts("-----  ---------  ------------\n");
    uint16_t used = 0, free_cnt = 0;
    for (uint16_t i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (fat_next(i) == FAT_FREE) free_cnt++; else used++;
    }
    kprintf("Used: %u block(s)   Free: %u block(s)\n", (uint32_t)used, (uint32_t)free_cnt);
}

static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Shutting down HoneyOS... Goodbye!\n");
    outw(0x604, 0x2000);  /* QEMU */
    outw(0xB004, 0x2000); /* VirtualBox / Bochs (older) */
    outw(0x4004, 0x3400); /* VirtualBox (newer ACPI) */
    __asm__ volatile ("cli; hlt");
    while (1);
}

/* -----------------------------------------------------------------------
 * Dispatch table
 * --------------------------------------------------------------------- */

typedef struct { const char *name; void (*fn)(int, char **); } cmd_t;

static const cmd_t commands[] = {
    { "help",     cmd_help     },
    { "clear",    cmd_clear    },
    { "tree",     cmd_tree     },
    { "ls",       cmd_ls       },
    { "mkdir",    cmd_mkdir    },
    { "cd",       cmd_cd       },
    { "rm",       cmd_rm       },
    { "cat",      cmd_cat      },
    { "write",    cmd_write    },
    { "edit",     cmd_edit     },
    { "fat",      cmd_fat      },
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
