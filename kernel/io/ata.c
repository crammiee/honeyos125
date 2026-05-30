/*
 * HoneyOS ATA PIO Driver (LBA28, Primary Bus, Master Drive)
 *
 * Single-sector polled I/O on ports 0x1F0-0x1F7. No DMA, no IRQs.
 *
 * Every wait is bounded by ATA_TIMEOUT spins so the kernel never hangs when
 * no drive is attached (an absent drive floats the status register to 0xFF,
 * which keeps BSY set forever). On timeout the calls report -1 and the
 * filesystem falls back to a RAM disk — see fs_init().
 */

#include "ata.h"
#include "ports.h"

#define ATA_TIMEOUT 1000000   /* generous; QEMU PIO clears BSY almost instantly */

static int ata_wait_bsy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++)
        if (!(inb(ATA_CMD) & ATA_SR_BSY))
            return 0;
    return -1;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t s = inb(ATA_CMD);
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static void ata_setup(uint32_t lba) {
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba        & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
}

int ata_read(uint32_t lba, uint8_t *buf) {
    if (ata_wait_bsy()) return -1;
    ata_setup(lba);
    outb(ATA_CMD, 0x20);
    if (ata_wait_drq()) return -1;
    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        p[i] = inw(ATA_DATA);
    return 0;
}

int ata_write(uint32_t lba, const uint8_t *buf) {
    if (ata_wait_bsy()) return -1;
    ata_setup(lba);
    outb(ATA_CMD, 0x30);
    if (ata_wait_drq()) return -1;
    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, p[i]);
    if (ata_wait_bsy()) return -1;
    outb(ATA_CMD, 0xE7);   /* FLUSH CACHE */
    ata_wait_bsy();
    return 0;
}
