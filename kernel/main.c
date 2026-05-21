/*
 * HoneyOS — Kernel Main (Stage 2: bare entry point)
 * Called from kernel_entry.asm after stack and BSS are ready.
 * Just spins for now — VGA, keyboard, and shell come in later stages.
 */

#include "types.h"

void kernel_main(void) {
    while (1) {}
}
