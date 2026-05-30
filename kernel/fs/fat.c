/*
 * HoneyOS FAT Filesystem Core
 *
 * Implements linked allocation using a File Allocation Table (FAT).
 * Each FAT entry is a uint16_t that maps a block to the next block in
 * its chain (FAT_FREE = unused, FAT_EOC = end of chain, N = next block).
 *
 * Storage normally goes to the ATA hard disk. When no drive responds at
 * boot (e.g. booted from a read-only ISO with no hard disk attached),
 * the filesystem falls back to an in-memory RAM disk so the shell
 * remains fully usable — changes just aren't persisted across reboots.
 *
 * Disk layout:
 *   Sector  0        : MBR (written by boot.asm, not touched here)
 *   Sectors 1–32     : Kernel binary
 *   Sector  33       : Superblock
 *   Sector  34       : FAT table (32 entries × 2 bytes = 64 bytes, 1 sector)
 *   Sectors 35–66    : Root directory region (512 dir_entry_t slots)
 *   Block   0        : Sectors 0–255 (system region, always reserved)
 *   Blocks  1–31     : Sectors 256–8191 (data blocks, 128 KB each)
 */

#include "fat.h"
#include "../screen/vga.h"
#include "../io/ata.h"

/* In-memory copy of the FAT table — loaded from disk on mount, flushed on every change */
static uint16_t fat_table[FAT_MAX_ENTRIES];

/*
 * Single-sector directory buffer.
 * dir_find and dir_find_free load one sector at a time into this buffer
 * and remember which LBA they loaded (dir_buf_sector), so dir_flush()
 * can write exactly that sector back without re-scanning.
 */
static uint8_t  dir_buf[SECTOR_SIZE];
static uint16_t dir_buf_sector;

/* Current working directory: ROOT_DIR_SECTOR for root, or
   block_to_sector(n) for a sub-directory living in block n. */
uint16_t cwd_sector = ROOT_DIR_SECTOR;
char     cwd_path[CWD_PATH_MAX] = "root";

/*
 * RAM disk fallback.
 * Sized to cover the system region (block 0) plus three data blocks
 * so the shell can create and read files even without a hard disk.
 * Lives in BSS, so it costs nothing in the on-disk kernel image.
 */
