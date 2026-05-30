#ifndef FAT_H
#define FAT_H

#include "../types.h"

/*
 * Disk Layout (512-byte sectors, 4 MB image, 128 KB blocks)
 *
 * The disk is divided into 32 fixed-size blocks of 128 KB each.
 * Block 0 is reserved for the system region (MBR, kernel, metadata).
 * Blocks 1–31 are the usable data area for files and directories.
 *
 * Sector map:
 *   0        MBR         written by boot.asm
 *   1–32     Kernel      loaded by the bootloader into memory at 0x1000
 *   33       Superblock  filesystem magic number and geometry
 *   34       FAT table   32 uint16_t entries — one per block
 *   35–66    Root dir    fixed 32-sector region, 512 directory entry slots
 *   67–255   (unused)    remainder of block 0
 *   256–8191 Data        blocks 1–31, 128 KB each
 */
#define SECTOR_SIZE             512
#define SECTORS_PER_BLOCK       256                              /* 128 KB per block */
#define BLOCK_SIZE              (SECTORS_PER_BLOCK * SECTOR_SIZE)
#define DISK_SECTORS            8192                             /* 4 MB total */
#define TOTAL_BLOCKS            (DISK_SECTORS / SECTORS_PER_BLOCK)  /* 32 blocks */

#define SUPERBLOCK_SECTOR       33
#define FAT_START_SECTOR        34
#define FAT_SECTORS             1    /* all 32 entries fit in one 512-byte sector */
#define ROOT_DIR_SECTOR         35   /* = FAT_START_SECTOR + FAT_SECTORS */
#define ROOT_DIR_SECTORS        32   /* sectors 35–66, 512 dir entry slots */
#define DATA_START_BLOCK        1    /* block 0 is the system block, always reserved */
#define DATA_START_SECTOR       (DATA_START_BLOCK * SECTORS_PER_BLOCK)  /* 256 */

/* FAT table has one entry per block */
#define FAT_MAX_ENTRIES         TOTAL_BLOCKS  /* 32 */

/* Convert a block number to the first sector of that block */
#define block_to_sector(b)      ((uint16_t)((b) * SECTORS_PER_BLOCK))

/*
 * FAT Entry Sentinels
 *
 * Each FAT entry is a uint16_t that describes the state of its block:
 *   FAT_FREE  — block is available for allocation
 *   FAT_EOC   — block is the last in a file/directory chain
 *   any other — the index of the next block in the chain
 */
#define FAT_FREE  0x0000
#define FAT_EOC   0xFFFF

/* Superblock magic — spells "HONE" in ASCII; used to detect a formatted disk */
#define FS_MAGIC  0x484F4E45

/*
 * On-Disk Structures
 */

/* Superblock — written to sector 33 on format, read on mount to verify magic */
typedef struct {
    uint32_t magic;          /* FS_MAGIC if disk is formatted */
    uint32_t total_sectors;  /* total disk size in sectors */
    uint16_t fat_start;      /* first sector of the FAT table */
    uint16_t fat_sectors;    /* number of sectors occupied by the FAT */
    uint16_t root_start;     /* first sector of the root directory region */
    uint16_t root_sectors;   /* number of sectors in the root directory region */
    uint16_t data_start;     /* first sector of the data region */
    uint16_t reserved[235];  /* pad to exactly 512 bytes */
} __attribute__((packed)) superblock_t;

/* Directory entry attribute flags */
#define ATTR_FREE  0x00  /* slot is unused — available for a new entry */
#define ATTR_FILE  0x20  /* entry describes a regular file */
#define ATTR_DIR   0x10  /* entry describes a sub-directory */

/* Number of directory entries that fit in one 512-byte sector */
#define DIR_ENTRIES_PER_SECTOR  (SECTOR_SIZE / sizeof(dir_entry_t))

/*
 * Directory Entry (28 bytes, packed)
 *
 * Stored in the root directory region or in a sub-directory's block.
 * Uses 8.3 name format: name is space-padded to 8 chars, ext to 3.
 *
 * first_block holds:
 *   - for files/dirs: the block number (0–31) of the first data block
 *   - for ".." entries: the parent directory's cwd_sector (sector number)
 */
typedef struct {
    char     name[8];        /* filename, space-padded, no NUL terminator */
    char     ext[3];         /* extension, space-padded */
    uint8_t  attr;           /* ATTR_FILE, ATTR_DIR, or ATTR_FREE */
    uint8_t  reserved[10];   /* unused padding */
    uint16_t first_block;    /* head of the FAT chain, or parent sector for ".." */
    uint32_t size;           /* file size in bytes (0 for directories) */
} __attribute__((packed)) dir_entry_t;

/*
 * Global State
 */

/* Sector number of the first sector of the current working directory.
   ROOT_DIR_SECTOR for root; block_to_sector(block) for sub-directories. */
extern uint16_t cwd_sector;

#define CWD_PATH_MAX 128
extern char cwd_path[CWD_PATH_MAX];  /* human-readable path shown in prompt */

/*
 * Public API
 */

void     fs_init(void);   /* mount or format, set up RAM fallback if no disk */

/* Low-level sector I/O — transparently routes to ATA or RAM disk */
void     sector_read (uint16_t lba, uint8_t *buf);
void     sector_write(uint16_t lba, const uint8_t *buf);

/* FAT chain operations */
uint16_t fat_alloc(void);                   /* allocate a free block, mark EOC */
void     fat_free_chain(uint16_t first);    /* free every block in a chain */
uint16_t fat_next(uint16_t block);          /* return the next block in a chain */
void     fat_set(uint16_t block, uint16_t val); /* set a FAT entry and flush to disk */

/* Directory operations */
dir_entry_t *dir_find       (const char *name, uint16_t dir_sec);
dir_entry_t *dir_find_free  (uint16_t dir_sec);
void         dir_flush      (void);
uint16_t     dir_next_sector(uint16_t first, uint16_t cur);
int          dir_is_empty   (uint16_t dir_first);

#endif /* FAT_H */
