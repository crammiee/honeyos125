/*
 * HoneyOS Shell
 *
 * A simple Read-Eval-Print Loop (REPL):
 *   1. Print a colored prompt showing the current directory path.
 *   2. Read a line of input from the keyboard.
 *   3. Tokenise the line on spaces into argv/argc.
 *   4. Look up the first token in the command table and dispatch.
 *
 * Commands are stored in a static table of (name, function) pairs.
 * Unknown commands print an error message and loop back to the prompt.
 */

#include "shell.h"
#include "../screen/vga.h"
#include "../io/keyboard.h"
#include "../io/ports.h"
#include "../fs/fat.h"
#include "../fs/file.h"
#include "../fs/dir.h"

/* Compare two null-terminated strings; return 1 if equal, 0 if not */
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/*
 * Read one line of input from the keyboard into buf (max len-1 chars).
 * Each character is echoed to the screen. Backspace removes the last character.
 * Returns when Enter or Return is pressed.
 */
static void read_line(char *buf, int len) {
    int i = 0;
    while (1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') { buf[i] = '\0'; vga_putchar('\n'); return; }
        if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } continue; }
        if (i < len - 1) { buf[i++] = c; vga_putchar(c); }
    }
}

/*
 * Split 'line' into tokens on space boundaries, writing pointers into argv.
 * Replaces each space delimiter with '\0' so the tokens are null-terminated.
 * Returns the number of tokens found (argc), capped at max.
 */
static int tokenise(char *line, char **argv, int max) {
    int argc = 0; char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') p++;      /* skip leading/extra spaces */
        if (!*p) break;
        argv[argc++] = p;           /* record start of token */
        while (*p && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0'; /* null-terminate the token */
    }
    return argc;
}

/* -----------------------------------------------------------------------
 * Command Handlers
 *
 * Each handler receives argc (argument count) and argv (argument vector).
 * argv[0] is always the command name itself.
 * --------------------------------------------------------------------- */

/* Print all available commands with a brief description */
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

/* Clear the screen and confirm to the user */
static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
    vga_puts("Screen cleared.\n");
}

/* -----------------------------------------------------------------------
 * tree command helpers
 * --------------------------------------------------------------------- */

/* Print the 8.3 name of a directory entry (name + optional ".ext") */
static void tree_print_name(const dir_entry_t *e) {
    for (int k = 0; k < 8 && e->name[k] != ' '; k++) vga_putchar(e->name[k]);
    if (e->attr == ATTR_FILE && e->ext[0] != ' ') {
        vga_putchar('.');
        for (int k = 0; k < 3 && e->ext[k] != ' '; k++) vga_putchar(e->ext[k]);
    }
}

/*
 * Recursively print all entries under dir_sector, indented by depth.
 * Directories are prefixed with "+ " and files with "- ".
 * Each sub-directory is recursed into with depth + 1.
 * Each recursive call has its own local buf on the stack, so there is no
 * shared state between levels — recursion is safe up to the stack limit.
 */
static void tree_recurse(uint16_t dir_sector, int depth) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec; sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;  /* skip "." and ".." */
            if (e[i].attr != ATTR_FILE && e[i].attr != ATTR_DIR) continue;
            /* Indent by depth level using two spaces per level */
            for (int d = 0; d < depth; d++) vga_puts("  ");
            vga_puts(e[i].attr == ATTR_DIR ? "+ " : "- ");
            tree_print_name(&e[i]);
            vga_putchar('\n');
            /* Descend into sub-directories */
            if (e[i].attr == ATTR_DIR)
                tree_recurse(block_to_sector(e[i].first_block), depth + 1);
        }
    }
}

/* Print the full directory tree starting from root */
static void cmd_tree(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("root\n");
    tree_recurse(ROOT_DIR_SECTOR, 1);
}

