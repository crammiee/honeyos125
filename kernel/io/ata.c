/*
 * HoneyOS ATA PIO Driver (LBA28, Primary Bus, Master Drive)
 *
 * Single-sector polled I/O on ports 0x1F0-0x1F7. No DMA, no IRQs.
 */

#include "ata.h"
#include "ports.h"

static void ata_wait_bsy(void) {
    while (inb(ATA_CMD) & ATA_SR_BSY)
        ;
}

static void ata_wait_drq(void) {
    uint8_t s;
    do { s = inb(ATA_CMD); } while ((s & ATA_SR_BSY) || !(s & ATA_SR_DRQ));
}

static void ata_setup(uint32_t lba) {
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba        & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
}

void ata_read(uint32_t lba, uint8_t *buf) {
    ata_wait_bsy();
    ata_setup(lba);
    outb(ATA_CMD, 0x20);
    ata_wait_drq();
    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        p[i] = inw(ATA_DATA);
}

void ata_write(uint32_t lba, const uint8_t *buf) {
    ata_wait_bsy();
    ata_setup(lba);
    outb(ATA_CMD, 0x30);
    ata_wait_drq();
    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, p[i]);
    ata_wait_bsy();
    outb(ATA_CMD, 0xE7);   /* FLUSH CACHE */
    ata_wait_bsy();
}
