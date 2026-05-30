#ifndef PORTS_H
#define PORTS_H

/*
 * x86 I/O Port Helpers
 *
 * The x86 architecture has a separate 64 KB I/O address space accessed via
 * IN/OUT instructions. These inline functions wrap those instructions so the
 * rest of the kernel can read/write hardware registers without inline asm.
 *
 * "Nd" constraint = 8-bit immediate or DX register — the only addressing
 * modes the IN/OUT instructions accept for the port operand.
 */

#include "../types.h"

/* Read one byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Write one byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read one 16-bit word from an I/O port (used by ATA data register) */
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Write one 16-bit word to an I/O port (used by ATA data register) */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/*
 * Post an unused write to port 0x80 (POST diagnostic port).
 * This creates a small hardware delay — useful after writes to slow
 * devices like the PS/2 keyboard controller that need settling time.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* PORTS_H */
