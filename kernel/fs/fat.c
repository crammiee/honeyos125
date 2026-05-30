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
#include "../io/ata.h"

/* Cached copy of the FAT table */
static uint16_t fat_table[FAT_MAX_ENTRIES];

/* One-sector directory buffer plus the LBA it currently holds. dir_find /
   dir_find_free load the relevant sector here and remember which one, so
   dir_flush() can write back exactly that sector. A single sector is enough
   because directories are now walked one sector at a time (see below). */
static uint8_t  dir_buf[SECTOR_SIZE];
static uint16_t dir_buf_sector;

uint16_t cwd_sector = ROOT_DIR_SECTOR;
char     cwd_path[CWD_PATH_MAX] = "root";   /* shown in the shell prompt */

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
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
    ata_read((uint32_t)lba, buf);
}

void sector_write(uint16_t lba, const uint8_t *buf) {
    ata_write((uint32_t)lba, buf);
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
    for (uint16_t i = DATA_START_SECTOR; i < DISK_SECTORS; i++) {
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

/* Walk a directory's sectors. The root directory is a fixed contiguous
   region (ROOT_DIR_SECTORS sectors); every other directory is a FAT chain,
   exactly like a file. Returns the sector after `cur`, or 0 when there are
   no more. `first` is the directory's starting sector, used to tell the
   root region apart from a chained sub-directory. */
uint16_t dir_next_sector(uint16_t first, uint16_t cur) {
    if (first == ROOT_DIR_SECTOR) {
        if (cur + 1 < ROOT_DIR_SECTOR + ROOT_DIR_SECTORS) return cur + 1;
        return 0;
    }
    uint16_t nxt = fat_next(cur);
    return (nxt == FAT_EOC || nxt == FAT_FREE) ? 0 : nxt;
}

/* Write the currently buffered directory sector back to disk. */
void dir_flush(void) {
    sector_write(dir_buf_sector, dir_buf);
}

/* Search a directory (given by its first sector) for an entry matching
   `name`. Each sector is loaded into dir_buf one at a time; the returned
   pointer is valid only until the next dir_find / dir_find_free call. */
dir_entry_t *dir_find(const char *name, uint16_t dir_sec) {
    for (uint16_t sec = dir_sec; sec; sec = dir_next_sector(dir_sec, sec)) {
        sector_read(sec, dir_buf);
        dir_buf_sector = sec;
        dir_entry_t *e = (dir_entry_t *)dir_buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (!name_matches(e[i].name, 8, name)) continue;
            /* Check extension if the name contains a dot */
            const char *dot = name;
            while (*dot && *dot != '.') dot++;
            if (*dot == '.' && !name_matches(e[i].ext, 3, dot + 1)) continue;
            return &e[i];
        }
    }
    return NULL;
}

/* Find a free entry slot in a directory. If the directory is a chained
   sub-directory and every existing sector is full, grow it by appending a
   fresh block to its FAT chain. The root region is fixed, so it returns
   NULL when full. */
dir_entry_t *dir_find_free(uint16_t dir_sec) {
    uint16_t last = dir_sec;
    for (uint16_t sec = dir_sec; sec; sec = dir_next_sector(dir_sec, sec)) {
        sector_read(sec, dir_buf);
        dir_buf_sector = sec;
        dir_entry_t *e = (dir_entry_t *)dir_buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++)
            if (e[i].attr == ATTR_FREE) return &e[i];
        last = sec;
    }
    if (dir_sec == ROOT_DIR_SECTOR) return NULL;   /* fixed-size root */
    uint16_t blk = fat_alloc();
    if (blk == FAT_EOC) return NULL;               /* disk full */
    fat_set(last, blk);                            /* append to the chain */
    mem_set(dir_buf, 0, SECTOR_SIZE);
    sector_write(blk, dir_buf);
    dir_buf_sector = blk;
    return (dir_entry_t *)dir_buf;                 /* entry 0 is now free */
}

/* A directory counts as empty when it holds no entries other than ".." . */
int dir_is_empty(uint16_t dir_first) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_first; sec; sec = dir_next_sector(dir_first, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;     /* skip "." / ".." */
            return 0;
        }
    }
    return 1;
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
    cwd_path[0] = 'r'; cwd_path[1] = 'o';
    cwd_path[2] = 'o'; cwd_path[3] = 't'; cwd_path[4] = '\0';
}
