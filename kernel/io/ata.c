/*
 * HoneyOS ATA PIO Driver (LBA28, Primary Bus, Master Drive)
 *
 * Performs single-sector reads and writes using Programmed I/O (PIO) —
 * the CPU transfers data directly through I/O ports with no DMA or IRQs.
 *
 * Every status poll is bounded by ATA_TIMEOUT iterations so the kernel
 * never hangs when no drive is attached. An absent drive floats the status
 * register to 0xFF which keeps BSY permanently set; the timeout detects
 * this and returns -1 so fs_init() can fall back to the RAM disk.
 */

#include "ata.h"
#include "ports.h"

/* Maximum polling iterations before declaring a timeout.
   Generous enough for real hardware; QEMU clears BSY almost instantly. */
#define ATA_TIMEOUT 1000000

/* Wait until the drive clears the BSY (busy) bit in the status register.
   Returns 0 when the drive is ready, -1 if the timeout expires. */
static int ata_wait_bsy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++)
        if (!(inb(ATA_CMD) & ATA_SR_BSY))
            return 0;
    return -1;  /* drive never became ready — likely not attached */
}

/* Wait until the drive sets DRQ (data request ready) AND clears BSY,
   meaning it is ready to accept or provide data words.
   Returns 0 on success, -1 on timeout or if the error bit is set. */
static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t s = inb(ATA_CMD);
        if (s & ATA_SR_ERR) return -1;            /* drive reported an error */
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

/* Programme the ATA registers with the target LBA28 address.
   The drive/head register uses 0xE0 to select master drive + LBA mode,
   OR-ed with the top 4 bits of the 28-bit address. */
static void ata_setup(uint32_t lba) {
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));  /* master, LBA, top 4 bits */
    outb(ATA_SECTOR_CNT, 1);                              /* transfer exactly 1 sector */
    outb(ATA_LBA_LO,  (uint8_t)(lba        & 0xFF));     /* bits 0–7 */
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));    /* bits 8–15 */
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));    /* bits 16–23 */
}

/* Read one 512-byte sector at the given LBA into buf.
   Issues the READ SECTORS command (0x20), then transfers 256 16-bit words. */
int ata_read(uint32_t lba, uint8_t *buf) {
    if (ata_wait_bsy()) return -1;  /* ensure drive is idle before commanding */
    ata_setup(lba);
    outb(ATA_CMD, 0x20);            /* READ SECTORS command */
    if (ata_wait_drq()) return -1;  /* wait until data is ready to be read */

    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)  /* 256 × 2 bytes = 512-byte sector */
        p[i] = inw(ATA_DATA);
    return 0;
}

/* Write one 512-byte sector from buf to the given LBA.
   Issues the WRITE SECTORS command (0x30), transfers the data, then
   issues FLUSH CACHE (0xE7) to ensure the write reaches the disk. */
int ata_write(uint32_t lba, const uint8_t *buf) {
    if (ata_wait_bsy()) return -1;  /* ensure drive is idle before commanding */
    ata_setup(lba);
    outb(ATA_CMD, 0x30);            /* WRITE SECTORS command */
    if (ata_wait_drq()) return -1;  /* wait until drive is ready to accept data */

    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)  /* 256 × 2 bytes = 512-byte sector */
        outw(ATA_DATA, p[i]);

    if (ata_wait_bsy()) return -1;  /* wait for write to complete */
    outb(ATA_CMD, 0xE7);            /* FLUSH CACHE — force write to physical disk */
    ata_wait_bsy();                 /* wait for flush to complete */
    return 0;
}
