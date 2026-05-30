/*
 * HoneyOS File Operations
 *
 * read  — print a file's content to the screen block by block.
 * write — create/overwrite a file; user types lines, ends with ".".
 * edit  — show current content then rewrite (calls write internally).
 * delete — free the FAT chain and clear the directory entry.
 *
 * All operations work on the current working directory (cwd_sector).
 */

#include "file.h"
#include "fat.h"
#include "../screen/vga.h"
#include "../io/keyboard.h"

static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst; while (n--) *p++ = val;
}
static void mem_copy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}
static int str_len(const char *s) { int n = 0; while (*s++) n++; return n; }

/* Copy up to dst_len chars, space-pad remainder (FAT 8.3 name format) */
static void name_to_fat(char *dst, const char *src, int dst_len) {
    int i = 0;
    while (i < dst_len && src[i] && src[i] != '.') { dst[i] = src[i]; i++; }
    while (i < dst_len) dst[i++] = ' ';
}

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

void file_read(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_FILE) {
        vga_puts("cat: file not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    uint8_t buf[SECTOR_SIZE];
    uint16_t block = e->first_block;
    uint32_t left  = e->size;
    while (block != FAT_EOC && block != FAT_FREE && left > 0) {
        sector_read(block, buf);
        uint32_t n = left < SECTOR_SIZE ? left : SECTOR_SIZE;
        for (uint32_t i = 0; i < n; i++) vga_putchar((char)buf[i]);
        left -= n;
        block = fat_next(block);
    }
    vga_putchar('\n');
}

/* -----------------------------------------------------------------------
 * file_write
 * --------------------------------------------------------------------- */

void file_write(const char *name) {
    /* Remove existing entry if present */
    dir_entry_t *old = dir_find(name, cwd_sector);
    if (old && old->attr == ATTR_FILE) {
        fat_free_chain(old->first_block);
        mem_set(old, 0, sizeof(dir_entry_t));
        dir_flush();
    }

    vga_puts("Enter text (type '.' on a line by itself to finish):\n");

    static uint8_t content[SECTOR_SIZE * 32];   /* up to ~16 KB per file */
    uint32_t total = 0;
    char line[256];

    while (1) {
        read_line(line, sizeof(line));
        if (line[0] == '.' && line[1] == '\0') break;
        int len = str_len(line);
        for (int i = 0; i < len && total < sizeof(content) - 1; i++)
            content[total++] = (uint8_t)line[i];
        if (total < sizeof(content) - 1)
            content[total++] = '\n';
    }
    if (total == 0) { vga_puts("(empty file — nothing written)\n"); return; }

    /* Write content into FAT blocks */
    uint16_t first = FAT_EOC, prev = FAT_EOC;
    uint32_t written = 0;
    while (written < total) {
        uint16_t block = fat_alloc();
        if (block == FAT_EOC) { vga_puts("write: disk full!\n"); break; }
        if (first == FAT_EOC) first = block;
        if (prev  != FAT_EOC) fat_set(prev, block);

        uint8_t buf[SECTOR_SIZE];
        mem_set(buf, 0, SECTOR_SIZE);
        uint32_t chunk = total - written;
        if (chunk > SECTOR_SIZE) chunk = SECTOR_SIZE;
        mem_copy(buf, content + written, chunk);
        sector_write(block, buf);
        written += chunk;
        prev = block;
    }

    dir_entry_t *slot = dir_find_free(cwd_sector);
    if (!slot) { vga_puts("write: directory full!\n"); fat_free_chain(first); return; }

    mem_set(slot, 0, sizeof(dir_entry_t));
    name_to_fat(slot->name, name, 8);
    const char *dot = name;
    while (*dot && *dot != '.') dot++;
    if (*dot == '.') name_to_fat(slot->ext, dot + 1, 3);
    else { slot->ext[0] = slot->ext[1] = slot->ext[2] = ' '; }
    slot->attr        = ATTR_FILE;
    slot->first_block = first;
    slot->size        = total;
    dir_flush();
    kprintf("Wrote %u bytes to '%s'.\n", total, name);
}

/* -----------------------------------------------------------------------
 * file_edit — show existing content then rewrite the file
 * --------------------------------------------------------------------- */

void file_edit(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_FILE) {
        vga_puts("edit: file not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    vga_puts("Current contents:\n");
    file_read(name);
    vga_puts("--- Enter new content ('.' to finish) ---\n");

    fat_free_chain(e->first_block);
    mem_set(e, 0, sizeof(dir_entry_t));
    dir_flush();
    file_write(name);
}

/* -----------------------------------------------------------------------
 * file_delete
 * --------------------------------------------------------------------- */

void file_delete(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e) {
        vga_puts("rm: not found: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    /* Refuse to remove a non-empty directory, so we never orphan the blocks
       its children point to. */
    if (e->attr == ATTR_DIR && !dir_is_empty(e->first_block)) {
        vga_puts("rm: directory not empty: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    fat_free_chain(e->first_block);   /* frees a file's data or a dir's blocks */
    mem_set(e, 0, sizeof(dir_entry_t));
    dir_flush();
    kprintf("Deleted '%s'.\n", name);
}
