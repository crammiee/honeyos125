/*
 * HoneyOS File Operations
 *
 * read   — print a file's content to the screen, block by block.
 * write  — create or overwrite a file; user types lines, ends with ".".
 * edit   — show current content then rewrite the file.
 * delete — free the FAT chain and clear the directory entry.
 *
 * File data is stored in 128 KB blocks (SECTORS_PER_BLOCK sectors each).
 * Each block is read/written one 512-byte sector at a time.
 * All operations work on the current working directory (cwd_sector).
 */

#include "file.h"
#include "fat.h"
#include "../screen/vga.h"
#include "../io/keyboard.h"

/* Local memory utilities — no standard library in freestanding mode */
static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst; while (n--) *p++ = val;
}
static void mem_copy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}
static int str_len(const char *s) { int n = 0; while (*s++) n++; return n; }

/*
 * Copy up to dst_len characters from src into a space-padded FAT 8.3 field.
 * Stops at NUL or '.' (the extension separator). Remaining positions are
 * filled with spaces as the FAT format requires.
 */
static void name_to_fat(char *dst, const char *src, int dst_len) {
    int i = 0;
    while (i < dst_len && src[i] && src[i] != '.') { dst[i] = src[i]; i++; }
    while (i < dst_len) dst[i++] = ' ';
}

/*
 * Read one line of input from the keyboard, echo each character to the screen.
 * Backspace removes the last character. Returns the number of characters read.
 */
static int read_line(char *buf, int len) {
    int i = 0;
    while (1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') { buf[i] = '\0'; vga_putchar('\n'); return i; }
        if (c == '\b') { if (i > 0) { i--; vga_putchar('\b'); } continue; }
        if (i < len - 1) { buf[i++] = c; vga_putchar(c); }
    }
}

/* -----------------------------------------------------------------------
 * file_read
 * --------------------------------------------------------------------- */

/*
 * Print a file's contents to the VGA screen.
 * Follows the FAT chain block by block; within each block reads sector by
 * sector, printing bytes up to the file's recorded size.
 */
void file_read(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_FILE) {
        vga_puts("cat: file not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }

    uint8_t buf[SECTOR_SIZE];
    uint16_t block = e->first_block;  /* start of the FAT chain */
    uint32_t left  = e->size;         /* bytes remaining to print */

    while (block != FAT_EOC && block != FAT_FREE && left > 0) {
        uint16_t base = block_to_sector(block);  /* first sector of this block */
        /* Read each sector within the block until we exhaust the file */
        for (int s = 0; s < SECTORS_PER_BLOCK && left > 0; s++) {
            sector_read(base + s, buf);
            uint32_t n = left < SECTOR_SIZE ? left : SECTOR_SIZE;
            for (uint32_t i = 0; i < n; i++) vga_putchar((char)buf[i]);
            left -= n;
        }
        block = fat_next(block);  /* follow chain to next block */
    }
    vga_putchar('\n');
}

/* -----------------------------------------------------------------------
 * file_write
 * --------------------------------------------------------------------- */

/*
 * Create or overwrite a file.
 * Reads lines from the keyboard into a static buffer until the user
 * types a lone "." on its own line, then writes the buffered content
 * to the disk block by block (128 KB per block, sector by sector).
 */
void file_write(const char *name) {
    /* If the file already exists, free its blocks and clear its directory entry */
    dir_entry_t *old = dir_find(name, cwd_sector);
    if (old && old->attr == ATTR_FILE) {
        fat_free_chain(old->first_block);
        mem_set(old, 0, sizeof(dir_entry_t));
        dir_flush();
    }

    vga_puts("Enter text (type '.' on a line by itself to finish):\n");

    /* Static buffer avoids large stack allocations in freestanding mode */
    static uint8_t content[SECTOR_SIZE * 32];  /* up to ~16 KB per file */
    uint32_t total = 0;
    char line[256];

    /* Collect lines until the terminator "." is entered alone */
    while (1) {
        read_line(line, sizeof(line));
        if (line[0] == '.' && line[1] == '\0') break;
        int len = str_len(line);
        for (int i = 0; i < len && total < sizeof(content) - 1; i++)
            content[total++] = (uint8_t)line[i];
        if (total < sizeof(content) - 1)
            content[total++] = '\n';  /* preserve newlines between lines */
    }

    if (total == 0) { vga_puts("(empty file — nothing written)\n"); return; }

    /* Write content to disk: allocate one block at a time, write sector by sector */
    uint16_t first = FAT_EOC, prev = FAT_EOC;
    uint32_t written = 0;
    while (written < total) {
        uint16_t block = fat_alloc();  /* get a free 128 KB block */
        if (block == FAT_EOC) { vga_puts("write: disk full!\n"); break; }
        if (first == FAT_EOC) first = block;         /* record chain head */
        if (prev  != FAT_EOC) fat_set(prev, block);  /* link previous block to this one */

        uint16_t base = block_to_sector(block);
        for (int s = 0; s < SECTORS_PER_BLOCK && written < total; s++) {
            uint8_t buf[SECTOR_SIZE];
            mem_set(buf, 0, SECTOR_SIZE);
            uint32_t chunk = total - written;
            if (chunk > SECTOR_SIZE) chunk = SECTOR_SIZE;
            mem_copy(buf, content + written, chunk);
            sector_write(base + s, buf);
            written += chunk;
        }
        prev = block;
    }

    /* Create the directory entry for this file */
    dir_entry_t *slot = dir_find_free(cwd_sector);
    if (!slot) { vga_puts("write: directory full!\n"); fat_free_chain(first); return; }

    mem_set(slot, 0, sizeof(dir_entry_t));
    name_to_fat(slot->name, name, 8);  /* space-pad filename to 8 chars */
    const char *dot = name;
    while (*dot && *dot != '.') dot++;
    if (*dot == '.') name_to_fat(slot->ext, dot + 1, 3);  /* space-pad extension */
    else { slot->ext[0] = slot->ext[1] = slot->ext[2] = ' '; }
    slot->attr        = ATTR_FILE;
    slot->first_block = first;   /* head of the FAT chain */
    slot->size        = total;   /* exact byte count for reads */
    dir_flush();
    kprintf("Wrote %u bytes to '%s'.\n", total, name);
}

/* -----------------------------------------------------------------------
 * file_edit
 * --------------------------------------------------------------------- */

/*
 * Show a file's current content then let the user rewrite it.
 * The old content and FAT chain are freed before the new write begins.
 */
void file_edit(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_FILE) {
        vga_puts("edit: file not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    vga_puts("Current contents:\n");
    file_read(name);
    vga_puts("--- Enter new content ('.' to finish) ---\n");

    /* Free the existing blocks before writing new content */
    fat_free_chain(e->first_block);
    mem_set(e, 0, sizeof(dir_entry_t));
    dir_flush();
    file_write(name);
}

/* -----------------------------------------------------------------------
 * file_delete
 * --------------------------------------------------------------------- */

/*
 * Delete a file or empty directory.
 * Refuses to delete a non-empty directory to prevent orphaning its blocks.
 * Frees the FAT chain and clears the directory entry.
 */
void file_delete(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e) {
        vga_puts("rm: not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    /* Prevent deleting a directory that still has contents */
    if (e->attr == ATTR_DIR && !dir_is_empty(block_to_sector(e->first_block))) {
        vga_puts("rm: directory not empty: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    fat_free_chain(e->first_block);   /* return all blocks to the free pool */
    mem_set(e, 0, sizeof(dir_entry_t));  /* mark directory slot as free */
    dir_flush();
    kprintf("Deleted '%s'.\n", name);
}