/* Simple wrappers that delegate to the filesystem layer */
static void cmd_ls   (int argc, char **argv) { (void)argc; (void)argv; dir_list(); }
static void cmd_mkdir(int argc, char **argv) { if (argc < 2) { vga_puts("Usage: mkdir <name>\n"); return; } dir_create(argv[1]); }
static void cmd_cd   (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cd <name>\n");    return; } dir_change(argv[1]); }
static void cmd_cat  (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: cat <file>\n");   return; } file_read(argv[1]); }
static void cmd_write(int argc, char **argv) { if (argc < 2) { vga_puts("Usage: write <file>\n"); return; } file_write(argv[1]); }
static void cmd_edit (int argc, char **argv) { if (argc < 2) { vga_puts("Usage: edit <file>\n");  return; } file_edit(argv[1]); }

/* -----------------------------------------------------------------------
 * rm / rm -r helpers
 * --------------------------------------------------------------------- */

/* Local memset — avoids dependency on libc which is not available */
static void shell_memset(void *dst, uint8_t val, int n) {
    uint8_t *p = (uint8_t *)dst; while (n--) *p++ = val;
}

/*
 * Recursively delete all contents of a directory (but not the directory itself).
 * For each entry:
 *   - Files: free their FAT chain.
 *   - Directories: recurse first, then free their block.
 * After processing all entries in a sector, write it back if any were cleared.
 * Each level has its own stack-local buf, so recursion does not corrupt
 * a parent level's buffer.
 */
static void delete_dir_contents(uint16_t dir_sector) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec; sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        int modified = 0;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;  /* skip ".." — don't touch parent */
            if (e[i].attr == ATTR_DIR) {
                /* Recurse into sub-directory before freeing its block */
                delete_dir_contents(block_to_sector(e[i].first_block));
                fat_free_chain(e[i].first_block);
            } else {
                /* Free the file's FAT chain */
                fat_free_chain(e[i].first_block);
            }
            shell_memset(&e[i], 0, sizeof(dir_entry_t));  /* clear the directory slot */
            modified = 1;
        }
        if (modified) sector_write(sec, buf);  /* write back only if we changed something */
    }
}

/*
 * Delete a file or directory.
 * Without -r: calls file_delete which refuses non-empty directories.
 * With -r: unlinks the directory entry first, then recursively frees all
 *          contents, and finally frees the directory's own block.
 */
static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { vga_puts("Usage: rm [-r] <name>\n"); return; }

    /* Detect "-r" flag: argv[1] must be "-r" and argv[2] must be the name */
    int recursive = (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'r');
    const char *name = recursive ? argv[2] : argv[1];

    if (recursive) {
        dir_entry_t *e = dir_find(name, cwd_sector);
        if (!e) { vga_puts("rm: not found: "); vga_puts(name); vga_putchar('\n'); return; }
        /* If it's a file rather than a directory, just do a normal delete */
        if (e->attr != ATTR_DIR) { file_delete(name); return; }

        /* Save the block number before clearing the entry (dir_buf will be reused) */
        uint16_t blk = e->first_block;
        shell_memset(e, 0, sizeof(dir_entry_t));  /* unlink from parent directory */
        dir_flush();
        delete_dir_contents(block_to_sector(blk)); /* free everything inside */
        fat_free_chain(blk);                       /* free the directory's own block */
        kprintf("Deleted '%s' and all contents.\n", name);
    } else {
        file_delete(name);
    }
}

/* -----------------------------------------------------------------------
 * fat command helpers
 * --------------------------------------------------------------------- */

/*
 * Build a reverse mapping from block index to owner filename.
 * Recursively scans the entire directory tree starting from root.
 * For each file or directory entry, follows its FAT chain and records
 * the entry's name in the owners array at the corresponding block index.
 */
static void scan_dir(uint16_t dir_sector, char owners[TOTAL_BLOCKS][13]) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_sector; sec; sec = dir_next_sector(dir_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr != ATTR_FILE && e[i].attr != ATTR_DIR) continue;
            if (e[i].name[0] == '.') continue;

            /* Build a printable null-terminated name from the 8.3 fields */
            char name[13]; int n = 0;
            for (int k = 0; k < 8 && e[i].name[k] != ' '; k++) name[n++] = e[i].name[k];
            if (e[i].attr == ATTR_FILE && e[i].ext[0] != ' ') {
                name[n++] = '.';
                for (int k = 0; k < 3 && e[i].ext[k] != ' '; k++) name[n++] = e[i].ext[k];
            }
            name[n] = '\0';

            /* Mark every block in this entry's FAT chain with the owner name */
            uint16_t blk = e[i].first_block;
            while (blk != FAT_EOC && blk != FAT_FREE && blk < TOTAL_BLOCKS) {
                for (int k = 0; k <= n; k++) owners[blk][k] = name[k];
                blk = fat_next(blk);
            }

            /* Recurse into sub-directories to find nested files */
            if (e[i].attr == ATTR_DIR)
                scan_dir(block_to_sector(e[i].first_block), owners);
        }
    }
}

/* Initialise the owner map to empty strings, then populate via scan_dir */
static void build_owner_map(char owners[TOTAL_BLOCKS][13]) {
    for (int i = 0; i < TOTAL_BLOCKS; i++) owners[i][0] = '\0';
    scan_dir(ROOT_DIR_SECTOR, owners);
}

