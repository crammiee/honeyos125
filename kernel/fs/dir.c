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

static int str_length(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* Append "/name" to the prompt path (name capped at 8 chars to match the
   stored 8.3 directory name). */
static void path_push(const char *name) {
    int len = str_length(cwd_path);
    if (len + 1 < CWD_PATH_MAX) cwd_path[len++] = '/';
    for (int i = 0; i < 8 && name[i] && len < CWD_PATH_MAX - 1; i++)
        cwd_path[len++] = name[i];
    cwd_path[len] = '\0';
}

/* Drop the last "/segment" from the prompt path (used by "cd .."). */
static void path_pop(void) {
    int len = str_length(cwd_path);
    while (len > 0 && cwd_path[len - 1] != '/') len--;
    if (len > 0) len--;          /* also drop the '/' itself */
    cwd_path[len] = '\0';
}

/* -----------------------------------------------------------------------
 * dir_list
 * --------------------------------------------------------------------- */

void dir_list(void) {
    uint8_t buf[SECTOR_SIZE];
    int count = 0;

    /* Walk every sector of the current directory (root region or chain). */
    for (uint16_t sec = cwd_sector; sec; sec = dir_next_sector(cwd_sector, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;   /* hide "." and ".." */
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
    /* "." and ".." are reserved for navigation and cannot be created. */
    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
        vga_puts("mkdir: reserved name: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    if (dir_find(name, cwd_sector)) {
        vga_puts("mkdir: already exists: "); vga_puts(name); vga_putchar('\n');
        return;
    }

    /* A sub-directory is its own FAT chain. Allocate its first block and seed
       it with a ".." entry (always slot 0) pointing back at the parent, so
       "cd .." can find its way home later. */
    uint16_t blk = fat_alloc();   /* blk is a block number (0-31) */
    if (blk == FAT_EOC) { vga_puts("mkdir: disk full\n"); return; }

    uint16_t blk_sector = block_to_sector(blk);
    uint8_t buf[SECTOR_SIZE];
    mem_set(buf, 0, SECTOR_SIZE);
    dir_entry_t *dotdot = (dir_entry_t *)buf;
    name_to_fat(dotdot->name, "..", 8);
    dotdot->ext[0] = dotdot->ext[1] = dotdot->ext[2] = ' ';
    dotdot->attr        = ATTR_DIR;
    dotdot->first_block = cwd_sector;   /* store parent's sector for cd .. */
    dotdot->size        = 0;
    sector_write(blk_sector, buf);

    /* Record the new directory in the parent directory. */
    dir_entry_t *slot = dir_find_free(cwd_sector);
    if (!slot) { vga_puts("mkdir: directory full\n"); fat_free_chain(blk); return; }

    mem_set(slot, 0, sizeof(dir_entry_t));
    name_to_fat(slot->name, name, 8);
    slot->ext[0] = slot->ext[1] = slot->ext[2] = ' ';
    slot->attr        = ATTR_DIR;
    slot->first_block = blk;
    slot->size        = 0;
    dir_flush();
    kprintf("Created directory '%s'.\n", name);
}

/* -----------------------------------------------------------------------
 * dir_change
 * --------------------------------------------------------------------- */

void dir_change(const char *name) {
    /* "." refers to the current directory — nothing to do. */
    if (name[0] == '.' && name[1] == '\0') return;

    /* ".." follows the parent pointer stored in slot 0 of this directory. */
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        if (cwd_sector == ROOT_DIR_SECTOR) {
            vga_puts("Already at root directory.\n");
            return;
        }
        uint8_t buf[SECTOR_SIZE];
        sector_read(cwd_sector, buf);
        dir_entry_t *dotdot = (dir_entry_t *)buf;   /* slot 0 == ".." */
        cwd_sector = dotdot->first_block;
        path_pop();
        return;
    }

    dir_entry_t *e = dir_find(name, cwd_sector);
    if (!e || e->attr != ATTR_DIR) {
        vga_puts("cd: not a directory: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    cwd_sector = block_to_sector(e->first_block);   /* convert block number to sector */
    path_push(name);
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
    if (!dir_is_empty(block_to_sector(e->first_block))) {
        vga_puts("rmdir: directory not empty: "); vga_puts(name); vga_putchar('\n');
        return;
    }
    fat_free_chain(e->first_block);   /* reclaim the directory's own blocks */
    mem_set(e, 0, sizeof(dir_entry_t));
    dir_flush();
    kprintf("Deleted directory '%s'.\n", name);
}
