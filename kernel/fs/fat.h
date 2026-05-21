#ifndef FAT_H
#define FAT_H

#include "../types.h"

/* -----------------------------------------------------------------------
 * Disk layout (512-byte sectors, 1.44 MB floppy image)
 * --------------------------------------------------------------------- */
#define SECTOR_SIZE             512
#define DISK_SECTORS            2880

#define SUPERBLOCK_SECTOR       1
#define FAT_START_SECTOR        2
#define FAT_SECTORS             16   /* 16 × 512 = 8 192 bytes → 4 096 uint16_t entries */
#define ROOT_DIR_SECTOR         18
#define ROOT_DIR_SECTORS        32
#define DATA_START_SECTOR       50

#define FAT_MAX_ENTRIES  (FAT_SECTORS * SECTOR_SIZE / sizeof(uint16_t))  /* 4 096 */

/* FAT entry sentinels (FAT16-style) */
#define FAT_FREE  0x0000
#define FAT_EOC   0xFFFF

/* Superblock magic — "HONE" in ASCII */
#define FS_MAGIC  0x484F4E45

/* -----------------------------------------------------------------------
 * On-disk structures
 * --------------------------------------------------------------------- */

typedef struct {
    uint32_t magic;
    uint32_t total_sectors;
    uint16_t fat_start;
    uint16_t fat_sectors;
    uint16_t root_start;
    uint16_t root_sectors;
    uint16_t data_start;
    uint16_t reserved[235];   /* pad to 512 bytes */
} __attribute__((packed)) superblock_t;

#define ATTR_FREE  0x00
#define ATTR_FILE  0x20
#define ATTR_DIR   0x10

#define DIR_ENTRIES_PER_SECTOR  (SECTOR_SIZE / sizeof(dir_entry_t))

typedef struct {
    char     name[8];        /* space-padded, no NUL — FAT 8.3 convention */
    char     ext[3];
    uint8_t  attr;           /* ATTR_FILE or ATTR_DIR */
    uint8_t  reserved[10];
    uint16_t first_block;    /* head of FAT chain (0 = empty) */
    uint32_t size;           /* bytes (0 for directories) */
} __attribute__((packed)) dir_entry_t;

/* -----------------------------------------------------------------------
 * Global state
 * --------------------------------------------------------------------- */

extern uint16_t cwd_sector;  /* first sector of the current working directory */

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

void     fs_init(void);

void     sector_read (uint16_t lba, uint8_t *buf);
void     sector_write(uint16_t lba, const uint8_t *buf);

uint16_t fat_alloc(void);
void     fat_free_chain(uint16_t first);
uint16_t fat_next(uint16_t block);
void     fat_set(uint16_t block, uint16_t val);

dir_entry_t *dir_find     (const char *name, uint16_t dir_sec);
dir_entry_t *dir_find_free(uint16_t dir_sec);
void         dir_flush    (uint16_t dir_sec);

#endif /* FAT_H */
