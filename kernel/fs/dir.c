/*
 * HoneyOS Directory Operations
 *
 * list   — scan the directory buffer and print each entry.
 * create — add a new ATTR_DIR entry in the current directory.
 * change — update cwd_sector ("..") goes back to root.
 * delete — clear a directory entry (does not recurse).
 */

#include "dir.h"
#include "fat.h"
#include "../screen/vga.h"

static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst; while (n--) *p++ = val;
}

static void name_to_fat(char *dst, const char *src, int dst_len) {
    int i = 0;
    while (i < dst_len && src[i]) { dst[i] = src[i]; i++; }
    while (i < dst_len) dst[i++] = ' ';
}

static void print_fat_name(const char *field, int len) {
    for (int i = 0; i < len && field[i] != ' '; i++)
        vga_putchar(field[i]);
}

/* -----------------------------------------------------------------------
 * dir_list
 * --------------------------------------------------------------------- */

void dir_list(void) {
    uint8_t buf[SECTOR_SIZE];
    int count = 0;

    for (int s = 0; s < ROOT_DIR_SECTORS; s++) {
        sector_read(cwd_sector + s, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        int per_sec = SECTOR_SIZE / (int)sizeof(dir_entry_t);
        for (int i = 0; i < per_sec; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].attr == ATTR_FILE)      vga_puts("  [FILE] ");
            else if (e[i].attr == ATTR_DIR)  vga_puts("  [DIR]  ");
            else continue;

            print_fat_name(e[i].name, 8);
            if (e[i].ext[0] != ' ') { vga_putchar('.'); print_fat_name(e[i].ext, 3); }
            if (e[i].attr == ATTR_FILE) { vga_puts("  ("); kprintf("%u", e[i].size); vga_puts(" bytes)"); }
            vga_putchar('\n');
            count++;
        }
    }
    if (count == 0) vga_puts("  (empty directory)\n");
}

/* -----------------------------------------------------------------------
 * dir_create
 * --------------------------------------------------------------------- */

void dir_create(const char *name) {
    if (dir_find(name, cwd_sector)) {
        vga_puts("mkdir: already exists: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    dir_entry_t *slot = dir_find_free(cwd_sector);
    if (!slot) { vga_puts("mkdir: directory full\n"); return; }

    mem_set(slot, 0, sizeof(dir_entry_t));
    name_to_fat(slot->name, name, 8);
    slot->ext[0] = slot->ext[1] = slot->ext[2] = ' ';
    slot->attr = ATTR_DIR;
    dir_flush(cwd_sector);
    kprintf("Created directory '%s'.\n", name);
}

/* -----------------------------------------------------------------------
 * dir_change
 * --------------------------------------------------------------------- */

void dir_change(const char *name) {
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        cwd_sector = ROOT_DIR_SECTOR;
        vga_puts("Changed to root directory.\n");
        return;
    }
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_DIR) {
        vga_puts("cd: not a directory: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    kprintf("Changed to '%s'.\n", name);
}

/* -----------------------------------------------------------------------
 * dir_delete
 * --------------------------------------------------------------------- */

void dir_delete(const char *name) {
    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_DIR) {
        vga_puts("rmdir: not a directory: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    mem_set(e, 0, sizeof(dir_entry_t));
    dir_flush(cwd_sector);
    kprintf("Deleted directory '%s'.\n", name);
}
