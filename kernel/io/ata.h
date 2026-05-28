#ifndef ATA_H
#define ATA_H

#include "../types.h"

#define ATA_DATA       0x1F0
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_CMD        0x1F7   /* write = command, read = status */

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

void ata_read (uint32_t lba, uint8_t *buf);
void ata_write(uint32_t lba, const uint8_t *buf);

#endif /* ATA_H */
