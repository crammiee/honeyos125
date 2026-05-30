#ifndef ATA_H
#define ATA_H

/*
 * ATA Primary Bus Register Map (I/O ports 0x1F0 – 0x1F7)
 *
 * The ATA primary bus sits at a fixed I/O address on the x86.
 * All communication with the hard disk goes through these ports.
 */

#include "../types.h"

#define ATA_DATA       0x1F0  /* 16-bit data register — read/write sector data */
#define ATA_SECTOR_CNT 0x1F2  /* number of sectors to transfer (we always use 1) */
#define ATA_LBA_LO     0x1F3  /* LBA bits 0–7 */
#define ATA_LBA_MID    0x1F4  /* LBA bits 8–15 */
#define ATA_LBA_HI     0x1F5  /* LBA bits 16–23 */
#define ATA_DRIVE_HEAD 0x1F6  /* drive select + LBA bits 24–27 */
#define ATA_CMD        0x1F7  /* write = command register, read = status register */

/* Status register bit masks (read from ATA_CMD port) */
#define ATA_SR_BSY  0x80  /* drive is busy — wait before issuing commands */
#define ATA_SR_DRQ  0x08  /* data request ready — safe to transfer words */
#define ATA_SR_ERR  0x01  /* error occurred during last command */

/* Read one 512-byte sector at LBA address into buf.
   Returns 0 on success, -1 on timeout or error (e.g. no drive attached). */
int ata_read (uint32_t lba, uint8_t *buf);

/* Write one 512-byte sector from buf to LBA address, then flush the cache.
   Returns 0 on success, -1 on timeout or error. */
int ata_write(uint32_t lba, const uint8_t *buf);

#endif /* ATA_H */
