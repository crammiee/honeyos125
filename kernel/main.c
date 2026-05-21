/*
 * HoneyOS — Kernel Main (Stage 3: VGA + welcome banner)
 * Called from kernel_entry.asm after stack and BSS are ready.
 */

#include "types.h"
#include "screen/vga.h"

void kernel_main(void) {
    vga_init();
    vga_print_welcome();

    while (1) {}
}