#define RAMDISK_SECTORS 1024   /* 4 blocks × 128 KB = 512 KB; blocks 1–3 usable */
static uint8_t ram_disk[RAMDISK_SECTORS * SECTOR_SIZE];
static int     ram_mode = 0;   /* 0 = use ATA disk, 1 = use RAM disk */

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static void mem_set(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

/*
 * Compare a space-padded FAT 8.3 name field against a plain C string.
 * The FAT name field is NOT null-terminated — trailing spaces are padding.
 * Returns 1 if the field matches the plain string, 0 otherwise.
 */
static int name_matches(const char *field, int flen, const char *plain) {
    int i = 0;
    /* match characters up to end of plain string or a '.' separator */
    while (i < flen && plain[i] && plain[i] != '.') {
        if (field[i] != plain[i]) return 0;
        i++;
    }
    /* remaining field characters must all be spaces (padding) */
    while (i < flen) { if (field[i] != ' ') return 0; i++; }
    return 1;
}

/* -----------------------------------------------------------------------
 * Sector I/O — transparently routes to ATA or RAM disk
 * --------------------------------------------------------------------- */

void sector_read(uint16_t lba, uint8_t *buf) {
    if (ram_mode) {
        /* Copy from the in-memory RAM disk; return zeroes if out of range */
        for (int i = 0; i < SECTOR_SIZE; i++)
            buf[i] = (lba < RAMDISK_SECTORS) ? ram_disk[lba * SECTOR_SIZE + i] : 0;
        return;
    }
    ata_read((uint32_t)lba, buf);
}

void sector_write(uint16_t lba, const uint8_t *buf) {
    if (ram_mode) {
        /* Write to the in-memory RAM disk only if within its range */
        if (lba < RAMDISK_SECTORS)
            for (int i = 0; i < SECTOR_SIZE; i++)
                ram_disk[lba * SECTOR_SIZE + i] = buf[i];
        return;
    }
    ata_write((uint32_t)lba, buf);
}

/* -----------------------------------------------------------------------
 * FAT Chain Operations
 * --------------------------------------------------------------------- */

/* Return the next block in the chain for the given block, or FAT_EOC if none */
uint16_t fat_next(uint16_t block) {
    return (block < FAT_MAX_ENTRIES) ? fat_table[block] : FAT_EOC;
}

/*
 * Write the entire in-memory FAT table back to its on-disk sector.
 * The FAT is only 64 bytes (32 × 2), so we pad the rest of the 512-byte
 * sector with zeroes to avoid writing stale data.
 */
static void fat_flush(void) {
    uint8_t buf[SECTOR_SIZE];
    mem_set(buf, 0, SECTOR_SIZE);
    for (int i = 0; i < (int)sizeof(fat_table); i++)
        buf[i] = ((uint8_t *)fat_table)[i];
    sector_write(FAT_START_SECTOR, buf);
}

/* Update one FAT entry in memory and immediately flush the table to disk */
void fat_set(uint16_t block, uint16_t val) {
    if (block >= FAT_MAX_ENTRIES) return;
    fat_table[block] = val;
    fat_flush();
}

/*
 * Allocate a free data block.
 * Scans the FAT starting from DATA_START_BLOCK, marks the first free
 * entry as FAT_EOC (end of chain), and returns its block number.
 * Returns FAT_EOC if the disk is full.
 * In RAM mode the search is capped at the RAM disk's block count.
 */
uint16_t fat_alloc(void) {
    uint16_t limit = ram_mode ? (RAMDISK_SECTORS / SECTORS_PER_BLOCK) : TOTAL_BLOCKS;
    for (uint16_t i = DATA_START_BLOCK; i < limit; i++) {
        if (fat_table[i] == FAT_FREE) { fat_set(i, FAT_EOC); return i; }
    }
    return FAT_EOC;  /* no free blocks */
}

/*
 * Free every block in a FAT chain starting at 'first'.
 * Walks the chain following next-block pointers, marking each entry
 * FAT_FREE as it goes. Stops at FAT_EOC or FAT_FREE.
 */
void fat_free_chain(uint16_t first) {
    while (first != FAT_EOC && first != FAT_FREE) {
        uint16_t next = fat_table[first];
        fat_set(first, FAT_FREE);
        first = next;
    }
}

/* -----------------------------------------------------------------------
 * Directory Helpers
 * --------------------------------------------------------------------- */

/*
 * Advance to the next sector of a directory.
 *
 * The root directory is a fixed contiguous region (ROOT_DIR_SECTORS sectors).
 * Sub-directories occupy exactly one 128 KB block (SECTORS_PER_BLOCK sectors)
 * — large enough for 4096+ entries so FAT chaining is never needed.
 *
 * Returns the next sector number, or 0 when there are no more.
 * 'first' identifies which kind of directory this is (root vs sub).
 */
uint16_t dir_next_sector(uint16_t first, uint16_t cur) {
    if (first == ROOT_DIR_SECTOR) {
        /* Root dir: walk the fixed contiguous region sector by sector */
        if (cur + 1 < ROOT_DIR_SECTOR + ROOT_DIR_SECTORS) return cur + 1;
        return 0;
    }
    /* Sub-directory: walk within its single allocated block */
    if (cur + 1 < first + SECTORS_PER_BLOCK) return cur + 1;
    return 0;
}

/* Write the currently buffered directory sector back to disk */
void dir_flush(void) {
    sector_write(dir_buf_sector, dir_buf);
}

/*
 * Search a directory for an entry whose name matches 'name'.
 * Loads one sector at a time into the shared dir_buf buffer.
 * The returned pointer is only valid until the next dir_find / dir_find_free.
 * Returns NULL if no matching entry is found.
 */
dir_entry_t *dir_find(const char *name, uint16_t dir_sec) {
    for (uint16_t sec = dir_sec; sec; sec = dir_next_sector(dir_sec, sec)) {
        sector_read(sec, dir_buf);
        dir_buf_sector = sec;
        dir_entry_t *e = (dir_entry_t *)dir_buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (!name_matches(e[i].name, 8, name)) continue;
            /* Also check the extension if the caller's name contains a '.' */
            const char *dot = name;
            while (*dot && *dot != '.') dot++;
            if (*dot == '.' && !name_matches(e[i].ext, 3, dot + 1)) continue;
            return &e[i];
        }
    }
    return NULL;
}