/*
 * Print one row of the FAT table: block number, status, and owner name.
 * Status values:
 *   [SYS   ] — block 0, permanently reserved for the system region
 *   [FREE  ] — block is available for allocation
 *   [END   ] — block is the last in a file/directory chain (FAT_EOC)
 *   [-> N  ] — block is mid-chain and points to block N
 */
static void print_fat_row(uint16_t block, const char *owner) {
    /* Right-align the block number in 5 characters */
    if (block < 10) vga_puts("    "); else vga_puts("   ");
    kprintf("%u", (uint32_t)block);
    vga_puts("  ");

    uint16_t entry = fat_next(block);
    if (block == 0)             vga_puts("[SYS   ]");
    else if (entry == FAT_FREE) vga_puts("[FREE  ]");
    else if (entry == FAT_EOC)  vga_puts("[END   ]");
    else {
        vga_puts("[->  ");
        kprintf("%u", (uint32_t)entry);
        if (entry < 10) vga_puts("  ]"); else vga_puts(" ]");
    }

    /* Show the owner filename if this block is allocated to something */
    if (owner && owner[0]) { vga_puts("  "); vga_puts(owner); }
    vga_putchar('\n');
}

/*
 * Display the File Allocation Table.
 * Paginates after 20 rows because the VGA screen is only 25 lines tall.
 * Shows used and free block counts as a summary at the end.
 */
static void cmd_fat(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Static to avoid a large stack allocation — safe since only one call runs at a time */
    static char owners[TOTAL_BLOCKS][13];
    build_owner_map(owners);

    vga_puts("FAT Table (32 blocks x 128 KB)\n");
    vga_puts("Block  Status     Owner\n");
    vga_puts("-----  ---------  ------------\n");

    for (uint16_t i = 0; i < TOTAL_BLOCKS; i++) {
        /* Pause after 20 rows to avoid scrolling past the top of the screen */
        if (i == 20) {
            vga_puts("--- Press any key for more ---\n");
            keyboard_getchar();
        }
        print_fat_row(i, owners[i]);
    }

    vga_puts("-----  ---------  ------------\n");

    /* Count used and free blocks in the data region (skip the system block 0) */
    uint16_t used = 0, free_cnt = 0;
    for (uint16_t i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (fat_next(i) == FAT_FREE) free_cnt++; else used++;
    }
    kprintf("Used: %u block(s)   Free: %u block(s)\n", (uint32_t)used, (uint32_t)free_cnt);
}

/*
 * Shut down the OS.
 * Tries multiple ACPI poweroff ports in order to support different VM environments.
 * Falls back to disabling interrupts and halting the CPU (cli; hlt) if none work.
 */
static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Shutting down HoneyOS... Goodbye!\n");
    outw(0x604, 0x2000);  /* QEMU ACPI poweroff */
    outw(0xB004, 0x2000); /* VirtualBox / Bochs (older ACPI) */
    outw(0x4004, 0x3400); /* VirtualBox (newer ACPI) */
    __asm__ volatile ("cli; hlt");  /* disable interrupts and halt — last resort */
    while (1);  /* unreachable; satisfies the compiler that the function doesn't return */
}

/* -----------------------------------------------------------------------
 * Command Dispatch Table
 *
 * Maps command name strings to their handler functions.
 * Terminated by a {NULL, NULL} sentinel so shell_run can detect the end.
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
    { NULL,       NULL         },  /* sentinel — marks end of table */
};

/* -----------------------------------------------------------------------
 * shell_run — one iteration of the REPL
 * --------------------------------------------------------------------- */

/*
 * Print the prompt, read a line, tokenise it, and dispatch to the matching
 * command. Called in a tight loop from kernel_main. Returns immediately after
 * each command so the prompt is re-printed on the next call.
 */
void shell_run(void) {
    static char line[SHELL_LINE_MAX];  /* static to avoid re-zeroing on every call */
    char *argv[SHELL_ARGC_MAX];

    /* Print the prompt in cyan, then reset to white for user input */
    vga_set_color(VGA_COLOR(VGA_LCYAN, VGA_BLACK));
    vga_puts(cwd_path);
    vga_puts("> ");
    vga_set_color(VGA_COLOR(VGA_WHITE, VGA_BLACK));

    read_line(line, SHELL_LINE_MAX);
    if (line[0] == '\0') return;  /* empty line — just reprint the prompt */

    int argc = tokenise(line, argv, SHELL_ARGC_MAX);
    if (argc == 0) return;

    /* Linear scan of the command table — only 12 entries so O(n) is fine */
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
