/*
 * HoneyOS FAT File System
 *
 * Implements a simple FAT using linked allocation. Each FAT entry is a
 * uint16_t: 0x0000 = free, 0xFFFF = end of chain, else = next block.
 *
 * The entire disk is represented as an in-memory byte array (disk_image[]).
 * sector_read/write copy data in and out of this array so the rest of the
 * kernel never has to deal with a real disk driver at this stage.
 *
 * Disk layout:
 *   Sector  0        : MBR (written by boot.asm, not touched here)
 *   Sector  1        : Superblock
 *   Sectors 2–17     : FAT table  (4 096 entries × 2 bytes)
 *   Sectors 18–49    : Root directory region (512 dir_entry_t slots)
 *   Sectors 50+      : Data blocks
 */

#include "fat.h"
#include "../screen/vga.h"

/* In-memory disk image (~1.44 MB, lives in BSS — zero-initialised at boot) */
static uint8_t disk_image[DISK_SECTORS * SECTOR_SIZE];

/* Cached copy of the FAT table */
static uint16_t fat_table[FAT_MAX_ENTRIES];

/* Single-sector directory buffer used by dir_find / dir_find_free */
static uint8_t dir_buf[ROOT_DIR_SECTORS * SECTOR_SIZE];

uint16_t cwd_sector = ROOT_DIR_SECTOR;

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void mem_copy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/* Compare a space-padded FAT name field against a plain C string */
static int name_matches(const char *field, int flen, const char *plain) {
    int i = 0;
    while (i < flen && plain[i] && plain[i] != '.') {
        if (field[i] != plain[i]) return 0;
        i++;
    }
    while (i < flen) { if (field[i] != ' ') return 0; i++; }
    return 1;
}

/* -----------------------------------------------------------------------
 * Sector I/O
 * --------------------------------------------------------------------- */

void sector_read(uint16_t lba, uint8_t *buf) {
    mem_copy(buf, disk_image + (uint32_t)lba * SECTOR_SIZE, SECTOR_SIZE);
}

void sector_write(uint16_t lba, const uint8_t *buf) {
    mem_copy(disk_image + (uint32_t)lba * SECTOR_SIZE, buf, SECTOR_SIZE);
}

/* -----------------------------------------------------------------------
 * FAT chain operations
 * --------------------------------------------------------------------- */

uint16_t fat_next(uint16_t block) {
    return (block < FAT_MAX_ENTRIES) ? fat_table[block] : FAT_EOC;
}

void fat_set(uint16_t block, uint16_t val) {
    if (block >= FAT_MAX_ENTRIES) return;
    fat_table[block] = val;
    /* Write the affected FAT sector back to disk_image */
    uint16_t eps = SECTOR_SIZE / sizeof(uint16_t);   /* entries per sector */
    uint16_t sec = block / eps;
    sector_write(FAT_START_SECTOR + sec,
                 (const uint8_t *)&fat_table[sec * eps]);
}

uint16_t fat_alloc(void) {
    for (uint16_t i = DATA_START_SECTOR; i < FAT_MAX_ENTRIES; i++) {
        if (fat_table[i] == FAT_FREE) { fat_set(i, FAT_EOC); return i; }
    }
    return FAT_EOC;   /* disk full */
}

void fat_free_chain(uint16_t first) {
    while (first != FAT_EOC && first != FAT_FREE) {
        uint16_t next = fat_table[first];
        fat_set(first, FAT_FREE);
        first = next;
    }
}

/* -----------------------------------------------------------------------
 * Directory helpers
 * --------------------------------------------------------------------- */

static void dir_load(uint16_t dir_sec) {
    for (int i = 0; i < ROOT_DIR_SECTORS; i++)
        sector_read(dir_sec + i, dir_buf + i * SECTOR_SIZE);
}

void dir_flush(uint16_t dir_sec) {
    for (int i = 0; i < ROOT_DIR_SECTORS; i++)
        sector_write(dir_sec + i, dir_buf + i * SECTOR_SIZE);
}

dir_entry_t *dir_find(const char *name, uint16_t dir_sec) {
    dir_load(dir_sec);
    int total = ROOT_DIR_SECTORS * DIR_ENTRIES_PER_SECTOR;
    dir_entry_t *e = (dir_entry_t *)dir_buf;
    for (int i = 0; i < total; i++) {
        if (e[i].attr == ATTR_FREE) continue;
        if (!name_matches(e[i].name, 8, name)) continue;
        /* Check extension if the name contains a dot */
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        if (*dot == '.') {
            if (!name_matches(e[i].ext, 3, dot + 1)) continue;
        }
        return &e[i];
    }
    return NULL;
}

dir_entry_t *dir_find_free(uint16_t dir_sec) {
    dir_load(dir_sec);
    int total = ROOT_DIR_SECTORS * DIR_ENTRIES_PER_SECTOR;
    dir_entry_t *e = (dir_entry_t *)dir_buf;
    for (int i = 0; i < total; i++)
        if (e[i].attr == ATTR_FREE) return &e[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * fs_init — mount or format the in-memory disk
 * --------------------------------------------------------------------- */

static void fat_load(void) {
    uint8_t *p = (uint8_t *)fat_table;
    for (int s = 0; s < FAT_SECTORS; s++)
        sector_read(FAT_START_SECTOR + s, p + s * SECTOR_SIZE);
}

static void fs_format(void) {
    mem_set(disk_image, 0, sizeof(disk_image));

    superblock_t sb;
    mem_set(&sb, 0, sizeof(sb));
    sb.magic         = FS_MAGIC;
    sb.total_sectors = DISK_SECTORS;
    sb.fat_start     = FAT_START_SECTOR;
    sb.fat_sectors   = FAT_SECTORS;
    sb.root_start    = ROOT_DIR_SECTOR;
    sb.root_sectors  = ROOT_DIR_SECTORS;
    sb.data_start    = DATA_START_SECTOR;
    sector_write(SUPERBLOCK_SECTOR, (const uint8_t *)&sb);

    mem_set(fat_table, 0, sizeof(fat_table));
    for (int s = 0; s < FAT_SECTORS; s++)
        sector_write(FAT_START_SECTOR + s,
                     (const uint8_t *)fat_table + s * SECTOR_SIZE);

    uint8_t empty[SECTOR_SIZE];
    mem_set(empty, 0, SECTOR_SIZE);
    for (int s = 0; s < ROOT_DIR_SECTORS; s++)
        sector_write(ROOT_DIR_SECTOR + s, empty);

    kprintf("HoneyOS FS: formatted fresh disk.\n");
}

void fs_init(void) {
    uint8_t buf[SECTOR_SIZE];
    sector_read(SUPERBLOCK_SECTOR, buf);
    superblock_t *sb = (superblock_t *)buf;

    if (sb->magic != FS_MAGIC) {
        fs_format();
    } else {
        fat_load();
        kprintf("HoneyOS FS: mounted existing disk.\n");
    }
    cwd_sector = ROOT_DIR_SECTOR;
}