/*
 * Find a free (ATTR_FREE) slot in a directory.
 * Sub-directories are one full 128 KB block so they can hold thousands of
 * entries and never need to grow. Returns NULL only if every slot is taken
 * (extremely unlikely in practice).
 */
dir_entry_t *dir_find_free(uint16_t dir_sec) {
    for (uint16_t sec = dir_sec; sec; sec = dir_next_sector(dir_sec, sec)) {
        sector_read(sec, dir_buf);
        dir_buf_sector = sec;
        dir_entry_t *e = (dir_entry_t *)dir_buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++)
            if (e[i].attr == ATTR_FREE) return &e[i];
    }
    return NULL;  /* directory is completely full */
}

/*
 * Check whether a directory is empty.
 * A directory is considered empty if it contains no entries other than ".."
 * (the parent pointer stored in slot 0 of every sub-directory).
 * Returns 1 if empty, 0 if it contains at least one file or sub-directory.
 */
int dir_is_empty(uint16_t dir_first) {
    uint8_t buf[SECTOR_SIZE];
    for (uint16_t sec = dir_first; sec; sec = dir_next_sector(dir_first, sec)) {
        sector_read(sec, buf);
        dir_entry_t *e = (dir_entry_t *)buf;
        for (int i = 0; i < (int)DIR_ENTRIES_PER_SECTOR; i++) {
            if (e[i].attr == ATTR_FREE) continue;
            if (e[i].name[0] == '.') continue;  /* skip "." / ".." entries */
            return 0;  /* found a real entry — not empty */
        }
    }
    return 1;
}

/* -----------------------------------------------------------------------
 * Filesystem Initialisation — Mount or Format
 * --------------------------------------------------------------------- */

/* Load the FAT table from its on-disk sector into the in-memory fat_table array */
static void fat_load(void) {
    uint8_t buf[SECTOR_SIZE];
    sector_read(FAT_START_SECTOR, buf);
    /* Only copy the FAT's actual size (64 bytes) — the rest of the sector is padding */
    for (int i = 0; i < (int)sizeof(fat_table); i++)
        ((uint8_t *)fat_table)[i] = buf[i];
}

/*
 * Format the disk (or RAM disk in RAM mode).
 * Writes a fresh superblock, zeroes the FAT (marking all blocks free),
 * reserves block 0 as the system block, and clears the root directory.
 */
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

    /* Zero the FAT — all blocks start as free */
    mem_set(fat_table, 0, sizeof(fat_table));
    /* Block 0 is the system block and must never be allocated to a file */
    fat_table[0] = FAT_EOC;
    fat_flush();

    /* Clear every sector of the root directory region */
    uint8_t empty[SECTOR_SIZE];
    mem_set(empty, 0, SECTOR_SIZE);
    for (int s = 0; s < ROOT_DIR_SECTORS; s++)
        sector_write(ROOT_DIR_SECTOR + s, empty);

    if (!ram_mode)
        kprintf("HoneyOS FS: formatted fresh disk.\n");
}

/*
 * fs_init — called once at boot.
 *
 * Probes the ATA drive by attempting to read the superblock sector.
 * If the drive doesn't respond (returns -1), switches to RAM disk mode
 * and formats an ephemeral filesystem there.
 * If the drive responds but the magic number is wrong, formats it.
 * If the magic matches, mounts the existing filesystem.
 */
void fs_init(void) {
    uint8_t buf[SECTOR_SIZE];

    if (ata_read(SUPERBLOCK_SECTOR, buf) != 0) {
        /* No ATA drive — switch to RAM disk and format in memory */
        ram_mode = 1;
        fs_format();
        fat_load();
        kprintf("HoneyOS FS: no disk detected - using RAM (changes are not saved).\n");
    } else {
        superblock_t *sb = (superblock_t *)buf;
        if (sb->magic != FS_MAGIC) {
            /* Drive exists but is unformatted — write a fresh filesystem */
            fs_format();
        } else {
            /* Valid filesystem found — load the FAT and mount */
            fat_load();
            kprintf("HoneyOS FS: mounted existing disk.\n");
        }
    }

    /* Always start the shell at the root directory */
    cwd_sector = ROOT_DIR_SECTOR;
    cwd_path[0] = 'r'; cwd_path[1] = 'o';
    cwd_path[2] = 'o'; cwd_path[3] = 't'; cwd_path[4] = '\0';
}
